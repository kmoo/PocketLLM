/* app/chat.h — the conversation, and the one thing the app does to it.
 *
 * Kept apart from the UI so it can be tested without a screen: append turns,
 * ask for a reply, watch it stream in. No drawing, no touch, no files.
 */
#ifndef PL_CHAT_H
#define PL_CHAT_H

#include <stddef.h>
#include "ui_types.h"
#include "../model/model.h"

/* Memory is the hard limit here, not taste. The largest model already holds
 * ~470 MB of the ~550 MB a Paperwhite has free, with no swap, so the
 * transcript gets a fixed allocation that cannot creep: exceed it and the
 * oldest turns are dropped, which is what the context window would force a few
 * turns later anyway. */
#define PL_MAX_TURNS 64
#define PL_TEXT_POOL (192 * 1024)

typedef struct {
    char   *text;          /* into `pool` */
    int     from_user;
} pl_chat_turn;

typedef struct {
    pl_chat_turn turns[PL_MAX_TURNS];
    size_t       n;

    char   pool[PL_TEXT_POOL];
    size_t used;

    pl_model *model;

    /* The reply currently arriving. Separate from the transcript so a stopped
     * or failed generation can be committed, or discarded, as one decision. */
    char   pending[8192];
    size_t pending_len;
    int    generating;
} pl_chat;

void pl_chat_init(pl_chat *c, pl_model *m);

/* Append a completed turn. Returns 0 if the text will never fit. */
int  pl_chat_add(pl_chat *c, int from_user, const char *text);

/* Forget everything. The model's KV cache sorts itself out on the next call,
 * because the prefix simply stops matching. */
void pl_chat_reset(pl_chat *c);

/* Called as the reply streams. Return 0 to stop. */
typedef int (*pl_chat_progress)(pl_chat *c, void *user);

/* Generate the next assistant turn for whatever is in the transcript, calling
 * `on_token` after each piece so the caller can redraw and check for a tap.
 * Commits the result as a turn.
 *
 * Returns the number of tokens produced, which is 0 for a failure and is also
 * how the caller measures this device's real generation rate. */
int  pl_chat_reply(pl_chat *c, pl_chat_progress on_token, void *user);

/* A view of the transcript for the renderer, including the in-flight reply. */
size_t pl_chat_messages(const pl_chat *c, pl_message *out, size_t max_out);

#endif
