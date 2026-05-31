#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
namespace ParamID
{
    constexpr auto input = "input";   // input trim before the circuit
    constexpr auto drive = "drive";   // Fuzz: sustain / gain
    constexpr auto tone  = "tone";    // Big Muff tone stack
    constexpr auto level = "level";   // Master volume
    constexpr auto bias   = "bias";    // clipping asymmetry (extra)
    constexpr auto dying  = "dying";   // failing-pedal amount (extra)
    constexpr auto dymode = "dymode";  // dying flavor (Bias Drift / Sputter / Cap Leak)
    constexpr auto warmth = "warmth";  // tube/valve warmth (extra)
    constexpr auto gate   = "gate";    // input noise gate (extra)
    constexpr auto mix    = "mix";     // dry/wet (extra)
    constexpr auto cabmode= "cabmode"; // post-fuzz direct/cab tame
    constexpr auto autolevel = "autolevel"; // level compensation
    constexpr auto voicing= "voicing"; // Mk I (orig) vs Mk II (closer to real)
}

namespace
{
    struct FactoryPreset
    {
        const char* name;
        float input;
        float drive;
        float tone;
        float level;
        float bias;
        float dying;
        float warmth;
        float gate;
        float mix;
        int dyMode;
        int cabMode;
        bool autoLevel;
        bool voicing;
    };

    constexpr std::array<FactoryPreset, 8> kFactoryPresets {{
        { "Drowning in Fuzz",  0.0f, 65.0f, 0.90f, -6.0f, -0.12f, 0.35f, 0.35f, -100.0f, 1.00f, 0, 1, true,  false },
        { "Battery Sink",      2.0f, 78.0f, 0.72f, -7.5f, -0.28f, 0.72f, 0.28f,  -78.0f, 1.00f, 0, 1, true,  true  },
        { "Wool Blanket",     -1.5f, 60.0f, 0.22f, -5.0f, -0.08f, 0.82f, 0.45f, -100.0f, 1.00f, 2, 2, true,  true  },
        { "Broken Speaker",    4.0f, 88.0f, 0.64f, -9.0f, -0.34f, 0.88f, 0.18f,  -72.0f, 1.00f, 1, 2, true,  false },
        { "Tight Lead",       -2.0f, 72.0f, 0.82f, -4.5f, -0.06f, 0.18f, 0.22f,  -68.0f, 1.00f, 1, 1, true,  true  },
        { "Doom Bloom",        1.0f, 82.0f, 0.36f, -8.5f, -0.18f, 0.62f, 0.52f,  -92.0f, 1.00f, 0, 2, true,  true  },
        { "Dry Rot Mix",       0.0f, 70.0f, 0.70f, -5.5f, -0.16f, 0.50f, 0.35f,  -82.0f, 0.58f, 0, 1, true,  false },
        { "Mk I Bright",      -3.0f, 58.0f, 0.96f, -6.0f,  0.00f, 0.00f, 0.12f, -100.0f, 1.00f, 0, 0, false, false },
    }};

    void setParameterValue (juce::AudioProcessorValueTreeState& state,
                            const char* id,
                            float value)
    {
        if (auto* param = state.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }
}

//==============================================================================
ProFuzzAudioProcessor::ProFuzzAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
ProFuzzAudioProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    // --- The three core fuzz controls ---

