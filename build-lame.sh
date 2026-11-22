#!/usr/bin/env bash
# Downloads and builds LAME 3.100 as a static 32-bit library into third_party/lame-install.
# Run from an MSYS2 shell with the mingw-w64-i686 toolchain and make available.
set -euo pipefail
cd "$(dirname "$0")"
MINGW=${MINGW:-/c/msys64/mingw32}
export PATH="$MINGW/bin:$PATH"
mkdir -p third_party && cd third_party
URL=https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz
SHA=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e
[ -f lame-3.100.tar.gz ] || curl -L -o lame-3.100.tar.gz "$URL"
echo "$SHA  lame-3.100.tar.gz" | sha256sum -c -
rm -rf lame-3.100 && tar xzf lame-3.100.tar.gz
cd lame-3.100
./configure --host=i686-w64-mingw32 --disable-shared --enable-static --disable-frontend \
    --disable-decoder --disable-gtktest --prefix="$PWD/../lame-install" CFLAGS=-O2
make -j"$(nproc)"
make install
cp libmp3lame/vector/.libs/liblamevectorroutines.a ../lame-install/lib/
