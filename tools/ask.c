/* One conversation, on this computer, printed to stdout.
 *
 * The device is where speed and memory are judged; this is where the ANSWERS
 * are. Reading real output is what finds prompt bugs -- reasoning about
 * prompts does not.
 *
 *   ./out/ask model.gguf "first message" ["second" ...]
 */
#include "../app/chat.h"
#include <stdio.h>
#include <string.h>

/* Print only what is new since the last call, so a piped run reads as one
 * reply rather than as the reply repeated once per token. */
static int echo(pl_chat *c, void *u) {
    size_t *shown = u;
    if (c->pending_len > *shown) {
        fwrite(c->pending + *shown, 1, c->pending_len - *shown, stdout);
        fflush(stdout);
        *shown = c->pending_len;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: ask <model.gguf> <message>...\n"); return 2; }

    pl_model *m = pl_model_open(argv[1], 4, 2048);
    if (!m) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    printf("model: %s\n", pl_model_name(m));

    static pl_chat c;
    pl_chat_init(&c, m);

    for (int i = 2; i < argc; i++) {
        printf("\n> %s\n\n", argv[i]);
        pl_chat_add(&c, 1, argv[i]);
        size_t shown = 0;
        pl_chat_reply(&c, echo, &shown);
        printf("\n");
    }
    pl_model_close(m);
    return 0;
}
