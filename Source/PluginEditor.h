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
class ProFuzzAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ProFuzzAudioProcessorEditor (ProFuzzAudioProcessor&);
    ~ProFuzzAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

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

    Knob drive, bias, tone, level, gate, mix, dying;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProFuzzAudioProcessorEditor)
};
