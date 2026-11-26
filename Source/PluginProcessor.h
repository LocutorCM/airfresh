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
    const juce::AudioProcessorValueTreeState& getAPVTS() const { return apvts; }

    void setBypassed(bool shouldBypass) { bypassed = shouldBypass; }
    bool isBypassed() const { return bypassed; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

private:
    bool bypassed = false;

    // === DSP DE ALTA CALIDAD ===
    // Oversampling 4x para que el exciter no genere aliasing / harsh
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    // Filtros en paralelo, no en serie
    // Mid Air: banda 2kHz - 8kHz
    // High Air: banda 8kHz en adelante
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> midHighPass;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> midLowPass;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> highHighPass;

    // Suavizado de parametros para que no haga clicks al mover knobs
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> midAirSmooth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> highAirSmooth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmooth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputSmooth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EC_AIRAudioProcessor)
};
