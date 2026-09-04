#!/bin/sh
# Build PocketLLM and lay out everything that goes on the Kindle.
#
#   ./build.sh              the two models worth having (~650 MB)
#   ./build.sh all          every model that fits on the device
#   ./build.sh smol360      just one
#   ./build.sh none         no models: build the app only
#
# Leaves you with dist/COPY-TO-KINDLE/, whose two folders are copied straight
# onto the Kindle's drive. Nothing else to install and nothing to configure.
set -e
cd "$(dirname "$0")"

WHICH="${1:-default}"
DIST=dist/COPY-TO-KINDLE
APPDIR="$DIST/extensions/pocketllm"

say() { printf '\n\033[1m%s\033[0m\n' "$*"; }
die() { printf '\n%s\n' "$*" >&2; exit 1; }

# --- 1. tools --------------------------------------------------------------
# Checked up front, together. Discovering the third missing tool after a
# ten-minute download is a bad way to spend an evening.
say "Checking what you have"
missing=
for t in zig cmake curl git make; do
    if command -v "$t" >/dev/null 2>&1; then
        echo "  ok       $t"
    else
        echo "  MISSING  $t"
        missing="$missing $t"
    fi
done
[ -z "$missing" ] || die "Install:$missing
  macOS:  brew install${missing}
  Debian: sudo apt install${missing}
(zig is the cross-compiler -- there is no ARM toolchain to set up besides it.)"

# --- 2. dependencies -------------------------------------------------------
say "Fetching dependencies"
sh tools/fetch-deps.sh

# --- 3. the binary ---------------------------------------------------------
# llama.cpp for armv7 is the slow part, several minutes, and only once: the
# build directory is kept, so running this again skips straight past it.
say "Building llama.cpp for the Kindle (a few minutes, once)"
make --no-print-directory llama

say "Building PocketLLM"
make --no-print-directory app-model

# --- 4. models -------------------------------------------------------------
if [ "$WHICH" = none ]; then
    say "Skipping models"
    mkdir -p "$APPDIR/models"
else
    say "Fetching models"
    sh tools/fetch-models.sh "$APPDIR/models" "$WHICH"
fi

# --- 5. lay it out exactly as it sits on the device ------------------------
# The folder mirrors the Kindle's drive, so installing is a copy rather than a
# set of instructions to follow correctly.
say "Assembling $DIST"
mkdir -p "$APPDIR" "$DIST/documents"
cp out/pocketllm "$APPDIR/"
cp assets/fonts/Literata.ttf assets/fonts/Inter.ttf "$APPDIR/"
cp assets/fonts/OFL-Literata.txt assets/fonts/OFL-Inter.txt "$APPDIR/"
cp dist/shortcut_pocketllm.sh "$DIST/documents/"
chmod +x "$APPDIR/pocketllm" "$DIST/documents/shortcut_pocketllm.sh"

cat > "$DIST/README.txt" <<'EOT'
Copy the two folders in here onto your Kindle's drive, merging with the
folders already there:

    documents/    -> Kindle/documents/
    extensions/   -> Kindle/extensions/

Then eject properly -- the models are large, and the copy is often still
finishing when the progress bar says it is not.

Open KUAL and choose PocketLLM. Without KUAL, the shortcut appears in your
library as something you can tap.

Tap the model name at the top to switch models. Each one shows what it costs
you in waiting.
EOT

say "Done"
printf '  %s\n\n' "$DIST"
find "$DIST" -type f | sort | while read -r f; do
    printf '    %-58s %s\n' "${f#$DIST/}" "$(du -h "$f" | cut -f1)"
done
printf '\n  total: %s\n\n' "$(du -sh "$DIST" | cut -f1)"
echo "  Copy documents/ and extensions/ onto the Kindle. See INSTALL.md."
