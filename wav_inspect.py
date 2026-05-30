"""Inspect a single recording: level, DC, clipping, spectrum, dropouts."""
import sys
import numpy as np
from scipy.io import wavfile

path = sys.argv[1] if len(sys.argv) > 1 else "profuzz1.wav"
sr, x = wavfile.read(path)
xf = x.astype(np.float64)
if xf.ndim > 1:
    chans = xf.shape[1]
    mono = xf.mean(axis=1)
else:
    chans = 1
    mono = xf

fullscale = {np.dtype('int16'): 32768.0, np.dtype('int32'): 2147483648.0,
             np.dtype('float32'): 1.0, np.dtype('float64'): 1.0}.get(x.dtype, np.max(np.abs(xf)) or 1.0)
n = mono / fullscale

print(f"file={path}")
print(f"sr={sr}  dtype={x.dtype}  chans={chans}  samples={len(n)}  dur={len(n)/sr:.2f}s")
print(f"peak={np.max(np.abs(n)):.4f}  rms={np.sqrt(np.mean(n**2)):.4f}  dc={np.mean(n):+.5f}")

# clipping: fraction of samples within 1% of full scale
clip = np.mean(np.abs(n) > 0.99)
print(f"near-fullscale frac (clipping)={clip*100:.2f}%")

# silence / dropouts: windowed rms, count near-silent windows
w = sr // 50  # 20ms
nw = len(n) // w
rms_w = np.array([np.sqrt(np.mean(n[i*w:(i+1)*w]**2)) for i in range(nw)])
silent = np.mean(rms_w < 1e-4)
print(f"silent-window frac (gating/dropout)={silent*100:.1f}%  (windows={nw})")

# crest factor
cf = np.max(np.abs(n)) / (np.sqrt(np.mean(n**2)) + 1e-12)
print(f"crest factor={cf:.2f}  ({20*np.log10(cf):.1f} dB)  (low=squashed/clipped)")

# spectrum bands
from scipy.signal import welch
f, p = welch(n, fs=sr, nperseg=8192)
p = 10*np.log10(p+1e-12)
print("band energy dB:")
for lo,hi in [(20,80),(80,160),(160,320),(320,640),(640,1280),(1280,2560),(2560,5120),(5120,10240),(10240,20000)]:
    sel=(f>=lo)&(f<hi)
    if np.any(sel):
        print(f"  {lo:5d}-{hi:5d}: {np.mean(p[sel]):6.1f}")

# any NaN/inf?
print(f"NaN={np.any(np.isnan(n))}  Inf={np.any(np.isinf(n))}")
