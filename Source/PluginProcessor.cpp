#include "PluginProcessor.h"
#include "PluginEditor.h"

EC_AIRAudioProcessor::EC_AIRAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    midAirParam  = apvts.getRawParameterValue("midAir");
    highAirParam = apvts.getRawParameterValue("highAir");
    mixParam     = apvts.getRawParameterValue("mix");
    outputParam  = apvts.getRawParameterValue("output");
    powerParam   = apvts.getRawParameterValue("power");
}

EC_AIRAudioProcessor::~EC_AIRAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout EC_AIRAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("midAir", "Mid Air", 0.0f, 12.0f, 2.4f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("highAir", "High Air", 0.0f, 12.0f, 4.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 100.0f, 65.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("output", "Output", -12.0f, 12.0f, -0.5f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("power", "Power", true));
    return { params.begin(), params.end() };
}

void EC_AIRAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = 2;
    for (int ch = 0; ch < 2; ++ch)
    {
        midHighPass[ch].prepare(spec);
        highHighPass[ch].prepare(spec);
        midShelf[ch].prepare(spec);
        highShelf[ch].prepare(spec);
        midHighPass[ch].reset();
        highHighPass[ch].reset();
        midShelf[ch].reset();
        highShelf[ch].reset();
        // Fresh Air Original: Mid 3.5k / High 7k
        *midHighPass[ch].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 3500.0);
        *highHighPass[ch].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 7000.0);
        *midShelf[ch].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate, 6000.0, 0.7f, 1.0f);
        *highShelf[ch].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate, 12000.0, 0.7f, 1.0f);
    }
    lastMidAir = -1.0f;
    lastHighAir = -1.0f;
    updateFilters(sampleRate);
}

void EC_AIRAudioProcessor::updateFilters(double sampleRate)
{
    float midAmount = midAirParam != nullptr ? midAirParam->load() : 0.0f;
    float highAmount = highAirParam != nullptr ? highAirParam->load() : 0.0f;
    if (std::abs(midAmount - lastMidAir) < 0.001f && std::abs(highAmount - lastHighAir) < 0.001f)
        return;
    lastMidAir = midAmount;
    lastHighAir = highAmount;
    // ARREGLO FRESH AIR: El High Shelf SI sube con el knob, por eso se escucha el brillo
    // Antes lo tenias en 1.0f (0dB) por eso no hacia nada
    float midGain = juce::Decibels::decibelsToGain(midAmount * 0.8f); // Mid: hasta +9.6dB
    float highGain = juce::Decibels::decibelsToGain(highAmount * 1.2f); // High: hasta +14.4dB EXAGERADO
    for (int ch = 0; ch < 2; ++ch)
    {
        *midShelf[ch].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate, 5500.0, 0.6f, midGain);
        *highShelf[ch].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate, 11000.0, 0.6f, highGain);
    }
}

void EC_AIRAudioProcessor::releaseResources() {}

bool EC_AIRAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    return true;
}

// EXCITADOR FRESH AIR - Exagerado
float EC_AIRAudioProcessor::excite(float x, float amount)
{
    if (amount <= 0.001f)
        return 0.0f;
    // Drive bien fuerte para que genere armonicos aunque la señal sea chiquita
    float driven = x * (1.0f + amount * 6.0f);
    float saturated = std::tanh(driven);
    // Armonico par = brillo dulce, no distorsion fea
    float even = saturated * saturated * 0.6f;
    if (saturated < 0.0f)
        even = -even;
    // Armonico impar = presencia
    float odd = saturated * 0.8f;
    float exciter = (odd + even * 0.5f) - x * 0.15f;
    return exciter * amount;
}

void EC_AIRAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    if (powerParam == nullptr || powerParam->load() < 0.5f)
        return;

    float midAmount = midAirParam != nullptr ? midAirParam->load() : 0.0f;
    float highAmount = highAirParam != nullptr ? highAirParam->load() : 0.0f;
    float mix = mixParam != nullptr ? (mixParam->load() / 100.0f) : 0.65f;
    float outGain = outputParam != nullptr ? juce::Decibels::decibelsToGain(outputParam->load()) : 1.0f;

    float midNorm = juce::jlimit(0.0f, 1.0f, midAmount / 12.0f);
    float highNorm = juce::jlimit(0.0f, 1.0f, highAmount / 12.0f);

    updateFilters(getSampleRate());

    for (int ch = 0; ch < totalNumInputChannels; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        int chIdx = juce::jmin(ch, 1);
        auto& midHP = midHighPass[chIdx];
        auto& highHP = highHighPass[chIdx];
        auto& midSh = midShelf[chIdx];
        auto& highSh = highShelf[chIdx];
        for (int s = 0; s < buffer.getNumSamples(); ++s)
        {
            float dry = data[s];

            // 1. Saca solo agudos
            float midBand = midHP.processSample(dry);
            float highBand = highHP.processSample(dry);

            // 2. Excita - High con el doble de ganancia porque viene mas bajito
            float midAir = excite(midBand * 2.0f, midNorm);
            float highAir = excite(highBand * 6.0f, highNorm);

            // 3. TRUCO FRESH AIR: Ademas del excitador, le suma el Shelf con boost
            // Por eso el High si se escucha aunque la voz no tenga mucho arriba
            float midBoost = midSh.processSample(dry) - dry;
            float highBoost = highSh.processSample(dry) - dry;

            // 4. Mezcla todo: Excitador + Boost = Brillo exagerado igual al original
            float air = midAir * 1.2f + highAir * 2.5f + midBoost * 0.8f + highBoost * 1.5f;

            float wet = dry + air;
            float mixed = dry * (1.0f - mix) + wet * mix;
            data[s] = juce::jlimit(-1.0f, 1.0f, mixed * outGain);
        }
    }
}

juce::AudioProcessorEditor* EC_AIRAudioProcessor::createEditor()
{
    return new EC_AIRAudioProcessorEditor(*this);
}

bool EC_AIRAudioProcessor::hasEditor() const { return true; }
const juce::String EC_AIRAudioProcessor::getName() const { return JucePlugin_Name; }
bool EC_AIRAudioProcessor::acceptsMidi() const { return false; }
bool EC_AIRAudioProcessor::producesMidi() const { return false; }
bool EC_AIRAudioProcessor::isMidiEffect() const { return false; }
double EC_AIRAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int EC_AIRAudioProcessor::getNumPrograms() { return 1; }
int EC_AIRAudioProcessor::getCurrentProgram() { return 0; }
void EC_AIRAudioProcessor::setCurrentProgram(int) {}
const juce::String EC_AIRAudioProcessor::getProgramName(int) { return {}; }
void EC_AIRAudioProcessor::changeProgramName(int, const juce::String&) {}

void EC_AIRAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void EC_AIRAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EC_AIRAudioProcessor();
}
