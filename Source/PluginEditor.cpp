#include "PluginProcessor.h"
#include "PluginEditor.h"

AirLookAndFeel::AirLookAndFeel()
{
    setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff00d4ff));
    setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a2f3a));
    setColour(juce::Slider::thumbColourId, juce::Colours::white);
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

void AirLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                      juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height).reduced(6);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto centre = bounds.getCentre();
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    bool isEnabled = slider.isEnabled();

    using Joint = juce::PathStrokeType::JointStyle;
    using Cap = juce::PathStrokeType::EndCapStyle;

    g.setColour(juce::Colour(0x40000000));
    g.fillEllipse(centre.x - radius + 3, centre.y - radius + 5, radius * 2, radius * 2);

    juce::ColourGradient grad(juce::Colour(0xff3a4152), centre.x, centre.y - radius,
                             juce::Colour(0xff14171f), centre.x, centre.y + radius, false);
    g.setGradientFill(grad);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);

    g.setColour(juce::Colour(0xff1a1d26));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2, 1.5f);

    if (isEnabled)
    {
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius + 4, radius + 4, 0, rotaryStartAngle, angle, true);
        g.setColour(juce::Colour(0xff00d4ff));
        g.strokePath(arc, juce::PathStrokeType(3.5f, Joint::curved, Cap::rounded));
        g.setColour(juce::Colour(0x3300d4ff));
        g.strokePath(arc, juce::PathStrokeType(8.0f, Joint::curved, Cap::rounded));
    }
    else
    {
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius + 4, radius + 4, 0, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0xff333333));
        g.strokePath(arc, juce::PathStrokeType(3.5f, Joint::curved, Cap::rounded));
    }

    juce::Path bgArc;
    bgArc.addCentredArc(centre.x, centre.y, radius + 4, radius + 4, 0, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colour(0x22ffffff));
    g.strokePath(bgArc, juce::PathStrokeType(1.0f));

    juce::Point<float> tick(centre.x + std::sin(angle) * (radius - 9),
                            centre.y - std::cos(angle) * (radius - 9));
    g.setColour(isEnabled ? juce::Colours::white : juce::Colour(0xff666666));
    g.drawLine(centre.x, centre.y, tick.x, tick.y, 3.0f);
    g.fillEllipse(tick.x - 3, tick.y - 3, 6, 6);

    g.setColour(juce::Colour(0x22ffffff));
    g.fillEllipse(centre.x - radius * 0.35f, centre.y - radius * 0.55f, radius * 0.5f, radius * 0.25f);
}

void AirLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool, bool)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(2);
    bool isOn = button.getToggleState();
    g.setColour(isOn ? juce::Colour(0xff00d4ff) : juce::Colour(0xff2a2f3a));
    g.fillRoundedRectangle(bounds, 18.0f);
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawRoundedRectangle(bounds, 18.0f, 1.0f);
    g.setColour(isOn ? juce::Colours::black : juce::Colours::white);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(button.getButtonText(), bounds, juce::Justification::centred);
}

