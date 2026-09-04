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
| RAM | 512 MB, no swap |
| Free at launch | ~550 MB reported, ~500 MB usable in practice |
| Panel | 8bpp greyscale, controller reports `hwtcon_v2` |

The 32-bit kernel is the single most consequential fact. Every accelerated
integer kernel in ggml (`i8mm`, `dotprod`, the repacked GEMM paths) is behind
`#ifdef __aarch64__`, so an armv7 build falls through to scalar C.

## Memory

| configuration | peak RSS | headroom |
|---|---|---|
| Gemma-3-270M @ n_ctx 512 | 309 MB | ~200 MB |
| Qwen2.5-0.5B @ n_ctx 512 | 452 MB | ~60 MB |
| Qwen2.5-0.5B @ n_ctx 2048 | 470 MB | ~42 MB |

KV cache costs roughly **19 KB per token**. Going from 512 to 4096 tokens of
context moved peak RSS by 66 MB.

PocketLLM ships at n_ctx 2048 — about 40 MB of KV — because a chat needs room
for a conversation, and 42 MB of headroom was measured working rather than
merely calculated. This is the number to lower first if the app is killed.

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

| model | disk | verdict |
|---|---|---|
| SmolLM2-135M Q4_K_M | 110 MB | fast, and about as coherent as that implies |
| SmolLM2-360M Q4_K_M | 270 MB | usable; short, blunt answers |
| Gemma-3-270M-QAT | 300 MB | cannot compose — loops within a paragraph |
| **Qwen2.5-0.5B Q4_K_M** | **384 MB** | **the default. Real sentences, holds a thread.** |
| LFM2-350M | 280 MB | fluent and confidently wrong |

Model capacity is the ceiling, not the prompt and not the retrieval. That was
established the hard way in the sibling project: an 11-question matrix on
Gemma-270M returned 2 usable answers, with failures like *"Mr Darcy is a
character that is presented as a character that is presented as a character."*
Qwen2.5-0.5B is a different class of thing and fits, with ~42 MB to spare.

## Sampling

Greedy decoding is right for a retrieval-grounded answer and wrong for open
chat: at 0.5B it repeats itself within a paragraph, and asking the same question
twice returns the same words. PocketLLM uses a repetition penalty (1.10 over the
last 64 tokens), top-k 40, top-p 0.95, temperature 0.7. The repetition penalty
matters considerably more here than the temperature does.
