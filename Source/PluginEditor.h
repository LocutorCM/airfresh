#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class AirLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AirLookAndFeel();
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override;
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

class EC_AIRAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    EC_AIRAudioProcessorEditor(EC_AIRAudioProcessor&);
    ~EC_AIRAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    EC_AIRAudioProcessor& audioProcessor;
    AirLookAndFeel airLNF;

    // === NUEVO: IMAGEN DE FONDO ===
    // Esta imagen viene de Assets/EC-Air-Exciter-Background.png
    // via juce_add_binary_data en CMakeLists.txt
    juce::Image backgroundImage;

    juce::Slider midAirSlider;
    juce::Slider highAirSlider;
    juce::Slider mixSlider;
    juce::Slider outputSlider;

    juce::ToggleButton powerButton;
    juce::TextButton presetAirButton;
    juce::TextButton presetVocalButton;
    juce::TextButton presetMasterButton;
    juce::TextButton presetSubtleButton;

    juce::Label midAirLabel, midAirValue;
    juce::Label highAirLabel, highAirValue;
    juce::Label mixLabel, mixValue;
    juce::Label outputLabel, outputValue;
    juce::Label titleLabel, subtitleLabel, brandLabel, presetLabel, footerLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> midAirAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highAirAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;

    float meterLevel = 0.0f;
    float meterTarget = 0.0f;

    void applyPreset(float mid, float high, float mix, float out);
    void setupKnob(juce::Slider& slider, juce::Label& label, juce::Label& value, const juce::String& name);
    void setupPresetButton(juce::TextButton& btn, const juce::String& name);
    void drawMeter(juce::Graphics& g, juce::Rectangle<int> area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EC_AIRAudioProcessorEditor)
};
