# PocketLLM

A language model that runs **on** a jailbroken Kindle. Not a client for one
somewhere else — the weights are on the device, the arithmetic happens on the
device, and it works in aeroplane mode.

Type or paste anything and it answers. No account, no API key, no wifi, no
telemetry. Nothing you write leaves the Kindle.

![the chat screen](docs/chat.png)

## Why this is unusual

The established way to get an LLM onto a Kindle is KOReader's Assistant plugin,
which makes the Kindle a thin client for OpenAI, Claude, or an Ollama server on
your desk. Its author's own words: *"requests aren't processed on the meager
hardware of my Kindle."*

That is the reasonable conclusion. This repository takes the other one.

## What you are working with

Measured on a 12th-generation Paperwhite, not read off a spec sheet:

| | |
|---|---|
| CPU | MediaTek MT8113, two Cortex-A53 cores, **32-bit kernel** |
| RAM | 512 MB, **no swap** |
| Screen | 8bpp greyscale e-ink, ~100 ms partial refresh, ~500 ms full |
| Measured | Gemma-3-270M: **5.52 tok/s** generating, 8.64 evaluating |

A reply of a hundred words takes somewhere between ten seconds and a minute,
depending on which model you pick. That is the deal, and the app is honest
about it.

Two of those numbers explain most of the code:

**`load_mode = LLAMA_LOAD_MODE_NONE`.** At llama.cpp's mmap default the kernel
evicts weight pages — there is no swap for them to go to — so every generated
token re-reads the model from eMMC and the whole thing runs **sixteen times
slower**. `MemAvailable` does not move, which is how the cause was eventually
found. One line, worth more than every other tuning decision here.

**The 32-bit kernel.** Every batched-GEMM fast path in ggml is `__aarch64__`-
gated, so an armv7 build falls through to scalar C. That is why evaluating the
prompt (8 t/s) is barely faster than generating (5 t/s), when the usual ratio is
10–50×. There is no flag that fixes it; writing ARMv7 NEON kernels is real,
unclaimed upstream work.

## You pick the model

Generation here is memory-bandwidth bound: every token reads the whole model,
so **size is speed**, exactly. That makes the model choice the most
consequential thing on the device — and it is a real trade, not an obvious win
— so it gets a screen rather than a config file. Tap the model name at the top.

![choosing a model](docs/models.png)

Each row shows what it will actually cost you. The times start as estimates
derived from the one measured point above, scaled by file size; **the first
time a model answers on your device, its real rate replaces the estimate** and
the row says `measured here`.

The one that loads by default is the one with the most memory headroom, not the
cleverest. A model killed for memory halfway through a reply is a worse first
experience than a slightly worse writer, and the better one is one tap away on
a screen that says plainly what it costs.

Drop any other `.gguf` into `extensions/pocketllm/models/` and it appears in the
list, sized and timed from its own bytes.

## Build it

One script. It checks your tools, fetches everything, cross-compiles, and lays
out a folder you copy straight onto the Kindle:

```sh
./build.sh
```

```
dist/COPY-TO-KINDLE/
    documents/shortcut_pocketllm.sh
    extensions/pocketllm/
        pocketllm              3.9 MB, static, no dependencies
        models/                the models you chose
        Literata.ttf  Inter.ttf  OFL-*.txt
```

Copy those two folders onto the Kindle's drive and open KUAL. That is the whole
installation — see [INSTALL.md](INSTALL.md).

```sh
./build.sh all        # every model that fits on the device
./build.sh smol360    # just one
./build.sh none       # the app alone
```

You need `zig` (the cross-compiler — there is no ARM toolchain to set up
besides it), `cmake`, `curl`, `git` and `make`. `build.sh` checks for all of
them before it downloads anything.

## Work on it without a Kindle

```sh
make test        # layout and transcript, under a second
make screens     # every screen as a PNG at true panel size
make ask && ./out/ask dist/COPY-TO-KINDLE/extensions/pocketllm/models/*0.5B*.gguf \
      "hello" "now say that in French"
```

`make screens` runs the *same drawing code* the device runs, against a memory
buffer instead of `/dev/fb0`, so what you look at here is what the panel draws.
`make ask` runs the *same conversation code* natively, so a reply takes a second
instead of the half-minute it takes on the device.

That second one matters more than it sounds. The lesson from the sibling project
was: **verify answers, not prompts.** Reading real output found three prompt bugs
in twenty minutes that no amount of reasoning about prompts had.

## How it is put together

```
app/       chat.c      the transcript; fixed pool, drops oldest turns
           models.c    what is installed, what each costs, what it really did
           screens.c   drawing and hit tests, one geometry function each
           main.c      the event loop -- the only device-only file
model/     model_llama.c   llama.cpp; model_none.c   the same API, no model
platform/  draw.c      text and shapes, shared by both backends
           kindle/     /dev/fb0 and evdev
           host/       the same interface, writing a PNG
```

The `platform/` layer is shared, essentially unchanged, with a companion
project that answers questions about the book you are reading. Both draw
through one interface with two backends, which is why either one's screens can
be reviewed as PNGs without a device.

## Some things that are true of e-ink and not of screens

- **Flash the tap.** No cursor, no haptics. An unacknowledged tap reads as
  "broken" long before it reads as "still working."
- **Never full-refresh on a tap.** It flashes the whole panel black for half a
  second. Partial refresh, always, except on a genuinely new screen.
- **Redraw at most every ~450 ms while streaming.** Per-token would spend more
  of the budget pushing pixels than generating them, and the panel cannot show
  it that fast anyway.
- **Big targets, and a scroll rail.** Swipes on an e-ink digitiser are
  unreliable enough that a target you cannot miss beats a scrollbar you can.

## Licence

MIT — see [LICENSE](LICENSE). Everything fetched rather than committed is under
its own licence; [THIRD-PARTY.md](THIRD-PARTY.md) lists each one, including a
precise note on the one Kindle-specific header.

Kindle and Amazon are trademarks of Amazon.com, Inc. This project is not
affiliated with or endorsed by Amazon. Jailbreaking your Kindle is your
decision and your risk; nothing here modifies the device's firmware.
