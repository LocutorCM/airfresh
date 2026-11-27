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
    midBandHP.prepare(spec); midBandLP.prepare(spec);
    highBandHP.prepare(spec);
    deesserHP.prepare(spec); deesserLP.prepare(spec);
    lowCut.prepare(spec); mudCut.prepare(spec);

    sourceHP.reset(); sourceLP.reset();
    midBandHP.reset(); midBandLP.reset(); highBandHP.reset();
    deesserHP.reset(); deesserLP.reset();
    lowCut.reset(); mudCut.reset();

    for (int i = 0; i < 2; ++i)
    {
        deesserEnv[i] = 0.0f;
        midDynEnv[i] = 0.0f;
        highDynEnv[i] = 0.0f;
        gateEnv[i] = 0.0f;
        deesserGainSmooth[i] = 1.0f;
    }
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

    // === V6: GANANCIAS QUE SI SE ESCUCHAN, PERO SIN SATURAR ===
    // Mid Air: 0 a 3.0 (antes 1.2, por eso no se escuchaba)
    // High Air: 0 a 4.0 (antes 1.8)
    // Esto es boost de EQ, no distorsion, por eso no suena rasposo
    float midGain = std::pow(midPct / 100.0f, 1.3f) * 3.0f;
    float highGain = std::pow(highPct / 100.0f, 1.3f) * 4.0f;
    float mix = mixPct / 100.0f;
    float outGain = outPct / 100.0f;

    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();
    double sr = getSampleRate();

    *sourceHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 500.0);
    *sourceLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 3000.0);
    *midBandHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 2000.0);
    *midBandLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 8000.0);
    *highBandHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 8000.0);
    *deesserHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 5500.0);
    *deesserLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 9000.0);
    *lowCut.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 25.0);
    *mudCut.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, 250.0, 0.8f, 0.96f);

    juce::AudioBuffer<float> dryBuf(numChannels, numSamples);
    juce::AudioBuffer<float> sourceBuf(numChannels, numSamples);
    juce::AudioBuffer<float> exciterBuf(numChannels, numSamples);
    juce::AudioBuffer<float> midBuf(numChannels, numSamples);
    juce::AudioBuffer<float> highBuf(numChannels, numSamples);
    juce::AudioBuffer<float> midEQBuf(numChannels, numSamples);  // NUEVO V6
    juce::AudioBuffer<float> highEQBuf(numChannels, numSamples); // NUEVO V6
    juce::AudioBuffer<float> deesserBuf(numChannels, numSamples);
    dryBuf.makeCopyOf(buffer);
    sourceBuf.makeCopyOf(buffer);
    deesserBuf.makeCopyOf(buffer);
    midEQBuf.makeCopyOf(buffer);  // El EQ sale directo de tu voz, no del exciter
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
    // === V6: ESTO ES LO QUE FALTABA - BOOST DE EQ DIRECTO ===
    // Antes solo teniamos armonicos chiquitos. Ahora tambien tenemos EQ.
    // Es como si tuvieras un Fresh Air + un EQ de agudos juntos.
    // Esto SI se escucha al activar/desactivar, aunque tu voz no tenga agudos
    {
        juce::dsp::AudioBlock<float> b(midEQBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        midBandHP.process(ctx);
        midBandLP.process(ctx);
    }
    {
        juce::dsp::AudioBlock<float> b(highEQBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        highBandHP.process(ctx);
    }

    // === GENERADOR DE ARMONICOS V6 - CORREGIDO ===
    // Antes: (soft - xd) * 3.0 = 0.00016, nada
    // Ahora: tanh suave + makeup gain x8, si se escucha pero sin raspar
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* src = sourceBuf.getReadPointer(ch);
        auto* exc = exciterBuf.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
        {
            float x = src[s];
            if (!std::isfinite(x)) x = 0;
            x = juce::jlimit(-0.9f, 0.9f, x);
            // Drive suave 0.6, no 4.0. Suficiente para crear armonicos, no para saturar
            float driven = std::tanh(x * 1.8f);
            // Nos quedamos solo con los armonicos generados
            float harmonics = driven - x * 0.85f;
            // Makeup gain - esto es lo que faltaba en V5
            exc[s] = harmonics * 2.5f;
        }
    }

    midBuf.makeCopyOf(exciterBuf);
    highBuf.makeCopyOf(exciterBuf);
    {
        juce::dsp::AudioBlock<float> b(midBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        midBandHP.process(ctx);
        midBandLP.process(ctx);
    }
    {
        juce::dsp::AudioBlock<float> b(highBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        highBandHP.process(ctx);
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
            float dryClean = std::isfinite(dry[s]) ? dry[s] : 0;
            float d = std::isfinite(deess[s]) ? deess[s] : 0;
            float m = std::isfinite(midB[s]) ? midB[s] : 0;
            float h = std::isfinite(highB[s]) ? highB[s] : 0;
            float mEQ = std::isfinite(midEQ[s]) ? midEQ[s] : 0;
            float hEQ = std::isfinite(highEQ[s]) ? highEQ[s] : 0;

            // Gate mas bajo - antes 0.012 te cortaba la voz si cantabas bajito
            // Ahora 0.006, solo corta el ruido, no tu voz
            gateEnv[chIdx] = updateEnvSmooth(gateEnv[chIdx], dryClean, 0.90f, 0.999f);
            float gate = 1.0f;
            if (gateEnv[chIdx] < 0.006f)
            {
                gate = gateEnv[chIdx] / 0.006f;
                gate *= gate;
            }

            // Dinamica Fresh Air - mas suave, no te baja tanto el aire
            midDynEnv[chIdx] = updateEnvSmooth(midDynEnv[chIdx], dryClean, 0.95f, 0.9995f);
            highDynEnv[chIdx] = updateEnvSmooth(highDynEnv[chIdx], dryClean, 0.95f, 0.9995f);
            float midDyn = 1.0f / (1.0f + midDynEnv[chIdx] * 1.2f);
            float highDyn = 1.0f / (1.0f + highDynEnv[chIdx] * 1.0f);
            midDyn = juce::jlimit(0.55f, 1.0f, midDyn);
            highDyn = juce::jlimit(0.55f, 1.0f, highDyn);

            // De-esser suave
            deesserEnv[chIdx] = updateEnvSmooth(deesserEnv[chIdx], d, 0.88f, 0.999f);
            float deesserTarget = 1.0f;
            if (deesserEnv[chIdx] > 0.18f)
            {
                float excess = (deesserEnv[chIdx] - 0.18f) / 0.18f;
                deesserTarget = juce::jlimit(0.70f, 1.0f, 1.0f / (1.0f + excess * 0.55f));
            }
            float smoothCoeff = deesserTarget < deesserGainSmooth[chIdx] ? 0.92f : 0.995f;
            deesserGainSmooth[chIdx] = smoothCoeff * deesserGainSmooth[chIdx] + (1.0f - smoothCoeff) * deesserTarget;

            // === MEZCLA V6: EQ + ARMONICOS ===
            // 70% es EQ directo de tu voz (esto es lo que SI se escucha)
            // 30% es armonicos generados (esto es lo que le da el "aire" extra)
            float midAir = (mEQ * 0.70f + m * 0.30f) * midGain * midDyn * gate;
            float highAir = (hEQ * 0.70f + h * 0.30f) * highGain * highDyn * gate;

            // De-esser solo al aire, no a tu voz
            midAir *= (0.55f + 0.45f * deesserGainSmooth[chIdx]);
            highAir *= (0.75f + 0.25f * deesserGainSmooth[chIdx]);

            float air = midAir + highAir;
            air = juce::jlimit(-0.45f, 0.45f, air);

            float mixed = dryClean + (air * mix);
            mixed = std::tanh(mixed * 0.95f) / 0.95f;
            mixed = juce::jlimit(-0.98f, 0.98f, mixed);
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