    // Input: pickup/reamp level trim before the circuit.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::input, 1 }, "Input",
        NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f));

    // Fuzz: 0..100 -> input gain into the first clip stage. The Big Muff lives
    // deep in clipping, so even moderate settings are very saturated.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::drive, 1 }, "Fuzz",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 65.0f));

    // Tone: 0 (dark, full low path) .. 1 (bright, full high path).
    // Default 0.9 = the spectral-match sweet spot vs the real pedal recording.
    // Knob still sweeps the full range for taste.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::tone, 1 }, "Tone",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.9f));

    // Master: output volume in dB.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::level, 1 }, "Master",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), -6.0f));

    // --- Extras (default to "off"/neutral so the pedal is authentic) ---

    // Bias: clipping asymmetry, -1..+1. Default -0.12 reproduces the measured
    // 0.88 asymmetry of the (dying) reference pedal -> octave-ish grit.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::bias, 1 }, "Bias",
        NormalisableRange<float> (-1.0f, 1.0f, 0.001f), -0.12f));

    // Dying: amount of failing-pedal character. 0 = healthy pedal.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::dying, 1 }, "Dying",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f));

    // Dying Mode: which failure flavor the Dying knob applies.
    //   Bias Drift = octave-ish grit + low bloat (the original loved sound)
    //   Sputter    = dying-battery sag/splutter (gain ducks under sustain)
    //   Cap Leak   = woolly, dull, blown-out lows + loss of treble definition
    params.push_back (std::make_unique<AudioParameterChoice>(
        ParameterID { ParamID::dymode, 1 }, "Dying Mode",
        StringArray { "Bias Drift", "Sputter", "Cap Leak" }, 0));

    // Warmth: tube/valve-style saturation blended over the fuzz. Rounds the
    // top end and adds even harmonics to soften the "digital" character.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::warmth, 1 }, "Warmth",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f));

    // Gate: threshold in dB. -100 = off.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::gate, 1 }, "Gate",
        NormalisableRange<float> (-100.0f, -20.0f, 0.1f), -100.0f));

    // Mix: 0 = dry, 1 = fully wet.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::mix, 1 }, "Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    // Cab Mode: a lightweight direct-recording tame after the fuzz.
    params.push_back (std::make_unique<AudioParameterChoice>(
        ParameterID { ParamID::cabmode, 1 }, "Cab Mode",
        StringArray { "Raw", "Amp", "Dark Cab" }, 1));

    // Auto Level: compensates some drive/input loudness jumps while browsing.
    params.push_back (std::make_unique<AudioParameterBool>(
        ParameterID { ParamID::autolevel, 1 }, "Auto Level", true));

    // Voicing footswitch: false = Mk I (the original bright voice you dialed in),
    // true = Mk II (re-tuned by spectral match to sound closer to the real pedal:
    // darker top, tighter lows, fuller mids).
    params.push_back (std::make_unique<AudioParameterBool>(
        ParameterID { ParamID::voicing, 1 }, "Voicing (Mk II)", false));

    return { params.begin(), params.end() };
}

//==============================================================================
int ProFuzzAudioProcessor::getNumPrograms()
{
    return (int) kFactoryPresets.size();
}

int ProFuzzAudioProcessor::getCurrentProgram()
{
    return currentProgram;
}

void ProFuzzAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) kFactoryPresets.size()))
        return;

    currentProgram = index;
    const auto& p = kFactoryPresets[(size_t) index];

    setParameterValue (apvts, ParamID::input,   p.input);
    setParameterValue (apvts, ParamID::drive,   p.drive);
    setParameterValue (apvts, ParamID::tone,    p.tone);
    setParameterValue (apvts, ParamID::level,   p.level);
    setParameterValue (apvts, ParamID::bias,    p.bias);
    setParameterValue (apvts, ParamID::dying,   p.dying);
    setParameterValue (apvts, ParamID::dymode,  (float) p.dyMode);
    setParameterValue (apvts, ParamID::warmth,  p.warmth);
    setParameterValue (apvts, ParamID::gate,    p.gate);
    setParameterValue (apvts, ParamID::mix,     p.mix);
    setParameterValue (apvts, ParamID::cabmode, (float) p.cabMode);
    setParameterValue (apvts, ParamID::autolevel, p.autoLevel ? 1.0f : 0.0f);
    setParameterValue (apvts, ParamID::voicing, p.voicing ? 1.0f : 0.0f);
}

const juce::String ProFuzzAudioProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, (int) kFactoryPresets.size()))
        return kFactoryPresets[(size_t) index].name;

    return {};
}

