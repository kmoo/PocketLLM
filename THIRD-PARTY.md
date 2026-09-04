# What is in here that is not ours

Nothing in this list is committed to the repository. `tools/fetch-deps.sh` and
`tools/fetch-models.sh` fetch each piece from its own upstream, so what ends up
on your Kindle is the original file under its own licence, and this repository
stays small and unencumbered.

| what | licence | where it comes from |
|---|---|---|
| **llama.cpp** — inference | MIT | github.com/ggml-org/llama.cpp, pinned to one commit |
| **stb_truetype.h** — glyph rasteriser | Public domain / MIT (dual) | github.com/nothings/stb |
| **Literata** — the reply typeface | SIL OFL 1.1 | Google Fonts. Drawn for e-readers, at e-reader sizes. |
| **Inter** — the interface typeface | SIL OFL 1.1 | Google Fonts |
| **Qwen2.5-0.5B-Instruct** (GGUF) | Apache-2.0 | Alibaba Cloud, quantised by bartowski |
| **SmolLM2** (GGUF) | Apache-2.0 | Hugging Face, quantised by bartowski |

`fetch-deps.sh` also fetches each font's `OFL.txt`, and `make package` copies
those next to the fonts, because OFL 1.1 requires the licence to travel with
the font files.

## Models we deliberately do not offer

`tools/fetch-models.sh` only offers Apache-2.0 models. Two that run perfectly
well on this hardware are left out on purpose:

- **Gemma** (Google). Not an open-source licence. The Gemma Terms of Use carry
  a use-restriction policy and require you to pass those terms on to anyone you
  give the weights to. Fine to use — but it is a decision for whoever ships the
  device, not a default we make for them.
- **LFM2** (Liquid AI). Its own bespoke licence, with conditions that depend on
  your revenue.

Both work if you add them yourself. Read the terms first.

## The Kindle-specific header

`platform/kindle/mtk_eink.h` declares the e-ink controller's ioctl numbers and
the structs they take. This is worth being precise about, because it is the one
place in this repository that touches anything of Amazon's.

**It contains no Amazon code.** It is an *interface description*: the request
codes and field layouts a program must use to ask the driver to refresh the
panel. There is no algorithm in it, and no way to express any of it differently
and still have the driver understand you. Under US law that is the kind of
material 17 U.S.C. §102(b) excludes from copyright — a method of operation —
and interoperability is exactly the use *Google v. Oracle* (2021) found to be
fair even where an interface was assumed copyrightable.

The declarations originate in the Kindle's own Linux kernel, which Amazon
publishes in source form as the GPL requires. Linux's userspace-facing (UAPI)
headers carry the `Linux-syscall-note` exception, which exists precisely so
that userspace programs under any licence may use them.

This subset was written with [FBInk](https://github.com/NiLuJe/FBInk)'s
`eink/mtk-kindle.h` open alongside it — FBInk having reconstructed the same
declarations from those kernel sources. **No FBInk code is used, copied, or
linked**, and PocketLLM does not depend on FBInk at runtime. Only the ABI that
FBInk and this file both describe is shared, and only the handful of constants
this app actually calls is kept.

## Trademarks

Kindle and Amazon are trademarks of Amazon.com, Inc. PocketLLM is not
affiliated with, endorsed by, or connected to Amazon in any way. The name is
used only to say which device the software runs on.
