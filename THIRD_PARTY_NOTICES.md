# Third-party components

## AT&T FlexTalk

`payload/` contains the FlexTalk text-to-speech engine DLLs, dictionaries and
voice inventory as distributed with AT&T's Watson 2.1 redistribution
(1997). They are included unmodified and are the property of their owner.

## LAME 3.100

voxtalk.exe statically links the LAME MP3 encoder library, version 3.100,
licensed under the GNU Library General Public License version 2
(`licenses/LAME-COPYING.txt`). LAME's source is available from
https://lame.sourceforge.io/; `build-lame.sh` fetches and builds the exact
release used. voxtalk's full source is in this repository, so the
executable can be rebuilt against a modified LAME.
