#!/bin/sh
# Everything the build needs that is not ours: fonts, a TrueType rasteriser,
# and llama.cpp. None of it is committed -- see THIRD-PARTY.md for what each
# piece is and under what licence.
set -e
cd "$(dirname "$0")/.."

mkdir -p vendor/stb assets/fonts

# --- stb_truetype (public domain / MIT, Sean Barrett) ----------------------
# One header, no build. It rasterises glyphs; platform/draw.c does the rest.
if [ ! -s vendor/stb/stb_truetype.h ]; then
    echo "fetching stb_truetype.h"
    curl -fL --progress-bar -o vendor/stb/stb_truetype.h \
      https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h
else
    echo "have stb_truetype.h"
fi

# --- fonts (SIL Open Font License 1.1) -------------------------------------
# Literata for the model's replies -- it was drawn for e-readers, at e-reader
# sizes. Inter for the interface. Both OFL, so they may be redistributed with
# the licence alongside; we fetch them rather than commit them so that what
# lands on your Kindle is the upstream file, unmodified.
font() { # url  dest
    if [ -s "$2" ]; then echo "have $(basename "$2")"; return; fi
    echo "fetching $(basename "$2")"
    curl -fL --progress-bar -o "$2" "$1"
}
GF=https://raw.githubusercontent.com/google/fonts/main
font "$GF/ofl/literata/Literata%5Bopsz,wght%5D.ttf" assets/fonts/Literata.ttf
font "$GF/ofl/inter/Inter%5Bopsz,wght%5D.ttf"       assets/fonts/Inter.ttf
font "$GF/ofl/literata/OFL.txt" assets/fonts/OFL-Literata.txt
font "$GF/ofl/inter/OFL.txt"    assets/fonts/OFL-Inter.txt

# --- llama.cpp (MIT) -------------------------------------------------------
# Pinned to an exact commit, not a branch. The API here moves fast --
# llama_memory_t and LLAMA_LOAD_MODE_NONE are both recent, and the second one
# is the difference between usable and sixteen times too slow -- so "latest"
# is not a reproducible build.
LLAMA_COMMIT="${LLAMA_COMMIT:-67a17c17caa95742186f8b1ecadd1b5abd6d5ebb}"
if [ ! -d vendor/llama.cpp ]; then
    echo "cloning llama.cpp @ $LLAMA_COMMIT"
    git clone --filter=blob:none https://github.com/ggml-org/llama.cpp vendor/llama.cpp
    (cd vendor/llama.cpp && git checkout --detach "$LLAMA_COMMIT")
else
    echo "have vendor/llama.cpp ($(cd vendor/llama.cpp && git rev-parse --short HEAD))"
fi

echo
echo "dependencies ready. next: make app-model"
