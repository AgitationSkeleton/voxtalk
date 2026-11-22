"""Acoustic comparison helpers for matching FlexTalk output to HL VOX samples."""
import sys, wave
import numpy as np
from scipy.signal import resample_poly, stft

SR = 11025


def load(path):
    with wave.open(path, 'rb') as w:
        sr, n, sw, ch = w.getframerate(), w.getnframes(), w.getsampwidth(), w.getnchannels()
        raw = w.readframes(n)
    if sw == 1:
        x = (np.frombuffer(raw, np.uint8).astype(np.float64) - 128) / 128
    else:
        x = np.frombuffer(raw, np.int16).astype(np.float64) / 32768
    if ch > 1:
        x = x.reshape(-1, ch).mean(1)
    if sr != SR:
        g = np.gcd(sr, SR)
        x = resample_poly(x, SR // g, sr // g)
    return x


def trim(x, db=-40):
    env = np.convolve(np.abs(x), np.ones(64) / 64, 'same')
    thr = env.max() * 10 ** (db / 20)
    idx = np.where(env > thr)[0]
    return x[idx[0]:idx[-1] + 1] if len(idx) else x


def f0_track(x, fmin=35, fmax=400, frame=600, hop=110):
    """Simple YIN F0 estimate per frame; NaN for unvoiced."""
    x = x / (np.abs(x).max() + 1e-9)
    out = []
    tmax = SR // fmin
    for s in range(0, len(x) - frame - tmax, hop):
        seg = x[s:s + frame + tmax]
        if np.sqrt(np.mean(seg[:frame] ** 2)) < 0.02:
            out.append(np.nan); continue
        d = np.array([np.sum((seg[:frame] - seg[t:t + frame]) ** 2) for t in range(1, tmax)])
        cmnd = d * np.arange(1, tmax) / np.maximum(np.cumsum(d), 1e-12)
        lo = SR // fmax
        c = cmnd[lo:]
        cand = np.where(c < 0.15)[0]
        if not len(cand):
            out.append(np.nan); continue
        t = cand[0]
        while t + 1 < len(c) and c[t + 1] < c[t]:
            t += 1
        out.append(SR / (t + lo + 1))
    return np.array(out)


def f0_shs(x, fmin=35, fmax=350, n=1024, hop=128, nh=8):
    """Subharmonic-summation F0 per frame (NaN where quiet)."""
    x = x / (np.abs(x).max() + 1e-9)
    N = 16384
    win = np.hanning(n)
    cands = np.arange(fmin, fmax, 0.5)
    fr = np.fft.rfftfreq(N, 1 / SR)
    rms_all = []
    frames = []
    for s in range(0, len(x) - n, hop):
        seg = x[s:s + n]
        rms_all.append(np.sqrt(np.mean(seg ** 2)))
        frames.append(seg)
    if not frames:
        return np.array([])
    rmax = max(rms_all)
    out = []
    for seg, r in zip(frames, rms_all):
        if r < rmax * 0.15:
            out.append(np.nan); continue
        mag = np.abs(np.fft.rfft(seg * win, N))
        lm = np.log(mag + 1e-6 * mag.max())
        lm -= lm.min()
        score = np.zeros_like(cands)
        for k in range(1, nh + 1):
            score += (0.84 ** (k - 1)) * np.interp(cands * k, fr, lm)
        out.append(cands[np.argmax(score)])
    return np.array(out)


def ltas(x, nfft=512):
    f, t, Z = stft(x, SR, nperseg=nfft)
    p = np.mean(np.abs(Z) ** 2, axis=1)
    return f, 10 * np.log10(p / p.max() + 1e-12)


def tail_decay(x):
    """dB drop per 100 ms of the envelope after the last loud point (reverb indicator)."""
    env = 20 * np.log10(np.convolve(np.abs(x), np.ones(110) / 110, 'same') + 1e-9)
    pk = env.max()
    last = np.where(env > pk - 20)[0][-1]
    seg = env[last:last + int(0.3 * SR)]
    if len(seg) < 200:
        return np.nan
    t = np.arange(len(seg)) / SR
    k = np.polyfit(t, seg, 1)[0]
    return k / 10


def f0_comb(x, n=1536, hop=128, lo=100, hi=1400, fmin=35, fmax=300):
    """F0 from harmonic spacing: autocorrelation of the (detrended) log spectrum
    along frequency. Robust to a missing/filtered fundamental."""
    x = x / (np.abs(x).max() + 1e-9)
    N = 16384
    df = SR / N
    win = np.hanning(n)
    i0, i1 = int(lo / df), int(hi / df)
    segs = [x[s:s + n] for s in range(0, len(x) - n, hop)]
    if not segs:
        return np.array([])
    rms = np.array([np.sqrt(np.mean(s ** 2)) for s in segs])
    out = []
    for s, r in zip(segs, rms):
        if r < rms.max() * 0.2:
            out.append(np.nan); continue
        m = np.abs(np.fft.rfft(s * win, N))[i0:i1]
        lm = np.log(m + 1e-9)
        k = 41
        lm = lm - np.convolve(lm, np.ones(k * 4) / (k * 4), 'same')
        ac = np.correlate(lm, lm, 'full')[len(lm) - 1:]
        lags = np.arange(len(ac)) * df
        sel = (lags >= fmin) & (lags <= fmax)
        cand = np.where(sel)[0]
        # prefer the smallest lag whose peak is >= 85% of the best (avoid octave-up errors)
        best = ac[cand].max()
        pk = [c for c in cand[1:-1] if ac[c] >= ac[c - 1] and ac[c] >= ac[c + 1] and ac[c] >= 0.85 * best]
        out.append(lags[pk[0]] if pk else np.nan)
    return np.array(out)


def summary(path):
    x = trim(load(path))
    f0 = f0_comb(x)
    v = f0[~np.isnan(f0)]
    f, L = ltas(x)
    bands = [(0, 300), (300, 800), (800, 1500), (1500, 2500), (2500, 4000), (4000, 5500)]
    bl = [L[(f >= a) & (f < b)].mean() for a, b in bands]
    cen = np.sum(f * 10 ** (L / 10)) / np.sum(10 ** (L / 10))
    return dict(dur=len(x) / SR, f0med=np.median(v) if len(v) else np.nan,
                f0lo=np.percentile(v, 10) if len(v) else np.nan, f0hi=np.percentile(v, 90) if len(v) else np.nan,
                voiced=len(v) / max(len(f0), 1), centroid=cen, bands=np.round(bl, 1), decay=tail_decay(x))


if __name__ == '__main__':
    for p in sys.argv[1:]:
        s = summary(p)
        print(f"{p.split('/')[-1].split(chr(92))[-1]:28s} dur={s['dur']:.3f} f0={s['f0med']:.0f} [{s['f0lo']:.0f}-{s['f0hi']:.0f}] "
              f"vo={s['voiced']:.2f} cen={s['centroid']:.0f} decay={s['decay']:.1f}dB/100ms bands={s['bands']}")
