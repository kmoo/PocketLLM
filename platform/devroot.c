#include "devroot.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const char *root(void) {
    const char *r = getenv("POCKETLLM_DEV_ROOT");
    return (r && *r) ? r : NULL;
}

int pl_is_simulated(void) { return root() != NULL; }

const char *pl_path(const char *abs, char *buf, size_t buflen) {
    const char *r = root();
    if (!r || !abs || !buf) return abs;
    int n = snprintf(buf, buflen, "%s%s", r, abs);
    if (n < 0 || (size_t)n >= buflen) return abs;   /* overflow: pass through */
    return buf;
}
