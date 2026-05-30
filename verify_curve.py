"""Sanity-check: does tanh(1.8*x) match the measured Pro Fuzz transfer curve?

Compares the model against the median curve points pulled from analyze.py.
Pure local check.
"""
import numpy as np

# measured (in, out) from analyze.py, |in| > 0.1 region (clean part)
meas = [
    (0.146, 0.159), (0.195, 0.248), (0.244, 0.323), (0.293, 0.396),
    (0.341, 0.456), (0.390, 0.513), (0.439, 0.557), (0.488, 0.578),
    (0.537, 0.618), (0.585, 0.635), (0.634, 0.657), (0.683, 0.676),
    (0.732, 0.692), (0.780, 0.717),
]
xs = np.array([m[0] for m in meas])
ys = np.array([m[1] for m in meas])

# try a few drive constants, with output scale fit per-drive (least squares)
print(" k    out_scale   RMS error")
best = None
for k in np.arange(1.2, 3.6, 0.1):
    model = np.tanh(k * xs)
    a = np.sum(model * ys) / np.sum(model * model)   # best output scale
    err = np.sqrt(np.mean((a * model - ys) ** 2))
    if best is None or err < best[0]:
        best = (err, k, a)
    print(f"{k:.1f}   {a:.3f}      {err:.4f}")

err, k, a = best
print(f"\nBEST: tanh({k:.1f} * x) * {a:.3f}   RMS err {err:.4f}")
print("\nin     measured   model")
for x, y in meas:
    print(f"{x:.3f}   {y:.3f}     {a*np.tanh(k*x):.3f}")
