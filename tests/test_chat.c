/* The transcript, without a screen or a model.
 *
 * The interesting behaviour here is what happens when it fills up: the pool is
 * fixed because the device has 512 MB and no swap, so appending must always
 * succeed by dropping old turns rather than by failing. A transcript that
 * silently stops accepting messages looks exactly like a hung app. */
#include "../app/chat.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;
static void check(int ok, const char *what) {
    if (!ok) { printf("  FAIL %s\n", what); fails++; }
}

int main(void) {
    static pl_chat c;
    pl_chat_init(&c, NULL);

    check(pl_chat_add(&c, 1, "hello") == 1, "append a message");
    check(c.n == 1, "one turn");
    check(strcmp(c.turns[0].text, "hello") == 0, "text survives");
    check(pl_chat_add(&c, 0, "") == 0, "empty message rejected");
    check(pl_chat_add(&c, 0, NULL) == 0, "null message rejected");

    pl_message msgs[PL_MAX_TURNS + 1];
    check(pl_chat_messages(&c, msgs, 8) == 1, "one message rendered");
    check(msgs[0].from_user == 1, "attributed to the user");
    check(msgs[0].streaming == 0, "not streaming");

    /* Fill past the turn limit. Every append must still succeed, the oldest
     * must be gone, and the newest must be intact. */
    pl_chat_reset(&c);
    for (int i = 0; i < PL_MAX_TURNS * 3; i++) {
        char buf[64];
        snprintf(buf, sizeof buf, "message number %d", i);
        if (!pl_chat_add(&c, i % 2 == 0, buf)) { check(0, "append past the turn limit"); break; }
    }
    check(c.n <= PL_MAX_TURNS, "turn count stays bounded");
    check(c.used <= PL_TEXT_POOL, "pool stays bounded");
    check(strcmp(c.turns[c.n - 1].text, "message number 191") == 0, "newest turn intact");

    /* Every remaining turn must still point inside the pool and be a valid
     * string -- compaction moves the text out from under the pointers. */
    for (size_t i = 0; i < c.n; i++) {
        check(c.turns[i].text >= c.pool && c.turns[i].text < c.pool + PL_TEXT_POOL,
              "turn points into the pool");
        check(strncmp(c.turns[i].text, "message number ", 15) == 0,
              "turn text is not corrupted");
    }

    /* Fill past the byte pool with messages far bigger than any turn limit
     * would catch -- a pasted article is the real case. */
    pl_chat_reset(&c);
    char *big = malloc(20000);
    memset(big, 'a', 19999);
    big[19999] = 0;
    for (int i = 0; i < 40; i++)
        if (!pl_chat_add(&c, i % 2 == 0, big)) { check(0, "append past the byte pool"); break; }
    check(c.used <= PL_TEXT_POOL, "pool stays bounded under large messages");
    check(c.n > 0, "something survives");
    free(big);

    /* Bigger than the whole pool: this one genuinely cannot be stored, and
     * must say so rather than loop forever trying to make room. */
    pl_chat_reset(&c);
    char *huge = malloc(PL_TEXT_POOL + 100);
    memset(huge, 'b', PL_TEXT_POOL + 99);
    huge[PL_TEXT_POOL + 99] = 0;
    check(pl_chat_add(&c, 1, huge) == 0, "an impossible message is refused");
    free(huge);

    /* With no model there is nothing to say, and it must not crash finding
     * that out -- this is the build the UI is developed against. */
    pl_chat_reset(&c);
    pl_chat_add(&c, 1, "hello");
    check(pl_chat_reply(&c, NULL, NULL) == 0, "no model produces no reply");
    check(c.generating == 0, "not left mid-generation");

    printf("%-20s %s\n", "chat", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
