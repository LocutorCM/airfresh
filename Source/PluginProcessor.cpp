#include "PluginProcessor.h"
#include "PluginEditor.h"

EC_AIRAudioProcessor::EC_AIRAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
        #if ! JucePlugin_IsMidiEffect
            #if ! JucePlugin_IsSynth
                .withInput("Input", juce::AudioChannelSet::stereo(), true)
            #endif
                .withOutput("Output", juce::AudioChannelSet::stereo(), true)
        #endif
    ),
#endif
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

EC_AIRAudioProcessor::~EC_AIRAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout EC_AIRAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("midAir", "Mid Air",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 35.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("highAir", "High Air",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 45.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 80.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("output", "Output",
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<juce::AudioParameterBool>("power", "Power", true));
    return { params.begin(), params.end() };
}

const juce::String EC_AIRAudioProcessor::getName() const { return JucePlugin_Name; }
bool EC_AIRAudioProcessor::acceptsMidi() const { return false; }
bool EC_AIRAudioProcessor::producesMidi() const { return false; }
bool EC_AIRAudioProcessor::isMidiEffect() const { return false; }
double EC_AIRAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int EC_AIRAudioProcessor::getNumPrograms() { return 1; }
int EC_AIRAudioProcessor::getCurrentProgram() { return 0; }
void EC_AIRAudioProcessor::setCurrentProgram(int index) { juce::ignoreUnused(index); }
const juce::String EC_AIRAudioProcessor::getProgramName(int index) { juce::ignoreUnused(index); return {}; }
void EC_AIRAudioProcessor::changeProgramName(int index, const juce::String& newName) { juce::ignoreUnused(index, newName); }

void EC_AIRAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
    spec.numChannels = (juce::uint32)getTotalNumOutputChannels();

    sourceHP.prepare(spec); sourceLP.prepare(spec);
    midHarmHP.prepare(spec); midHarmLP.prepare(spec); highHarmHP.prepare(spec);
    midEQHP.prepare(spec); midEQLP.prepare(spec); highEQHP.prepare(spec);
    deesserHP.prepare(spec); deesserLP.prepare(spec);
    lowCut.prepare(spec); mudCut.prepare(spec);
    exciterLowPass.prepare(spec); highSmoothLP.prepare(spec);

    sourceHP.reset(); sourceLP.reset();
    midHarmHP.reset(); midHarmLP.reset(); highHarmHP.reset();
    midEQHP.reset(); midEQLP.reset(); highEQHP.reset();
    deesserHP.reset(); deesserLP.reset();
    lowCut.reset(); mudCut.reset();
    exciterLowPass.reset(); highSmoothLP.reset();

    for (int i = 0; i < 2; ++i)
    {
        deesserEnv[i] = 0.0f; midDynEnv[i] = 0.0f; highDynEnv[i] = 0.0f;
        gateEnv[i] = 0.0f; deesserGainSmooth[i] = 1.0f;
    }
    midGainSmooth = 0.0f; highGainSmooth = 0.0f; mixSmooth = 0.8f;
}

void EC_AIRAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool EC_AIRAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif
    return true;
}
#endif

static inline float updateEnvSmooth(float env, float input, float attackCoeff, float releaseCoeff)
{
    float absIn = std::abs(input);
    if (absIn > env)
        return attackCoeff * env + (1.0f - attackCoeff) * absIn;
    else
        return releaseCoeff * env + (1.0f - releaseCoeff) * absIn;
}