//==============================================================================
void ProFuzzAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    const auto numChannels = (juce::uint32) getTotalNumOutputChannels();

    // 8x oversampling (order 3) around the two clipping + tube stages. The extra
    // headroom keeps the added harmonics from aliasing back down as harsh
    // "digital" fizz -- part of the warmer, more analog character.
    constexpr int kOSFactor = 8;
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        numChannels, 3,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
    oversampling->initProcessing ((size_t) samplesPerBlock);

    // The clip stages + inter-stage LPFs run inside the oversampled domain.
    const double osRate = sampleRate * (double) kOSFactor;

    juce::dsp::ProcessSpec specBase;
    specBase.sampleRate       = sampleRate;
    specBase.maximumBlockSize = (juce::uint32) samplesPerBlock;
    specBase.numChannels      = 1;

    juce::dsp::ProcessSpec specOS = specBase;
    specOS.sampleRate       = osRate;
    specOS.maximumBlockSize = (juce::uint32) samplesPerBlock * kOSFactor;

    for (auto& f : inputHPF)
    {
        f.prepare (specBase);
        // Coupling-cap high-pass, dropped to 45 Hz: the reference pedal's failing
        // cap let far more low end through (+13 dB measured) than a healthy Muff.
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 45.0f);
        f.reset();
    }

    for (auto& f : interLPF1)
    {
        f.prepare (specOS);
        // First inter-stage low-pass. Cutoff 18 kHz: re-tuned by spectral match
        // AFTER adding the DC-blocking coupling caps (which shifted stage 2's
        // operating point). The real pedal keeps real presence/bite up high;
        // earlier dark settings were compensating for a DC artifact. See match.py.
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (osRate, 18000.0f);
        f.reset();
    }

    for (auto& f : interHPF1)
    {
        f.prepare (specOS);
        // Coupling cap after stage 1: blocks the DC that Bias injects, so the
        // asymmetry adds even harmonics to the SIGNAL instead of a static offset
        // (and stage 2 isn't starved by a standing DC level).
        // MUST be first-order: a 2nd-order biquad this far below the (oversampled)
        // sample rate puts its pole pair ~1e-4 from z=1 and goes unstable in
        // float32 -> motorboating. A one-pole HPF cannot oscillate.
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (osRate, 20.0f);
        f.reset();
    }

    for (auto& f : interLPF2)
    {
        f.prepare (specOS);
        // Second inter-stage low-pass, 15 kHz (matched). Together with LPF1 this
        // gives the broadband fuzz texture measured in Gtr_with_fuzz.wav.
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (osRate, 15000.0f);
        f.reset();
    }

    for (auto& f : interHPF2)
    {
        f.prepare (specOS);
        // Coupling cap after stage 2: blocks DC before the output stage.
        // First-order for the same numerical-stability reason as interHPF1.
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (osRate, 20.0f);
        f.reset();
    }

    // Big Muff passive tone stack = a low-pass and a high-pass whose outputs are
    // crossfaded by the Tone knob. The dip where they cross = the mid scoop.
    for (auto& f : toneLPF)
    {
        f.prepare (specBase);
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 800.0f);
        f.reset();
    }
    for (auto& f : toneHPF)
    {
        f.prepare (specBase);
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 2000.0f);
        f.reset();
    }

    for (auto& f : toneScoop)
    {
        f.prepare (specBase);
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            sampleRate, 850.0f, 0.65f, juce::Decibels::decibelsToGain (-5.0f));
        f.reset();
    }

    for (auto& f : bloatShelf)
    {
        f.prepare (specBase);
        // Low shelf below 180 Hz; gain driven by the Dying knob in processBlock.
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            sampleRate, 180.0f, 0.7f, 1.0f);
        f.reset();
    }

    for (auto& f : capLeakLPF)
    {
        f.prepare (specBase);
        // "Cap Leak" dying mode dulls the top; cutoff modulated in processBlock.
        // Default wide-open (no effect when that mode/amount is zero).
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 20000.0f);
        f.reset();
    }

    for (auto& f : cabHPF)
    {
        f.prepare (specBase);
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (sampleRate, 20.0f);
        f.reset();
    }
    for (auto& f : cabPeak)
    {
        f.prepare (specBase);
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, 1000.0f, 0.8f, 1.0f);
        f.reset();
    }
    for (auto& f : cabLPF)
    {
        f.prepare (specBase);
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 20000.0f);
        f.reset();
    }

    for (auto& f : outputDC)
    {
        f.prepare (specBase);
        // Final safety DC blocker at the output. First-order so it can never
        // self-oscillate; catches any residual subsonic before it reaches the DAW.
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (sampleRate, 12.0f);
        f.reset();
    }

    // Apply the current voicing's cutoffs (overrides the Mk I defaults above).
    osRateHz    = osRate;
    lastVoicing = -1;
    lastCabMode = -1;
    lastToneForScoop = std::numeric_limits<float>::quiet_NaN();
    lastBloatDb = std::numeric_limits<float>::quiet_NaN();
    lastCapLeakHz = std::numeric_limits<float>::quiet_NaN();
    applyVoicing ((int) apvts.getRawParameterValue (ParamID::voicing)->load());

    gateGain.fill (1.0f);
    sputterEnv.fill (0.0f);

    const double ramp = 0.02; // 20 ms smoothing
    inputGain.reset (sampleRate, ramp);
    driveGain.reset (sampleRate, ramp);
    biasAmt.reset   (osRate, ramp);
    outLevel.reset  (sampleRate, ramp);
    mixAmt.reset    (sampleRate, ramp);
    toneBlend.reset (sampleRate, ramp);
    dyingAmt.reset  (sampleRate, ramp);
    warmthAmt.reset (osRate, ramp);

    dryBuffer.setSize ((int) numChannels, samplesPerBlock);
}

