#!/usr/bin/env bash
# Builds voxtalk.exe (32-bit; the FlexTalk engine is a 32-bit DLL).
# Requirements: MSYS2 with the mingw-w64-i686 toolchain, and LAME built into
# third_party/lame-install (see README "Building").
set -euo pipefail
cd "$(dirname "$0")"
MINGW=${MINGW:-/c/msys64/mingw32}
export PATH="$MINGW/bin:$PATH"
LAME=${LAME:-third_party/lame-install}

mkdir -p build
# 1. pack the engine payload (FlexTalk DLLs + voice data)
g++ -O2 -static -municode -o build/packer.exe tools/packer.cpp
./build/packer.exe build/engine.pak payload 2>/dev/null

# 2. resources
windres -I src src/voxtalk.rc -O coff -o build/voxtalk.res

# 3. compile + link
g++ -std=c++17 -O2 -s -static -municode -mwindows -Wall -Wno-unused-function \
    -I"$LAME/include" \
    src/main.cpp src/engine.cpp src/dsp.cpp src/voice.cpp src/audio_out.cpp src/pack.cpp \
    build/voxtalk.res \
    -L"$LAME/lib" -lmp3lame -llamevectorroutines \
    -lole32 -luuid -lcomctl32 -lcomdlg32 -lwinmm -lshell32 -ladvapi32 \
    -o build/voxtalk.exe
ls -la build/voxtalk.exe
