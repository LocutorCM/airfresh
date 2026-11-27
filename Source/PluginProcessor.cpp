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
    const auto channels = (juce::uint32)juce::jmax(1, getTotalNumOutputChannels());
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)juce::jmax(1, samplesPerBlock), channels };

    sourceHP.prepare(spec); sourceLP.prepare(spec);
    midHarmHP.prepare(spec); midHarmLP.prepare(spec); highHarmHP.prepare(spec);
    midEQHP.prepare(spec); midEQLP.prepare(spec); highEQHP.prepare(spec);
    deesserHP.prepare(spec); deesserLP.prepare(spec);
    lowCut.prepare(spec); mudCut.prepare(spec);
    exciterLowPass.prepare(spec); highSmoothLP.prepare(spec); highCleanSmoothLP.prepare(spec);

    sourceHP.reset(); sourceLP.reset();
    midHarmHP.reset(); midHarmLP.reset(); highHarmHP.reset();
    midEQHP.reset(); midEQLP.reset(); highEQHP.reset();
    deesserHP.reset(); deesserLP.reset();
    lowCut.reset(); mudCut.reset();
    exciterLowPass.reset(); highSmoothLP.reset(); highCleanSmoothLP.reset();

    const int ch = juce::jmax(1, getTotalNumOutputChannels());
    const int block = juce::jmax(1, samplesPerBlock);
    sourceScratch.setSize(ch, block, false, false, true);
    exciterScratch.setSize(ch, block, false, false, true);
    midScratch.setSize(ch, block, false, false, true);
    highScratch.setSize(ch, block, false, false, true);
    cleanMidScratch.setSize(ch, block, false, false, true);
    cleanHighScratch.setSize(ch, block, false, false, true);

    for (int i = 0; i < 2; ++i)
    {
        deesserEnv[i] = midDynEnv[i] = highDynEnv[i] = gateEnv[i] = 0.0f;
        deesserGainSmooth[i] = 1.0f;
    }
    midGainSmooth = highGainSmooth = 0.0f;
    mixSmooth = 0.8f;
    outputSmooth = 1.0f;
}

void EC_AIRAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool EC_AIRAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;
   #if ! JucePlugin_IsSynth
    if (output != layouts.getMainInputChannelSet())
        return false;
   #endif
    return true;
}
#endif

static inline float smoothEnvelope(float env, float input, float attackCoefficient, float releaseCoefficient)
{
    const float coefficient = std::abs(input) > env ? attackCoefficient : releaseCoefficient;
    const float level = std::abs(input);
    return coefficient * env + (1.0f - coefficient) * level;
}

static inline float safeCutoff(double sampleRate, float requested)
{
    return juce::jlimit(10.0f, (float)(sampleRate * 0.44), requested);
}

