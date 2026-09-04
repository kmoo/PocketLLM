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

## 1. Build the package

On a computer, with `zig`, `cmake` and `curl` installed:

```sh
git clone https://github.com/<you>/PocketLLM
cd PocketLLM
make deps                                  # fonts, stb_truetype, llama.cpp
sh tools/fetch-models.sh dist/pocketllm    # the model, ~384 MB
make package
```

That leaves you with:

```
dist/pocketllm/
    pocketllm          the app -- one static binary, no dependencies
    model.gguf         the weights
    Literata.ttf       replies
    Inter.ttf          interface
    OFL-*.txt          the fonts' licence, which has to travel with them
dist/shortcut_pocketllm.sh
```

`sh tools/fetch-models.sh --help` lists the alternatives. Take **smol360** if
`model.gguf` gets killed for memory on your device — half the RAM, twice the
speed, blunter answers.

## 2. Copy it over

Plug the Kindle in by USB. It appears as a drive; that drive is `/mnt/us`.

```
Kindle/
    extensions/
        pocketllm/          <- the whole dist/pocketllm folder
            pocketllm
            model.gguf
            Literata.ttf
            Inter.ttf
    documents/
        shortcut_pocketllm.sh   <- dist/shortcut_pocketllm.sh
```

On macOS or Linux, with the Kindle mounted:

```sh
mkdir -p /Volumes/Kindle/extensions
cp -R dist/pocketllm /Volumes/Kindle/extensions/
cp dist/shortcut_pocketllm.sh /Volumes/Kindle/documents/
```

Eject properly. The model is large, and the copy is often still finishing when
the progress bar says it is not.

## 3. Run it

Open **KUAL** and choose **PocketLLM**. Without KUAL, the shortcut shows up in
your library as an item you can tap.

The first screen takes a moment — half a gigabyte of weights has to be read off
the eMMC. After that, tap the bar at the bottom, type, and press **send**.

Replies arrive at about five words a second. **stop** takes whatever has
arrived so far and keeps it.

## If it does not work

Everything it does is logged to `pocketllm.log` at the top level of the Kindle's
drive. Plug in and read it — the last line before it stopped is usually enough.

**A blank screen, or nothing happens.** The executable bit did not survive the
copy. The shortcut runs `chmod +x` itself, so this normally fixes itself on the
second launch.

**"No model installed."** `model.gguf` is not in
`extensions/pocketllm/`, or the copy was cut short. Check the size: it should
match what `fetch-models.sh` reported.

**It starts, then the screen goes back to your book.** The app was killed for
memory. Use a smaller model:
`sh tools/fetch-models.sh dist/pocketllm smol360`.

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
