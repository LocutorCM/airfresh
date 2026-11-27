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

    // === FIX V4: Filtros STEREO de verdad ===
    // Antes: juce::dsp::IIR::Filter<float> = MONO, se cancelaba en stereo
    // Ahora: ProcessorDuplicator = un filtro por canal, no se pisa
    using StereoFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    StereoFilter sourceHP, sourceLP;
    StereoFilter presenceHP, presenceLP;
    StereoFilter midHP, midLP;
    StereoFilter highHP, superHP;
    StereoFilter deesserHP, deesserLP;
    StereoFilter lowCut, mudCut;

    float deesserEnv[2] = { 0.0f, 0.0f };
    float airEnv[2] = { 0.0f, 0.0f };
    float gateEnv[2] = { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EC_AIRAudioProcessor)
};
