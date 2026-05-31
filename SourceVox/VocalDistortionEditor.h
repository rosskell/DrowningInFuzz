#pragma once

#include <JuceHeader.h>
#include "VocalDistortionProcessor.h"

class VoxLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VoxLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle,
                           juce::Slider&) override;
};

class DrowningInVoxAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit DrowningInVoxAudioProcessorEditor (DrowningInVoxAudioProcessor&);
    ~DrowningInVoxAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attach;
    };

    void setupKnob (Knob&, const juce::String& paramID, const juce::String& label);

    DrowningInVoxAudioProcessor& processor;
    VoxLookAndFeel laf;

    Knob input, drive, bite, body, compress, gate, mix, output;

    juce::ComboBox modeBox;
    juce::Label modeLabel;
    std::unique_ptr<ComboAttachment> modeAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrowningInVoxAudioProcessorEditor)
};
