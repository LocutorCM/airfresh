#pragma once
#include <JuceHeader.h>
#include <array>

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

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    // CORREGIDO: Esto es lo que faltaba para que no se caiga al abrir en Monterey
    // Antes recreabas los coeficientes en processBlock, ahora solo cuando cambia el knob
    void updateFilters(double sampleRate);

    std::array<juce::dsp::IIR::Filter<float>, 2> midHighPass;
    std::array<juce::dsp::IIR::Filter<float>, 2> highHighPass;
    std::array<juce::dsp::IIR::Filter<float>, 2> midShelf;
    std::array<juce::dsp::IIR::Filter<float>, 2> highShelf;

    std::atomic<float>* midAirParam = nullptr;
    std::atomic<float>* highAirParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* powerParam = nullptr;

    // CORREGIDO: Guardan el ultimo valor para no recalcular en cada bloque
    float lastMidAir = -1.0f;
    float lastHighAir = -1.0f;

    float excite(float x, float amount);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EC_AIRAudioProcessor)
};
