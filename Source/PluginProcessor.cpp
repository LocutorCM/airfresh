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
        juce::NormalisableRange<float>(0.0f, 12.0f, 0.1f), 2.4f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "highAir", "High Air",
        juce::NormalisableRange<float>(0.0f, 12.0f, 0.1f), 4.1f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mix", "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 65.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "output", "Output",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), -0.5f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
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

    midHighPass.prepare(spec);
    midLowPass.prepare(spec);
    highHighPass.prepare(spec);
    midHighPass.reset();
    midLowPass.reset();
    highHighPass.reset();

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

void EC_AIRAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    // 1. BYPASS - sale limpio
    if (bypassed)
        return;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // 2. POWER
    auto powerParam = apvts.getRawParameterValue("power");
    bool isPowered = powerParam != nullptr ? (powerParam->load() > 0.5f) : true;
    if (!isPowered)
        return;

    // 3. Parametros con seguridad - si algo viene NaN, no truena
    float midAirDb = apvts.getRawParameterValue("midAir")->load();
    float highAirDb = apvts.getRawParameterValue("highAir")->load();
    float mixPct = apvts.getRawParameterValue("mix")->load();
    float outputDb = apvts.getRawParameterValue("output")->load();

    if (!std::isfinite(midAirDb)) midAirDb = 0.0f;
    if (!std::isfinite(highAirDb)) highAirDb = 0.0f;
    if (!std::isfinite(mixPct)) mixPct = 0.0f;
    if (!std::isfinite(outputDb)) outputDb = 0.0f;

    midAirDb = juce::jlimit(0.0f, 12.0f, midAirDb);
    highAirDb = juce::jlimit(0.0f, 12.0f, highAirDb);
    mixPct = juce::jlimit(0.0f, 100.0f, mixPct);
    outputDb = juce::jlimit(-12.0f, 12.0f, outputDb);

    // Ganancias SEGURAS - antes el High Air multiplicaba x2 y x0.8 y se iba a +20dB
    // Ahora el exciter nunca puede pasar de 0dB por si solo
    float midGain = juce::Decibels::decibelsToGain(midAirDb) - 1.0f; // 0 cuando knob en 0
    float highGain = juce::Decibels::decibelsToGain(highAirDb) - 1.0f;
    float mix = mixPct / 100.0f;
    float outGain = juce::Decibels::decibelsToGain(outputDb);

    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();

    // Filtros - se actualizan 1 vez por bloque, no por sample
    double sr = getSampleRate();
    auto midHP = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 2000.0);
    auto midLP = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 9000.0);
    auto highHP = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 9000.0);
    *midHighPass.state = *midHP;
    *midLowPass.state = *midLP;
    *highHighPass.state = *highHP;

    // Buffers temporales con el MISMO tamaño que el buffer original - AQUI ESTABA EL CORTO
    juce::AudioBuffer<float> midBuffer(numChannels, numSamples);
    juce::AudioBuffer<float> highBuffer(numChannels, numSamples);
    midBuffer.makeCopyOf(buffer);
    highBuffer.makeCopyOf(buffer);

    // Procesar bandas en paralelo
    {
        juce::dsp::AudioBlock<float> b(midBuffer);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        midHighPass.process(ctx);
        midLowPass.process(ctx);
    }
    {
        juce::dsp::AudioBlock<float> b(highBuffer);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        highHighPass.process(ctx);
    }

    // Mezcla final - DRY INTACTO + AIR LIMITADO
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* out = buffer.getWritePointer(ch);
        auto* mid = midBuffer.getReadPointer(ch);
        auto* high = highBuffer.getReadPointer(ch);

        for (int s = 0; s < numSamples; ++s)
        {
            float dry = out[s]; // original, no se toca
            float midVal = mid[s];
            float highVal = high[s];

            // Si algun valor viene NaN/inf, lo resetea a 0 - esto evita el corto
            if (!std::isfinite(midVal)) midVal = 0.0f;
            if (!std::isfinite(highVal)) highVal = 0.0f;
            if (!std::isfinite(dry)) dry = 0.0f;

            // Exciter suave - tanh siempre da entre -1 y 1, nunca explota
            float midExcited = std::tanh(midVal * 1.5f);
            float highExcited = std::tanh(highVal * 2.0f);

            // Air total, limitado a max 0.5 para que nunca sature la mezcla
            float air = (midExcited * midGain * 0.35f) + (highExcited * highGain * 0.35f);
            air = juce::jlimit(-0.5f, 0.5f, air);

            float mixed = dry + (air * mix);
            // Limitador final de seguridad - nunca pasa de -0.5dB
            mixed = std::tanh(mixed * 0.9f) / 0.9f;
            mixed = juce::jlimit(-0.95f, 0.95f, mixed);

            out[s] = mixed * outGain;

            // Ultima proteccion anti-corto
            if (!std::isfinite(out[s]))
                out[s] = 0.0f;
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
