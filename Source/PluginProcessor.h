#pragma once
#include <JuceHeader.h>

class EC_AIRAudioProcessor : public juce::AudioProcessor
{
public:
    EC_AIRAudioProcessor();
    ~EC_AIRAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    void setBypassed(bool b) { bypassed = b; }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    bool bypassed = false;

    using StereoFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    StereoFilter sourceHP, sourceLP;
    StereoFilter midHarmHP, midHarmLP;
    StereoFilter highHarmHP;
    StereoFilter midEQHP, midEQLP;
    StereoFilter highEQHP;
    StereoFilter deesserHP, deesserLP;
    StereoFilter lowCut, mudCut;
    StereoFilter exciterLowPass;
    StereoFilter highSmoothLP;
    StereoFilter highCleanSmoothLP;

    // Reused work buffers: avoid per-block allocations in the normal audio path.
    juce::AudioBuffer<float> sourceScratch, exciterScratch, midScratch, highScratch;
    juce::AudioBuffer<float> cleanMidScratch, cleanHighScratch;

    float deesserEnv[2] = { 0.0f, 0.0f };
    float midDynEnv[2] = { 0.0f, 0.0f };
    float highDynEnv[2] = { 0.0f, 0.0f };
    float gateEnv[2] = { 0.0f, 0.0f };
    float deesserGainSmooth[2] = { 1.0f, 1.0f };

    // Smoothed controls are shared across channels so stereo movement stays linked.
    float midGainSmooth = 0.0f;
    float highGainSmooth = 0.0f;
    float mixSmooth = 0.8f;
    float outputSmooth = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EC_AIRAudioProcessor)
};
