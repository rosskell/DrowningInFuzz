"""Tune ProFuzz to the real pedal: render DI through the replica, compare its
spectrum to the real wet recording, report per-band error and a total score.

Spectrum is gap-robust (unlike crest factor), so it's the tuning target.
Both signals are RMS-normalized before comparison so loudness doesn't dominate.
"""
import sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import welch
import warnings; warnings.filterwarnings("ignore")
import dsp_replica as R

BANDS = [(40,80),(80,160),(160,320),(320,640),(640,1280),
         (1280,2560),(2560,5120),(5120,10240),(10240,18000)]

def load(p):
    sr,x = wavfile.read(p); x=x.astype(np.float64)
    if x.ndim>1: x=x.mean(axis=1)
    return sr, x/32768.0

def band_spectrum(x, sr):
    f,p = welch(x, fs=sr, nperseg=8192)
    p = 10*np.log10(p+1e-12)
    return np.array([np.mean(p[(f>=lo)&(f<hi)]) for lo,hi in BANDS])

def rms_norm(x):
    return x/(np.sqrt(np.mean(x**2))+1e-12)

def main(**params):
    sr, di  = load("Gtr_without_fuzz.wav")
    _,  wet = load("Gtr_with_fuzz.wav")
    target = band_spectrum(rms_norm(wet), sr)
    y = R.render(di, sr, mix=1.0, **params)
    got = band_spectrum(rms_norm(y), sr)
    # align overall offset (we already rms-normed, but tilt is what matters)
    diff = got - target
    diff -= np.mean(diff)            # remove constant offset; compare shape
    score = np.sqrt(np.mean(diff**2))
    print("params:", params if params else "(defaults)")
    print(f"{'band':>13} {'target':>7} {'model':>7} {'shapeErr':>8}")
    for (lo,hi),t,g,d in zip(BANDS,target,got,diff):
        print(f"{lo:5d}-{hi:5d} {t:7.1f} {g:7.1f} {d:+8.1f}")
    print(f"SHAPE SCORE (RMS dB, lower=better): {score:.2f}")
    return score

if __name__ == "__main__":
    kw = {}
    for a in sys.argv[1:]:
        k,v = a.split("="); kw[k]=float(v)
    main(**kw)