void ProFuzzAudioProcessor::releaseResources()
{
    if (oversampling != nullptr)
        oversampling->reset();
}

//==============================================================================
void ProFuzzAudioProcessor::applyVoicing (int v)
{
    if (v == lastVoicing)
        return;
    lastVoicing = v;

    // Mk II values came from a spectral match of the DI rendered through the
    // chain against the real-pedal recording: the real pedal is much darker up
    // top and tighter in the lows than Mk I, with less mid scoop.
    const float inHz  = (v == 1) ?    75.0f :    45.0f; // input coupling HPF
    const float lp1Hz = (v == 1) ?  9500.0f : 18000.0f; // inter-stage LPF 1 (brighter Mk II)
    const float lp2Hz = (v == 1) ?  6500.0f : 15000.0f; // inter-stage LPF 2 (brighter Mk II)
    const float tLpHz = (v == 1) ?  1200.0f :   800.0f; // tone low path (more body)
    const float tHpHz = (v == 1) ?  1600.0f :  2000.0f; // tone high path (less scoop)

    for (auto& f : inputHPF)
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, inHz);
    for (auto& f : interLPF1)
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (osRateHz, lp1Hz);
    for (auto& f : interLPF2)
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (osRateHz, lp2Hz);
    for (auto& f : toneLPF)
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (currentSampleRate, tLpHz);
    for (auto& f : toneHPF)
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, tHpHz);
}

