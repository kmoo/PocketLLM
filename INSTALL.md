# Installing PocketLLM

About ten minutes, most of it copying a 384 MB file over USB.

## Before you start

**Your Kindle must be jailbroken.** PocketLLM cannot install it for you and
nothing here modifies the firmware. If yours is not, start at
[the MobileRead wiki](https://wiki.mobileread.com/wiki/Kindle_Hacks_Information)
and come back. Jailbreaking is your decision and your risk.

You also want **KUAL** (the Kindle Unified Application Launcher), the usual way
to run anything on a jailbroken Kindle. If you do not have it, the shortcut
below still works — it just appears in your book list instead of a menu.

Tested on a 12th-generation Paperwhite. Other Kindles with a jailbreak and a
touchscreen should work: the app reads the panel's real size at startup rather
than assuming one.

## 1. Build it

On a computer, with `zig`, `cmake`, `curl`, `git` and `make` installed — the
script checks for all five before it downloads anything:

```sh
git clone https://github.com/<you>/PocketLLM
cd PocketLLM
./build.sh
```

It asks which Kindle you have first, because what fits depends on free memory:

```
  1)  Paperwhite 12th gen (2024), or any Kindle with 1 GB of RAM   all five
  2)  A 512 MB Kindle -- Paperwhite 11th gen, Oasis, Scribe        four
  3)  An older 256 MB Kindle                                       one
  4)  Not sure -- take all five and let the device decide
  5)  No models. Just build the app.
```

Only option 1 has been measured. Getting it wrong is harmless: PocketLLM reads
your Kindle's actual free memory at startup and greys out anything that will
not run.

Two of the five — LFM2 and Gemma — are not under open-source licences, so the
script shows you the terms and asks before fetching either. Declining skips
that model and carries on.

Then it fetches the fonts and llama.cpp, cross-compiles, and leaves you with a
folder shaped exactly like the Kindle's drive:

```
dist/COPY-TO-KINDLE/
    documents/
        shortcut_pocketllm.sh
    extensions/
        pocketllm/
            pocketllm                    the app: static, 3.9 MB, no dependencies
            models/
                SmolLM2-135M-...gguf     fastest
                LFM2-350M-...gguf        best prose
                gemma-3-270m-...gguf     briefest
                SmolLM2-360M-...gguf     clearest explanations
                Qwen2.5-0.5B-...gguf     most accurate, slowest
                GEMMA-TERMS.txt          licences that must travel with
                LFM2-LICENCE.txt         those two models
            Literata.ttf  Inter.ttf      the typefaces
            OFL-*.txt                    their licence, which travels with them
```

Non-interactively: `./build.sh 1gb`, `512mb`, `256mb`, `none`, or one model by
name. `sh tools/fetch-models.sh --help` describes each one and what it costs
you in memory and in waiting.

## 2. Copy it over

Plug the Kindle in by USB. It appears as a drive; that drive is `/mnt/us`.
**Copy the two folders inside `dist/COPY-TO-KINDLE/` onto it, merging with the
folders already there.** That is the entire installation.

On macOS or Linux, with the Kindle mounted:

```sh
cp -R dist/COPY-TO-KINDLE/. /Volumes/Kindle/
```

On Windows, drag `documents` and `extensions` onto the Kindle drive and choose
"merge" when asked.

Eject properly. The models are large, and the copy is often still finishing
when the progress bar says it is not.

## 3. Run it

Open **KUAL** and choose **PocketLLM**. Without KUAL, the shortcut shows up in
your library as an item you can tap.

The first screen takes a moment — a few hundred megabytes of weights has to be
read off the eMMC. After that, tap the bar at the bottom, type, and press
**send**.

**stop** keeps whatever has arrived so far.

## 4. Pick your model

Tap the model name at the top of the screen. Every model you installed is
listed with what it costs you:

| | a short reply | what it is like |
|---|--:|---|
| **SmolLM2 135M** | ~8s | Quick, and readable. Gets facts right and details invented. |
| **LFM2 350M** | ~13s | The best writer here, and the most confidently wrong. |
| **Gemma 3 270M** | ~14s | Brief and obedient. Says little, but rarely rambles. |
| **SmolLM2 360M** | ~15s | Clear explanations. Repeats whole sentences in longer answers. |
| **Qwen2.5 0.5B** | ~24s | The most accurate, and the only one that stops when it is done. |

The one that loads first is whichever has the most memory to spare, not the
cleverest — a model killed mid-reply is a worse first impression than a
slightly worse writer.

The times start as estimates. **The first time each model answers on your
device, the row switches to what it really did** and says `measured here`.

Your choice is remembered across restarts. Any other `.gguf` you drop into
`extensions/pocketllm/models/` appears in the list too, timed from its size.

## If it does not work

Everything it does is logged to `pocketllm.log` at the top level of the Kindle's
drive. Plug in and read it — the last line before it stopped is usually enough.

**A blank screen, or nothing happens.** The executable bit did not survive the
copy. The shortcut runs `chmod +x` itself, so this normally fixes itself on the
second launch.

**"No models installed."** `extensions/pocketllm/models/` is empty or missing.
Check the sizes against what `build.sh` printed; a cut-short copy is the usual
cause.

**It starts, then the screen goes back to your book.** The app was killed for
memory — almost always Qwen2.5 0.5B, which runs with little to spare. Reopen
it, tap the model name at the top, and choose a smaller one. It is remembered
from then on. `pocketllm.log` records how much memory the device reported at
startup, which is the first thing to check.

**Replies stop mid-sentence.** That is the cap, not a crash: a reply is limited
to about two hundred tokens so it cannot run for five minutes. Ask it to
continue.

## Removing it

Delete `extensions/pocketllm/` and `documents/shortcut_pocketllm.sh`. That is
all of it — nothing was installed anywhere else, and nothing on the device was
modified.

## What it does with your data

Nothing. It has no network code of any kind. What you type stays in memory
until you close it, and the log records timings and error messages, not your
messages. There is no history file: closing PocketLLM forgets the conversation.

Two small files are written next to the app: `chosen.txt`, the model you
picked, and `speeds.txt`, how fast each model ran here. Both are a line each,
and neither contains anything you typed.
