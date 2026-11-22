"""Estimate a feedback-echo loop filter: y[n] = s[n] + sum_k h[k] y[n-D0-k].
Pools all files in a dir. loopfilt.py dir D0_samples K"""
import sys, glob, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze import load, SR

d, D0, K = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
files = [f for f in sorted(glob.glob(os.path.join(d, '*.wav'))) if not os.path.basename(f).startswith('_')]
R = np.zeros((K, K)); r = np.zeros(K)
for f in files:
    y = load(f)
    if len(y) < D0 + K + 100:
        continue
    n0 = D0 + K
    Y = np.stack([y[n0 - D0 - k: len(y) - D0 - k] for k in range(K)], 1)
    t = y[n0:]
    R += Y.T @ Y; r += Y.T @ t
h = np.linalg.solve(R + 1e-6 * np.trace(R) / K * np.eye(K), r)
print('taps (lag ms, coef):')
for k in range(K):
    if abs(h[k]) > 0.02:
        print(f'  {1000 * (D0 + k) / SR:7.2f} ms  {h[k]:+.3f}')
print('sum (DC gain) = %.3f' % h.sum())
H = np.fft.rfft(h, 1024)
fr = np.fft.rfftfreq(1024, 1 / SR)
for fq in [100, 250, 500, 1000, 2000, 3000, 4000, 5000]:
    print(f'  |H({fq})| = {20 * np.log10(abs(H[np.argmin(abs(fr - fq))]) + 1e-9):6.1f} dB')
np.save(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'loop_h.npy'), h)
