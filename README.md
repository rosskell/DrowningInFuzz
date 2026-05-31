# Drowning in Fuzz

A 64-bit VST3 fuzz guitar pedal plugin, built with [JUCE](https://juce.com) and C++.
By-ear emulation of a fuzz pedal, with extra studio controls: input trim, noise
gate, dry/wet mix, cab tame, auto level, and oversampled clipping.

## Controls

| Control | What it does |
|---------|--------------|
| Input | Pickup/reamp trim before the fuzz circuit (-18 to +18 dB). |
| Fuzz | Input drive into the clipping stage (0-40 dB). The main fuzz amount. |
| Tone | Big Muff-style low/high blend with a moving mid scoop. |
| Master | Output volume trim. |
| Dying | Failing-pedal character amount. |
| Warmth | Softer tube-like rounding after the clipping stages. |
| Bias | Waveshaper asymmetry. Off-centre = even harmonics, gnarlier/"dying battery" character. |
| Gate | Input noise gate threshold. Far left = off. Tames hiss/hum at high fuzz. |
| Mix | Dry/wet blend. |
| Dying Mode | Bias Drift, Sputter, or Cap Leak flavor. |
| Cab | Raw, Amp, or Dark Cab direct-recording tame. |
| Auto Level | Adds light output compensation as input/drive/dying/warmth rise. |
| Mk I / Mk II | Voicing selector. Mk II is darker/tighter with fuller mids. |

## DSP Chain

```text
input -> input trim -> noise gate -> drive -> [oversample x8] -> bias -> waveshaper
      -> warmth -> [downsample] -> tone stack + scoop -> dying modes
      -> cab tame -> output level / auto level -> dry/wet
```

Clipping is oversampled 8x to keep aliasing out. DC blockers and slope-asymmetric
clipping keep the dying modes from turning into motorboating.

## Build

Prerequisites: CMake 3.22+ and Visual Studio 2022 with C++ tools. JUCE is fetched
automatically by CMake on first configure.

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The VST3 is built to:

```text
build/DrowningInFuzz_artefacts/Release/VST3/Drowning in Fuzz.vst3
```

For day-to-day REAPER testing, use:

```powershell
.\scripts\build.ps1
```

That builds Release, refreshes `dist/Drowning in Fuzz.vst3`, and installs a copy
to your per-user VST3 folder:

```text
%LOCALAPPDATA%\Programs\Common\VST3
```

## Tuning

Everything lives in `Source/PluginProcessor.cpp`:

- `clipStage()` in `PluginProcessor.h` is the clipping curve.
- Drive range, tone cutoff range, default values, factory presets, and mode
  mappings live in `createParameterLayout()`, the preset table, and `processBlock()`.
