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
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "midAir", "Mid Air",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 45.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "highAir", "High Air",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 60.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mix", "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "output", "Output",
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

    bodyFilter.prepare(spec); presenceHP.prepare(spec); presenceLP.prepare(spec);
    midHP.prepare(spec); midLP.prepare(spec); highHP.prepare(spec); superHP.prepare(spec);
    deesserHP.prepare(spec); deesserLP.prepare(spec);
    midShelf.prepare(spec); highShelf.prepare(spec);

    bodyFilter.reset(); presenceHP.reset(); presenceLP.reset();
    midHP.reset(); midLP.reset(); highHP.reset(); superHP.reset();
    deesserHP.reset(); deesserLP.reset();
    midShelf.reset(); highShelf.reset();

    deesserEnv[0] = deesserEnv[1] = 0.0f;
    airEnv[0] = airEnv[1] = 0.0f;

    midAirSmooth.reset(sampleRate, 0.02);
    highAirSmooth.reset(sampleRate, 0.02);
    mixSmooth.reset(sampleRate, 0.02);
    outputSmooth.reset(sampleRate, 0.02);
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

static inline float percentToGain(float percent, float maxDb)
{
    float p = juce::jlimit(0.0f, 100.0f, percent) / 100.0f;
    float curved = std::pow(p, 1.6f);
    return juce::Decibels::decibelsToGain(curved * maxDb) - 1.0f;
}

