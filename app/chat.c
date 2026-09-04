#include "chat.h"
#include <string.h>
#include <stdio.h>

/* How much of a reply we are willing to wait for. At ~5 tokens/second on this
 * CPU, 200 tokens is around forty seconds -- past that a reader has already
 * decided the device is broken, and the stop button is right there. */
#define PL_MAX_REPLY_TOKENS 200

/* Above this the oldest turns go before asking, rather than after the model
 * has spent thirty seconds discovering the prompt does not fit. */
#define PL_CONTEXT_HIGH_WATER 75

void pl_chat_init(pl_chat *c, pl_model *m) {
    memset(c, 0, sizeof *c);
    c->model = m;
}

void pl_chat_reset(pl_chat *c) {
    c->n = 0;
    c->used = 0;
    c->pending_len = 0;
    c->generating = 0;
}

/* Drop the oldest exchange. Turns point into the pool, so it is compacted
 * rather than freed -- there is nothing to free, and a fixed pool cannot
 * fragment. */
static void drop_oldest(pl_chat *c) {
    if (c->n < 2) { pl_chat_reset(c); return; }

    /* Two at a time: a user turn without its reply, or the reverse, reads as
     * the model ignoring what was said. */
    size_t drop = 2;
    size_t bytes = (size_t)(c->turns[drop].text - c->pool);
    memmove(c->pool, c->pool + bytes, c->used - bytes);
    c->used -= bytes;
    for (size_t i = drop; i < c->n; i++) {
        c->turns[i - drop] = c->turns[i];
        c->turns[i - drop].text -= bytes;
    }
    c->n -= drop;
}

int pl_chat_add(pl_chat *c, int from_user, const char *text) {
    if (!text) return 0;
    size_t len = strlen(text);
    if (len == 0) return 0;
    if (len + 1 > PL_TEXT_POOL) return 0;     /* will never fit, at any size */

    while (c->n >= PL_MAX_TURNS || c->used + len + 1 > PL_TEXT_POOL) {
        size_t before = c->n;
        drop_oldest(c);
        if (c->n == before) return 0;         /* nothing left to drop */
    }

    char *dst = c->pool + c->used;
    memcpy(dst, text, len + 1);
    c->used += len + 1;
    c->turns[c->n].text      = dst;
    c->turns[c->n].from_user = from_user;
    c->n++;
    return 1;
}

size_t pl_chat_messages(const pl_chat *c, pl_message *out, size_t max_out) {
    size_t n = 0;
    for (size_t i = 0; i < c->n && n < max_out; i++) {
        out[n].from_user = c->turns[i].from_user;
        out[n].text      = c->turns[i].text;
        out[n].streaming = 0;
        n++;
    }
    if (c->generating && n < max_out) {
        out[n].from_user = 0;
        out[n].text      = c->pending;
        out[n].streaming = 1;
        n++;
    }
    return n;
}

typedef struct {
    pl_chat         *c;
    pl_chat_progress on_token;
    void            *user;
} stream_ctx;

static int on_piece(const char *piece, void *user) {
    stream_ctx *s = user;
    pl_chat *c = s->c;

    size_t len = strlen(piece);
    if (c->pending_len + len + 1 >= sizeof c->pending) return 0;   /* full */

    /* A reply that opens with whitespace draws as a blank first line, which on
     * e-ink looks like nothing happened at all. */
    if (c->pending_len == 0) {
        while (*piece == ' ' || *piece == '\n') { piece++; len--; }
        if (len == 0) return 1;
    }

    memcpy(c->pending + c->pending_len, piece, len);
    c->pending_len += len;
    c->pending[c->pending_len] = 0;

    return s->on_token ? s->on_token(c, s->user) : 1;
}

int pl_chat_reply(pl_chat *c, pl_chat_progress on_token, void *user) {
    if (!c->model || !pl_model_available(c->model) || c->n == 0) return 0;

    c->pending[0]   = 0;
    c->pending_len  = 0;
    c->generating   = 1;

    int produced = -1;
    for (;;) {
        pl_turn turns[PL_MAX_TURNS];
        for (size_t i = 0; i < c->n; i++) {
            turns[i].role = c->turns[i].from_user ? "user" : "assistant";
            turns[i].text = c->turns[i].text;
        }

        /* Trim before asking, not after failing. */
        if (pl_model_context_used(c->model, turns, c->n) > PL_CONTEXT_HIGH_WATER
            && c->n > 1) {
            size_t before = c->n;
            drop_oldest(c);
            if (c->n != before) continue;
        }

        stream_ctx s = { c, on_token, user };
        produced = pl_model_chat(c->model, turns, c->n,
                                 PL_MAX_REPLY_TOKENS, on_piece, &s);

        /* -1 with room to shrink means the prompt did not fit after all. */
        if (produced < 0 && c->n > 1) {
            size_t before = c->n;
            drop_oldest(c);
            if (c->n != before) continue;
        }
        break;
    }

    c->generating = 0;

    /* Whatever arrived before a stop is still an answer worth keeping; only a
     * genuinely empty reply is dropped. */
    if (c->pending_len > 0) {
        pl_chat_add(c, 0, c->pending);
        c->pending[0] = 0;
        c->pending_len = 0;
        return produced > 0 ? produced : 1;
    }
    return 0;
}
