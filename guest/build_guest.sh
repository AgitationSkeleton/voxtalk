#!/usr/bin/env bash
# Builds the guest kernel (kernel.bin) and the engine ramdisk (engine.vxrd).
set -euo pipefail
cd "$(dirname "$0")"
MINGW=${MINGW:-/c/msys64/mingw32}
export PATH="$MINGW/bin:$PATH"
PY=${PY:-python}
OUT=${OUT:-../build/web}
mkdir -p obj "$OUT"
CFLAGS="-m32 -O2 -march=i686 -mno-sse -ffreestanding -fno-builtin -nostdlib -fno-asynchronous-unwind-tables -fno-stack-protector -mno-stack-arg-probe -fno-pic -Wall -Wno-unused-function -Wno-attributes"
for f in rt thread fs pe registry win32 host; do gcc $CFLAGS -c $f.c -o obj/$f.o; done
gcc -m32 -c boot.S -o obj/boot.o
$PY gen_shims.py argbytes.txt obj/shims_gen.c obj/win32.o obj/registry.o
gcc $CFLAGS -I. -c obj/shims_gen.c -o obj/shims_gen.o
gcc -m32 -nostdlib -Wl,--image-base,0x100000 -Wl,--section-alignment,0x1000 -Wl,--file-alignment,0x1000 \
    -Wl,-e,_start -o obj/kernel.exe obj/boot.o obj/rt.o obj/thread.o obj/fs.o obj/pe.o obj/registry.o obj/win32.o obj/host.o obj/shims_gen.o -lgcc
objcopy -O binary obj/kernel.exe "$OUT/kernel.bin"
$PY make_ramdisk.py ../payload "$OUT/engine.vxrd"
ls -la "$OUT"
