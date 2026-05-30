# ProFuzz

A 64-bit VST3 fuzz guitar pedal plugin, built with [JUCE](https://juce.com) and C++.
By-ear emulation of a Pro Fuzz pedal, with a few extras the original never had
(noise gate, dry/wet mix, oversampled clipping).

## Controls

| Knob  | What it does |
|-------|--------------|
| Fuzz  | Input drive into the clipping stage (0–48 dB). The main fuzz amount. |
| Bias  | Waveshaper asymmetry. Off-centre = even harmonics, gnarlier/"dying battery" character. |
| Tone  | Post-clip low-pass. Dark (700 Hz) → bright (8 kHz). |
| Level | Output volume trim. |
| Gate  | Input noise gate threshold. Far left = off. Tames hiss/hum at high fuzz. |
| Mix   | Dry/wet blend. |

## DSP chain

```
input -> noise gate -> input drive -> [oversample x4] -> bias -> waveshaper
      -> [downsample] -> DC blocker -> tone low-pass -> output level -> dry/wet
```

Clipping is oversampled 4x to keep aliasing out. A DC blocker removes the offset
that `Bias` introduces.

## Build (Windows, Visual Studio 2022)

Prerequisites: CMake 3.22+ and VS2022 (Community is fine). JUCE is fetched
automatically by CMake on first configure — no manual SDK download.

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The VST3 is built to `build/ProFuzz_artefacts/Release/VST3/ProFuzz.vst3` and
(because `COPY_PLUGIN_AFTER_BUILD` is on) copied to your system VST3 folder
(`%COMMONPROGRAMFILES%\VST3`).

## Tuning the tone by ear

Everything lives in `Source/PluginProcessor.cpp`:

- `shape()` in `PluginProcessor.h` — the clipping curve. This is the heart of the
  fuzz character. Swap in different nonlinearities here.
- Drive range, tone cutoff range, default values — `createParameterLayout()` and
  the param reads in `processBlock()`.
```
