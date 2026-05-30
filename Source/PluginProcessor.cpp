#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
namespace ParamID
{
    constexpr auto drive = "drive";   // Fuzz: sustain / gain
    constexpr auto tone  = "tone";    // Big Muff tone stack
    constexpr auto level = "level";   // Master volume
    constexpr auto bias  = "bias";    // clipping asymmetry (extra)
    constexpr auto dying = "dying";   // failing-pedal grit + low bloat (extra)
    constexpr auto gate  = "gate";    // input noise gate (extra)
    constexpr auto mix   = "mix";     // dry/wet (extra)
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

    // --- The three real Pro Fuzz knobs ---

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

    // Dying: macro for the failing-pedal character that made the loved sample.
    // Pushes bias drift (grit) and low-end bloat together. 0 = healthy pedal.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::dying, 1 }, "Dying",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f));

    // Gate: threshold in dB. -100 = off.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::gate, 1 }, "Gate",
        NormalisableRange<float> (-100.0f, -20.0f, 0.1f), -100.0f));

    // Mix: 0 = dry, 1 = fully wet.
    params.push_back (std::make_unique<AudioParameterFloat>(
        ParameterID { ParamID::mix, 1 }, "Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
void ProFuzzAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    const auto numChannels = (juce::uint32) getTotalNumOutputChannels();

    // 4x oversampling (order 2) around the two clipping stages.
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        numChannels, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
    oversampling->initProcessing ((size_t) samplesPerBlock);

    // The clip stages + inter-stage LPFs run inside the oversampled domain.
    const double osRate = sampleRate * 4.0;

    juce::dsp::ProcessSpec specBase;
    specBase.sampleRate       = sampleRate;
    specBase.maximumBlockSize = (juce::uint32) samplesPerBlock;
    specBase.numChannels      = 1;

    juce::dsp::ProcessSpec specOS = specBase;
    specOS.sampleRate       = osRate;
    specOS.maximumBlockSize = (juce::uint32) samplesPerBlock * 4;

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

    for (auto& f : bloatShelf)
    {
        f.prepare (specBase);
        // Low shelf below 180 Hz; gain driven by the Dying knob in processBlock.
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            sampleRate, 180.0f, 0.7f, 1.0f);
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

    gateGain.fill (1.0f);

    const double ramp = 0.02; // 20 ms smoothing
    driveGain.reset (sampleRate, ramp);
    biasAmt.reset   (sampleRate, ramp);
    outLevel.reset  (sampleRate, ramp);
    mixAmt.reset    (sampleRate, ramp);
    toneBlend.reset (sampleRate, ramp);
    dyingAmt.reset  (sampleRate, ramp);

    dryBuffer.setSize ((int) numChannels, samplesPerBlock);
}

void ProFuzzAudioProcessor::releaseResources()
{
    if (oversampling != nullptr)
        oversampling->reset();
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
    const float driveDb = juce::jmap (apvts.getRawParameterValue (ParamID::drive)->load(),
                                      0.0f, 100.0f, 0.0f, 40.0f);
    driveGain.setTargetValue (juce::Decibels::decibelsToGain (driveDb));
    outLevel.setTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (ParamID::level)->load()));
    mixAmt.setTargetValue (apvts.getRawParameterValue (ParamID::mix)->load());
    toneBlend.setTargetValue (apvts.getRawParameterValue (ParamID::tone)->load());

    const float dying = apvts.getRawParameterValue (ParamID::dying)->load();
    dyingAmt.setTargetValue (dying);

    // Dying pushes the clipping further off-centre (more octave grit) on top of
    // the user Bias setting.
    const float baseBias = apvts.getRawParameterValue (ParamID::bias)->load();
    biasAmt.setTargetValue ((baseBias - dying * 0.25f) * 0.5f);

    // Dying opens the low-shelf bloat: 0 dB (healthy) .. +9 dB (failing cap).
    const float bloatDb = dying * 9.0f;
    for (auto& f : bloatShelf)
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            currentSampleRate, 180.0f, 0.7f, juce::Decibels::decibelsToGain (bloatDb));

    const float gateThrDb = apvts.getRawParameterValue (ParamID::gate)->load();
    const float gateThr   = juce::Decibels::decibelsToGain (gateThrDb);

    // --- Stash dry signal for the mix control ---
    dryBuffer.makeCopyOf (buffer, true);

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

        for (int n = 0; n < osSamples; ++n)
        {
            const float b = bv.getNextValue();
            float s = d[n];
            s = clipStage (s, b);     // stage 1
            s = l1.processSample (s); // inter-stage LPF 1
            s = h1.processSample (s); // coupling cap 1 (DC block)
            s = clipStage (s, b);     // stage 2
            s = l2.processSample (s); // inter-stage LPF 2
            s = h2.processSample (s); // coupling cap 2 (DC block)
            d[n] = s;
        }
    }
    biasAmt.skip (osSamples);   // advance master smoother (see driveGain note)

    oversampling->processSamplesDown (block);

    // --- Big Muff tone stack: crossfade low-path vs high-path ---
    for (int ch = 0; ch < numCh && ch < kNumCh; ++ch)
    {
        auto* x = buffer.getWritePointer (ch);
        auto& lp = toneLPF[(size_t) ch];
        auto& hp = toneHPF[(size_t) ch];
        auto& bs = bloatShelf[(size_t) ch];
        auto tb = toneBlend;
        for (int n = 0; n < numSamples; ++n)
        {
            const float t    = tb.getNextValue();
            const float low  = lp.processSample (x[n]);
            const float high = hp.processSample (x[n]);
            // Tone-stack crossfade, then failing-cap low bloat (Dying knob).
            x[n] = bs.processSample (low * (1.0f - t) + high * t);
        }
    }
    toneBlend.skip (numSamples);   // advance master smoother (see driveGain note)

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