//==============================================================================
bool ProFuzzAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void ProFuzzAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // --- Read & smooth parameters ---
    const float inputDb = apvts.getRawParameterValue (ParamID::input)->load();
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (inputDb));

    const float driveDb = juce::jmap (apvts.getRawParameterValue (ParamID::drive)->load(),
                                      0.0f, 100.0f, 0.0f, 40.0f);
    driveGain.setTargetValue (juce::Decibels::decibelsToGain (driveDb));
    mixAmt.setTargetValue (apvts.getRawParameterValue (ParamID::mix)->load());
    const float tone = apvts.getRawParameterValue (ParamID::tone)->load();
    toneBlend.setTargetValue (tone);

    const float dying = apvts.getRawParameterValue (ParamID::dying)->load();
    dyingAmt.setTargetValue (dying);
    const float warmth = apvts.getRawParameterValue (ParamID::warmth)->load();
    warmthAmt.setTargetValue (warmth);

    const bool autoLevelOn = apvts.getRawParameterValue (ParamID::autolevel)->load() >= 0.5f;
    const float autoCompDb = autoLevelOn
        ? -juce::jlimit (0.0f, 10.0f, driveDb * 0.10f + juce::jmax (0.0f, inputDb) * 0.35f
                                      + dying * 2.5f + warmth * 1.5f)
        : 0.0f;
    outLevel.setTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (ParamID::level)->load() + autoCompDb));

    // Voicing footswitch: refresh filter cutoffs only when it changes.
    applyVoicing ((int) apvts.getRawParameterValue (ParamID::voicing)->load());

    const float baseBias = apvts.getRawParameterValue (ParamID::bias)->load();
    const int   dyMode   = (int) apvts.getRawParameterValue (ParamID::dymode)->load();
    const int   cabMode  = (int) apvts.getRawParameterValue (ParamID::cabmode)->load();

    // Each dying flavor maps the Dying knob to a different mix of: extra bias
    // (grit), low-shelf bloat, top-end dulling (Cap Leak), and sag (Sputter).
    float biasExtra = 0.0f, bloatDb = 0.0f, capLeakHz = 20000.0f;
    float sputterDepth = 0.0f;
    switch (dyMode)
    {
        case 1: // Sputter — dying-battery sag/splutter
            biasExtra    = -dying * 0.12f;
            bloatDb      =  dying * 4.0f;
            sputterDepth =  dying;
            break;
        case 2: // Cap Leak — woolly, dull, blown lows + lost treble
            bloatDb   = dying * 12.0f;
            capLeakHz = juce::jmap (dying, 0.0f, 1.0f, 20000.0f, 1800.0f);
            break;
        default: // 0 = Bias Drift — octave grit + bloat (the original sound)
            biasExtra = -dying * 0.25f;
            bloatDb   =  dying * 9.0f;
            break;
    }
    biasAmt.setTargetValue ((baseBias + biasExtra) * 0.5f);

    // These filters are part of the anti-motorboating design: they shape the
    // "dying" modes without injecting energy into silence. Rebuild their
    // coefficient objects only when the effective control-rate values change;
    // doing it every block is needless audio-thread churn.
    if (std::isnan (lastBloatDb) || std::abs (bloatDb - lastBloatDb) >= 0.05f)
    {
        lastBloatDb = bloatDb;
        for (auto& f : bloatShelf)
            f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
                currentSampleRate, 180.0f, 0.7f, juce::Decibels::decibelsToGain (bloatDb));
    }

    if (std::isnan (lastCapLeakHz) || std::abs (capLeakHz - lastCapLeakHz) >= 5.0f)
    {
        lastCapLeakHz = capLeakHz;
        for (auto& f : capLeakLPF)
            f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (
                currentSampleRate, capLeakHz);
    }

    if (std::isnan (lastToneForScoop) || std::abs (tone - lastToneForScoop) >= 0.02f)
    {
        lastToneForScoop = tone;
        const float scoopHz = juce::jmap (tone, 0.0f, 1.0f, 650.0f, 1150.0f);
        const float scoopDb = juce::jmap (tone, 0.0f, 1.0f, -3.0f, -8.0f);
        for (auto& f : toneScoop)
            f.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                currentSampleRate, scoopHz, 0.72f, juce::Decibels::decibelsToGain (scoopDb));
    }

    if (cabMode != lastCabMode)
    {
        lastCabMode = cabMode;

        const float hpHz = (cabMode == 2) ? 95.0f : 80.0f;
        const float lpHz = (cabMode == 2) ? 3900.0f : 6500.0f;
        const float midHz = (cabMode == 2) ? 850.0f : 1250.0f;
        const float midDb = (cabMode == 2) ? -4.5f : -2.0f;

        for (auto& f : cabHPF)
            f.coefficients = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (currentSampleRate, hpHz);
        for (auto& f : cabPeak)
            f.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                currentSampleRate, midHz, 0.8f, juce::Decibels::decibelsToGain (midDb));
        for (auto& f : cabLPF)
            f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (currentSampleRate, lpHz);
    }

    const float gateThrDb = apvts.getRawParameterValue (ParamID::gate)->load();
    const float gateThr   = juce::Decibels::decibelsToGain (gateThrDb);

    // --- Stash dry signal for the mix control ---
    dryBuffer.makeCopyOf (buffer, true);

    // --- Pickup/reamp level trim before the fuzz circuit ---
    for (int ch = 0; ch < numCh && ch < kNumCh; ++ch)
    {
        auto* x = buffer.getWritePointer (ch);
        auto ig = inputGain;
        for (int n = 0; n < numSamples; ++n)
            x[n] *= ig.getNextValue();
    }
    inputGain.skip (numSamples);

    // --- Input noise gate (per channel, smoothed gain) ---
    const float gateRelease = 0.9995f; // close slowly to avoid chatter
    for (int ch = 0; ch < numCh && ch < kNumCh; ++ch)
    {
        auto* x = buffer.getWritePointer (ch);
        float g = gateGain[(size_t) ch];

        for (int n = 0; n < numSamples; ++n)
        {
            const float target = (std::abs (x[n]) > gateThr) ? 1.0f : 0.0f;
            g = (target > g) ? target                       // open fast
                             : g * gateRelease + target * (1.0f - gateRelease);
            x[n] *= g;
        }
        gateGain[(size_t) ch] = g;
    }

    // --- Input coupling high-pass + drive ---
    for (int ch = 0; ch < numCh && ch < kNumCh; ++ch)
    {
        auto* x = buffer.getWritePointer (ch);
        auto& hp = inputHPF[(size_t) ch];
        auto dg = driveGain;   // per-channel copy so each channel sees the same ramp
        for (int n = 0; n < numSamples; ++n)
            x[n] = hp.processSample (x[n]) * dg.getNextValue();
    }
    // Advance the master smoother once for the block. Without this, the member
    // never moves: each block restarts the ramp from a stuck value, producing a
    // block-rate sawtooth on the parameter = motorboating while knobs change.
    driveGain.skip (numSamples);

    // --- Oversampled two-stage clipping with inter-stage filtering ---
    juce::dsp::AudioBlock<float> block (buffer);
    auto osBlock = oversampling->processSamplesUp (block);

    const int osSamples = (int) osBlock.getNumSamples();
    for (int ch = 0; ch < (int) osBlock.getNumChannels() && ch < kNumCh; ++ch)
    {
        auto* d  = osBlock.getChannelPointer ((size_t) ch);
        auto& l1 = interLPF1[(size_t) ch];
        auto& h1 = interHPF1[(size_t) ch];
        auto& l2 = interLPF2[(size_t) ch];
        auto& h2 = interHPF2[(size_t) ch];
        auto  bv = biasAmt;
        auto  wv = warmthAmt;

        for (int n = 0; n < osSamples; ++n)
        {
            const float b = bv.getNextValue();
            const float w = wv.getNextValue();
            float s = d[n];
            s = clipStage (s, b);     // stage 1
            s = l1.processSample (s); // inter-stage LPF 1
            s = h1.processSample (s); // coupling cap 1 (DC block)
            s = clipStage (s, b);     // stage 2
            s = l2.processSample (s); // inter-stage LPF 2
            s = h2.processSample (s); // coupling cap 2 (DC block)
            s = tubeStage (s, w);     // valve warmth (DC-safe, soft top)
            d[n] = s;
        }
    }
    biasAmt.skip   (osSamples);   // advance master smoothers (see driveGain note)
    warmthAmt.skip (osSamples);

    oversampling->processSamplesDown (block);

    // --- Big Muff tone stack: crossfade low-path vs high-path ---
    for (int ch = 0; ch < numCh && ch < kNumCh; ++ch)
    {
        auto* x = buffer.getWritePointer (ch);
        auto& lp = toneLPF[(size_t) ch];
        auto& hp = toneHPF[(size_t) ch];
        auto& sc = toneScoop[(size_t) ch];
        auto& bs = bloatShelf[(size_t) ch];
        auto tb = toneBlend;
        for (int n = 0; n < numSamples; ++n)
        {
            const float t    = tb.getNextValue();
            const float low  = lp.processSample (x[n]);
            const float high = hp.processSample (x[n]);
            // Tone-stack crossfade with a moving mid scoop, then failing-cap
            // low bloat (Dying knob).
            x[n] = bs.processSample (sc.processSample (low * (1.0f - t) + high * t));
        }
    }
    toneBlend.skip (numSamples);   // advance master smoother (see driveGain note)

    // --- Dying-mode post FX (Cap Leak dulling + Sputter sag) ---
    // Both are multiplicative / low-pass only, so silence stays silent (the
    // motorboating lesson): no source can inject energy with no input.
    if (dyMode == 2 && capLeakHz < 19000.0f)
    {
        for (int ch = 0; ch < numCh && ch < kNumCh; ++ch)
        {
            auto* x = buffer.getWritePointer (ch);
            auto& lp = capLeakLPF[(size_t) ch];
            for (int n = 0; n < numSamples; ++n)
                x[n] = lp.processSample (x[n]);
        }
    }
    if (dyMode == 1 && sputterDepth > 0.0f)
    {
        // Dying-battery sag: a slow envelope ducks the gain under sustain, so
        // notes bloom then choke and splutter. env -> 0 on silence => gain -> 1.
        const float atk = 1.0f - std::exp (-1.0f / (0.012f * (float) currentSampleRate));
        const float rel = 1.0f - std::exp (-1.0f / (0.180f * (float) currentSampleRate));
        const float sagThreshold = 0.035f;
        for (int ch = 0; ch < numCh && ch < kNumCh; ++ch)
        {
            auto* x = buffer.getWritePointer (ch);
            float env = sputterEnv[(size_t) ch];
            for (int n = 0; n < numSamples; ++n)
            {
                const float a = std::abs (x[n]);
                env += (a > env ? atk : rel) * (a - env);
                const float sag = sputterDepth * juce::jmax (0.0f, env - sagThreshold) * 8.0f;
                x[n] *= 1.0f / (1.0f + sag);
            }
            sputterEnv[(size_t) ch] = env;
        }
    }

    // --- Direct-recording cab/amp tame ---
    if (cabMode > 0)
    {
        for (int ch = 0; ch < numCh && ch < kNumCh; ++ch)
        {
            auto* x = buffer.getWritePointer (ch);
            auto& hp = cabHPF[(size_t) ch];
            auto& pk = cabPeak[(size_t) ch];
            auto& lp = cabLPF[(size_t) ch];
            for (int n = 0; n < numSamples; ++n)
                x[n] = lp.processSample (pk.processSample (hp.processSample (x[n])));
        }
    }

    // --- Master volume + dry/wet mix ---
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* wet = buffer.getWritePointer (ch);
        const auto* dry = dryBuffer.getReadPointer (juce::jmin (ch, dryBuffer.getNumChannels() - 1));
        auto ol = outLevel;
        auto mx = mixAmt;
        for (int n = 0; n < numSamples; ++n)
        {
            const float m = mx.getNextValue();
            wet[n] = (wet[n] * ol.getNextValue() * m) + (dry[n] * (1.0f - m));
        }
    }
    outLevel.skip (numSamples);   // advance master smoothers (see driveGain note)
    mixAmt.skip   (numSamples);

    // --- Final safety: DC block + clamp (defends against any subsonic build-up
    //     or NaN/inf leaking to the DAW) ---
    for (int ch = 0; ch < numCh && ch < kNumCh; ++ch)
    {
        auto* x  = buffer.getWritePointer (ch);
        auto& dc = outputDC[(size_t) ch];
        for (int n = 0; n < numSamples; ++n)
        {
            float s = dc.processSample (x[n]);
            if (! std::isfinite (s)) s = 0.0f;
            x[n] = juce::jlimit (-2.0f, 2.0f, s);
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* ProFuzzAudioProcessor::createEditor()
{
    return new ProFuzzAudioProcessorEditor (*this);
}

//==============================================================================
void ProFuzzAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ProFuzzAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ProFuzzAudioProcessor();
}