void EC_AIRAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin(buffer.getNumChannels(), 2);
    if (bypassed || numSamples == 0)
        return;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);

    const auto* power = apvts.getRawParameterValue("power");
    if (power != nullptr && power->load() < 0.5f)
        return;

    const auto readParam = [this](const char* id, float fallback)
    {
        const auto* value = apvts.getRawParameterValue(id);
        const float v = value != nullptr ? value->load() : fallback;
        return std::isfinite(v) ? v : fallback;
    };

    const float midPct = juce::jlimit(0.0f, 100.0f, readParam("midAir", 35.0f));
    const float highPct = juce::jlimit(0.0f, 100.0f, readParam("highAir", 45.0f));
    const float mixPct = juce::jlimit(0.0f, 100.0f, readParam("mix", 80.0f));
    const float outputPct = juce::jlimit(0.0f, 200.0f, readParam("output", 100.0f));

    // Spectral lift is calculated in dB below; no nonlinear drive stage is used.
    const float mixTarget = mixPct * 0.01f;
    const float outputTarget = outputPct * 0.01f;

    const double sr = juce::jmax(22050.0, getSampleRate());
    // All coefficient updates are sample-rate safe. Independent filter instances
    // preserve separate histories for the clean and harmonic paths.
    *sourceHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, safeCutoff(sr, 650.0f));
    *sourceLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, safeCutoff(sr, 14500.0f));
    *midHarmHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, safeCutoff(sr, 1800.0f));
    *midHarmLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, safeCutoff(sr, 8500.0f));
    *highHarmHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, safeCutoff(sr, 8500.0f));
    *midEQHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, safeCutoff(sr, 1800.0f));
    *midEQLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, safeCutoff(sr, 8500.0f));
    *highEQHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, safeCutoff(sr, 8500.0f));
    *deesserHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, safeCutoff(sr, 5200.0f));
    *deesserLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, safeCutoff(sr, 11500.0f));
    *lowCut.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, safeCutoff(sr, 28.0f));
    *mudCut.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, safeCutoff(sr, 280.0f), 0.8f, 0.94f);
    *exciterLowPass.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, safeCutoff(sr, 17000.0f));
    *highSmoothLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, safeCutoff(sr, 17000.0f));
    *highCleanSmoothLP.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, safeCutoff(sr, 19500.0f));

    // Scratch buffers are allocated in prepareToPlay; resize only for an unexpected
    // host block larger than the advertised maximum.
    if (sourceScratch.getNumSamples() < numSamples || sourceScratch.getNumChannels() < numChannels)
    {
        sourceScratch.setSize(numChannels, numSamples, false, false, true);
        exciterScratch.setSize(numChannels, numSamples, false, false, true);
        midScratch.setSize(numChannels, numSamples, false, false, true);
        highScratch.setSize(numChannels, numSamples, false, false, true);
    }

    for (int ch = 0; ch < numChannels; ++ch)
        sourceScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    {
        juce::dsp::AudioBlock<float> block(sourceScratch);
        auto active = block.getSubBlock(0, (size_t)numSamples);
        juce::dsp::ProcessContextReplacing<float> context(active);
        lowCut.process(context);
        mudCut.process(context);
        sourceHP.process(context);
        sourceLP.process(context);
    }

    // Build clean spectral bands from the original input. This is intentionally
    // not a waveshaper: Fresh-Air-style enhancement should open the top end without
    // adding broadband distortion or clipping the vocal.
    if (cleanMidScratch.getNumSamples() < numSamples || cleanMidScratch.getNumChannels() < numChannels)
    {
        cleanMidScratch.setSize(numChannels, numSamples, false, false, true);
        cleanHighScratch.setSize(numChannels, numSamples, false, false, true);
    }
    for (int ch = 0; ch < numChannels; ++ch)
    {
        cleanMidScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);
        cleanHighScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);
    }
    {
        juce::dsp::AudioBlock<float> block(cleanMidScratch);
        auto active = block.getSubBlock(0, (size_t)numSamples);
        juce::dsp::ProcessContextReplacing<float> context(active);
        midEQHP.process(context);
        midEQLP.process(context);
    }
    {
        juce::dsp::AudioBlock<float> block(cleanHighScratch);
        auto active = block.getSubBlock(0, (size_t)numSamples);
        juce::dsp::ProcessContextReplacing<float> context(active);
        highEQHP.process(context);
        highCleanSmoothLP.process(context);
    }

    const float gainSmoothing = std::exp(-1.0f / (0.030f * (float)sr));
    const float mixSmoothing = std::exp(-1.0f / (0.020f * (float)sr));
    const float outputSmoothingCoeff = std::exp(-1.0f / (0.020f * (float)sr));
    const float highAttack = std::exp(-1.0f / (0.004f * (float)sr));
    const float highRelease = std::exp(-1.0f / (0.120f * (float)sr));

    // The controls set the amount of spectral lift, not the level of a distorted
    // signal. Mid Air reaches a gentle +3 dB; High Air reaches +6 dB.
    const float midDb = std::pow(midPct / 100.0f, 1.35f) * 3.0f;
    const float highDb = std::pow(highPct / 100.0f, 1.35f) * 6.0f;
    const float midBoostTarget = juce::Decibels::decibelsToGain(midDb) - 1.0f;
    const float highBoostTarget = juce::Decibels::decibelsToGain(highDb) - 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        midGainSmooth = gainSmoothing * midGainSmooth + (1.0f - gainSmoothing) * midBoostTarget;
        highGainSmooth = gainSmoothing * highGainSmooth + (1.0f - gainSmoothing) * highBoostTarget;
        mixSmooth = mixSmoothing * mixSmooth + (1.0f - mixSmoothing) * mixTarget;
        outputSmooth = outputSmoothingCoeff * outputSmooth + (1.0f - outputSmoothingCoeff) * outputTarget;

        // Small automatic level compensation prevents the added high-band energy
        // from pushing an already-hot input into clipping.
        const float autoTrim = 1.0f / (1.0f + 0.12f * (midGainSmooth + highGainSmooth) * mixSmooth);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* out = buffer.getWritePointer(ch);
            const float* midBand = cleanMidScratch.getReadPointer(ch);
            const float* highBand = cleanHighScratch.getReadPointer(ch);
            const int channel = juce::jmin(ch, 1);

            const float input = std::isfinite(out[i]) ? out[i] : 0.0f;
            const float highLevel = std::abs(highBand[i]);
            const float deEssTarget = juce::jlimit(0.72f, 1.0f,
                1.0f - juce::jmax(0.0f, highLevel - 0.20f) * 0.35f);
            const float deEssCoeff = deEssTarget < deesserGainSmooth[channel] ? highAttack : highRelease;
            deesserGainSmooth[channel] = deEssCoeff * deesserGainSmooth[channel]
                                       + (1.0f - deEssCoeff) * deEssTarget;

            const float spectralLift = (midBand[i] * midGainSmooth)
                                     + (highBand[i] * highGainSmooth * deesserGainSmooth[channel]);
            const float processed = (input + spectralLift * mixSmooth) * outputSmooth * autoTrim;
            out[i] = std::isfinite(processed) ? processed : 0.0f;
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
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EC_AIRAudioProcessor();
}
