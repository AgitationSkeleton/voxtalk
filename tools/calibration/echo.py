"""Estimate discrete echo delay from the averaged power cepstrum over many files.
echo.py dir [maxfiles]"""
import sys, glob, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze import load, SR

d = sys.argv[1]
files = sorted(glob.glob(os.path.join(d, '*.wav')))
files = [f for f in files if not os.path.basename(f).startswith('_')]
if len(sys.argv) > 2:
    files = files[:int(sys.argv[2])]
N = 1 << 14
acc = np.zeros(N)
envacc = np.zeros(N)
for f in files:
    x = load(f)
    if len(x) < 0.25 * SR:
        continue
    x = x[:N]
    X = np.fft.fft(x, N)
    c = np.real(np.fft.ifft(np.log(np.abs(X) ** 2 + 1e-9)))
    acc += c
    # envelope autocorrelation (smoothed over the pitch period)
    e = np.sqrt(np.convolve(x ** 2, np.ones(220) / 220, 'same'))
    e = e - e.mean()
    E = np.fft.fft(e, 2 * N)
    a = np.real(np.fft.ifft(np.abs(E) ** 2))[:N]
    envacc += a / (a[0] + 1e-12)
q = np.arange(N) / SR * 1000
sel = (q > 25) & (q < 600)
order = np.argsort(acc[sel])[::-1]
print('top cepstral peaks (ms):', [round(q[sel][i], 1) for i in order[:12]])
# envelope-autocorr local maxima
ea = envacc / len(files)
pk = [i for i in range(1, N - 1) if sel[i] and ea[i] > ea[i - 1] and ea[i] > ea[i + 1]]
pk = sorted(pk, key=lambda i: -ea[i])[:10]
print('envelope autocorr peaks (ms, value):', [(round(q[i], 1), round(ea[i], 3)) for i in sorted(pk)])
