#!/bin/sh
# Download models into a folder the device can read. Nothing here is committed:
# the weights are hundreds of megabytes and carry their own licences, which are
# not ours to redistribute.
#
# Sizes below are the real files, and the times are for a ~60-token reply on a
# 12th-generation Paperwhite. Every note is what that model actually did on the
# same three questions -- an explanation, a piece of fiction, and a fact.
set -e

DIR="${1:-dist/COPY-TO-KINDLE/extensions/pocketllm/models}"
WHICH="${2:-1gb}"

usage() {
    cat <<'EOT'
usage: tools/fetch-models.sh [dest-dir] [set-or-model]

Sets, by how much memory the Kindle has free:

  1gb       everything below. ~1.2 GB.   Paperwhite 12th gen and anything
            else with 1 GB of RAM, where ~550 MB is free with the reader
            running. This is the one that has been measured.
  512mb     the four under 300 MB. ~820 MB.
  256mb     SmolLM2-135M alone, and even that will be tight.
  none      no models; build the app only.

The five, smallest first:

  smol135   SmolLM2-135M-Instruct   100 MB    ~8s
            Quick, and readable. Gets facts right and details invented.

  lfm2      LFM2-350M               219 MB   ~13s
            The best writer here, and the most confidently wrong -- it told
            us Canberra is Australia's largest city, at length.
            LFM Open License, NOT Apache. Opt-in; you will be asked.
            https://huggingface.co/LiquidAI/LFM2-350M

  gemma270m Gemma-3-270M-it-QAT     241 MB   ~14s
            Brief and obedient. Says little, but rarely rambles, and it is
            the only one that reliably respects "in two sentences".
            Gemma Terms of Use, NOT an open-source licence. Opt-in.
            https://ai.google.dev/gemma/terms

  smol360   SmolLM2-360M-Instruct   258 MB   ~15s
            Clear explanations. Repeats whole sentences in longer answers.

  qwen05    Qwen2.5-0.5B-Instruct   379 MB   ~24s
            The most accurate, and the only one that stops when it is done.
            Peaks near 480 MB -- on a 1 GB Kindle that works, with little
            to spare.

Also available, and NOT recommended:

  qwen15    Qwen2.5-1.5B-Instruct   1.1 GB   will not load on any Kindle.
            Fetch it to compare against on a computer.

Deliberately not offered: Qwen3-0.6B. Same size as qwen05 and newer, but it
is a reasoning model -- asked for the capital of Australia it spent the whole
token budget thinking out loud, concluded Sydney, and invented a 1935 riot to
explain why. At five tokens a second, thinking out loud is not a feature.

The app lists whatever .gguf files it finds, so anything you add by hand shows
up too, sized and timed from its own bytes.

Licences: Apache-2.0 except lfm2 and gemma270m, which are asked for by name.
EOT
}

for arg in "$@"; do
    case "$arg" in -h|--help) usage; exit 0 ;; esac
done

# --- licences that are not open source ------------------------------------
# Neither of these can be a default. Both oblige whoever redistributes the
# weights to pass the terms on, which makes accepting them a decision for the
# person installing rather than something a script does quietly.
#
# Declining is not an error. Inside a set it skips that one model and says how
# to include it later; only asking for it by name and then refusing stops the
# script, because there is then nothing else it was asked to do.
IN_SET=0

accept_terms() { # name  terms-url  extra-url  -> 0 accept, 1 decline
    key="POCKETLLM_ACCEPT_$(echo "$1" | tr '[:lower:]' '[:upper:]')_TERMS"
    eval "already=\${$key:-}"
    [ "$already" = 1 ] && return 0

    printf '\n  %s is not under an open-source licence.\n\n' "$1"
    printf '  Read the terms before accepting:\n\n      %s\n' "$2"
    [ -n "$3" ] && printf '      %s\n' "$3"
    printf '\n'

    if [ ! -t 0 ]; then
        echo "  No terminal to ask. To include it, re-run with:"
        printf '      %s=1 %s\n\n' "$key" "$0 '$DIR' $WHICH"
        return 1
    fi
    printf '  Accept? [y/N] '
    read -r reply
    case "$reply" in [yY]*) return 0 ;; esac
    printf '  Declined. To change your mind later:\n      %s=1 %s\n\n' \
           "$key" "$0 '$DIR' <name>"
    return 1
}

# Wraps a decline into "skip this one" or "stop", depending on whether the
# model was asked for by name.
gate() { # name  terms-url  extra-url
    if accept_terms "$@"; then return 0; fi
    [ "$IN_SET" = 1 ] && return 1
    exit 1
}

# Redistributing either model means shipping its terms alongside.
write_notice() { # file  body
    cat > "$DIR/$1"
}

fetch() { # key
    case "$1" in
      smol135) R=bartowski/SmolLM2-135M-Instruct-GGUF; F=SmolLM2-135M-Instruct-Q4_K_M.gguf ;;
      smol360) R=bartowski/SmolLM2-360M-Instruct-GGUF; F=SmolLM2-360M-Instruct-Q4_K_M.gguf ;;
      qwen05)  R=bartowski/Qwen2.5-0.5B-Instruct-GGUF; F=Qwen2.5-0.5B-Instruct-Q4_K_M.gguf ;;
      qwen15)  R=bartowski/Qwen2.5-1.5B-Instruct-GGUF; F=Qwen2.5-1.5B-Instruct-Q4_K_M.gguf ;;
      lfm2)
          gate LFM2 "https://huggingface.co/LiquidAI/LFM2-350M" \
                    "https://www.liquid.ai/lfm-license" || return 0
          R=LiquidAI/LFM2-350M-GGUF; F=LFM2-350M-Q4_K_M.gguf
          write_notice LFM2-LICENCE.txt <<'EOT'
LFM2-350M-Q4_K_M.gguf in this folder is provided by Liquid AI under the LFM
Open License, which is NOT an OSI open-source licence and carries conditions
of its own:

    https://huggingface.co/LiquidAI/LFM2-350M
    https://www.liquid.ai/lfm-license

This applies to that file only. The other models here are Apache-2.0, and
PocketLLM itself is MIT. If you pass this folder on, this file goes with it.
EOT
          ;;
      gemma270m)
          gate Gemma "https://ai.google.dev/gemma/terms" \
                     "https://ai.google.dev/gemma/prohibited_use_policy" || return 0
          R=unsloth/gemma-3-270m-it-qat-GGUF; F=gemma-3-270m-it-qat-Q4_K_M.gguf
          write_notice GEMMA-TERMS.txt <<'EOT'
Gemma is provided under and subject to the Gemma Terms of Use found at
ai.google.dev/gemma/terms

The full terms and the prohibited use policy:
    https://ai.google.dev/gemma/terms
    https://ai.google.dev/gemma/prohibited_use_policy

This applies to gemma-3-270m-it-qat-Q4_K_M.gguf in this folder only. The other
models here are Apache-2.0, and PocketLLM itself is MIT. If you pass this
folder on, this file goes with it.
EOT
          ;;
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
    none)  ;;
    1gb)   IN_SET=1; fetch smol135; fetch lfm2; fetch gemma270m; fetch smol360; fetch qwen05 ;;
    512mb) IN_SET=1; fetch smol135; fetch lfm2; fetch gemma270m; fetch smol360 ;;
    256mb) IN_SET=1; fetch smol135 ;;
    *)     fetch "$WHICH" ;;
esac

echo
ls "$DIR" >/dev/null 2>&1 && ls -lh "$DIR" | tail -n +2 | awk '{printf "  %-46s %s\n", $9, $5}'
