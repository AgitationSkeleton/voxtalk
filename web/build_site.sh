#!/usr/bin/env bash
# Assemble the static web version into $SITE (default ../docs for GitHub Pages).
# Needs: the guest kernel + ramdisk (guest/build_guest.sh), node_modules with v86
# and lamejs (npm install in web/), python, gzip.
set -euo pipefail
cd "$(dirname "$0")"
SITE=${SITE:-../docs}
PY=${PY:-python}
WEBBUILD=${WEBBUILD:-../build/web}

mkdir -p "$SITE"
find "$SITE" -mindepth 1 -delete   # empty it (keeps the folder, e.g. while a local server uses it)
mkdir -p "$SITE"/{css,js,vendor,engine,sounds}
cp index.html "$SITE"/
cp css/style.css "$SITE"/css/
$PY tools/make_voxdata.py >/dev/null
cp js/app.js js/engine.js js/voice.js js/dsp.js js/audio.js js/voxdata.js "$SITE"/js/
cp node_modules/v86/build/libv86.mjs node_modules/v86/build/v86.wasm node_modules/v86/LICENSE "$SITE"/vendor/
mv "$SITE"/vendor/LICENSE "$SITE"/vendor/v86-LICENSE.txt
cp node_modules/lamejs/lame.min.js "$SITE"/vendor/
cp node_modules/lamejs/LICENSE "$SITE"/vendor/lamejs-LICENSE.txt
cp "$WEBBUILD"/kernel.bin "$SITE"/engine/
gzip -9 -n -c "$WEBBUILD"/engine.vxrd > "$SITE"/engine/engine.vxrd.gz
cp ../payload/Sounds/*.wav "$SITE"/sounds/
$PY tools/favicon.py "$SITE"/favicon.png
echo "voxtalk.viosarcade.xyz" > "$SITE"/CNAME
touch "$SITE"/.nojekyll
du -sh "$SITE"; find "$SITE" -type f | sort
