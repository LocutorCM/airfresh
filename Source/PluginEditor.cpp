#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// AirLookAndFeel - Knobs estilo imagen: azul claro + arco cyan glow
//==============================================================================
AirLookAndFeel::AirLookAndFeel()
{
    setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff29b6f6));
    setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xffb0d8f0));
    setColour(juce::Slider::thumbColourId, juce::Colour(0xffffffff));
}

void AirLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                      juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height).reduced(6.0f);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto centre = bounds.getCentre();
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // --- Arco de fondo (gris azulado muy suave) ---
    juce::Path bgArc;
    bgArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colour(0xffcfe6f5));
    g.strokePath(bgArc, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // --- Arco activo cyan con glow (como en la imagen) ---
    if (sliderPos > 0.001f)
    {
        juce::Path activeArc;
        activeArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, angle, true);

        // Glow exterior
        g.setColour(juce::Colour(0xff29b6f6).withAlpha(0.35f));
        g.strokePath(activeArc, juce::PathStrokeType(10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // Arco principal
        g.setColour(juce::Colour(0xff29b6f6));
        g.strokePath(activeArc, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // Punto brillante al final
        auto endPoint = centre.getPointOnCircumference(radius, angle);
        g.setColour(juce::Colours::white);
        g.fillEllipse(endPoint.x - 3.0f, endPoint.y - 3.0f, 6.0f, 6.0f);
    }

    // --- Cuerpo del knob: gradiente azul claro a blanco como en la imagen ---
    auto knobRadius = radius - 12.0f;
    juce::Rectangle<float> knobRect(centre.x - knobRadius, centre.y - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);

    juce::ColourGradient grad(juce::Colour(0xfff2f9ff), centre.x, centre.y - knobRadius,
                              juce::Colour(0xffb8d6ec), centre.x, centre.y + knobRadius, false);
    g.setGradientFill(grad);
    g.fillEllipse(knobRect);

    // Sombra suave abajo
    g.setColour(juce::Colour(0x22000000));
    g.drawEllipse(knobRect, 1.0f);

    // Borde fino
    g.setColour(juce::Colour(0xff9cc8e8));
    g.drawEllipse(knobRect, 1.5f);

    // --- Indicador blanco-cyan arriba ---
    juce::Path indicator;
    auto indicatorLength = knobRadius * 0.75f;
    auto indicatorWidth = knobRadius * 0.08f;
    indicator.addRectangle(-indicatorWidth / 2.0f, -knobRadius * 0.92f, indicatorWidth, indicatorLength);
    g.setColour(juce::Colours::white);
    g.fillPath(indicator, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));

    juce::ignoreUnused(slider);
}

void AirLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                      bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    // Switch POWER estilo imagen: pill con cyan cuando esta ON
    auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);
    bool isOn = button.getToggleState();

    juce::Colour bg = isOn ? juce::Colour(0xff29b6f6) : juce::Colour(0xffcbd8e4);
    g.setColour(bg);
    g.fillRoundedRectangle(bounds, bounds.getHeight() / 2.0f);

    // Brillo interior cyan si esta ON
    if (isOn)
    {
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.fillRoundedRectangle(bounds.reduced(2.0f), bounds.getHeight() / 2.0f);
    }

    // Bolita blanca del switch
    float knobSize = bounds.getHeight() - 6.0f;
    float knobX = isOn ? bounds.getRight() - knobSize - 3.0f : bounds.getX() + 3.0f;
    g.setColour(juce::Colours::white);
    g.fillEllipse(knobX, bounds.getY() + 3.0f, knobSize, knobSize);

    juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
}

