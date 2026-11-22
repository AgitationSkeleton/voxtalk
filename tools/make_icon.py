"""Generates src/voxtalk.ico: a rounded dark tile with an amber waveform (needs only numpy)."""
import struct, os, zlib
import numpy as np


def render(size, ss=4):
    n = size * ss
    y, x = np.mgrid[0:n, 0:n] + 0.5
    x /= n; y /= n
    r = 0.2
    dx = np.maximum(np.abs(x - 0.5) - (0.5 - r) + 0.02, 0)
    dy = np.maximum(np.abs(y - 0.5) - (0.5 - r) + 0.02, 0)
    tile = (np.hypot(dx, dy) <= r).astype(float)
    rgba = np.zeros((n, n, 4))
    top, bot = np.array([46, 52, 60]), np.array([24, 27, 32])
    rgba[..., :3] = top[None, None, :] * (1 - y[..., None]) + bot[None, None, :] * y[..., None]
    rgba[..., 3] = tile * 255
    heights = [0.22, 0.42, 0.62, 0.36, 0.52, 0.28, 0.16]
    bw = 0.075
    gap = (0.72 - len(heights) * bw) / (len(heights) - 1)
    amber = np.array([242, 160, 40])
    for i, h in enumerate(heights):
        cx = 0.14 + i * (bw + gap) + bw / 2
        rr = bw / 2
        dyy = np.maximum(np.abs(y - 0.5) - (h / 2 - rr), 0)
        m = (np.hypot(np.abs(x - cx), dyy) <= rr) & (tile > 0)
        rgba[m, :3] = amber
    img = rgba.reshape(size, ss, size, ss, 4).mean(axis=(1, 3))
    return np.clip(np.round(img), 0, 255).astype(np.uint8)


def bmp_entry(img):
    s = img.shape[0]
    xor = img[::-1, :, [2, 1, 0, 3]].tobytes()
    andmask = bytes(((s + 31) // 32) * 4 * s)
    hdr = struct.pack('<IiiHHIIiiII', 40, s, s * 2, 1, 32, 0, len(xor) + len(andmask), 0, 0, 0, 0)
    return hdr + xor + andmask


def png_entry(img):
    h, w = img.shape[:2]
    raw = b''.join(bytes([0]) + img[r].tobytes() for r in range(h))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    sig = bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A])
    return (sig + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    sizes = [16, 24, 32, 48, 64, 256]
    entries = [png_entry(render(s)) if s >= 256 else bmp_entry(render(s)) for s in sizes]
    out = struct.pack('<HHH', 0, 1, len(sizes))
    off = 6 + 16 * len(sizes)
    for s, e in zip(sizes, entries):
        out += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(e), off)
        off += len(e)
    out += b''.join(entries)
    dst = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'src', 'voxtalk.ico')
    open(dst, 'wb').write(out)
    print('wrote', dst, len(out))
