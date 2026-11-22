"""Generate web/js/voxdata.js from src/vox_timing.h and src/vox_pitch.h."""
import re, os, json
here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, '..', '..', 'src')
timing = {m.group(1): int(m.group(2)) for m in re.finditer(r'\{"([^"]+)", (\d+)\}', open(os.path.join(src, 'vox_timing.h')).read())}
pitch = {m.group(1): [int(v) for v in m.group(2).split(',')]
         for m in re.finditer(r'\{"([^"]+)", \{([-\d, ]+)\}\}', open(os.path.join(src, 'vox_pitch.h')).read())}
out = os.path.join(here, '..', 'js', 'voxdata.js')
with open(out, 'w', newline='\n') as f:
    f.write('// Generated from src/vox_timing.h and src/vox_pitch.h (measurements of Half-Life\'s\n')
    f.write('// sound/vox word files: length in samples at 11025 Hz, pitch contour in fifths of a\n')
    f.write('// semitone re 100 Hz per eighth of the voiced speech).\n')
    f.write('export const VOX_TIMING = ' + json.dumps(timing, separators=(',', ':')) + ';\n')
    f.write('export const VOX_PITCH = ' + json.dumps(pitch, separators=(',', ':')) + ';\n')
print(len(timing), 'timings,', len(pitch), 'contours ->', out)