EC_AIRAudioProcessorEditor::EC_AIRAudioProcessorEditor(EC_AIRAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(720, 420);
    setLookAndFeel(&airLNF);

    // === NUEVO: CARGAR IMAGEN DE FONDO DESDE GitHub / BinaryData ===
    // Archivo: Assets/EC-Air-Exciter-Background.png
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::ECAirExciterBackground_png,
                                                      BinaryData::ECAirExciterBackground_pngSize);
    bool hasBg = backgroundImage.isValid();

    brandLabel.setText("EC  AUDIO", juce::dontSendNotification);
    brandLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    brandLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00d4ff));
    brandLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(brandLabel);

    titleLabel.setText("AIR  EXCITER", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(30.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("High-Frequency Exciter  •  Add air without harshness", juce::dontSendNotification);
    subtitleLabel.setFont(juce::FontOptions(11.5f));
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9aa3b2));
    subtitleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(subtitleLabel);

    powerButton.setButtonText("POWER");
    powerButton.setClickingTogglesState(true);
    addAndMakeVisible(powerButton);
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, "power", powerButton);

    setupKnob(midAirSlider, midAirLabel, midAirValue, "MID AIR");
    setupKnob(highAirSlider, highAirLabel, highAirValue, "HIGH AIR");
    setupKnob(mixSlider, mixLabel, mixValue, "MIX");
    setupKnob(outputSlider, outputLabel, outputValue, "OUTPUT");

    midAirAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "midAir", midAirSlider);
    highAirAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "highAir", highAirSlider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "mix", mixSlider);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "output", outputSlider);

    presetLabel.setText("PRESETS", juce::dontSendNotification);
    presetLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    presetLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6b7280));
    presetLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(presetLabel);

    setupPresetButton(presetAirButton, "AIR");
    setupPresetButton(presetVocalButton, "VOCAL");
    setupPresetButton(presetMasterButton, "MASTER");
    setupPresetButton(presetSubtleButton, "SUBTLE");

    presetAirButton.onClick = [this] { applyPreset(2.4f, 4.1f, 65.0f, -0.5f); };
    presetVocalButton.onClick = [this] { applyPreset(3.5f, 5.0f, 55.0f, -1.0f); };
    presetMasterButton.onClick = [this] { applyPreset(1.2f, 2.0f, 40.0f, -0.2f); };
    presetSubtleButton.onClick = [this] { applyPreset(0.8f, 1.2f, 35.0f, 0.0f); };

    footerLabel.setText("6 kHz  •  12 kHz  •  Analog-style saturation  •  Made for vocals, masters & buses", juce::dontSendNotification);
    footerLabel.setFont(juce::FontOptions(10.0f));
    footerLabel.setColour(juce::Label::textColourId, juce::Colour(0xff5a6270));
    footerLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(footerLabel);

    // === NUEVO: SI HAY IMAGEN, ESCONDE TEXTOS DUPLICADOS QUE YA VIENEN EN LA FOTO ===
    if (hasBg)
    {
        brandLabel.setVisible(false);
        titleLabel.setVisible(false);
        subtitleLabel.setVisible(false);
        presetLabel.setVisible(false);
        footerLabel.setVisible(false);
        // Los labels MID AIR / HIGH AIR / MIX / OUTPUT ya estan en la imagen,
        // solo dejamos visibles los valores dinamicos (+2.4 dB, etc)
        midAirLabel.setVisible(false);
        highAirLabel.setVisible(false);
        mixLabel.setVisible(false);
        outputLabel.setVisible(false);
    }

    startTimerHz(30);
}

EC_AIRAudioProcessorEditor::~EC_AIRAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void EC_AIRAudioProcessorEditor::setupKnob(juce::Slider& slider, juce::Label& label, juce::Label& value, const juce::String& name)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setLookAndFeel(&airLNF);
    addAndMakeVisible(slider);

    label.setText(name, juce::dontSendNotification);
    label.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, juce::Colour(0xffcbd5e1));
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);

    value.setText("0.0", juce::dontSendNotification);
    value.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    value.setColour(juce::Label::textColourId, juce::Colour(0xff00d4ff));
    value.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(value);
}

void EC_AIRAudioProcessorEditor::setupPresetButton(juce::TextButton& btn, const juce::String& name)
{
    btn.setButtonText(name);
    btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1e2330));
    btn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff00d4ff));
    btn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffcbd5e1));
    btn.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible(btn);
}

void EC_AIRAudioProcessorEditor::applyPreset(float mid, float high, float mix, float out)
{
    auto* midP = audioProcessor.apvts.getParameter("midAir");
    auto* highP = audioProcessor.apvts.getParameter("highAir");
    auto* mixP = audioProcessor.apvts.getParameter("mix");
    auto* outP = audioProcessor.apvts.getParameter("output");
    if (midP != nullptr) midP->setValueNotifyingHost(midP->convertTo0to1(mid));
    if (highP != nullptr) highP->setValueNotifyingHost(highP->convertTo0to1(high));
    if (mixP != nullptr) mixP->setValueNotifyingHost(mixP->convertTo0to1(mix));
    if (outP != nullptr) outP->setValueNotifyingHost(outP->convertTo0to1(out));
}

