"""
Analyze a dry/wet guitar pair to reverse-engineer the Pro Fuzz pedal.

Inputs: a clean DI/chorus take and the same take reamped through the pedal.
Outputs (printed): sample rate, levels, time alignment, the input->output
transfer curve (the waveshaper), and spectral tilt before/after.

Pure local analysis. Nothing is uploaded anywhere.
"""
import numpy as np
from scipy.io import wavfile
from scipy.signal import correlate, welch

DRY = "Gtr_without_fuzz.wav"
WET = "Gtr_with_fuzz.wav"


def load_mono(path):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1:                      # stereo -> mono
        x = x.mean(axis=1)
    # normalize by dtype range so both sit in ~[-1, 1]
    peak = np.max(np.abs(x)) or 1.0
    return sr, x, peak


def main():
    sr_d, dry, peak_d = load_mono(DRY)
    sr_w, wet, peak_w = load_mono(WET)
    print(f"DRY: sr={sr_d}  samples={len(dry)}  dur={len(dry)/sr_d:.2f}s  peak={peak_d:.0f}")
    print(f"WET: sr={sr_w}  samples={len(wet)}  dur={len(wet)/sr_w:.2f}s  peak={peak_w:.0f}")
    if sr_d != sr_w:
        print("!! sample rates differ — resample needed before transfer-curve step")

    # work in normalized amplitude
    d = dry / peak_d
    w = wet / peak_w

    # --- time alignment via cross-correlation (use a middle chunk for speed) ---
    n = min(len(d), len(w))
    a = d[:n]; b = w[:n]
    seg = slice(n // 4, n // 4 + min(n // 2, sr_d * 5))  # up to 5s window
    corr = correlate(b[seg] - b[seg].mean(), a[seg] - a[seg].mean(), mode="full")
    lag = np.argmax(np.abs(corr)) - (len(a[seg]) - 1)
    print(f"\nAlignment: wet lags dry by {lag} samples ({1000*lag/sr_d:.2f} ms)")

    # shift wet back onto dry
    if lag > 0:
        ds = d[:n - lag]; ws = w[lag:n]
    elif lag < 0:
        ds = d[-lag:n]; ws = w[:n + lag]
    else:
        ds = d[:n]; ws = w[:n]
    m = min(len(ds), len(ws))
    ds, ws = ds[:m], ws[:m]

    # correlation after alignment (sanity)
    cc = np.corrcoef(ds, ws)[0, 1]
    print(f"Aligned correlation dry vs wet: {cc:.3f}  (higher = cleaner reamp match)")

    # --- gain match: scale dry so its RMS matches wet input region ---
    rms_d = np.sqrt(np.mean(ds**2)) or 1e-9
    rms_w = np.sqrt(np.mean(ws**2)) or 1e-9
    print(f"RMS dry={rms_d:.4f}  wet={rms_w:.4f}  wet/dry={rms_w/rms_d:.2f}")

    # --- transfer curve: bin dry amplitude, average wet per bin ---
    nb = 41
    edges = np.linspace(-1, 1, nb + 1)
    centers = 0.5 * (edges[:-1] + edges[1:])
    idx = np.digitize(ds, edges) - 1
    idx = np.clip(idx, 0, nb - 1)
    curve = np.full(nb, np.nan)
    spread = np.full(nb, np.nan)
    for i in range(nb):
        sel = ws[idx == i]
        if len(sel) > 20:
            curve[i] = np.median(sel)
            spread[i] = np.std(sel)
    print("\nTRANSFER CURVE (dry_in -> wet_out median):")
    print(" in     out     spread  n")
    for i in range(nb):
        sel_n = int(np.sum(idx == i))
        if not np.isnan(curve[i]):
            print(f"{centers[i]:+.3f}  {curve[i]:+.4f}  {spread[i]:.4f}  {sel_n}")

    # asymmetry: compare output at +x vs -x
    pos = curve[centers > 0.1]
    neg = curve[centers < -0.1]
    pa = np.nanmean(np.abs(pos)); na = np.nanmean(np.abs(neg))
    print(f"\nAsymmetry: mean|out| pos-half={pa:.4f}  neg-half={na:.4f}  ratio={pa/na:.3f}")
    print("  ratio != 1.0 => asymmetric clipping (set Bias). >1 clips negative harder.")

    # --- spectra ---
    def spec(x, label):
        f, p = welch(x, fs=sr_d, nperseg=8192)
        p = 10 * np.log10(p + 1e-12)
        bands = [(80,160),(160,320),(320,640),(640,1280),(1280,2560),
                 (2560,5120),(5120,10240)]
        print(f"\n{label} band energy (dB):")
        for lo, hi in bands:
            sel = (f >= lo) & (f < hi)
            print(f"  {lo:5d}-{hi:5d} Hz: {np.mean(p[sel]):6.1f}")
        return f, p

    spec(ds, "DRY")
    spec(ws, "WET")


if __name__ == "__main__":
    main()
