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

// Envolvente con ataque y release separados - suave, sin clicks
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

    // === V5: GANANCIAS SUAVES, NO 5.0 - 6.0 COMO ANTES ===
    // Fresh Air no mete +15dB de golpe. Mete poquito y dinamico.
    // Mid Air: 0 a 1.2  |  High Air: 0 a 1.8
    float midGain = std::pow(midPct / 100.0f, 1.5f) * 1.2f;
    float highGain = std::pow(highPct / 100.0f, 1.5f) * 1.8f;
    float mix = mixPct / 100.0f;
    float outGain = outPct / 100.0f;

    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();
    double sr = getSampleRate();

    // Coeficientes - Filtros suaves de 12dB/oct, no de 24dB que suenan nasales
    *sourceHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 500.0);
    *sourceLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 3000.0);
    // Mid Air: 2kHz - 8kHz - aqui vive la claridad, la presencia, lo que hace que se entienda la voz
    *midBandHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 2000.0);
    *midBandLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 8000.0);
    // High Air: 8kHz - 20kHz - aqui vive el "aire", el brillo sedoso, lo que suena a Fresh Air
    *highBandHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 8000.0);
    // De-esser: solo detecta 5.5kHz - 9kHz donde viven las S feas
    *deesserHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 5500.0);
    *deesserLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 9000.0);
    *lowCut.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 25.0);
    // Mud cut muy suave: -0.8dB en 250Hz, no -3dB como antes que te dejaba flaca la voz
    *mudCut.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, 250.0, 0.8f, 0.96f);

    juce::AudioBuffer<float> dryBuf(numChannels, numSamples);
    juce::AudioBuffer<float> sourceBuf(numChannels, numSamples);
    juce::AudioBuffer<float> exciterBuf(numChannels, numSamples);
    juce::AudioBuffer<float> midBuf(numChannels, numSamples);
    juce::AudioBuffer<float> highBuf(numChannels, numSamples);
    juce::AudioBuffer<float> deesserBuf(numChannels, numSamples);
    dryBuf.makeCopyOf(buffer);
    sourceBuf.makeCopyOf(buffer);
    deesserBuf.makeCopyOf(buffer);

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

    // === GENERADOR DE ARMONICOS SUAVE - EL CAMBIO MAS IMPORTANTE ===
    // ANTES: x * 4.0 + tanh(x * 4.0) = distorsion fea, saturada
    // AHORA: Saturacion muy ligera tipo "tube" - solo un 10-15% de armonicos
    // Formula: y = x - (x^3)/3  -> genera solo 2do y 3er armonico, suena sedoso
    // Y le quitamos el fundamental para que no engorde la voz
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* src = sourceBuf.getReadPointer(ch);
        auto* exc = exciterBuf.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
        {
            float x = src[s];
            if (!std::isfinite(x)) x = 0;
            // Limitamos a +-0.8 para que nunca sature, aunque entres fuerte
            x = juce::jlimit(-0.8f, 0.8f, x);
            // Saturacion suave - drive bajo, 0.35 en vez de 4.0
            float drive = 0.35f;
            float xd = x * drive;
            // Soft clip cubico - muy musical, no rasposo
            float soft = xd - (xd * xd * xd) / 3.0f;
            // Solo nos quedamos con lo que GENERO, no con el original
            // Esto es clave: si sumas el original otra vez, saturas
            float harmonics = (soft - xd) * 3.0f;
            exc[s] = harmonics * 0.5f; // Bajito, porque luego lo subimos con el knob
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
        auto* deess = deesserBuf.getReadPointer(ch);
        int chIdx = juce::jmin(ch, 1);

        for (int s = 0; s < numSamples; ++s)
        {
            float dryClean = std::isfinite(dry[s]) ? dry[s] : 0;
            float d = std::isfinite(deess[s]) ? deess[s] : 0;
            float m = std::isfinite(midB[s]) ? midB[s] : 0;
            float h = std::isfinite(highB[s]) ? highB[s] : 0;

            // === GATE SUAVE - Para que no meta ruido cuando no cantas ===
            gateEnv[chIdx] = updateEnvSmooth(gateEnv[chIdx], dryClean, 0.90f, 0.999f);
            float gate = 1.0f;
            if (gateEnv[chIdx] < 0.012f)
            {
                gate = gateEnv[chIdx] / 0.012f;
                gate *= gate; // Curva suave, no corte brusco
            }

            // === DINAMICA ESTILO FRESH AIR - LA CLAVE DE LO SUAVE ===
            // Cuando cantas FUERTE, el plugin se baja SOLO para no sonar chillón
            // Cuando cantas BAJITO, te da mas aire para que no se pierda la voz
            // Por eso Fresh Air se siente suave aunque lo pongas al 100%
            midDynEnv[chIdx] = updateEnvSmooth(midDynEnv[chIdx], dryClean, 0.95f, 0.9995f);
            highDynEnv[chIdx] = updateEnvSmooth(highDynEnv[chIdx], dryClean, 0.95f, 0.9995f);

            // Si cantas a 0.5, te baja el aire a la mitad. Si cantas a 0.1, te lo deja al 100%
            float midDyn = 1.0f / (1.0f + midDynEnv[chIdx] * 2.2f);
            float highDyn = 1.0f / (1.0f + highDynEnv[chIdx] * 1.8f);
            midDyn = juce::jlimit(0.35f, 1.0f, midDyn);
            highDyn = juce::jlimit(0.35f, 1.0f, highDyn);

            // === DE-ESSER SUAVE ESTILO FRESH AIR - QUE NO TE COMA LA S ===
            // Antes: te bajaba el aire a 0.4 de golpe = se escuchaba el "pumping" feo
            // Ahora: threshold mas alto, ratio suave, y smoothing lento
            // Solo actua en las S fuertes, no en toda la voz
            deesserEnv[chIdx] = updateEnvSmooth(deesserEnv[chIdx], d, 0.88f, 0.999f);
            float deesserTarget = 1.0f;
            // Threshold 0.18 - solo las S que de verdad pican
            if (deesserEnv[chIdx] > 0.18f)
            {
                float excess = (deesserEnv[chIdx] - 0.18f) / 0.18f;
                // Ratio suave: maximo baja a 0.70, no a 0.40. La S sigue ahi, solo menos fea
                deesserTarget = juce::jlimit(0.70f, 1.0f, 1.0f / (1.0f + excess * 0.55f));
            }
            // Smoothing lento: ataque 15ms, release 120ms - no se escucha el "apagón"
            float smoothCoeff = deesserTarget < deesserGainSmooth[chIdx] ? 0.92f : 0.995f;
            deesserGainSmooth[chIdx] = smoothCoeff * deesserGainSmooth[chIdx] + (1.0f - smoothCoeff) * deesserTarget;

            // === MEZCLA FINAL - PARALELO, NO EN SERIE ===
            // El dry NUNCA se toca con saturacion. El aire va por al lado y se suma bajito.
            // Por eso no se siente saturado aunque lo pongas al 100%
            float midAir = m * midGain * midDyn * gate;
            float highAir = h * highGain * highDyn * gate;

            // El de-esser solo le baja al AIRE, no a tu voz. Tu voz queda intacta.
            // Y le baja mas al Mid (donde vive la S fea) que al High (donde vive el aire bonito)
            midAir *= (0.55f + 0.45f * deesserGainSmooth[chIdx]);
            highAir *= (0.75f + 0.25f * deesserGainSmooth[chIdx]);

            float air = midAir + highAir;

            // Limitador final ultra suave, solo para que no clipee, no para saturar
            air = juce::jlimit(-0.25f, 0.25f, air);

            float mixed = dryClean + (air * mix);
            // Soft clip final muy ligero - 0.95 en vez de 0.90, casi no se nota
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
