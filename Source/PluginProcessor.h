#pragma once

#include <JuceHeader.h>

//==============================================================================
// Drowning in Fuzz -- Big Muff Pi style fuzz. Authentic 4-stage topology:
//
//   input HPF (coupling cap) -> input boost (Fuzz knob)
//     -> [ oversample x8 ]
//        -> soft-clip stage 1 -> inter-stage LPF
//        -> soft-clip stage 2 -> inter-stage LPF
//     -> [ downsample ]
//     -> Big Muff tone stack (LPF <-> HPF blend around a mid scoop)
//     -> Master volume
//
// Extras beyond the original 3-knob pedal (off by default so it stays authentic):
//   - Bias    : clipping asymmetry
//   - Dying   : models the failing pedal that made the loved sample -- adds
//               bias-drift grit (asymmetric octave splat) and failing-coupling-
//               cap low-end bloat. 0 = healthy, up = the dying magic.
//   - Gate    : input noise gate
//   - Mix     : dry/wet blend
//==============================================================================
class ProFuzzAudioProcessor : public juce::AudioProcessor
{
public:
    ProFuzzAudioProcessor();
    ~ProFuzzAudioProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==========================================================================
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==========================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Public so the editor can attach sliders to it.
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Set the voicing-dependent filter cutoffs. v=0 -> Mk I (original voice,
    // bright/open), v=1 -> Mk II (tuned closer to the real pedal: darker top,
    // tighter lows, fuller mids). Safe to call from the audio thread.
    void applyVoicing (int v);

    //==========================================================================
    // Big Muff clipping stage: a transistor gain cell swinging into a pair of
    // clipping diodes. Soft asymmetric clip.
    //
    // CRITICAL: asymmetry is done by giving the two halves DIFFERENT drive, NOT
    // by an input offset. f(0)=0 for any bias, so the stage injects NO DC. An
    // offset bias (tanh(2*(x+bias))) outputs tanh(2*bias) at zero input -> a
    // standing DC term that, while Bias/Dying are being swept, becomes a moving
    // DC step the downstream filters reproduce as motorboating. Slope-asymmetry
    // gives the same even-harmonic "dying/octave" character with zero DC.
    static inline float clipStage (float x, float bias) noexcept
    {
        // bias in ~[-0.7, +0.7]; negative => negative half clips harder.
        const float k = 2.0f * (x >= 0.0f ? 1.0f : (1.0f + bias));
        return std::tanh (k * x);
    }

    // Valve/tube-style saturation. Gentle asymmetric soft clip: the two halves
    // round at slightly different rates (triode-like) -> 2nd-harmonic "warmth",
    // smoother top end than the hard diode clip. Each half is normalised so the
    // small-signal slope is ~1 (warmth changes tone, not level). f(0)=0 => no DC.
    // amt 0..1 blends dry->warm. This is what tames the "digital" edge.
    static inline float tubeStage (float x, float amt) noexcept
    {
        if (amt <= 0.0f) return x;
        const float kp = 1.0f + amt * 0.8f;   // positive-half drive
        const float kn = 1.0f + amt * 1.7f;   // negative-half drive (harder)
        const float warm = (x >= 0.0f) ? std::tanh (kp * x) / kp
                                       : std::tanh (kn * x) / kn;
        return x * (1.0f - amt) + warm * amt;
    }

    // --- DSP state ---
    double currentSampleRate = 44100.0;
    double osRateHz          = 44100.0 * 8.0; // oversampled rate (for voicing)
    int    lastVoicing       = -1;            // forces filter refresh on change
    float  lastBloatDb       = std::numeric_limits<float>::quiet_NaN();
    float  lastCapLeakHz     = std::numeric_limits<float>::quiet_NaN();

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    static constexpr int kNumCh = 2;

    // Per-channel filter chain (Big Muff has these between every stage).
    std::array<juce::dsp::IIR::Filter<float>, kNumCh> inputHPF;     // coupling cap
    std::array<juce::dsp::IIR::Filter<float>, kNumCh> interLPF1;    // after clip 1
    std::array<juce::dsp::IIR::Filter<float>, kNumCh> interHPF1;    // coupling cap (DC block) after clip 1
    std::array<juce::dsp::IIR::Filter<float>, kNumCh> interLPF2;    // after clip 2
    std::array<juce::dsp::IIR::Filter<float>, kNumCh> interHPF2;    // coupling cap (DC block) after clip 2
    std::array<juce::dsp::IIR::Filter<float>, kNumCh> toneLPF;      // tone stack low path
    std::array<juce::dsp::IIR::Filter<float>, kNumCh> toneHPF;      // tone stack high path
    std::array<juce::dsp::IIR::Filter<float>, kNumCh> bloatShelf;   // failing-cap low boost
    std::array<juce::dsp::IIR::Filter<float>, kNumCh> capLeakLPF;   // "Cap Leak" dying mode dulling
    std::array<juce::dsp::IIR::Filter<float>, kNumCh> outputDC;     // final DC blocker (safety)

    // Smoothed gate gain per channel.
    std::array<float, kNumCh> gateGain { 1.0f, 1.0f };

    // "Sputter" dying-mode envelope follower per channel (dying-battery sag).
    std::array<float, kNumCh> sputterEnv { 0.0f, 0.0f };

    // Dry copy for the mix control.
    juce::AudioBuffer<float> dryBuffer;

    // Smoothed params to avoid zipper noise.
    juce::SmoothedValue<float> driveGain, biasAmt, outLevel, mixAmt, toneBlend, dyingAmt, warmthAmt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProFuzzAudioProcessor)
};
