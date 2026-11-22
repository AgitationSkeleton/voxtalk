"""Assemble a Half-Life VOX sentence from the game's own word files, following the
sentence syntax (word(sNN eNN), standalone (eNN) defaults, , and . pauses).
assemble.py voxdir out.wav "sentence" [--marks]   (prints word boundaries with --marks)"""
import sys, os, re, wave
import numpy as np


def load(path):
    with wave.open(path, 'rb') as w:
        sr, sw, n = w.getframerate(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(n)
    x = (np.frombuffer(raw, np.uint8).astype(np.float32) - 128) / 128 if sw == 1 else \
        np.frombuffer(raw, np.int16).astype(np.float32) / 32768
    if sr == 22050:
        x = x[::2]
    return x


def assemble(voxdir, sentence):
    defaults = {'s': 0.0, 'e': 100.0}
    out, marks = [], []
    pos = 0
    tokens = re.findall(r'\([^)]*\)|[^\s(,.]+(?:\([^)]*\))?|[,.]', sentence)
    for tok in tokens:
        if tok.startswith('('):
            for k, v in re.findall(r'([sept])(\d+)', tok):
                if k in 'se': defaults[k] = float(v)
            continue
        if tok in (',', '.'):
            tok = '_comma' if tok == ',' else '_period'
        m = re.match(r'([^()]+)(\([^)]*\))?', tok)
        word, mods = m.group(1).split('/')[-1].lower(), m.group(2) or ''
        if not word:
            continue
        p = dict(defaults)
        for k, v in re.findall(r'([sept])(\d+)', mods):
            if k in 'se': p[k] = float(v)
        x = load(os.path.join(voxdir, word + '.wav'))
        a, b = int(len(x) * p['s'] / 100), int(len(x) * p['e'] / 100)
        seg = x[a:b]
        marks.append((word, pos / 11025, (pos + len(seg)) / 11025))
        out.append(seg)
        pos += len(seg)
    return np.concatenate(out), marks


if __name__ == '__main__':
    voxdir, dst, sentence = sys.argv[1:4]
    y, marks = assemble(voxdir, sentence)
    with wave.open(dst, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(1); w.setframerate(11025)
        w.writeframes(np.clip(np.round(y * 128 + 128), 0, 255).astype(np.uint8).tobytes())
    if '--marks' in sys.argv:
        for wd, a, b in marks:
            print(f'  {wd:12s} {a:6.2f} - {b:6.2f}  ({b - a:.2f}s)')
    print(f'total {len(y) / 11025:.2f}s')