// Envolvente suave: ataque rapido, release lento - como un compresor
static inline float updateEnv(float env, float input, float attack, float release)
{
    float absIn = std::abs(input);
    if (absIn > env)
        return attack * env + (1.0f - attack) * absIn;
    else
        return release * env + (1.0f - release) * absIn;
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
    if (!std::isfinite(midPct)) midPct = 0;
    if (!std::isfinite(highPct)) highPct = 0;
    if (!std::isfinite(mixPct)) mixPct = 100;
    if (!std::isfinite(outPct)) outPct = 100;
    midPct = juce::jlimit(0.0f, 100.0f, midPct);
    highPct = juce::jlimit(0.0f, 100.0f, highPct);
    mixPct = juce::jlimit(0.0f, 100.0f, mixPct);
    outPct = juce::jlimit(0.0f, 200.0f, outPct);

    // === NIVELES ===
    // Mid Air controla Presence + Mid juntos - por eso da mas definicion
    float midGain = percentToGain(midPct, 18.0f);
    float highGain = percentToGain(highPct, 24.0f);
    float mix = mixPct / 100.0f;
    float outGain = outPct / 100.0f;

    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();
    double sr = getSampleRate();

    // === FILTROS - FRECUENCIAS EXPANDIDAS PARA QUITAR LO OPACO ===
    // Body: pasa-bajas 250Hz para cuerpo - se suma muy poquito, solo para dar peso
    // Presence: 2kHz - 5kHz - AQUI ESTA LA DEFINICION, quita el trapo
    // Mid Air: 5kHz - 9kHz - claridad
    // High Air: 10kHz+ - brillo
    // Super Air: 16kHz+ - aire ultra que el tuyo no tenia
    // De-Esser: 6kHz - 9kHz - detecta S
    *bodyFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 250.0);
    *presenceHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 2000.0);
    *presenceLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 5000.0);
    *midHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 5000.0);
    *midLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 9000.0);
    *highHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 10000.0);
    *superHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 16000.0);
    *deesserHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 6000.0);
    *deesserLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 9000.0);
    // Shelfs suaves para brillo real sin harsh
    *midShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr, 3500.0, 0.7f, 1.0f + midGain * 0.15f);
    *highShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr, 12000.0, 0.7f, 1.0f + highGain * 0.15f);

    // Buffers mismo tamaño - SEGURO
    juce::AudioBuffer<float> bodyBuf(numChannels, numSamples);
    juce::AudioBuffer<float> presBuf(numChannels, numSamples);
    juce::AudioBuffer<float> midBuf(numChannels, numSamples);
    juce::AudioBuffer<float> highBuf(numChannels, numSamples);
    juce::AudioBuffer<float> superBuf(numChannels, numSamples);
    juce::AudioBuffer<float> deesserBuf(numChannels, numSamples);
    juce::AudioBuffer<float> shelfBuf(numChannels, numSamples);
    bodyBuf.makeCopyOf(buffer); presBuf.makeCopyOf(buffer);
    midBuf.makeCopyOf(buffer); highBuf.makeCopyOf(buffer);
    superBuf.makeCopyOf(buffer); deesserBuf.makeCopyOf(buffer);
    shelfBuf.makeCopyOf(buffer);

    auto processOne = [](auto& filt, juce::AudioBuffer<float>& buf) {
        juce::dsp::AudioBlock<float> b(buf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        filt.process(ctx);
    };
    processOne(bodyFilter, bodyBuf);
    { juce::dsp::AudioBlock<float> b(presBuf); juce::dsp::ProcessContextReplacing<float> ctx(b); presenceHP.process(ctx); presenceLP.process(ctx); }
    { juce::dsp::AudioBlock<float> b(midBuf); juce::dsp::ProcessContextReplacing<float> ctx(b); midHP.process(ctx); midLP.process(ctx); }
    processOne(highHP, highBuf);
    processOne(superHP, superBuf);
    { juce::dsp::AudioBlock<float> b(deesserBuf); juce::dsp::ProcessContextReplacing<float> ctx(b); deesserHP.process(ctx); deesserLP.process(ctx); }
    { juce::dsp::AudioBlock<float> b(shelfBuf); juce::dsp::ProcessContextReplacing<float> ctx(b); midShelf.process(ctx); highShelf.process(ctx); }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* out = buffer.getWritePointer(ch);
        auto* body = bodyBuf.getReadPointer(ch);
        auto* pres = presBuf.getReadPointer(ch);
        auto* mid = midBuf.getReadPointer(ch);
        auto* high = highBuf.getReadPointer(ch);
        auto* superA = superBuf.getReadPointer(ch);
        auto* deess = deesserBuf.getReadPointer(ch);
        auto* shelf = shelfBuf.getReadPointer(ch);
        int chIdx = juce::jmin(ch, 1);

        for (int s = 0; s < numSamples; ++s)
        {
            float dry = out[s];
            if (!std::isfinite(dry)) dry = 0;
            float b = std::isfinite(body[s]) ? body[s] : 0;
            float p = std::isfinite(pres[s]) ? pres[s] : 0;
            float m = std::isfinite(mid[s]) ? mid[s] : 0;
            float h = std::isfinite(high[s]) ? high[s] : 0;
            float sup = std::isfinite(superA[s]) ? superA[s] : 0;
            float d = std::isfinite(deess[s]) ? deess[s] : 0;
            float sh = std::isfinite(shelf[s]) ? shelf[s] : 0;

            // === 1. CONTROL DINAMICO DE-ESSER ===
            // Detecta S: si la banda 6-9kHz es mucho mas fuerte que el resto, es una S
            // Ataque rapido para atrapar la S, release lento para no hacer pumping
            deesserEnv[chIdx] = updateEnv(deesserEnv[chIdx], d, 0.85f, 0.998f);
            // Umbral: solo actua cuando la S es fuerte
            float deesserThreshold = 0.12f;
            float deesserReduction = 1.0f;
            if (deesserEnv[chIdx] > deesserThreshold)
            {
                float excess = (deesserEnv[chIdx] - deesserThreshold) / deesserThreshold;
                // Baja hasta -8dB en las S fuertes, suave, no de golpe
                deesserReduction = 1.0f / (1.0f + excess * 1.8f);
                deesserReduction = juce::jlimit(0.35f, 1.0f, deesserReduction);
            }

            // === 2. CONTROL DINAMICO DE AIRE ===
            // Si el aire total se pasa, lo baja solo - como Fresh Air original
            // Esto evita que suene harsh aunque subas a 100%
            float airLevel = std::abs(h) + std::abs(sup) * 0.5f;
            airEnv[chIdx] = updateEnv(airEnv[chIdx], airLevel, 0.90f, 0.995f);
            float airControl = 1.0f;
            if (airEnv[chIdx] > 0.25f)
            {
                float excess = (airEnv[chIdx] - 0.25f) / 0.25f;
                airControl = 1.0f / (1.0f + excess * 0.8f);
                airControl = juce::jlimit(0.5f, 1.0f, airControl);
            }

            // === 3. GENERAR AIRE CON CUERPO Y DEFINICION ===
            // Body: muy poquito, solo para dar peso - 250Hz con saturacion suave
            float bodyExcited = std::tanh(b * 1.2f) * 0.15f;

            // Presence: QUITA EL TRAPO - 2-5kHz es donde se entiende la voz
            // Saturacion suave + enfasis, sin harsh porque esta banda no tiene S fuertes
            float presExcited = std::tanh(p * 2.0f);
            presExcited = presExcited / (1.0f + std::abs(presExcited) * 0.25f);

            // Mid: claridad 5-9kHz
            float midExcited = std::tanh(m * 2.2f);
            midExcited = midExcited / (1.0f + std::abs(midExcited) * 0.3f);

            // High: brillo 10kHz+ - con de-esser aplicado, ya no pica en las S
            float highExcited = std::tanh(h * 2.5f) * 0.7f + std::tanh(h * 5.0f) * 0.3f;
            highExcited = highExcited / (1.0f + std::abs(highExcited) * 0.2f);
            highExcited *= deesserReduction; // <-- AQUI SE QUITA LO HARSH DE LAS S

            // Super Air: 16kHz+ - aire ultra sedoso, casi no tiene S, por eso no pica
            float superExcited = std::tanh(sup * 3.0f) * 0.8f;
            superExcited *= deesserReduction * 0.8f + 0.2f;

            // Shelf: brillo real
            float shelfAir = sh - dry;

            // Mezcla final con control dinamico
            // Presence y Body van ligados a Mid Air - por eso ya no suena opaco
            float air = (bodyExcited * midGain * 0.25f)
                      + (presExcited * midGain * 0.55f)  // <-- MAS DEFINICION
                      + (midExcited * midGain * 0.45f)
                      + (highExcited * highGain * 0.50f)
                      + (superExcited * highGain * 0.35f) // <-- MAS AIRE ARRIBA
                      + (shelfAir * 0.4f * (midGain + highGain) * 0.2f);

            air *= airControl; // <-- CONTROL DINAMICO, baja solo si se pasa
            air = juce::jlimit(-0.6f, 0.6f, air);

            float mixed = dry + (air * mix);
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
