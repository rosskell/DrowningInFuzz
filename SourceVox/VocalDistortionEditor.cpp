#include "VocalDistortionEditor.h"

namespace VX
{
    const juce::Colour panelTop  { 0xffd6c6a2 };
    const juce::Colour panelBot  { 0xffa98f63 };
    const juce::Colour ink       { 0xff2b1c13 };
    const juce::Colour brown     { 0xff2f2118 };
    const juce::Colour brass     { 0xffc29a45 };
    const juce::Colour brassDark { 0xff7f642f };
    const juce::Colour cream     { 0xffefe1bd };
    const juce::Colour redLamp   { 0xffb62319 };
}

VoxLookAndFeel::VoxLookAndFeel()
{
    setColour (juce::Label::textColourId, VX::ink);
    setColour (juce::Slider::textBoxTextColourId, VX::ink);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

void VoxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                       float pos, float startAngle, float endAngle,
                                       juce::Slider&)
{
    auto area = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (5.0f);
    const auto cx = area.getCentreX();
    const auto cy = area.getCentreY();
    const auto r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto angle = startAngle + pos * (endAngle - startAngle);

    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillEllipse (cx - r + 2.0f, cy - r + 3.0f, r * 2.0f, r * 2.0f);

    g.setColour (VX::brassDark);
    g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);

    const auto capR = r * 0.82f;
    juce::ColourGradient cap (juce::Colour (0xff3b2a1f), cx, cy - capR,
                              juce::Colour (0xff120d0a), cx, cy + capR, false);
    cap.addColour (0.42, juce::Colour (0xff261a13));
    g.setGradientFill (cap);
    g.fillEllipse (cx - capR, cy - capR, capR * 2.0f, capR * 2.0f);

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (cx - capR + 2.0f, cy - capR + 2.0f, capR * 2.0f - 4.0f, capR * 2.0f - 4.0f, 1.0f);

    juce::Path pointer;
    const auto pw = capR * 0.11f;
    pointer.addRoundedRectangle (-pw * 0.5f, -capR * 0.88f, pw, capR * 0.56f, pw * 0.45f);
    g.setColour (VX::cream);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (cx, cy));
}

DrowningInVoxAudioProcessorEditor::DrowningInVoxAudioProcessorEditor (DrowningInVoxAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&laf);

    setupKnob (input, "input", "INPUT");
    setupKnob (drive, "drive", "DRIVE");
    setupKnob (bite, "bite", "BITE");
    setupKnob (body, "body", "BODY");
    setupKnob (compress, "comp", "COMP");
    setupKnob (gate, "gate", "GATE");
    setupKnob (mix, "mix", "MIX");
    setupKnob (output, "output", "OUTPUT");

    modeBox.addItemList ({ "Smooth", "Warm", "Blown" }, 1);
    modeBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff20150f));
    modeBox.setColour (juce::ComboBox::textColourId, VX::cream);
    modeBox.setColour (juce::ComboBox::outlineColourId, VX::brass.withAlpha (0.75f));
    modeBox.setColour (juce::ComboBox::arrowColourId, VX::brass);
    addAndMakeVisible (modeBox);
    modeAttach = std::make_unique<ComboAttachment> (processor.apvts, "mode", modeBox);

    modeLabel.setText ("MODE", juce::dontSendNotification);
    modeLabel.setFont (juce::Font (12.0f, juce::Font::bold));
    modeLabel.setJustificationType (juce::Justification::centred);
    modeLabel.setColour (juce::Label::textColourId, VX::ink);
    addAndMakeVisible (modeLabel);

    setSize (760, 310);
}

DrowningInVoxAudioProcessorEditor::~DrowningInVoxAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void DrowningInVoxAudioProcessorEditor::setupKnob (Knob& k,
                                                   const juce::String& paramID,
                                                   const juce::String& text)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 17);
    addAndMakeVisible (k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::Font (12.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, VX::ink);
    addAndMakeVisible (k.label);

    k.attach = std::make_unique<SliderAttachment> (processor.apvts, paramID, k.slider);
}

void DrowningInVoxAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (VX::panelTop, 0, 0, VX::panelBot, 0, bounds.getBottom(), false));
    g.fillAll();

    g.setColour (VX::brown);
    g.fillRect (0, 0, 28, getHeight());
    g.fillRect (getWidth() - 28, 0, 28, getHeight());

    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.drawRect (getLocalBounds(), 2);
    g.drawRect (getLocalBounds().reduced (30, 16), 1);

    // Brushed faceplate grain.
    juce::Random rng (1971);
    for (int i = 0; i < 55; ++i)
    {
        const int y = 20 + rng.nextInt (getHeight() - 40);
        g.setColour ((i % 2 == 0 ? juce::Colours::white : juce::Colours::black).withAlpha (0.025f));
        g.drawHorizontalLine (y, 36.0f, (float) getWidth() - 36.0f);
    }

    g.setColour (VX::ink);
    g.setFont (juce::Font (30.0f, juce::Font::bold));
    g.drawText ("DROWNING IN VOX", 42, 24, 310, 36, juce::Justification::centredLeft, false);
    g.setFont (juce::Font (12.0f, juce::Font::bold));
    g.drawText ("VALVE DISTORTION AMPLIFIER", 45, 58, 260, 18, juce::Justification::centredLeft, false);

    // Meter window, decorative but useful as visual language.
    juce::Rectangle<float> meter (485.0f, 25.0f, 210.0f, 72.0f);
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillRoundedRectangle (meter.translated (0.0f, 2.0f), 5.0f);
    g.setColour (juce::Colour (0xffead8a8));
    g.fillRoundedRectangle (meter, 5.0f);
    g.setColour (VX::ink.withAlpha (0.75f));
    g.drawRoundedRectangle (meter, 5.0f, 1.2f);

    g.setFont (juce::Font (10.0f, juce::Font::bold));
    g.drawText ("GRIT", meter.toNearestInt().reduced (8, 5), juce::Justification::topLeft, false);
    for (int i = 0; i <= 10; ++i)
    {
        const float x = meter.getX() + 20.0f + i * 16.5f;
        const float h = (i % 5 == 0) ? 18.0f : 11.0f;
        g.drawLine (x, meter.getBottom() - 12.0f, x, meter.getBottom() - 12.0f - h, 1.0f);
    }
    g.setColour (VX::redLamp);
    juce::Path needle;
    needle.addTriangle (590.0f, meter.getBottom() - 11.0f, 596.0f, meter.getBottom() - 11.0f, 647.0f, meter.getY() + 24.0f);
    g.fillPath (needle);

    // Jewel lamps.
    auto lamp = [&] (float x, float y, bool red)
    {
        g.setColour (juce::Colours::black.withAlpha (0.38f));
        g.fillEllipse (x - 9.0f, y - 8.0f, 18.0f, 18.0f);
        g.setColour (red ? VX::redLamp : juce::Colour (0xffd9a733));
        g.fillEllipse (x - 7.0f, y - 7.0f, 14.0f, 14.0f);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.fillEllipse (x - 4.0f, y - 5.0f, 5.0f, 5.0f);
    };
    lamp (405.0f, 55.0f, true);
    lamp (435.0f, 55.0f, false);
}

void DrowningInVoxAudioProcessorEditor::resized()
{
    auto place = [] (Knob& k, int cx, int cy)
    {
        k.label.setBounds (cx - 42, cy - 50, 84, 18);
        k.slider.setBounds (cx - 42, cy - 36, 84, 100);
    };

    const int y1 = 158;
    const int y2 = 250;
    place (input,  82, y1);
    place (drive, 178, y1);
    place (bite,  274, y1);
    place (body,  370, y1);
    place (compress, 466, y1);
    place (gate,  562, y1);
    place (mix,   650, y1);
    place (output, 650, y2);

    modeLabel.setBounds (74, 226, 120, 16);
    modeBox.setBounds (74, 244, 120, 25);
}
