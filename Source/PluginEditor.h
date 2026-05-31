#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Cream/ivory rotary knob styled after physical pedal knobs:
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
// Two-position voicing selector. Toggle ON = Mk II voicing. Attached to the
// "voicing" parameter so host automation stays in sync; repaints on every
// state change (no timer needed).
class VoicingSwitch : public juce::Button
{
public:
    VoicingSwitch() : juce::Button ("voicing") { setClickingTogglesState (true); }
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
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

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

    Knob input, drive, bias, tone, level, gate, mix, dying, warmth;

    // Mode selectors.
    juce::ComboBox dyModeBox;
    juce::Label    dyModeLabel;
    std::unique_ptr<ComboAttachment> dyModeAttach;
    juce::ComboBox cabModeBox;
    juce::Label    cabModeLabel;
    std::unique_ptr<ComboAttachment> cabModeAttach;

    juce::ToggleButton autoLevelButton;
    std::unique_ptr<ButtonAttachment> autoLevelAttach;

    // Mk I / Mk II voicing selector.
    VoicingSwitch footSw;
    std::unique_ptr<ButtonAttachment> footAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProFuzzAudioProcessorEditor)
};
