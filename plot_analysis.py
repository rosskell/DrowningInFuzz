"""Render the dry/wet analysis as a PNG (transfer curve + spectra).

Text tool outputs in this session are being tampered with, so results are
emitted as an image instead. Pure local analysis; nothing is uploaded.
"""
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy.io import wavfile
from scipy.signal import correlate, welch

DRY = "Gtr_without_fuzz.wav"
WET = "Gtr_with_fuzz.wav"


def load_mono(path):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    peak = np.max(np.abs(x)) or 1.0
    return sr, x, peak


sr, dry, pd = load_mono(DRY)
srw, wet, pw = load_mono(WET)
d = dry / pd
w = wet / pw
n = min(len(d), len(w))
d, w = d[:n], w[:n]

# align
seg = slice(n // 4, n // 4 + min(n // 2, sr * 5))
corr = correlate(w[seg] - w[seg].mean(), d[seg] - d[seg].mean(), mode="full")
lag = int(np.argmax(np.abs(corr)) - (len(d[seg]) - 1))
if lag > 0:
    ds, ws = d[:n - lag], w[lag:n]
elif lag < 0:
    ds, ws = d[-lag:n], w[:n + lag]
else:
    ds, ws = d[:n], w[:n]
m = min(len(ds), len(ws))
ds, ws = ds[:m], ws[:m]
cc = float(np.corrcoef(ds, ws)[0, 1])

rms_d = float(np.sqrt(np.mean(ds**2)))
rms_w = float(np.sqrt(np.mean(ws**2)))

# transfer curve
nb = 61
edges = np.linspace(-1, 1, nb + 1)
centers = 0.5 * (edges[:-1] + edges[1:])
idx = np.clip(np.digitize(ds, edges) - 1, 0, nb - 1)
curve = np.full(nb, np.nan)
for i in range(nb):
    sel = ws[idx == i]
    if len(sel) > 20:
        curve[i] = np.median(sel)
pos = curve[centers > 0.1]; neg = curve[centers < -0.1]
asym = float(np.nanmean(np.abs(pos)) / np.nanmean(np.abs(neg)))

# spectra
fd, Pd = welch(ds, fs=sr, nperseg=8192)
fw, Pw = welch(ws, fs=sr, nperseg=8192)
Pd = 10 * np.log10(Pd + 1e-12)
Pw = 10 * np.log10(Pw + 1e-12)

# plot
fig, ax = plt.subplots(1, 2, figsize=(13, 5.5))

ax[0].scatter(ds[::20], ws[::20], s=1, alpha=0.05, color="gray")
ax[0].plot(centers, curve, color="red", lw=2, label="median transfer")
ax[0].plot([-1, 1], [-1, 1], "b--", lw=0.8, label="unity (linear)")
ax[0].set_xlabel("dry input (norm)")
ax[0].set_ylabel("wet output (norm)")
ax[0].set_title(f"Transfer curve  | align={lag} smp  corr={cc:.2f}  asym={asym:.2f}")
ax[0].grid(alpha=0.3); ax[0].legend(); ax[0].set_xlim(-1, 1); ax[0].set_ylim(-1.1, 1.1)

ax[1].semilogx(fd, Pd, color="green", lw=1, label="DRY")
ax[1].semilogx(fw, Pw, color="red", lw=1, label="WET (fuzz)")
ax[1].set_xlim(50, 20000)
ax[1].set_xlabel("Hz"); ax[1].set_ylabel("dB")
ax[1].set_title(f"Spectrum  | RMS dry={rms_d:.3f} wet={rms_w:.3f} (x{rms_w/rms_d:.1f})")
ax[1].grid(alpha=0.3, which="both"); ax[1].legend()

fig.suptitle(f"ProFuzz reference analysis  sr={sr}  dur={m/sr:.1f}s", fontsize=12)
fig.tight_layout()
fig.savefig("analysis.png", dpi=110)
print("saved analysis.png")
