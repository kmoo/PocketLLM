#!/bin/sh
# Download models into a folder the device can read. Nothing here is committed:
# the weights are hundreds of megabytes and carry their own licences, which are
# not ours to redistribute.
#
# Every model offered is Apache-2.0. The app lists whatever .gguf files it
# finds, so anything you add by hand shows up too.
set -e

DIR="${1:-dist/COPY-TO-KINDLE/extensions/pocketllm/models}"
WHICH="${2:-default}"

usage() {
    cat <<'EOT'
usage: tools/fetch-models.sh [dest-dir] [which]

  default   SmolLM2-360M and Qwen2.5-0.5B -- the fast one and the good one,
            which is the choice worth having on the device. ~650 MB.
  all       everything below that fits on a Kindle. ~760 MB.

Or name one:

  qwen05    Qwen2.5-0.5B-Instruct   ~380 MB   ~24s a short reply
            The best writer that fits. Peaks near 480 MB of the device's
            ~512 MB -- it works, with little to spare.

  smol360   SmolLM2-360M-Instruct   ~270 MB   ~17s a short reply
            Quick and solid on facts, though it embellishes freely.
            The one to use if qwen05 gets killed for memory.

  smol135   SmolLM2-135M-Instruct   ~110 MB    ~8s a short reply
            Nearly instant, and about as coherent as that implies.

  qwen15    Qwen2.5-1.5B-Instruct   ~1.1 GB   desktop only, will NOT fit.
            Fetch it to compare against on a computer.

Times are for ~60 generated tokens, derived from a measured rate on a
12th-generation Paperwhite. The app replaces them with what your own device
actually does, the first time each model answers.

Licences: Apache-2.0 throughout.
  Qwen2.5   https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct
  SmolLM2   https://huggingface.co/HuggingFaceTB/SmolLM2-360M-Instruct

Deliberately not offered: Gemma (Google's own terms restrict use and must be
passed downstream) and LFM2 (bespoke, revenue-conditional licence). Both run
fine -- add them yourself if you have read and accepted those terms.
EOT
}

for arg in "$@"; do
    case "$arg" in -h|--help) usage; exit 0 ;; esac
done

fetch() { # key
    case "$1" in
      qwen05)  R=bartowski/Qwen2.5-0.5B-Instruct-GGUF; F=Qwen2.5-0.5B-Instruct-Q4_K_M.gguf ;;
      qwen15)  R=bartowski/Qwen2.5-1.5B-Instruct-GGUF; F=Qwen2.5-1.5B-Instruct-Q4_K_M.gguf ;;
      smol360) R=bartowski/SmolLM2-360M-Instruct-GGUF; F=SmolLM2-360M-Instruct-Q4_K_M.gguf ;;
      smol135) R=bartowski/SmolLM2-135M-Instruct-GGUF; F=SmolLM2-135M-Instruct-Q4_K_M.gguf ;;
      *) echo "unknown model: $1"; echo; usage; exit 1 ;;
    esac

    OUT="$DIR/$F"
    if [ -s "$OUT" ]; then
        echo "  have  $F ($(du -h "$OUT" | cut -f1))"
        return
    fi

    echo "  fetch $F"
    # Download beside the target and rename only on success, so an interrupted
    # transfer never leaves half a model that loads to a crash.
    curl -fL --progress-bar -o "$OUT.part" \
         "https://huggingface.co/$R/resolve/main/$F"

    # GGUF's magic is four bytes at offset 0. Checking costs nothing and turns
    # "downloaded an HTML error page" into a message rather than a segfault.
    if [ "$(dd if="$OUT.part" bs=1 count=4 2>/dev/null)" != "GGUF" ]; then
        echo "  that is not a GGUF file -- the download failed or the URL moved"
        rm -f "$OUT.part"
        exit 1
    fi
    mv "$OUT.part" "$OUT"
}

mkdir -p "$DIR"
case "$WHICH" in
    default) fetch smol360; fetch qwen05 ;;
    all)     fetch smol135; fetch smol360; fetch qwen05 ;;
    *)       fetch "$WHICH" ;;
esac

echo
ls -lh "$DIR" | tail -n +2 | awk '{printf "  %-46s %s\n", $9, $5}'
