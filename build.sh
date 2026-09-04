#!/bin/sh
# Build PocketLLM and lay out everything that goes on the Kindle.
#
#   ./build.sh              asks which Kindle you have, then builds everything
#   ./build.sh 1gb          Paperwhite 12th gen and other 1 GB devices
#   ./build.sh 512mb        512 MB devices
#   ./build.sh 256mb        the oldest, where only the smallest model fits
#   ./build.sh smol360      one model by name
#   ./build.sh none         no models: build the app only
#
# Leaves you with dist/COPY-TO-KINDLE/, whose two folders are copied straight
# onto the Kindle's drive. Nothing else to install and nothing to configure.
set -e
cd "$(dirname "$0")"

WHICH="${1:-}"
DIST=dist/COPY-TO-KINDLE
APPDIR="$DIST/extensions/pocketllm"

say() { printf '\n\033[1m%s\033[0m\n' "$*"; }
die() { printf '\n%s\n' "$*" >&2; exit 1; }

# --- 0. which Kindle -------------------------------------------------------
# What decides this is how much memory is free with the reader still running,
# not the model number -- so that is what is asked, with the devices as hints.
#
# Getting it wrong is recoverable and not dangerous: the app reads
# /proc/meminfo at startup and judges every model against the real figure, so
# a model too big for your device is listed and greyed out rather than
# crashing. The question only decides what gets downloaded.
ask_device() {
    cat <<'EOT'

  Which Kindle is this for?

    1)  Paperwhite 12th gen (2024), or any Kindle with 1 GB of RAM
        ~550 MB free with the reader running. Measured on real hardware.
        All five models.                                      about 1.2 GB

    2)  A 512 MB Kindle -- Paperwhite 11th gen, Oasis, Scribe, and most
        models from the last decade.
        The four under 300 MB.                                about 820 MB

    3)  An older 256 MB Kindle.
        SmolLM2-135M alone, and even that will be tight.       about 100 MB

    4)  Not sure -- take all five and let the device decide.
        PocketLLM greys out anything that will not fit.        about 1.2 GB

    5)  No models. Just build the app.

  Only option 1 has been measured; the rest are the published figures for
  those devices. The app checks for itself either way.

EOT
    printf '  [1-5, default 1] '
    read -r reply
    case "$reply" in
        2)  WHICH=512mb ;;
        3)  WHICH=256mb ;;
        4)  WHICH=1gb   ;;
        5)  WHICH=none  ;;
        *)  WHICH=1gb   ;;
    esac
}

if [ -z "$WHICH" ]; then
    if [ -t 0 ]; then
        ask_device
    else
        # Non-interactive and unasked: take the measured device rather than
        # guess, and say so.
        WHICH=1gb
        echo "No device given and no terminal to ask -- assuming a 1 GB Kindle."
    fi
fi

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
    say "Fetching models ($WHICH)"
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
you in waiting, and anything too big for your Kindle is greyed out -- the app
reads the device's real free memory at startup rather than assuming.
EOT

say "Done"
printf '  %s\n\n' "$DIST"
find "$DIST" -type f | sort | while read -r f; do
    printf '    %-58s %s\n' "${f#$DIST/}" "$(du -h "$f" | cut -f1)"
done
printf '\n  total: %s\n\n' "$(du -sh "$DIST" | cut -f1)"
echo "  Copy documents/ and extensions/ onto the Kindle. See INSTALL.md."
