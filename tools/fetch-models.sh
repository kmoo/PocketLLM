#!/bin/sh
# Download a model. Nothing here is committed to the repository: the weights
# are hundreds of megabytes and they carry their own licences, which are not
# ours to redistribute.
#
# Every model offered below is under a licence that permits redistribution and
# commercial use. Read the LICENCE column before you pick one; the link goes to
# the actual terms, not to a summary.
set -e

DIR="${1:-dist/pocketllm}"

usage() {
    cat <<'EOT'
usage: tools/fetch-models.sh [dest-dir] [model]

  qwen05   Qwen2.5-0.5B-Instruct  Q4_K_M  ~400 MB   Apache-2.0   (default)
           The recommendation. It composes real sentences and holds a
           conversation. Peaks around 470 MB of the device's ~512 MB, which
           is tight but was measured working.

  qwen15   Qwen2.5-1.5B-Instruct  Q4_K_M  ~1.1 GB   Apache-2.0
           Noticeably better, and WILL NOT FIT on a 512 MB Kindle. Here for
           testing the same build on a desktop.

  smol360  SmolLM2-360M-Instruct  Q4_K_M  ~270 MB   Apache-2.0
           Half the memory and about twice the speed. Shorter, blunter
           answers. The one to use if qwen05 gets killed for memory.

  smol135  SmolLM2-135M-Instruct  Q4_K_M  ~110 MB   Apache-2.0
           Fast enough to feel instant, and about as coherent as that
           implies. Useful for checking the app works at all.

Licences: Apache-2.0 for every model above.
  Qwen2.5   https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct
  SmolLM2   https://huggingface.co/HuggingFaceTB/SmolLM2-360M-Instruct

Deliberately NOT offered: Gemma (Google's Gemma Terms of Use restrict use and
require passing the terms downstream) and LFM2 (its own non-OSI licence). They
run fine -- add them yourself if you have read and accepted those terms.
EOT
}

case "$2" in
    -h|--help) usage; exit 0 ;;
esac
case "$1" in
    -h|--help) usage; exit 0 ;;
esac

MODEL="${2:-qwen05}"
case "$MODEL" in
  qwen05)  REPO=bartowski/Qwen2.5-0.5B-Instruct-GGUF; FILE=Qwen2.5-0.5B-Instruct-Q4_K_M.gguf ;;
  qwen15)  REPO=bartowski/Qwen2.5-1.5B-Instruct-GGUF; FILE=Qwen2.5-1.5B-Instruct-Q4_K_M.gguf ;;
  smol360) REPO=bartowski/SmolLM2-360M-Instruct-GGUF; FILE=SmolLM2-360M-Instruct-Q4_K_M.gguf ;;
  smol135) REPO=bartowski/SmolLM2-135M-Instruct-GGUF; FILE=SmolLM2-135M-Instruct-Q4_K_M.gguf ;;
  *) echo "unknown model: $MODEL"; echo; usage; exit 1 ;;
esac

mkdir -p "$DIR"
OUT="$DIR/model.gguf"

if [ -s "$OUT" ]; then
    echo "already have $OUT ($(du -h "$OUT" | cut -f1)) -- delete it to re-download"
    exit 0
fi

echo "fetching $MODEL ($FILE)"
echo "  from https://huggingface.co/$REPO"
echo "  to   $OUT"
echo

# Download beside the target and rename only on success, so an interrupted
# transfer never leaves a half a model that loads to a crash.
curl -fL --progress-bar -o "$OUT.part" \
     "https://huggingface.co/$REPO/resolve/main/$FILE"

# GGUF's magic is four bytes at offset 0. Checking it costs nothing and turns
# "downloaded an HTML error page" into a message rather than a segfault.
if [ "$(dd if="$OUT.part" bs=1 count=4 2>/dev/null)" != "GGUF" ]; then
    echo "that is not a GGUF file -- the download failed or the URL moved"
    rm -f "$OUT.part"
    exit 1
fi

mv "$OUT.part" "$OUT"
echo
echo "done: $OUT ($(du -h "$OUT" | cut -f1))"
