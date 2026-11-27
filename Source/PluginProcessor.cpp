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
    midPct = juce::jlimit(0.0f, 100.0f, std::isfinite(midPct) ? midPct : 35.0f);
    highPct = juce::jlimit(0.0f, 100.0f, std::isfinite(highPct) ? highPct : 45.0f);
    mixPct = juce::jlimit(0.0f, 100.0f, std::isfinite(mixPct) ? mixPct : 80.0f);
    outPct = juce::jlimit(0.0f, 200.0f, std::isfinite(outPct) ? outPct : 100.0f);

    // V4: ganancias mas fuertes, porque ahora el filtro stereo si deja pasar el aire
    float midGain = (std::pow(midPct / 100.0f, 1.3f)) * 5.0f;   // 0 a 5.0
    float highGain = (std::pow(highPct / 100.0f, 1.3f)) * 6.0f; // 0 a 6.0
    float mix = mixPct / 100.0f;
    float outGain = outPct / 100.0f;

    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();
    double sr = getSampleRate();

    *sourceHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 400.0);
    *sourceLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 3500.0);
    *presenceHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 2000.0);
    *presenceLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 6000.0);
    *midHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 6000.0);
    *midLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 11000.0);
    *highHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 10000.0);
    *superHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 14000.0);
    *deesserHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 6000.0);
    *deesserLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 9000.0);
    *lowCut.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 20.0);
    *mudCut.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, 250.0, 0.8f, 0.91f);

    juce::AudioBuffer<float> dryBuf(numChannels, numSamples);
    juce::AudioBuffer<float> sourceBuf(numChannels, numSamples);
    juce::AudioBuffer<float> exciterBuf(numChannels, numSamples);
    juce::AudioBuffer<float> presBuf(numChannels, numSamples);
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

    // === V4: PRE-GANANCIA - LA CLAVE ===
    // Tu voz entra bajita (~0.05). Sin pre-gain, tanh no genera nada.
    // La subimos x4 ANTES de saturar, generamos armonicos, y luego la bajamos.
    // Asi al 100% SI se ve en el analyzer
    float midDrive = 1.5f + (midPct / 100.0f) * 2.5f;   // 1.5 a 4.0
    float highDrive = 1.8f + (highPct / 100.0f) * 3.0f; // 1.8 a 4.8
    const float preGain = 4.0f;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* src = sourceBuf.getReadPointer(ch);
        auto* exc = exciterBuf.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
        {
            float x = src[s] * preGain; // <-- aqui esta el fix
            if (!std::isfinite(x)) x = 0;
            x = juce::jlimit(-1.5f, 1.5f, x);
            float soft = std::tanh(x * midDrive);
            float hard = std::tanh(x * highDrive);
            float generated = (soft * 0.6f + hard * 0.4f) * 0.6f;
            // Quitamos el fundamental que quedo, para dejar solo armonicos
            generated -= std::tanh(x) * 0.25f;
            exc[s] = generated;
        }
    }

    presBuf.makeCopyOf(exciterBuf);
    midBuf.makeCopyOf(exciterBuf);
    highBuf.makeCopyOf(exciterBuf);
    {
        juce::dsp::AudioBlock<float> b(presBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        presenceHP.process(ctx);
        presenceLP.process(ctx);
    }
    {
        juce::dsp::AudioBlock<float> b(midBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        midHP.process(ctx);
        midLP.process(ctx);
    }
    {
        juce::dsp::AudioBlock<float> b(highBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        highHP.process(ctx);
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* out = buffer.getWritePointer(ch);
        auto* dry = dryBuf.getReadPointer(ch);
        auto* pres = presBuf.getReadPointer(ch);
        auto* midB = midBuf.getReadPointer(ch);
        auto* highB = highBuf.getReadPointer(ch);
        auto* deess = deesserBuf.getReadPointer(ch);
        int chIdx = juce::jmin(ch, 1);

        for (int s = 0; s < numSamples; ++s)
        {
            float dryClean = std::isfinite(dry[s]) ? dry[s] : 0;
            float d = std::isfinite(deess[s]) ? deess[s] : 0;

            gateEnv[chIdx] = updateEnv(gateEnv[chIdx], dryClean, 0.85f, 0.995f);
            float gate = 1.0f;
            if (gateEnv[chIdx] < 0.015f)
            {
                gate = gateEnv[chIdx] / 0.015f;
                gate *= gate;
            }

            deesserEnv[chIdx] = updateEnv(deesserEnv[chIdx], d, 0.85f, 0.998f);
            float deesserReduction = 1.0f;
            if (deesserEnv[chIdx] > 0.15f)
            {
                float excess = (deesserEnv[chIdx] - 0.15f) / 0.15f;
                deesserReduction = juce::jlimit(0.4f, 1.0f, 1.0f / (1.0f + excess * 1.5f));
            }

            float p = std::isfinite(pres[s]) ? pres[s] : 0;
            float m = std::isfinite(midB[s]) ? midB[s] : 0;
            float h = std::isfinite(highB[s]) ? highB[s] : 0;

            float air = (p * midGain * 0.9f)
                      + (m * midGain * 0.8f * (deesserReduction * 0.5f + 0.5f))
                      + (h * highGain * 1.0f * deesserReduction);

            air *= gate;
            air = std::tanh(air * 1.2f) / 1.2f;
            air = juce::jlimit(-0.4f, 0.4f, air);

            float mixed = dryClean + (air * mix);
            mixed = std::tanh(mixed * 0.9f) / 0.9f;
            mixed = juce::jlimit(-0.95f, 0.95f, mixed);
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