//==============================================================================
// Editor
//==============================================================================
EC_AIRAudioProcessorEditor::EC_AIRAudioProcessorEditor(EC_AIRAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // Tamaño igual a la imagen: 800 x 520
    setSize(800, 520);

    // --- Cargar imagen de fondo de GitHub ---
    // Si no existe, EC_HAS_BACKGROUND_IMAGE = 0 y usa fondo plano
#if EC_HAS_BACKGROUND_IMAGE
    int dataSize = 0;
    const void* data = BinaryData::getNamedResource("EC_AIR_EXCITER_BACKGROUND_PNG", dataSize);
    if (data != nullptr && dataSize > 0)
        backgroundImage = juce::ImageCache::getFromMemory(data, dataSize);
    // Fallback por si el nombre generado es diferente
    if (backgroundImage.isNull())
    {
        // Intenta con el nombre alternativo que genera JUCE
        data = BinaryData::getNamedResource("EC_AIR_EXCITER_BACKGROUND_PNG", dataSize);
        if (data != nullptr)
            backgroundImage = juce::ImageCache::getFromMemory(data, dataSize);
    }
#endif

    // --- Knobs ---
    setupKnob(midAirSlider);
    setupKnob(highAirSlider);
    setupKnob(mixSlider);
    setupKnob(outputSlider);

    // --- POWER (switch) ---
    powerButton.setClickingTogglesState(true);
    powerButton.setLookAndFeel(&airLNF);
    addAndMakeVisible(powerButton);

    // --- BYPASS ---
    bypassButton.setButtonText("BYPASS");
    bypassButton.setClickingTogglesState(true);
    bypassButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffeef4f9));
    bypassButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff5a7a96));
    bypassButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff29b6f6));
    addAndMakeVisible(bypassButton);
    bypassButton.onClick = [this]()
    {
        // No toca tu DSP, solo usa el bypass nativo de JUCE / host
        audioProcessor.setBypassed(bypassButton.getToggleState());
    };

    // --- Preset ComboBox como en la imagen (arriba derecha) ---
    presetCombo.addItem("Warm & Airy", 1);
    presetCombo.addItem("Vocal", 2);
    presetCombo.addItem("Master", 3);
    presetCombo.addItem("Subtle", 4);
    presetCombo.setSelectedId(1, juce::dontSendNotification);
    presetCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colours::white.withAlpha(0.0f));
    presetCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xff2a6ea5));
    presetCombo.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff5a7a96));
    presetCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xffb0d8f0));
    addAndMakeVisible(presetCombo);
    presetCombo.onChange = [this]() { applyPreset(presetCombo.getSelectedId()); };

    presetLabel.setText("Preset:", juce::dontSendNotification);
    presetLabel.setColour(juce::Label::textColourId, juce::Colour(0xff5a7a96));
    presetLabel.setFont(juce::Font(16.0f));
    presetLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(presetLabel);

    // --- Labels de valores ---
    auto setupValueLabel = [](juce::Label& label)
    {
        label.setColour(juce::Label::textColourId, juce::Colour(0xff2a6ea5));
        label.setFont(juce::Font(16.0f, juce::Font::bold));
        label.setJustificationType(juce::Justification::centred);
    };
    setupValueLabel(midAirValue);
    setupValueLabel(highAirValue);
    setupValueLabel(mixValue);
    setupValueLabel(outputValue);
    addAndMakeVisible(midAirValue);
    addAndMakeVisible(highAirValue);
    addAndMakeVisible(mixValue);
    addAndMakeVisible(outputValue);

    // --- Attachments - MISMOS PARAMETROS, MISMO AUDIO ---
    // Si tus IDs son diferentes, cambia solo estos strings
    auto& apvts = audioProcessor.getAPVTS();
    midAirAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "midAir", midAirSlider);
    highAirAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "highAir", highAirSlider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "mix", mixSlider);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "output", outputSlider);
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, "power", powerButton);

    startTimerHz(30);
}

EC_AIRAudioProcessorEditor::~EC_AIRAudioProcessorEditor()
{
    stopTimer();
    powerButton.setLookAndFeel(nullptr);
}

void EC_AIRAudioProcessorEditor::setupKnob(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setLookAndFeel(&airLNF);
    slider.setDoubleClickReturnValue(true, 0.0);
    addAndMakeVisible(slider);
}

