#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// LookAndFeel que dibuja los knobs EXACTO como la imagen:
// cuerpo azul claro / blanco, borde, arco cyan con glow, indicador blanco-cyan
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

    // Imagen de fondo que subiste a GitHub
    // Assets/EC-Air-Exciter-Background.png via juce_add_binary_data
    juce::Image backgroundImage;

    // Knobs - AHORA TODO EN % ESTILO FRESH AIR
    // Mid Air: 0-100% | High Air: 0-100% | Mix: 0-100% | Output: 0-200% (100% = 0dB)
    juce::Slider midAirSlider;
    juce::Slider highAirSlider;
    juce::Slider mixSlider;
    juce::Slider outputSlider;

    // Botones - mismos que en la imagen
    juce::ToggleButton powerButton; // Switch POWER
    juce::TextButton bypassButton;   // BYPASS

    // Preset como en la imagen: dropdown arriba a la derecha
    juce::ComboBox presetCombo;
    juce::Label presetLabel;

    // Labels de valores - van debajo de cada knob como en la imagen
    // Ahora muestran "45%" no "4.5 dB"
    juce::Label midAirValue;
    juce::Label highAirValue;
    juce::Label mixValue;
    juce::Label outputValue;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> midAirAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highAirAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;

    void applyPreset(int index);
    void setupKnob(juce::Slider& slider);
    void updateValueLabels();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EC_AIRAudioProcessorEditor)
};
