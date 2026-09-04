/* The no-model build: everything but llama.cpp, linking in seconds.
 *
 * This is how the UI is worked on -- layout, touch, scrolling and the
 * keyboard are all exercised without waiting on a cross-compiled inference
 * library, and `make app` produces a binary that runs on the device and shows
 * exactly the "no model installed" screen a user with a failed download sees.
 */
#include "model.h"

pl_model *pl_model_open(const char *p, int t, int c) { (void)p; (void)t; (void)c; return 0; }
void        pl_model_close(pl_model *m) { (void)m; }
int         pl_model_available(const pl_model *m) { (void)m; return 0; }
const char *pl_model_name(const pl_model *m) { (void)m; return "none"; }

int pl_model_chat(pl_model *m, const pl_turn *turns, size_t n_turns,
                  int max_tokens, pl_model_stream cb, void *user) {
    (void)m; (void)turns; (void)n_turns; (void)max_tokens; (void)cb; (void)user;
    return -1;
}

int pl_model_context_used(pl_model *m, const pl_turn *turns, size_t n_turns) {
    (void)m; (void)turns; (void)n_turns;
    return 0;
}
