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

That fetches the fonts and llama.cpp, cross-compiles, downloads two models
(~650 MB), and leaves you with a folder shaped exactly like the Kindle's drive:

```
dist/COPY-TO-KINDLE/
    documents/
        shortcut_pocketllm.sh
    extensions/
        pocketllm/
            pocketllm                    the app: static, 3.9 MB, no dependencies
            models/
                SmolLM2-360M-...gguf     quick
                Qwen2.5-0.5B-...gguf     better, slower, tight on memory
            Literata.ttf  Inter.ttf      the typefaces
            OFL-*.txt                    their licence, which travels with them
```

`./build.sh all` adds SmolLM2-135M as well; `./build.sh smol360` takes one;
`./build.sh none` builds the app alone. `sh tools/fetch-models.sh --help`
explains what each one costs in memory and in waiting.

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

- **SmolLM2 360M** — about 15 seconds for a short reply. Loads by default,
  because it has room to spare.
- **Qwen2.5 0.5B** — about 25 seconds, and a noticeably better writer. Peaks
  near 480 MB of the device's ~512 MB. It works; there is not much left over.
- **SmolLM2 135M** — about 8 seconds. Simple answers, often wrong.

The times shown start as estimates. The first time each model answers on your
device, the row switches to what it really did.

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
it: PocketLLM will come back on that model, so tap the model name at the top
and choose SmolLM2 360M instead. It is remembered from then on.

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
