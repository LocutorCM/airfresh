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

// === TODO EN % COMO FRESH AIR, NADA EN dB ===
juce::AudioProcessorValueTreeState::ParameterLayout EC_AIRAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Fresh Air: 0-100% - 0 = sin aire, 100 = maximo brillo
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "midAir", "Mid Air",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 25.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "highAir", "High Air",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 35.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mix", "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Output tambien en % como Trim de Fresh Air: 0-200% = -inf a +6dB
    // 100% = 0dB, 200% = +6dB
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

    midHighPass.prepare(spec);
    midLowPass.prepare(spec);
    highHighPass.prepare(spec);
    midShelf.prepare(spec);
    highShelf.prepare(spec);
    midHighPass.reset(); midLowPass.reset(); highHighPass.reset();
    midShelf.reset(); highShelf.reset();

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

// Curva Fresh Air: el % no es lineal, es exponencial
// 50% ya se escucha fuerte, 100% llega al maximo sin explotar
static inline float percentToGain(float percent, float maxDb)
{
    float p = juce::jlimit(0.0f, 100.0f, percent) / 100.0f;
    // Curva: p^1.6 - mas control abajo, mas potencia arriba
    float curved = std::pow(p, 1.6f);
    float db = curved * maxDb;
    // Convertir dB a ganancia, pero restar 1 para que solo sea el AIRE extra
    return juce::Decibels::decibelsToGain(db) - 1.0f;
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

    // === NIVELES FRESH AIR ===
    // Mid Air: hasta +18dB de aire - claridad de voz
    // High Air: hasta +24dB de aire - brillo sedoso, igual que Fresh Air
    float midGain = percentToGain(midPct, 18.0f);
    float highGain = percentToGain(highPct, 24.0f);
    float mix = mixPct / 100.0f;
    // Output: 100% = 0dB, 200% = +6dB, 0% = mute
    float outGain = outPct / 100.0f;

    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();
    double sr = getSampleRate();

    // === FILTROS - MISMAS FRECUENCIAS QUE FRESH AIR ===
    // Mid Air: banda 2kHz - 8kHz + Shelf en 4kHz para claridad
    auto midHP = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 2000.0);
    auto midLP = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 8000.0);
    // High Air: 10kHz en adelante + Shelf en 12kHz para brillo
    auto highHP = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 10000.0);
    // Shelfs con ganancia dinamica segun el % - esto es lo que da el brillo REAL, no solo saturacion
    auto midShelfCoeff = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr, 4000.0, 0.7f, juce::Decibels::decibelsToGain(midGain * 0.3f));
    auto highShelfCoeff = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr, 12000.0, 0.7f, juce::Decibels::decibelsToGain(highGain * 0.3f));

    *midHighPass.state = *midHP;
    *midLowPass.state = *midLP;
    *highHighPass.state = *highHP;
    *midShelf.state = *midShelfCoeff;
    *highShelf.state = *highShelfCoeff;

    // Buffers temporales mismo tamaño - SEGURO, sin corto
    juce::AudioBuffer<float> midBuffer(numChannels, numSamples);
    juce::AudioBuffer<float> highBuffer(numChannels, numSamples);
    juce::AudioBuffer<float> shelfBuffer(numChannels, numSamples);
    midBuffer.makeCopyOf(buffer);
    highBuffer.makeCopyOf(buffer);
    shelfBuffer.makeCopyOf(buffer);

    // Mid: pasa-banda 2k-8k
    {
        juce::dsp::AudioBlock<float> b(midBuffer);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        midHighPass.process(ctx);
        midLowPass.process(ctx);
    }
    // High: pasa-altas 10k
    {
        juce::dsp::AudioBlock<float> b(highBuffer);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        highHighPass.process(ctx);
    }
    // Shelf: brillo real sobre la señal completa
    {
        juce::dsp::AudioBlock<float> b(shelfBuffer);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        midShelf.process(ctx);
        highShelf.process(ctx);
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* out = buffer.getWritePointer(ch);
        auto* mid = midBuffer.getReadPointer(ch);
        auto* high = highBuffer.getReadPointer(ch);
        auto* shelf = shelfBuffer.getReadPointer(ch);

        for (int s = 0; s < numSamples; ++s)
        {
            float dry = out[s];
            float midVal = mid[s];
            float highVal = high[s];
            float shelfVal = shelf[s];

            if (!std::isfinite(dry)) dry = 0;
            if (!std::isfinite(midVal)) midVal = 0;
            if (!std::isfinite(highVal)) highVal = 0;
            if (!std::isfinite(shelfVal)) shelfVal = 0;

            // === CLARIDAD DE VOZ ===
            // Mid Air: saturacion suave + enfasis en 3kHz-5kHz donde esta la inteligibilidad
            // Esto es lo que hace que la voz se entienda aunque haya musica
            float midExcited = std::tanh(midVal * 2.2f);
            // Compresion suave en la banda Mid para que la claridad no se pierda con picos
            midExcited = midExcited / (1.0f + std::abs(midExcited) * 0.3f);

            // === BRILLO FRESH AIR ===
            // High Air: doble armonico para que suene sedoso, no harsh
            // 1er armonico brillante + 2do armonico ultra-aire
            float highExcited = std::tanh(highVal * 2.5f) * 0.7f + std::tanh(highVal * 5.0f) * 0.3f;
            highExcited = highExcited / (1.0f + std::abs(highExcited) * 0.2f);

            // Shelf: brillo real - diferencia entre señal con shelf y dry
            float shelfAir = shelfVal - dry;

            // Mezcla Fresh Air style:
            // Dry intacto + Shelf (brillo real) + Exciter (armonicos) todo * Mix
            float air = (midExcited * midGain * 0.45f)
                      + (highExcited * highGain * 0.45f)
                      + (shelfAir * 0.5f * (midGain + highGain) * 0.25f);

            air = juce::jlimit(-0.6f, 0.6f, air);

            float mixed = dry + (air * mix);
            // Limitador suave de seguridad - nunca pasa de 0dB, no mas corto
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