void EC_AIRAudioProcessorEditor::timerCallback()
{
    auto getVal = [this](const char* id) -> float
    {
        auto* v = audioProcessor.apvts.getRawParameterValue(id);
        return v != nullptr ? v->load() : 0.0f;
    };

    float mid = getVal("midAir");
    float high = getVal("highAir");
    float mix = getVal("mix");
    float out = getVal("output");

    midAirValue.setText(juce::String(mid, 1) + " dB", juce::dontSendNotification);
    highAirValue.setText(juce::String(high, 1) + " dB", juce::dontSendNotification);
    mixValue.setText(juce::String((int) mix) + "%", juce::dontSendNotification);
    outputValue.setText(juce::String(out, 1) + " dB", juce::dontSendNotification);

    auto* powerParam = audioProcessor.apvts.getRawParameterValue("power");
    bool isOn = true;
    if (powerParam != nullptr)
        isOn = powerParam->load() > 0.5f;

    midAirSlider.setEnabled(isOn);
    highAirSlider.setEnabled(isOn);
    mixSlider.setEnabled(isOn);
    outputSlider.setEnabled(isOn);

    meterTarget = isOn ? juce::jlimit(0.0f, 1.0f, (mid + high) / 16.0f * (mix / 100.0f) + 0.08f) : 0.0f;
    meterLevel += (meterTarget - meterLevel) * 0.18f;
    repaint();
}

void EC_AIRAudioProcessorEditor::drawMeter(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour(0xff151a23));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);
    int meterW = (int) (area.getWidth() * meterLevel);
    if (meterW > 4)
    {
        juce::ColourGradient grad(juce::Colour(0xff00d4ff), (float) area.getX(), 0,
                                 juce::Colour(0xffa855f7), (float) area.getRight(), 0, false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(juce::Rectangle<float>((float) area.getX(), (float) area.getY(), (float) meterW, (float) area.getHeight()), 6.0f);
    }
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawRoundedRectangle(area.toFloat(), 6.0f, 1.0f);
    g.setColour(juce::Colour(0xff9aa3b2));
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText("AIR LEVEL", area, juce::Justification::centredLeft);
}

void EC_AIRAudioProcessorEditor::paint(juce::Graphics& g)
{
    // === NUEVO: SI HAY IMAGEN, DIBUJALA Y NO DIBUJES EL FONDO VIEJO ===
    if (backgroundImage.isValid())
    {
        g.drawImage(backgroundImage, getLocalBounds().toFloat());
        // Solo dibujamos el medidor encima, porque la imagen ya trae todo lo demas
        // El medidor va donde esta la forma de onda en tu foto
        drawMeter(g, juce::Rectangle<int>(48, 348, getWidth() - 96, 18));
        return;
    }

    // Fallback si no hay imagen (por si no subiste el PNG)
    juce::ColourGradient bg(juce::Colour(0xff1a1f2e), 0, 0,
                            juce::Colour(0xff0b0e14), 0, (float) getHeight(), false);
    g.setGradientFill(bg);
    g.fillAll();
    g.setColour(juce::Colour(0xff00d4ff));
    g.fillRect(0, 0, getWidth(), 2);

    auto panel = getLocalBounds().reduced(18, 82).withHeight(228);
    g.setColour(juce::Colour(0x22ffffff));
    g.fillRoundedRectangle(panel.toFloat(), 16.0f);
    g.setColour(juce::Colour(0x18ffffff));
    g.drawRoundedRectangle(panel.toFloat(), 16.0f, 1.0f);

    for (int i = 1; i < 4; ++i)
    {
        int x = panel.getX() + panel.getWidth() * i / 4;
        g.setColour(juce::Colour(0x14ffffff));
        g.drawLine((float) x, panel.getY() + 24, (float) x, panel.getBottom() - 24, 1.0f);
    }
    drawMeter(g, juce::Rectangle<int>(panel.getX() + 20, panel.getBottom() + 12, panel.getWidth() - 40, 20));
    g.setColour(juce::Colour(0xff6b7280));
    g.setFont(juce::FontOptions(9.0f));
}

