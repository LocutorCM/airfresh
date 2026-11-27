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

    // V7 FIX: Filtros SEPARADOS - antes usaba el mismo filtro para EQ y para armonicos y por eso sonaba a interferencia
    StereoFilter sourceHP, sourceLP;
    StereoFilter midHarmHP, midHarmLP;   // Solo para armonicos 2k-8k
    StereoFilter highHarmHP;             // Solo para armonicos 8k-16k
    StereoFilter midEQHP, midEQLP;       // Solo para EQ directo 2k-8k
    StereoFilter highEQHP;               // Solo para EQ directo 8k-20k
    StereoFilter deesserHP, deesserLP;
    StereoFilter lowCut, mudCut;
    StereoFilter exciterLowPass;         // NUEVO: quita el aliasing que sonaba a interferencia
    StereoFilter highSmoothLP;           // NUEVO: suaviza el High para que no pique

    float deesserEnv[2] = { 0.0f, 0.0f };
    float midDynEnv[2] = { 0.0f, 0.0f };
    float highDynEnv[2] = { 0.0f, 0.0f };
    float gateEnv[2] = { 0.0f, 0.0f };
    float deesserGainSmooth[2] = { 1.0f, 1.0f };
    // NUEVO V7: Smoothing de ganancias - antes al mover el knob sonaba el "zipper" como interferencia
    float midGainSmooth = 0.0f;
    float highGainSmooth = 0.0f;
    float mixSmooth = 0.8f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EC_AIRAudioProcessor)
};