void EC_AIRAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;
    if (bypassed) return;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    auto powerParam = apvts.getRawParameterValue("power");
    bool isPowered = powerParam != nullptr ? (powerParam->load() > 0.5f) : true;
    if (!isPowered) return;

    float midPct = apvts.getRawParameterValue("midAir")->load();
    float highPct = apvts.getRawParameterValue("highAir")->load();
    float mixPct = apvts.getRawParameterValue("mix")->load();
    float outPct = apvts.getRawParameterValue("output")->load();
    midPct = juce::jlimit(0.0f, 100.0f, std::isfinite(midPct) ? midPct : 35.0f);
    highPct = juce::jlimit(0.0f, 100.0f, std::isfinite(highPct) ? highPct : 45.0f);
    mixPct = juce::jlimit(0.0f, 100.0f, std::isfinite(mixPct) ? mixPct : 80.0f);
    outPct = juce::jlimit(0.0f, 200.0f, std::isfinite(outPct) ? outPct : 100.0f);

    // === V7: GANANCIAS MAS BAJAS Y EN dB, NO EN MULTIPLICADOR BRUTO ===
    // Antes: High 4.0x = +12dB de ruido. Ahora max +5dB, suena a aire, no a interferencia
    // Mid: 0 a +4dB  |  High: 0 a +5dB
    float midTarget = std::pow(midPct / 100.0f, 1.4f) * 1.6f;  // 1.6x = +4dB
    float highTarget = std::pow(highPct / 100.0f, 1.4f) * 1.8f; // 1.8x = +5dB
    float mixTarget = mixPct / 100.0f;
    float outGain = outPct / 100.0f;

    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();
    double sr = getSampleRate();

    // FIX: No cambiar coeficientes cada bloque si no hace falta - eso tambien causaba clicks
    // Ahora solo los seteamos una vez por bloque, pero con filtros separados ya no se contaminan
    *sourceHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 500.0);
    *sourceLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 3000.0);
    *midHarmHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 2000.0);
    *midHarmLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 8000.0);
    *highHarmHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 8000.0);
    *midEQHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 2000.0);
    *midEQLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 8000.0);
    *highEQHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 8000.0);
    *deesserHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 5500.0);
    *deesserLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 9000.0);
    *lowCut.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 25.0);
    *mudCut.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, 250.0, 0.8f, 0.96f);
    // FIX INTERFERENCIA: Lowpass a 16kHz en el exciter - quita el aliasing que sonaba a radio
    *exciterLowPass.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 16000.0);
    // Suaviza el High para que no pique
    *highSmoothLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 18000.0);

    juce::AudioBuffer<float> dryBuf(numChannels, numSamples);
    juce::AudioBuffer<float> sourceBuf(numChannels, numSamples);
    juce::AudioBuffer<float> exciterBuf(numChannels, numSamples);
    juce::AudioBuffer<float> midBuf(numChannels, numSamples);
    juce::AudioBuffer<float> highBuf(numChannels, numSamples);
    juce::AudioBuffer<float> midEQBuf(numChannels, numSamples);
    juce::AudioBuffer<float> highEQBuf(numChannels, numSamples);
    juce::AudioBuffer<float> deesserBuf(numChannels, numSamples);
    dryBuf.makeCopyOf(buffer);
    sourceBuf.makeCopyOf(buffer);
    deesserBuf.makeCopyOf(buffer);
    midEQBuf.makeCopyOf(buffer);
    highEQBuf.makeCopyOf(buffer);

    {
        juce::dsp::AudioBlock<float> b(dryBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        lowCut.process(ctx);
        mudCut.process(ctx);
    }
    {
        juce::dsp::AudioBlock<float> b(sourceBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        sourceHP.process(ctx);
        sourceLP.process(ctx);
    }
    {
        juce::dsp::AudioBlock<float> b(deesserBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        deesserHP.process(ctx);
        deesserLP.process(ctx);
    }
    // EQ directo - ahora con sus PROPIOS filtros, no comparten con los armonicos
    {
        juce::dsp::AudioBlock<float> b(midEQBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        midEQHP.process(ctx);
        midEQLP.process(ctx);
    }
    {
        juce::dsp::AudioBlock<float> b(highEQBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        highEQHP.process(ctx);
        highSmoothLP.process(ctx); // Suaviza el High
    }

    // Generador de armonicos - drive mas bajo y con lowpass para quitar aliasing
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* src = sourceBuf.getReadPointer(ch);
        auto* exc = exciterBuf.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
        {
            float x = src[s];
            if (!std::isfinite(x)) x = 0;
            x = juce::jlimit(-0.85f, 0.85f, x);
            // Drive 1.2, no 1.8 - menos armonicos agudos que sonaban a interferencia
            float driven = std::tanh(x * 1.2f);
            float harmonics = driven - x * 0.90f;
            exc[s] = harmonics * 1.2f; // Antes 2.5, ahora 1.2 - mas limpio
        }
    }
    // Quita el aliasing / interferencia del exciter
    {
        juce::dsp::AudioBlock<float> b(exciterBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        exciterLowPass.process(ctx);
    }

    midBuf.makeCopyOf(exciterBuf);
    highBuf.makeCopyOf(exciterBuf);
    {
        juce::dsp::AudioBlock<float> b(midBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        midHarmHP.process(ctx);
        midHarmLP.process(ctx);
    }
    {
        juce::dsp::AudioBlock<float> b(highBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        highHarmHP.process(ctx);
        highSmoothLP.process(ctx);
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* out = buffer.getWritePointer(ch);
        auto* dry = dryBuf.getReadPointer(ch);
        auto* midB = midBuf.getReadPointer(ch);
        auto* highB = highBuf.getReadPointer(ch);
        auto* midEQ = midEQBuf.getReadPointer(ch);
        auto* highEQ = highEQBuf.getReadPointer(ch);
        auto* deess = deesserBuf.getReadPointer(ch);
        int chIdx = juce::jmin(ch, 1);

        for (int s = 0; s < numSamples; ++s)
        {
            // === V7 FIX: Smoothing de ganancias POR MUESTRA ===
            // Antes el knob saltaba de 0 a 4.0 en un bloque y sonaba el "crack" de interferencia
            // Ahora sube suave, como un fader, sin clicks
            float smoothCoeff = 0.9995f; // Muy lento = muy suave
            midGainSmooth = smoothCoeff * midGainSmooth + (1.0f - smoothCoeff) * midTarget;
            highGainSmooth = smoothCoeff * highGainSmooth + (1.0f - smoothCoeff) * highTarget;
            mixSmooth = smoothCoeff * mixSmooth + (1.0f - smoothCoeff) * mixTarget;

            float dryClean = std::isfinite(dry[s]) ? dry[s] : 0;
            float d = std::isfinite(deess[s]) ? deess[s] : 0;
            float m = std::isfinite(midB[s]) ? midB[s] : 0;
            float h = std::isfinite(highB[s]) ? highB[s] : 0;
            float mEQ = std::isfinite(midEQ[s]) ? midEQ[s] : 0;
            float hEQ = std::isfinite(highEQ[s]) ? highEQ[s] : 0;

            gateEnv[chIdx] = updateEnvSmooth(gateEnv[chIdx], dryClean, 0.90f, 0.999f);
            float gate = 1.0f;
            if (gateEnv[chIdx] < 0.006f)
            {
                gate = gateEnv[chIdx] / 0.006f;
                gate *= gate;
            }

            midDynEnv[chIdx] = updateEnvSmooth(midDynEnv[chIdx], dryClean, 0.95f, 0.9995f);
            highDynEnv[chIdx] = updateEnvSmooth(highDynEnv[chIdx], dryClean, 0.95f, 0.9995f);
            float midDyn = 1.0f / (1.0f + midDynEnv[chIdx] * 1.0f);
            float highDyn = 1.0f / (1.0f + highDynEnv[chIdx] * 0.8f);
            midDyn = juce::jlimit(0.60f, 1.0f, midDyn);
            highDyn = juce::jlimit(0.60f, 1.0f, highDyn);

            deesserEnv[chIdx] = updateEnvSmooth(deesserEnv[chIdx], d, 0.88f, 0.999f);
            float deesserTarget = 1.0f;
            if (deesserEnv[chIdx] > 0.18f)
            {
                float excess = (deesserEnv[chIdx] - 0.18f) / 0.18f;
                deesserTarget = juce::jlimit(0.70f, 1.0f, 1.0f / (1.0f + excess * 0.55f));
            }
            float dsCoeff = deesserTarget < deesserGainSmooth[chIdx] ? 0.92f : 0.995f;
            deesserGainSmooth[chIdx] = dsCoeff * deesserGainSmooth[chIdx] + (1.0f - dsCoeff) * deesserTarget;

            // V7: Mas EQ, menos armonicos - el EQ es limpio, los armonicos eran los que sonaban a interferencia
            // 85% EQ limpio + 15% armonicos suaves
            float midAir = (mEQ * 0.85f + m * 0.15f) * midGainSmooth * midDyn * gate;
            float highAir = (hEQ * 0.85f + h * 0.15f) * highGainSmooth * highDyn * gate;

            midAir *= (0.55f + 0.45f * deesserGainSmooth[chIdx]);
            highAir *= (0.75f + 0.25f * deesserGainSmooth[chIdx]);

            float air = midAir + highAir;
            // Limite mas bajo para que nunca sature a interferencia
            air = juce::jlimit(-0.30f, 0.30f, air);

            float mixed = dryClean + (air * mixSmooth);
            // Sin tanh fuerte al final - el tanh tambien generaba armonicos agudos
            // Solo un soft clip muy ligero
            if (mixed > 0.98f) mixed = 0.98f + (mixed - 0.98f) * 0.1f;
            if (mixed < -0.98f) mixed = -0.98f + (mixed + 0.98f) * 0.1f;
            out[s] = mixed * outGain;
            if (!std::isfinite(out[s])) out[s] = 0.0f;
        }
    }
}

bool EC_AIRAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* EC_AIRAudioProcessor::createEditor()
{
    return new EC_AIRAudioProcessorEditor(*this);
}

void EC_AIRAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void EC_AIRAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EC_AIRAudioProcessor();
}
