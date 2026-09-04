/* model/model_llama.c — Tier C on llama.cpp.
 *
 * Deliberately small: load, tokenize, decode, stream, stop. Everything that
 * makes this fast enough to use was measured on the device, not chosen.
 */
#include "model.h"
#include "llama.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct pl_model {
    struct llama_model   *model;
    struct llama_context *ctx;
    struct llama_sampler *smpl;
    const struct llama_vocab *vocab;
    char   name[96];
    /* The GGUF carries its own chat template (Gemma-3's <start_of_turn>
     * format). An instruct model given raw text instead of its expected turn
     * markers is undefined behaviour, not a smaller version of the right
     * behaviour -- observed on device as a bare "Answer: In the book." rather
     * than prose. NULL means none was found, and we fall back to raw text
     * rather than fail the model open. */
    const char *chat_tmpl;

    /* The tokens currently held in the KV cache. Two jobs: without it every
     * question stacked on the previous one's context until n_ctx overflowed,
     * and re-decoding the shared instruction prefix cost real seconds on a CPU
     * where prompt eval is barely faster than generation. */
    llama_token *prev;
    int          n_prev;
    int          n_ctx;
};

static void quiet_log(enum ggml_log_level level, const char *text, void *ud) {
    (void)level; (void)text; (void)ud;   /* no console on a Kindle */
}

