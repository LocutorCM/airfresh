#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
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

EC_AIRAudioProcessor::~EC_AIRAudioProcessor()
{
}

//==============================================================================
// Layout de parametros - MISMOS IDs que usa el Editor, NO los cambies
// Si los cambias, el Editor ya no los encuentra
//==============================================================================
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

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "power", "Power", true));

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String EC_AIRAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool EC_AIRAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool EC_AIRAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool EC_AIRAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double EC_AIRAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int EC_AIRAudioProcessor::getNumPrograms()
{
    return 1;
}

int EC_AIRAudioProcessor::getCurrentProgram()
{
    return 0;
}

void EC_AIRAudioProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String EC_AIRAudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void EC_AIRAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void EC_AIRAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
    spec.numChannels = (juce::uint32)getTotalNumOutputChannels();

    // Filtros del exciter - TU DSP ORIGINAL
    midFilter.prepare(spec);
    highFilter.prepare(spec);
    midFilter.reset();
    highFilter.reset();
}

void EC_AIRAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool EC_AIRAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

//==============================================================================
// processBlock - TU AUDIO INTACTO, solo se agrego BYPASS y POWER al inicio
//==============================================================================
void EC_AIRAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    // === BYPASS - si esta activado, pasa el audio limpio sin procesar ===
    if (bypassed)
        return;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // === POWER - si esta apagado, no procesa, deja el audio limpio ===
    auto powerParam = apvts.getRawParameterValue("power");
    bool isPowered = powerParam != nullptr ? (powerParam->load() > 0.5f) : true;
    if (!isPowered)
        return;

    // === TU DSP ORIGINAL - NO SE MODIFICO LA CALIDAD ===
    float midAirDb = apvts.getRawParameterValue("midAir")->load();
    float highAirDb = apvts.getRawParameterValue("highAir")->load();
    float mixPct = apvts.getRawParameterValue("mix")->load();
    float outputDb = apvts.getRawParameterValue("output")->load();

    float mix = mixPct / 100.0f;
    float outputGain = juce::Decibels::decibelsToGain(outputDb);

    // Actualizar filtros con los valores actuales
    // Mid Air: shelf en ~3.5kHz, High Air: shelf en ~10kHz
    auto midCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        getSampleRate(), 3500.0, 0.7, juce::Decibels::decibelsToGain(midAirDb));
    auto highCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        getSampleRate(), 10000.0, 0.7, juce::Decibels::decibelsToGain(highAirDb));

    *midFilter.state = *midCoeffs;
    *highFilter.state = *highCoeffs;

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);

    // Guardar señal seca para el MIX
    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf(buffer);

    // Procesar: exciter = señal filtrada + saturacion suave
    midFilter.process(context);
    highFilter.process(context);

    // Saturacion suave tipo exciter (tanh) - le da el aire sin sonar harsh
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        for (int s = 0; s < buffer.getNumSamples(); ++s)
        {
            float x = data[s];
            // Excitacion: solo la parte brillante se satura
            data[s] = std::tanh(x * 1.5f) * 0.7f;
        }
    }

    // Mezcla Dry/Wet + Output
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* wet = buffer.getWritePointer(ch);
        auto* dry = dryBuffer.getReadPointer(ch);
        for (int s = 0; s < buffer.getNumSamples(); ++s)
        {
            float drySample = dry[s];
            float wetSample = wet[s];
            float mixed = drySample * (1.0f - mix) + (drySample + wetSample) * mix;
            wet[s] = mixed * outputGain;
        }
    }
}

//==============================================================================
bool EC_AIRAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* EC_AIRAudioProcessor::createEditor()
{
    return new EC_AIRAudioProcessorEditor(*this);
}

//==============================================================================
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

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EC_AIRAudioProcessor();
}
