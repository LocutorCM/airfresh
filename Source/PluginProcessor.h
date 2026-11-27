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

    // V5: Ya no son 10 filtros de distorsion, son shelves suaves estilo Fresh Air
    StereoFilter sourceHP, sourceLP;      // Para sacar la fuente 400Hz - 3.5kHz
    StereoFilter midBandHP, midBandLP;    // 2kHz - 8kHz  -> Mid Air
    StereoFilter highBandHP;              // 8kHz - 20kHz -> High Air
    StereoFilter deesserHP, deesserLP;    // 5.5kHz - 9kHz para detectar la S
    StereoFilter lowCut, mudCut;          // Limpieza 20Hz y 250Hz

    // Envolventes suaves - ataque lento = no se siente brusco
    float deesserEnv[2] = { 0.0f, 0.0f };
    float midDynEnv[2] = { 0.0f, 0.0f };
    float highDynEnv[2] = { 0.0f, 0.0f };
    float gateEnv[2] = { 0.0f, 0.0f };
    // Smoothing para que el de-esser no haga "pumping"
    float deesserGainSmooth[2] = { 1.0f, 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EC_AIRAudioProcessor)
};
