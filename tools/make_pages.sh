#!/bin/sh
# The GitHub Pages site (the gh-pages branch): fm1-base's site (front page, web installer, firmware) and play/, the
# firmware in the browser through fm1-emu. Run from the folder that holds fm1-base/ and tri6-fm1/, after a release
# build (python3 fm1-base/tools/build.py tri6-fm1/app --release X.Y):
#   sh tri6-fm1/tools/make_pages.sh X.Y        -> tri6-fm1/site/
# fm1-base's make_site.py also wants the Emscripten emulator (build.py --web); without Emscripten a stand-in lets it
# run, and play/ (fm1-emu) stands in for the emulator.
set -e
V="$1"
[ -n "$V" ] || { echo "usage: sh tri6-fm1/tools/make_pages.sh X.Y"; exit 2; }
cd "$(dirname "$0")/../.."
PY=fm1-base/.venv/bin/python
[ -x "$PY" ] || PY=python3
EMU=tri6-fm1/app/build/emu
STUB=
if [ ! -f "$EMU/app.wasm" ]; then
    mkdir -p "$EMU" && : > "$EMU/app.wasm" && STUB=1
fi
"$PY" fm1-base/tools/make_site.py tri6-fm1/app "tri6-fm1/app/build/tri6fm1-$V.fwsc" "$V" tri6-fm1/site
if [ -n "$STUB" ]; then
    rm -rf tri6-fm1/site/emu "$EMU"
fi
mkdir -p tri6-fm1/site/play
sed "s/^const VERSION = \".*\";/const VERSION = \"$V\";/" tri6-fm1/app/site/play.html > tri6-fm1/site/play/index.html
echo "pages: tri6-fm1/site (play/ plays tri6fm1-$V.fwsc)"
