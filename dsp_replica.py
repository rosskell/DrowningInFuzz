"""Offline replica of the ProFuzz C++ DSP, for ground-truth testing.

Mirrors PluginProcessor.cpp so we can render the DI through the exact chain
and measure the result, instead of recording from the DAW.

NOTE: keep in sync with PluginProcessor.cpp. This is a verification tool.
"""
import numpy as np
from scipy.io import wavfile
from scipy.signal import bilinear, lfilter
import warnings; warnings.filterwarnings("ignore")


def biquad_lowpass(fs, fc):
    # match JUCE makeLowPass (RBJ, Q=1/sqrt2)
    w0 = 2*np.pi*fc/fs; cs=np.cos(w0); sn=np.sin(w0); Q=1/np.sqrt(2)
    al=sn/(2*Q)
    b0=(1-cs)/2; b1=1-cs; b2=(1-cs)/2
    a0=1+al; a1=-2*cs; a2=1-al
    return np.array([b0,b1,b2])/a0, np.array([1,a1/a0,a2/a0])

def biquad_highpass(fs, fc):
    w0=2*np.pi*fc/fs; cs=np.cos(w0); sn=np.sin(w0); Q=1/np.sqrt(2)
    al=sn/(2*Q)
    b0=(1+cs)/2; b1=-(1+cs); b2=(1+cs)/2
    a0=1+al; a1=-2*cs; a2=1-al
    return np.array([b0,b1,b2])/a0, np.array([1,a1/a0,a2/a0])

def biquad_lowshelf(fs, fc, gain_db, S=0.7):
    A=10**(gain_db/40); w0=2*np.pi*fc/fs; cs=np.cos(w0); sn=np.sin(w0)
    al=sn/2*np.sqrt((A+1/A)*(1/S-1)+2)
    tsa=2*np.sqrt(A)*al
    b0=A*((A+1)-(A-1)*cs+tsa); b1=2*A*((A-1)-(A+1)*cs); b2=A*((A+1)-(A-1)*cs-tsa)
    a0=(A+1)+(A-1)*cs+tsa; a1=-2*((A-1)+(A+1)*cs); a2=(A+1)+(A-1)*cs-tsa
    return np.array([b0,b1,b2])/a0, np.array([1,a1/a0,a2/a0])


def clip_stage(x, bias):
    # slope-asymmetry, f(0)=0, no DC injection (mirrors PluginProcessor.h)
    k = 2.0*np.where(x >= 0.0, 1.0, 1.0+bias)
    return np.tanh(k*x)


def render(di, fs, fuzz=65, tone=0.9, master_db=-6, bias=-0.12, dying=0.35, mix=1.0,
           lpf1=18000.0, lpf2=15000.0):
    # param mapping (mirror processBlock)
    drive_db = np.interp(fuzz, [0,100], [0,40])
    drive = 10**(drive_db/20)
    master = 10**(master_db/20)
    bias_eff = (bias - dying*0.25)*0.5
    bloat_db = dying*9.0

    dry = di.copy()

    # input HPF 45 Hz
    b,a = biquad_highpass(fs,45.0); x = lfilter(b,a,di)
    x = x*drive

    # oversample 4x (linear interp up, then decimate; good enough for replica)
    os = 4; xo = np.interp(np.arange(len(x)*os)/os, np.arange(len(x)), x)
    fso = fs*os
    # clip1 -> LPF8k -> clip2 -> LPF6k
    def onepole_hp(x, fc):
        # first-order high-pass (DC blocker); stable at tiny cutoff ratios
        a1 = np.exp(-2*np.pi*fc/fso)
        return lfilter([1, -1], [1, -a1], x)
    xo = clip_stage(xo, bias_eff)
    b,a = biquad_lowpass(fso,lpf1); xo = lfilter(b,a,xo)
    xo = onepole_hp(xo, 20.0)                               # coupling cap 1 (DC block)
    xo = clip_stage(xo, bias_eff)
    b,a = biquad_lowpass(fso,lpf2); xo = lfilter(b,a,xo)
    xo = onepole_hp(xo, 20.0)                               # coupling cap 2 (DC block)
    # downsample: anti-alias LPF then take every os-th
    b,a = biquad_lowpass(fso, fs*0.45); xo = lfilter(b,a,xo)
    x = xo[::os][:len(di)]

    # tone stack: crossfade LPF800 / HPF2k
    bl,al = biquad_lowpass(fs,800.0); low = lfilter(bl,al,x)
    bh,ah = biquad_highpass(fs,2000.0); high = lfilter(bh,ah,x)
    x = low*(1-tone) + high*tone
    # bloat shelf
    b,a = biquad_lowshelf(fs,180.0,bloat_db); x = lfilter(b,a,x)

    wet = x*master
    out = wet*mix + dry*(1-mix)
    # final safety DC blocker (first-order, base rate)
    a1 = np.exp(-2*np.pi*12.0/fs)
    out = lfilter([1,-1],[1,-a1], out)
    return out


def crest_db(x):
    return 20*np.log10(np.max(np.abs(x))/(np.sqrt(np.mean(x**2))+1e-12))


if __name__ == "__main__":
    sr,x = wavfile.read("Gtr_without_fuzz.wav")
    x=x.astype(np.float64)
    if x.ndim>1: x=x.mean(axis=1)
    x/=32768.0
    for mix in (0.0, 1.0):
        y = render(x, sr, mix=mix)
        print("mix=%.0f  peak=%.4f rms=%.4f crest=%.1fdB" %
              (mix, np.max(np.abs(y)), np.sqrt(np.mean(y**2)), crest_db(y)))
