#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Cream/ivory rotary knob styled after the physical GFS Pro Fuzz Classic knobs:
// a round ivory cap with a pointer notch, sitting on a dark base ring.
class ProFuzzLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ProFuzzLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle,
                           juce::Slider&) override;
};

//==============================================================================
// Chrome stomp footswitch with an LED above it. Toggle ON = Mk II voicing
// (LED lit). Attached to the "voicing" parameter so host automation stays in
// sync; repaints on every state change (no timer needed).
class FootSwitch : public juce::Button
{
public:
    FootSwitch() : juce::Button ("voicing") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

//==============================================================================
class ProFuzzAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ProFuzzAudioProcessorEditor (ProFuzzAudioProcessor&);
    ~ProFuzzAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // One labelled rotary knob.
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<SliderAttachment> attach;
    };

    void setupKnob (Knob& k, const juce::String& paramID, const juce::String& text,
                    bool big);

    ProFuzzAudioProcessor& processor;
    ProFuzzLookAndFeel laf;

    Knob drive, bias, tone, level, gate, mix, dying, warmth;

    // Dying-flavor selector.
    juce::ComboBox dyModeBox;
    juce::Label    dyModeLabel;
    std::unique_ptr<ComboAttachment> dyModeAttach;

    // Mk I / Mk II voicing footswitch + LED.
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    FootSwitch footSw;
    std::unique_ptr<ButtonAttachment> footAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProFuzzAudioProcessorEditor)
};
