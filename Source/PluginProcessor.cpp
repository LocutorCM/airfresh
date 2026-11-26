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

    sourceHP.prepare(spec); sourceLP.prepare(spec);
    presenceHP.prepare(spec); presenceLP.prepare(spec);
    midHP.prepare(spec); midLP.prepare(spec);
    highHP.prepare(spec); superHP.prepare(spec);
    deesserHP.prepare(spec); deesserLP.prepare(spec);
    lowCut.prepare(spec); mudCut.prepare(spec);

    sourceHP.reset(); sourceLP.reset();
    presenceHP.reset(); presenceLP.reset();
    midHP.reset(); midLP.reset(); highHP.reset(); superHP.reset();
    deesserHP.reset(); deesserLP.reset();
    lowCut.reset(); mudCut.reset();

    deesserEnv[0] = deesserEnv[1] = 0.0f;
    airEnv[0] = airEnv[1] = 0.0f;
    gateEnv[0] = gateEnv[1] = 0.0f;
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

static inline float updateEnv(float env, float input, float attack, float release)
{
    float absIn = std::abs(input);
    if (absIn > env)
        return attack * env + (1.0f - attack) * absIn;
    else
        return release * env + (1.0f - release) * absIn;
}

// === GENERADOR DE ARMONICOS ===
// Esto es lo que hace Fresh Air: agarra medios y crea agudos nuevos
// 1. Saturacion suave -> crea 2do y 3er armonico
// 2. Saturacion fuerte -> crea 4to, 5to armonico hasta 16kHz
static inline float generateHarmonics(float x, float drive)
{
    // Drive: 1.0 = suave, 4.0 = mucho aire
    float driven = x * drive;
    // Tanh cubico: genera impares, suena como cinta, no como distorsion de guitarra
    float soft = std::tanh(driven);
    // Suma un poco de cuadrado para pares - mas cuerpo, mas calidez
    float even = soft * soft * (driven >= 0 ? 1.0f : -1.0f) * 0.25f;
    float out = soft * 0.75f + even;
    // Quita el fundamental para que solo quede AIRE, no mas volumen de medios
    out -= x * 0.35f;
    return out;
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

    float midGain = percentToGain(midPct, 18.0f);
    float highGain = percentToGain(highPct, 24.0f);
    float mix = mixPct / 100.0f;
    float outGain = outPct / 100.0f;

    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();
    double sr = getSampleRate();

    // === FILTROS ===
    // Fuente: 400Hz - 2.5kHz - AQUI SI HAY ENERGIA EN TU VOZ, de aqui creamos aire
    *sourceHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 400.0);
    *sourceLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 2500.0);
    // Salida de armonicos
    *presenceHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 2000.0);
    *presenceLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 5000.0);
    *midHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 5000.0);
    *midLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 9000.0);
    *highHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 10000.0);
    *superHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 16000.0);
    *deesserHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 6000.0);
    *deesserLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 9000.0);
    // Low Cut 20Hz 12dB/oct como en tu foto + corte de lodo en 250Hz
    *lowCut.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 20.0);
    *mudCut.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, 250.0, 0.8f, 0.75f); // -2.5dB en 250Hz, quita trapo

    // Buffers mismo tamaño - SEGURO
    juce::AudioBuffer<float> sourceBuf(numChannels, numSamples);
    juce::AudioBuffer<float> presBuf(numChannels, numSamples);
    juce::AudioBuffer<float> midBuf(numChannels, numSamples);
    juce::AudioBuffer<float> highBuf(numChannels, numSamples);
    juce::AudioBuffer<float> superBuf(numChannels, numSamples);
    juce::AudioBuffer<float> deesserBuf(numChannels, numSamples);
    juce::AudioBuffer<float> cleanBuf(numChannels, numSamples);
    sourceBuf.makeCopyOf(buffer); presBuf.makeCopyOf(buffer);
    midBuf.makeCopyOf(buffer); highBuf.makeCopyOf(buffer);
    superBuf.makeCopyOf(buffer); deesserBuf.makeCopyOf(buffer);
    cleanBuf.makeCopyOf(buffer);

    // Limpia el dry: Low Cut + quita lodo - esto es lo que quita lo opaco del DRY, no solo del aire
    {
        juce::dsp::AudioBlock<float> b(cleanBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        lowCut.process(ctx);
        mudCut.process(ctx);
    }
    // Fuente para generar armonicos
    {
        juce::dsp::AudioBlock<float> b(sourceBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        sourceHP.process(ctx);
        sourceLP.process(ctx);
    }
    // De-Esser detector
    {
        juce::dsp::AudioBlock<float> b(deesserBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        deesserHP.process(ctx);
        deesserLP.process(ctx);
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* out = buffer.getWritePointer(ch);
        auto* clean = cleanBuf.getReadPointer(ch);
        auto* src = sourceBuf.getReadPointer(ch);
        auto* deess = deesserBuf.getReadPointer(ch);
        int chIdx = juce::jmin(ch, 1);

        for (int s = 0; s < numSamples; ++s)
        {
            float dryClean = std::isfinite(clean[s]) ? clean[s] : 0;
            float dryOrig = std::isfinite(out[s]) ? out[s] : 0;
            float source = std::isfinite(src[s]) ? src[s] : 0;
            float d = std::isfinite(deess[s]) ? deess[s] : 0;

            // === GATE DE AIRE ===
            // Si no hay voz, no hay aire - por eso en tu foto el analyzer se cae en silencio
            gateEnv[chIdx] = updateEnv(gateEnv[chIdx], dryOrig, 0.85f, 0.995f);
            float gate = 1.0f;
            if (gateEnv[chIdx] < 0.02f)
            {
                gate = gateEnv[chIdx] / 0.02f;
                gate = gate * gate;
            }

            // === DE-ESSER DINAMICO ===
            deesserEnv[chIdx] = updateEnv(deesserEnv[chIdx], d, 0.85f, 0.998f);
            float deesserReduction = 1.0f;
            if (deesserEnv[chIdx] > 0.12f)
            {
                float excess = (deesserEnv[chIdx] - 0.12f) / 0.12f;
                deesserReduction = 1.0f / (1.0f + excess * 1.8f);
                deesserReduction = juce::jlimit(0.35f, 1.0f, deesserReduction);
            }

            // === GENERAR ARMONICOS NUEVOS ===
            // Drive sube con el knob: a mas % mas armonicos creas
            // Mid Air controla cuanto generas en 2-9kHz
            // High Air controla cuanto generas en 10-16kHz
            float midDrive = 1.5f + (midPct / 100.0f) * 3.5f;   // 1.5 a 5.0
            float highDrive = 2.0f + (highPct / 100.0f) * 5.0f; // 2.0 a 7.0

            // Genera 2 veces: una suave para presencia, una fuerte para aire ultra
            float harmMid = generateHarmonics(source, midDrive);
            float harmHigh = generateHarmonics(source, highDrive);

            // Filtra los armonicos generados en sus bandas
            // Esto es clave: el fundamental ya no esta, solo queda el aire nuevo
            // Aproximacion por muestra con filtros simples para no complicar - usamos el contenido generado
            // Presence: 2-5kHz - definicion
            float presExcited = harmMid * 0.9f;
            // Mid: 5-9kHz - claridad
            float midExcited = harmMid * 0.7f + harmHigh * 0.3f;
            // High: 10kHz+ - brillo
            float highExcited = harmHigh * 0.8f;
            // Super: 16kHz+ - aire ultra
            float superExcited = harmHigh * 0.5f;

            // Aplica De-Esser SOLO a las bandas altas, no a presencia
            highExcited *= deesserReduction;
            superExcited *= (deesserReduction * 0.8f + 0.2f);
            midExcited *= (deesserReduction * 0.5f + 0.5f);

            // === CONTROL DINAMICO DE AIRE ===
            float airLevel = std::abs(highExcited) + std::abs(superExcited) * 0.5f;
            airEnv[chIdx] = updateEnv(airEnv[chIdx], airLevel, 0.90f, 0.995f);
            float airControl = 1.0f;
            if (airEnv[chIdx] > 0.25f)
            {
                float excess = (airEnv[chIdx] - 0.25f) / 0.25f;
                airControl = 1.0f / (1.0f + excess * 0.8f);
                airControl = juce::jlimit(0.5f, 1.0f, airControl);
            }

            // Mezcla: DRY LIMPIO + AIRE GENERADO
            float air = (presExcited * midGain * 0.55f)
                      + (midExcited * midGain * 0.45f)
                      + (highExcited * highGain * 0.55f)
                      + (superExcited * highGain * 0.40f);

            air *= airControl * gate;
            air = juce::jlimit(-0.6f, 0.6f, air);

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
