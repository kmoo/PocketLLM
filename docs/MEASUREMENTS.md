# Numbers, and where each one came from

Everything here was measured on a 12th-generation Paperwhite or in a
`linux/arm/v7` container capped to the device's memory. Nothing is a spec sheet
figure and nothing is a guess. Where a number decided a line of code, that line
is named.

## The device

| | |
|---|---|
| SoC | MediaTek MT8113, 2× Cortex-A53 |
| Kernel | **32-bit** (`armv7l`) |
| RAM | **1 GB** (`MemTotal: 979852 kB`), **no swap** |
| Free at launch | **`MemAvailable: 562848 kB`** — ~550 MB, with the reader running |
| Panel | 8bpp greyscale, controller reports `hwtcon_v2` |

The 32-bit kernel is the single most consequential fact. Every accelerated
integer kernel in ggml (`i8mm`, `dotprod`, the repacked GEMM paths) is behind
`#ifdef __aarch64__`, so an armv7 build falls through to scalar C.

## Memory

| configuration | peak RSS | headroom of the ~550 MB |
|---|---|---|
| Gemma-3-270M @ n_ctx 512 | 309 MB | ~240 MB |
| Qwen2.5-0.5B @ n_ctx 512 | 452 MB | ~100 MB |
| Qwen2.5-0.5B @ n_ctx 2048 | 470 MB | ~80 MB |

Under load, `MemAvailable` was watched down to a low-water mark of **238 MB**
during a Qwen2.5-0.5B run — so the app really does take ~310 MB of what the
device had free, and the headroom above is not theoretical.

**The number that matters is `MemAvailable`, not `MemTotal`.** They differ by
430 MB here, because the reader framework stays resident. Sizing against the
wrong one is the difference between a working app and one killed mid-sentence,
which is why `app/main.c` reads `/proc/meminfo` at startup instead of carrying
a constant.

KV cache costs roughly **19 KB per token**. Going from 512 to 4096 tokens of
context moved peak RSS by 66 MB.

PocketLLM ships at n_ctx 2048 — about 40 MB of KV — because a chat needs room
for a conversation, and it was measured working rather than merely calculated.
This is the number to lower first if the app is killed.

## Speed

| stage | rate |
|---|---|
| prompt evaluation | 8.64 tokens/second |
| generation | 5.52 tokens/second |

Those two being nearly equal is the anomaly. On a machine with batched GEMM the
ratio is 10–50×. Here it is 1.6×, because the batched paths are AArch64-gated
and this is armv7 — so a long prompt costs almost exactly what generating the
same number of tokens would.

**Consequence in the code:** `pl_model_chat` reuses the KV cache for the
unchanged prefix. In a conversation that prefix is every turn but the newest,
which is the difference between a reply starting in two seconds and in thirty.

## The mmap finding

The most expensive thing learned, and one line of code:

```c
mp.load_mode = LLAMA_LOAD_MODE_NONE;   /* model/model_llama.c */
```

At llama.cpp's default the weights are mmap'd. On a device with no swap the
kernel evicts those pages under pressure, and since they are file-backed it is
free to do so — so every generated token faults them back in from eMMC.
Generation ran **16× slower**, and `MemAvailable` did not move, which is why it
took so long to find: every memory metric looked healthy.

## E-ink

| refresh | cost | what it looks like |
|---|---|---|
| partial (`GL16`/`DU`) | ~100 ms | text updates, slight ghosting |
| full (`GC16`) | ~500 ms | the whole panel flashes black |

**Consequences in the code:** taps never trigger a full refresh (that was a real
bug in the sibling project — every tap flashed the screen black); the streaming
redraw is throttled to 450 ms, because per-token would spend more of the budget
pushing pixels than generating them; and a tap is acknowledged with a flashed
dot before any work starts, since e-ink has no cursor and no haptics.

## Models, on this hardware

All five read side by side on the same three prompts — explain a hash map in
two sentences, write two sentences of fiction, name the capital of Australia:

| model | disk | what it did |
|---|--:|---|
| SmolLM2-135M | 100 MB | Canberra, correct. Overran "two sentences" every time, and invented a "tree data structure" inside the hash map. |
| LFM2-350M | 219 MB | The best prose by a distance — and told us Canberra is Australia's largest city, then explained its own answer back to us. |
| Gemma-3-270M-QAT | 241 MB | The only one that reliably stopped at two sentences. Also the thinnest. |
| SmolLM2-360M | 258 MB | The clearest hash-map answer. Repeated a whole sentence verbatim in the fiction. |
| **Qwen2.5-0.5B** | **379 MB** | "The capital of Australia is Canberra." Full stop. The most accurate and the most disciplined. |

**Rankings do not transfer between tasks.** LFM2 was rejected in the sibling
book-chat project as "fluent and confidently wrong" — but that task was
answering *from supplied passages without inventing*, where fluency is a
liability. In open chat it is the best writer of the five. A verdict earned on
one task is not evidence about another, and every note in the picker was
re-earned here.

**Qwen3-0.6B was tested and excluded.** Same size as Qwen2.5-0.5B, a year
newer, Apache-2.0. Asked for the capital of Australia it emitted a `<think>`
block, reasoned its way to Sydney, invented "the 1935 Sydney riots" as the
cause, and hit the 200-token cap before producing an answer. On a device at
five tokens a second, a reasoning model spends the entire budget before it
starts.

## Sampling

Greedy decoding is right for a retrieval-grounded answer and wrong for open
chat: at 0.5B it repeats itself within a paragraph, and asking the same question
twice returns the same words. PocketLLM uses a repetition penalty (1.10 over the
last 64 tokens), top-k 40, top-p 0.95, temperature 0.7. The repetition penalty
matters considerably more here than the temperature does.
