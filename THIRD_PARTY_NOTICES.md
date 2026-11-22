# Third-party components

## AT&T FlexTalk

`payload/` contains the FlexTalk text-to-speech engine DLLs, dictionaries and
voice inventory as distributed with AT&T's Watson 2.1 redistribution
(1997). They are included unmodified and are the property of their owner.

## Half-Life sound effects (Valve)

`payload/Sounds` contains the VOX sound effects `bizwarn`, `bloop`, `buzwarn`,
`dadeda`, `deeoo`, `doop`, `woop` and `endgame` from Half-Life and Team Fortress
Classic, by Valve Corporation, included unmodified. `src/vox_timing.h` and
`src/vox_pitch.h` list the lengths and pitch contours of Half-Life's VOX word
files (measurements only, no audio).

## v86 (web version)

`docs/vendor/libv86.mjs` and `docs/vendor/v86.wasm` are the x86 emulator
[v86](https://github.com/copy/v86) 0.5.476, BSD 2-Clause license
(`docs/vendor/v86-LICENSE.txt`), unmodified.

## lamejs (web version)

`docs/vendor/lame.min.js` is [lamejs](https://github.com/zhuker/lamejs) 1.2.1, a
JavaScript port of LAME, licensed under the GNU Lesser General Public License
version 3 (`docs/vendor/lamejs-LICENSE.txt`), unmodified. Its source is available
from the lamejs repository and the npm package `lamejs@1.2.1`; the page loads it
as a separate file, so it can be replaced with a modified build.

## LAME 3.100

voxtalk.exe statically links the LAME MP3 encoder library, version 3.100,
licensed under the GNU Library General Public License version 2
(`licenses/LAME-COPYING.txt`). LAME's source is available from
https://lame.sourceforge.io/; `build-lame.sh` fetches and builds the exact
release used. voxtalk's full source is in this repository, so the
executable can be rebuilt against a modified LAME.
