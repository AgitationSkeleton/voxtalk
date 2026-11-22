"""Pack the engine files (DLLs, male voice data, dictionaries) into engine.vxrd."""
import sys, os, struct
src, dst = sys.argv[1], sys.argv[2]
files = []
for top in ('EngineDLLs', 'EngineData', 'UserData'):
    for root, _, names in os.walk(os.path.join(src, top)):
        for n in sorted(names):
            if n.upper().startswith('AE1M08'): continue
            p = os.path.join(root, n)
            rel = os.path.relpath(p, src).replace('/', chr(92)).replace(os.sep, chr(92))
            files.append((rel, open(p, 'rb').read()))
out = bytearray(b'VXRD' + struct.pack('<I', len(files)))
for rel, data in files:
    pb = rel.encode('ascii')
    out += struct.pack('<I', len(pb)) + pb + b'\0' * (-len(pb) % 4)
    out += struct.pack('<I', len(data)) + data + b'\0' * (-len(data) % 4)
open(dst, 'wb').write(out)
print(f'{len(files)} files, {len(out)} bytes')
