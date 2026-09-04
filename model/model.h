/* model/model.h — the language model, behind an interface thin enough to stub.
 *
 * Two implementations satisfy this: model_llama.c wraps llama.cpp, and
 * model_none.c reports "unavailable" so the app links and runs with no model
 * at all. The second is not a courtesy: it is how the UI is developed, and how
 * a missing download degrades into a clear message instead of a crash.
 *
 * The constraints are the device's, and they were measured rather than
 * guessed (see docs/MEASUREMENTS.md):
 *   - weights MUST load unmapped. At llama.cpp's mmap default the kernel
 *     evicts weight pages -- there is no swap for them to live in -- and every
 *     generated token re-reads the model from eMMC: 16x slower.
 *   - two cores, ~5 tokens/second. The prompt length IS most of the wait.
 *   - 512 MB of RAM, no swap. A 0.5B model at Q4 peaks around 470 MB.
 */
#ifndef PL_MODEL_H
#define PL_MODEL_H

#include <stddef.h>

typedef struct pl_model pl_model;

/* One turn of the conversation. `role` is "user" or "assistant" -- it is
 * handed to the GGUF's own chat template, so the model sees the turn markers
 * it was trained on rather than raw text pretending to be a dialogue. */
typedef struct {
    const char *role;
    const char *text;
} pl_turn;

/* Called with each piece of text as it is produced, so the UI can stream.
 * Return 0 to stop generating -- that is how the stop button works. */
typedef int (*pl_model_stream)(const char *piece, void *user);

/* NULL if the file is missing or will not load. Never fatal to the caller. */
pl_model *pl_model_open(const char *gguf_path, int n_threads, int n_ctx);
void      pl_model_close(pl_model *m);

/* 1 if a model is actually loaded and usable. */
int pl_model_available(const pl_model *m);

/* Continue the conversation: generate at most max_tokens of the next
 * assistant turn, streaming through `cb`. Returns tokens produced, or -1.
 *
 * The whole conversation is passed every time rather than kept in here. The
 * caller owns history, so trimming it, editing it or starting over needs no
 * cooperation from this layer. It costs nothing, because the KV cache is
 * reused for whatever leading tokens are unchanged between calls -- which in a
 * chat is every turn but the newest. */
int pl_model_chat(pl_model *m, const pl_turn *turns, size_t n_turns,
                  int max_tokens, pl_model_stream cb, void *user);

/* How much of the context window this conversation would occupy, as a
 * percentage. The UI warns with it before a long paste silently pushes the
 * beginning of the conversation out. */
int pl_model_context_used(pl_model *m, const pl_turn *turns, size_t n_turns);

/* Human-readable, for a device with no console. */
const char *pl_model_name(const pl_model *m);

#endif