void EC_AIRAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Fondo = tu imagen de GitHub, estirada a todo el editor
    if (backgroundImage.isValid())
    {
        g.drawImage(backgroundImage, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    }
    else
    {
        // Fallback si falta la imagen: fondo plano igual a la imagen
        g.fillAll(juce::Colour(0xffeaf4fb));
        g.setColour(juce::Colour(0xff2a6ea5));
        g.setFont(juce::Font(32.0f, juce::Font::bold));
        g.drawText("EC AIR EXCITER", getLocalBounds().removeFromTop(90), juce::Justification::centred, true);
    }
}

void EC_AIRAudioProcessorEditor::resized()
{
    // Layout EXACTO como la imagen (800 x 520)
    // Preset arriba derecha
    presetLabel.setBounds(520, 28, 70, 24);
    presetCombo.setBounds(590, 24, 185, 32);

    // Knobs grandes centrados
    midAirSlider.setBounds(165, 170, 190, 190);
    highAirSlider.setBounds(445, 170, 190, 190);

    // Valores debajo de knobs grandes
    midAirValue.setBounds(165, 360, 190, 24);
    highAirValue.setBounds(445, 360, 190, 24);

    // Barra inferior: POWER / BYPASS / MIX / OUTPUT
    powerButton.setBounds(225, 415, 52, 26);
    bypassButton.setBounds(290, 412, 78, 30);

    mixSlider.setBounds(365, 390, 70, 70);
    outputSlider.setBounds(525, 390, 70, 70);
    mixValue.setBounds(365, 460, 70, 20);
    outputValue.setBounds(525, 460, 70, 20);
}

void EC_AIRAudioProcessorEditor::timerCallback()
{
    updateValueLabels();
}

void EC_AIRAudioProcessorEditor::updateValueLabels()
{
    // Formato igual a la imagen: +2.4 dB, +4.1 dB, 65%, -0.5 dB
    auto formatDb = [](float v) {
        juce::String s = (v >= 0 ? "+" : "") + juce::String(v, 1) + " dB";
        return s;
    };
    midAirValue.setText(formatDb((float)midAirSlider.getValue()), juce::dontSendNotification);
    highAirValue.setText(formatDb((float)highAirSlider.getValue()), juce::dontSendNotification);
    mixValue.setText(juce::String((int)mixSlider.getValue()) + "%", juce::dontSendNotification);
    outputValue.setText(formatDb((float)outputSlider.getValue()), juce::dontSendNotification);
}

void EC_AIRAudioProcessorEditor::applyPreset(int index)
{
    // MISMOS PRESETS, MISMO AUDIO - solo cambia valores de los params
    // Ajusta estos valores a tus presets originales si son diferentes
    float mid = 2.4f, high = 4.1f, mix = 65.0f, out = -0.5f;
    if (index == 1)      { mid = 2.4f; high = 4.1f; mix = 65.0f; out = -0.5f; } // Warm & Airy
    else if (index == 2) { mid = 3.0f; high = 5.5f; mix = 70.0f; out = -1.0f; } // Vocal
    else if (index == 3) { mid = 1.5f; high = 3.0f; mix = 50.0f; out = 0.0f; }  // Master
    else if (index == 4) { mid = 1.0f; high = 1.8f; mix = 40.0f; out = 0.0f; }  // Subtle

    auto& apvts = audioProcessor.getAPVTS();
    if (auto* p = apvts.getParameter("midAir"))  p->setValueNotifyingHost(p->convertTo0to1(mid));
    if (auto* p = apvts.getParameter("highAir")) p->setValueNotifyingHost(p->convertTo0to1(high));
    if (auto* p = apvts.getParameter("mix"))     p->setValueNotifyingHost(p->convertTo0to1(mix));
    if (auto* p = apvts.getParameter("output"))  p->setValueNotifyingHost(p->convertTo0to1(out));
}
