"""Remove the VOX feedback echo using the regression loop filter: x = y - sum h[k] y[n-D0-k]."""
import sys, os, glob, wave
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze import load, SR
src, dst, D0 = sys.argv[1], sys.argv[2], int(sys.argv[3])
h = np.load(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'loop_h.npy'))
os.makedirs(dst, exist_ok=True)
for f in glob.glob(os.path.join(src, '*.wav')):
    y = load(f)
    fb = np.zeros_like(y)
    for k, c in enumerate(h):
        L = D0 + k
        if L < len(y): fb[L:] += c * y[:-L]
    x = y - fb
    with wave.open(os.path.join(dst, os.path.basename(f)), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((np.clip(x, -1, 1) * 32767).astype('<i2').tobytes())