void EC_AIRAudioProcessorEditor::resized()
{
    // === NUEVO: LAYOUT QUE COINCIDE CON TU FOTO (720 x 420) ===
    if (backgroundImage.isValid())
    {
        // Fila superior: presets + POWER ya estan en la imagen, solo ponemos botones invisibles encima
        // Presets en tu foto estan arriba a la derecha
        presetAirButton.setBounds(430, 22, 52, 22);
        presetVocalButton.setBounds(488, 22, 58, 22);
        presetMasterButton.setBounds(552, 22, 68, 22);
        presetSubtleButton.setBounds(626, 22, 62, 22);
        // POWER
        powerButton.setBounds(610, 72, 78, 28);

        // Knobs grandes - justo sobre los de la foto
        midAirSlider.setBounds(115, 135, 145, 145);
        highAirSlider.setBounds(400, 135, 145, 145);

        // Valores debajo de cada knob grande
        midAirValue.setBounds(115, 282, 145, 20);
        highAirValue.setBounds(400, 282, 145, 20);

        // Knobs chicos abajo
        mixSlider.setBounds(105, 330, 42, 42); // se ajusta en resized final abajo
        outputSlider.setBounds(580, 330, 42, 42);

        // En realidad MIX y OUTPUT en tu foto son chicos a los lados del medidor
        // Los dejamos mas precisos:
        mixSlider.setBounds(28, 315, 70, 70);
        outputSlider.setBounds(622, 315, 70, 70);
        mixValue.setBounds(28, 388, 70, 18);
        outputValue.setBounds(622, 388, 70, 18);

        // Labels escondidos ya no necesitan bounds
        brandLabel.setBounds(0, 0, 0, 0);
        titleLabel.setBounds(0, 0, 0, 0);
        subtitleLabel.setBounds(0, 0, 0, 0);
        presetLabel.setBounds(0, 0, 0, 0);
        footerLabel.setBounds(0, 0, 0, 0);
        return;
    }

    // Layout viejo si no hay imagen
    auto area = getLocalBounds().reduced(24, 0);
    auto header = area.removeFromTop(78);
    brandLabel.setBounds(header.removeFromTop(16));
    auto titleRow = header.removeFromTop(36);
    titleLabel.setBounds(titleRow.removeFromLeft(340));
    powerButton.setBounds(titleRow.removeFromRight(110).reduced(0, 4));
    subtitleLabel.setBounds(header.removeFromTop(20));

    auto panel = area.removeFromTop(228);
    panel = panel.reduced(10, 10);
    int kw = panel.getWidth() / 4;
    auto place = [&](juce::Slider& s, juce::Label& l, juce::Label& v, int idx)
    {
        int x = panel.getX() + kw * idx;
        s.setBounds(x + 14, panel.getY() + 14, kw - 28, kw - 28);
        l.setBounds(x, panel.getY() + kw + 8, kw, 18);
        v.setBounds(x, panel.getY() + kw + 28, kw, 18);
    };
    place(midAirSlider, midAirLabel, midAirValue, 0);
    place(highAirSlider, highAirLabel, highAirValue, 1);
    place(mixSlider, mixLabel, mixValue, 2);
    place(outputSlider, outputLabel, outputValue, 3);

    area.removeFromTop(12);
    area.removeFromTop(20);
    area.removeFromTop(8);

    auto presetRow = area.removeFromTop(28);
    presetLabel.setBounds(presetRow.removeFromLeft(70));
    presetAirButton.setBounds(presetRow.removeFromLeft(90).reduced(3, 0));
    presetVocalButton.setBounds(presetRow.removeFromLeft(90).reduced(3, 0));
    presetMasterButton.setBounds(presetRow.removeFromLeft(90).reduced(3, 0));
    presetSubtleButton.setBounds(presetRow.removeFromLeft(90).reduced(3, 0));

    area.removeFromTop(8);
    footerLabel.setBounds(area.removeFromTop(20));
}
