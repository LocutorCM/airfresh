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

EC_AIRAudioProcessor::~EC_AIRAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout EC_AIRAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Ahora el High Air si se va a escuchar: rango mas musical y con mas resolucion
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

//==============================================================================
void EC_AIRAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
    spec.numChannels = (juce::uint32)getTotalNumOutputChannels();

    // Oversampling 4x con filtro de alta calidad
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        (int)getTotalNumOutputChannels(), 2, // 2 = 4x
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing((size_t)samplesPerBlock);
    oversampler->reset();

    juce::dsp::ProcessSpec overSpec;
    overSpec.sampleRate = sampleRate * 4.0;
    overSpec.maximumBlockSize = (juce::uint32)samplesPerBlock * 4;
    overSpec.numChannels = (juce::uint32)getTotalNumOutputChannels();

    midHighPass.prepare(overSpec);
    midLowPass.prepare(overSpec);
    highHighPass.prepare(overSpec);
    midHighPass.reset();
    midLowPass.reset();
    highHighPass.reset();

    midAirSmooth.reset(sampleRate, 0.05);
    highAirSmooth.reset(sampleRate, 0.05);
    mixSmooth.reset(sampleRate, 0.05);
    outputSmooth.reset(sampleRate, 0.05);
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

//==============================================================================
// FIX HIGH AIR + MUCHA MAS CALIDAD
// - Paralelo: Dry siempre intacto, Air se suma por encima
// - Mid Air y High Air son bandas separadas, ya no se pelean
// - Harmonicos solo en la banda alta, con oversampling para que no suene harsh
//==============================================================================
void EC_AIRAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    if (bypassed)
        return;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    auto powerParam = apvts.getRawParameterValue("power");
    bool isPowered = powerParam != nullptr ? (powerParam->load() > 0.5f) : true;
    if (!isPowered)
        return;

    float midAirDb = apvts.getRawParameterValue("midAir")->load();
    float highAirDb = apvts.getRawParameterValue("highAir")->load();
    float mixPct = apvts.getRawParameterValue("mix")->load();
    float outputDb = apvts.getRawParameterValue("output")->load();

    midAirSmooth.setTargetValue(midAirDb);
    highAirSmooth.setTargetValue(highAirDb);
    mixSmooth.setTargetValue(mixPct / 100.0f);
    outputSmooth.setTargetValue(juce::Decibels::decibelsToGain(outputDb));

    // Guardar DRY intacto - esto es lo que te da calidad, el original nunca se toca
    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf(buffer);

    // Subir a 4x para procesar el exciter sin aliasing
    juce::dsp::AudioBlock<float> block(buffer);
    auto overBlock = oversampler->processSamplesUp(block);

    // === FILTROS EN PARALELO ===
    // Los coeficientes se actualizan con la sampleRate oversampleada
    double overRate = getSampleRate() * 4.0;

    // Mid Air: pasa-banda 2kHz - 9kHz
    auto midHP = juce::dsp::IIR::Coefficients<float>::makeHighPass(overRate, 2000.0);
    auto midLP = juce::dsp::IIR::Coefficients<float>::makeLowPass(overRate, 9000.0);
    // High Air: pasa-altas 9kHz - AQUI ESTABA EL BUG, antes era shelf a 10k que se perdia
    auto highHP = juce::dsp::IIR::Coefficients<float>::makeHighPass(overRate, 9000.0);

    *midHighPass.state = *midHP;
    *midLowPass.state = *midLP;
    *highHighPass.state = *highHP;

    // Copias separadas para cada banda
    juce::AudioBuffer<float> midBuffer;
    juce::AudioBuffer<float> highBuffer;
    midBuffer.makeCopyOf(buffer);
    highBuffer.makeCopyOf(buffer);

    // --- Banda MID ---
    {
        juce::dsp::AudioBlock<float> midBlock(midBuffer);
        juce::dsp::ProcessContextReplacing<float> ctx(midBlock);
        midHighPass.process(ctx);
        midLowPass.process(ctx);
    }

    // --- Banda HIGH - ESTA ES LA QUE NO SE ESCUCHABA ---
    {
        juce::dsp::AudioBlock<float> highBlock(highBuffer);
        juce::dsp::ProcessContextReplacing<float> ctx(highBlock);
        highHighPass.process(ctx);
    }

    // --- Generar AIR ---
    // Por cada sample, sacamos armónicos suaves solo de las bandas
    // Mid: saturación suave, High: saturación más brillante y con más presencia
    int numSamples = (int)overBlock.getNumSamples();
    int numCh = (int)overBlock.getNumChannels();

    // overBlock es el buffer oversampleado, lo usamos como salida del exciter
    // Lo limpiamos y lo reconstruimos con Mid + High
    overBlock.clear();

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* out = overBlock.getChannelPointer((size_t)ch);
        auto* mid = midBuffer.getReadPointer(ch);
        auto* high = highBuffer.getReadPointer(ch);

        for (int s = 0; s < numSamples; ++s)
        {
            float midVal = mid[s];
            float highVal = high[s];

            // Mid Air: armónico par suave, da cuerpo y presencia
            float midExcited = std::tanh(midVal * 2.0f) * 0.5f;
            // High Air: armónico brillante, da aire y brillo sin harsh
            // Antes usabas tanh(x*1.5)*0.7 en TODO y lo aplastabas, ahora solo en la banda alta
            float highExcited = std::tanh(highVal * 3.0f) * 0.6f
                              + std::tanh(highVal * 6.0f) * 0.2f;

            // Ganancia por knob, con suavizado
            float mGain = juce::Decibels::decibelsToGain(midAirSmooth.getNextValue());
            float hGain = juce::Decibels::decibelsToGain(highAirSmooth.getNextValue());

            // El Air se suma, no reemplaza
            out[s] = (midExcited * mGain * 0.5f) + (highExcited * hGain * 0.8f);
        }
    }

    // Bajar de 4x a sample rate normal
    oversampler->processSamplesDown(block);

    // --- MIX DRY / WET + OUTPUT ---
    // Dry intacto + Air * Mix. Ya no haces dry+wet que te cancelaba.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* wet = buffer.getWritePointer(ch);
        auto* dry = dryBuffer.getReadPointer(ch);
        for (int s = 0; s < buffer.getNumSamples(); ++s)
        {
            float mix = mixSmooth.getNextValue();
            float outGain = outputSmooth.getNextValue();
            // Dry + (Air * Mix) * Output
            wet[s] = (dry[s] + wet[s] * mix * 2.0f) * outGain;
        }
    }
}

//==============================================================================
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