pl_model *pl_model_open(const char *gguf_path, int n_threads, int n_ctx) {
    if (!gguf_path) return NULL;
    FILE *probe = fopen(gguf_path, "rb");
    if (!probe) return NULL;
    fclose(probe);

    llama_log_set(quiet_log, NULL);
    llama_backend_init();

    struct llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = 0;
    /* THE measurement from Phase 0b. With mmap the kernel evicts the weight
     * pages -- there is no swap for them to live in -- so every generated
     * token re-reads the model from eMMC and generation runs 16x slower.
     * MemAvailable does not even move, which is how the cause was found.
     * This single line is worth more than every other tuning choice here. */
    mp.load_mode = LLAMA_LOAD_MODE_NONE;

    struct llama_model *model = llama_model_load_from_file(gguf_path, mp);
    if (!model) { llama_backend_free(); return NULL; }

    struct llama_context_params cp = llama_context_default_params();
    cp.n_ctx           = (uint32_t)(n_ctx > 0 ? n_ctx : 512);
    cp.n_batch         = cp.n_ctx;
    cp.n_threads       = n_threads > 0 ? n_threads : 2;   /* the device has 2 cores */
    cp.n_threads_batch = cp.n_threads;

    struct llama_context *ctx = llama_init_from_model(model, cp);
    if (!ctx) { llama_model_free(model); llama_backend_free(); return NULL; }

    pl_model *m = calloc(1, sizeof *m);
    if (!m) { llama_free(ctx); llama_model_free(model); llama_backend_free(); return NULL; }
    m->model = model;
    m->ctx   = ctx;
    m->vocab = llama_model_get_vocab(model);
    m->n_ctx = (int)cp.n_ctx;
    m->prev  = calloc((size_t)m->n_ctx, sizeof *m->prev);
    if (!m->prev) { free(m); llama_free(ctx); llama_model_free(model);
                    llama_backend_free(); return NULL; }

    /* Open chat wants some variety -- greedy decoding at this size falls into
     * repeating itself within a paragraph, and asking the same thing twice
     * should not return the same words. A repetition penalty matters more here
     * than the temperature does. */
    struct llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    m->smpl = llama_sampler_chain_init(sp);
    llama_sampler_chain_add(m->smpl,
        llama_sampler_init_penalties(llama_vocab_n_tokens(m->vocab), 64, 1.10f, 0.0f, 0.0f));
    llama_sampler_chain_add(m->smpl, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(m->smpl, llama_sampler_init_top_p(0.95f, 1));
    llama_sampler_chain_add(m->smpl, llama_sampler_init_temp(0.7f));
    llama_sampler_chain_add(m->smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    m->chat_tmpl = llama_model_chat_template(model, NULL);

    const char *slash = strrchr(gguf_path, '/');
    snprintf(m->name, sizeof m->name, "%s", slash ? slash + 1 : gguf_path);
    return m;
}

void pl_model_close(pl_model *m) {
    if (!m) return;
    free(m->prev);
    if (m->smpl)  llama_sampler_free(m->smpl);
    if (m->ctx)   llama_free(m->ctx);
    if (m->model) llama_model_free(m->model);
    llama_backend_free();
    free(m);
}

int pl_model_available(const pl_model *m) { return m && m->ctx; }
const char *pl_model_name(const pl_model *m) { return m ? m->name : "none"; }

/* Render the conversation through the model's own chat template. Returns the
 * length written, or -1 if it will not fit -- the caller drops old turns and
 * asks again rather than sending a silently truncated history. */
static int render(pl_model *m, const pl_turn *turns, size_t n_turns,
                  char *out, size_t cap) {
    if (n_turns == 0 || n_turns > 128) return -1;

    llama_chat_message msgs[128];
    for (size_t i = 0; i < n_turns; i++) {
        msgs[i].role    = turns[i].role;
        msgs[i].content = turns[i].text;
    }

    if (m->chat_tmpl) {
        /* add_ass=true appends the marker that opens the assistant's turn, so
         * generation continues as the model replying rather than as it
         * guessing what comes next in a transcript. */
        int32_t need = llama_chat_apply_template(m->chat_tmpl, msgs, n_turns, true,
                                                 out, (int32_t)cap);
        if (need > 0 && (size_t)need < cap) { out[need] = 0; return need; }
        if (need > 0) return -1;    /* would not fit: caller trims */
    }

    /* No template in the GGUF. A plain transcript is a poor substitute, but it
     * is a great deal better than failing the whole conversation over it. */
    size_t n = 0;
    for (size_t i = 0; i < n_turns; i++) {
        int w = snprintf(out + n, cap - n, "%s: %s\n",
                         strcmp(turns[i].role, "user") == 0 ? "User" : "Assistant",
                         turns[i].text);
        if (w < 0 || (size_t)w >= cap - n) return -1;
        n += (size_t)w;
    }
    int w = snprintf(out + n, cap - n, "Assistant:");
    if (w < 0 || (size_t)w >= cap - n) return -1;
    return (int)(n + (size_t)w);
}

/* The rendered prompt can be far larger than any single message: people paste
 * whole articles in. Sized against the context window rather than a guess --
 * roughly 4 bytes per token, plus slack for template markup. */
static char *scratch(pl_model *m, size_t *cap) {
    static char  *buf;
    static size_t buf_cap;
    size_t want = (size_t)m->n_ctx * 6 + 4096;
    if (buf_cap < want) {
        char *nb = realloc(buf, want);
        if (!nb) { *cap = buf_cap; return buf; }
        buf = nb; buf_cap = want;
    }
    *cap = buf_cap;
    return buf;
}

int pl_model_context_used(pl_model *m, const pl_turn *turns, size_t n_turns) {
    if (!m || !m->ctx) return 0;
    size_t cap; char *buf = scratch(m, &cap);
    if (!buf) return 0;
    int len = render(m, turns, n_turns, buf, cap);
    if (len < 0) return 100;
    int n = llama_tokenize(m->vocab, buf, len, NULL, 0, true, true);
    if (n < 0) n = -n;                      /* the count comes back negated */
    int pct = (int)((long)n * 100 / m->n_ctx);
    return pct > 100 ? 100 : pct;
}

int pl_model_chat(pl_model *m, const pl_turn *turns, size_t n_turns,
                  int max_tokens, pl_model_stream cb, void *user) {
    if (!m || !m->ctx || !turns || !n_turns) return -1;

    size_t cap; char *buf = scratch(m, &cap);
    if (!buf) return -1;
    int len = render(m, turns, n_turns, buf, cap);
    if (len < 0) return -1;

    /* Leave room to actually answer: a prompt that fills the window produces
     * nothing and looks exactly like a hang. */
    int max_prompt = m->n_ctx - max_tokens - 8;
    if (max_prompt < 16) return -1;

    llama_token *toks = malloc((size_t)m->n_ctx * sizeof *toks);
    if (!toks) return -1;
    int n = llama_tokenize(m->vocab, buf, len, toks, max_prompt, true, true);
    if (n < 0) { free(toks); return -1; }   /* too long; caller drops old turns */

    /* Reuse the leading tokens that have not changed since the last call. In a
     * chat that is the entire conversation up to the newest message, which on
     * a CPU with no batched-GEMM acceleration is the difference between a
     * reply starting in two seconds and in thirty. */
    int keep = 0;
    while (keep < m->n_prev && keep < n - 1 && m->prev[keep] == toks[keep]) keep++;

    llama_memory_t mem = llama_get_memory(m->ctx);
    if (mem) llama_memory_seq_rm(mem, 0, keep, -1);
    else keep = 0;

    struct llama_batch batch = llama_batch_get_one(toks + keep, n - keep);
    if (llama_decode(m->ctx, batch) != 0) {
        /* A partial removal can fail. Start clean rather than decode onto a
         * cache we no longer understand. */
        if (mem) llama_memory_clear(mem, true);
        m->n_prev = 0;
        batch = llama_batch_get_one(toks, n);
        if (llama_decode(m->ctx, batch) != 0) { free(toks); return -1; }
    }
    for (int i = 0; i < n; i++) m->prev[i] = toks[i];
    m->n_prev = n;
    free(toks);

    int produced = 0;
    char piece[256];
    while (produced < max_tokens) {
        llama_token id = llama_sampler_sample(m->smpl, m->ctx, -1);
        if (llama_vocab_is_eog(m->vocab, id)) break;

        int plen = llama_token_to_piece(m->vocab, id, piece, (int32_t)sizeof piece - 1, 0, true);
        if (plen < 0) break;
        piece[plen] = 0;

        /* Streamed rather than buffered: on a device where a reply takes
         * thirty seconds, watching it arrive is the whole difference between
         * "thinking" and "frozen". */
        if (cb && !cb(piece, user)) break;
        produced++;

        llama_sampler_accept(m->smpl, id);
        if (m->n_prev < m->n_ctx) m->prev[m->n_prev++] = id;

        struct llama_batch next = llama_batch_get_one(&id, 1);
        if (llama_decode(m->ctx, next) != 0) break;
    }
    return produced;
}
