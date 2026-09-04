#define _POSIX_C_SOURCE 200809L
#include "models.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* --- where the estimates come from --------------------------------------
 *
 * Generation on this device is memory-bandwidth bound, not compute bound:
 * every token reads the whole model. So tokens/second falls roughly as 1/size,
 * and one measured point fixes the constant.
 *
 * The measured point is Gemma-3-270M-QAT on a 12th-generation Paperwhite:
 * 5.52 tokens/second generating, 8.64 evaluating the prompt. Its file is
 * 253,115,488 bytes -- 241 MB, weighed rather than inferred -- so
 *
 *     effective bandwidth ~= 5.52 * 241 MB ~= 1330 MB/s
 *
 * (309 MB resident against 241 MB of weights is also where the ~70 MB runtime
 * overhead below comes from, so the two estimates share one measurement.)
 *
 * Below about 130 MB the model stops being the bottleneck and two A53 cores
 * become one, and there is no measurement out there -- so the rate is capped
 * rather than extrapolated into a number that would flatter a tiny model.
 *
 * All of this is replaced by the real rate the moment a model has generated
 * anything on this particular device. See pl_models_record_speed. */
#define BANDWIDTH_MB   1330
#define TG_CAP_X100    1000   /* 10 tok/s */
#define PP_RATIO_X100   157   /* 8.64 / 5.52, measured                      */
#define PP_CAP_X100    1600

/* Peak resident memory ~= weights + ~70 MB runtime + ~40 MB of KV cache at
 * n_ctx 2048 (measured at ~19 KB per token). Checked against Qwen2.5-0.5B:
 * predicts 480 MB, measured 470 MB. */
#define OVERHEAD_MB     100

/* Headroom kept back from MemAvailable. The reader framework is still running
 * and still allocating; taking every last megabyte is how an app gets killed
 * two minutes in rather than refusing to start. RESERVE is what a model must
 * leave spare to be called comfortable, MARGIN the least it may leave at all. */
#define RESERVE_MB      120
#define MARGIN_MB        30

/* Measured on a 12th-generation Paperwhite: 979,852 kB total, ~550 MB
 * available with the reader running, zero swap. Only a fallback -- the app
 * reads the real figure at startup. */
static long g_budget_mb = 550;

void pl_models_set_budget(long available_mb) {
    /* A number small enough to fail everything is more likely a parse failure
     * than a real device, and silently listing nothing would be a mystery. */
    if (available_mb >= 128) g_budget_mb = available_mb;
}

long pl_models_budget(void) { return g_budget_mb; }

/* A new turn plus the chat template, in tokens. Everything before it is
 * already in the KV cache, which is the whole reason the cache is kept. */
#define NEW_TURN_TOKENS  40

typedef struct {
    const char *match;     /* lowercase substring of the filename */
    const char *name;
    const char *note;
    const char *licence;
} known;

/* Only models that have actually been run and read. The notes are what each
 * one did on the same three questions -- an explanation, a piece of fiction,
 * and a fact -- not what its card claims. Anything not listed here still
 * appears in the picker, described by its size alone.
 *
 * The ordering below is search order, not display order. */
static const known KNOWN[] = {
    { "qwen2.5-0.5b", "Qwen2.5 0.5B",
      "The most accurate, and the only one that stops when it is done.", "Apache-2.0" },
    { "smollm2-360m", "SmolLM2 360M",
      "Clear explanations. Repeats whole sentences in longer answers.",  "Apache-2.0" },
    { "smollm2-135m", "SmolLM2 135M",
      "Quick, and readable. Gets facts right and details invented.",     "Apache-2.0" },
    { "lfm2-350m",    "LFM2 350M",
      "The best writer here, and the most confidently wrong.",           "LFM licence" },
    { "gemma-3-270m", "Gemma 3 270M",
      "Brief and obedient. Says little, but rarely rambles.",            "Gemma terms" },

    /* Listed so that someone who copies one over is told why it will not run,
     * rather than left wondering whether the file is corrupt. */
    { "qwen3-",       "Qwen3 0.6B",
      "Thinks out loud for a minute, then runs out of budget. Not here.", "Apache-2.0" },
    { "qwen2.5-1.5b", "Qwen2.5 1.5B",  "Far too big for a Kindle.",       "Apache-2.0" },
    { "llama-3.2-1b", "Llama 3.2 1B",  "Far too big for a Kindle.",       "Llama licence" },
};

static void title_from_file(const char *file, char *out, size_t cap) {
    /* "Qwen2.5-0.5B-Instruct-Q4_K_M.gguf" -> "Qwen2.5 0.5B Instruct". Not
     * clever, just better than showing a filename with a quantisation suffix
     * to someone deciding which one to run. */
    size_t w = 0;
    for (size_t i = 0; file[i] && w + 1 < cap; i++) {
        if (file[i] == '.' && strcasecmp(file + i, ".gguf") == 0) break;
        char c = file[i];
        if (c == '-' || c == '_') {
            if (w == 0 || out[w - 1] == ' ') continue;
            c = ' ';
        }
        out[w++] = c;
    }
    while (w && out[w - 1] == ' ') w--;
    out[w] = 0;
}

void pl_model_describe(const char *filename, long bytes, pl_model_info *out) {
    memset(out, 0, sizeof *out);
    snprintf(out->file, sizeof out->file, "%s", filename ? filename : "");
    out->bytes = bytes > 0 ? bytes : 0;

    char lower[160];
    size_t n = 0;
    for (; out->file[n] && n + 1 < sizeof lower; n++) {
        char c = out->file[n];
        lower[n] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    lower[n] = 0;

    const known *k = NULL;
    for (size_t i = 0; i < sizeof KNOWN / sizeof KNOWN[0]; i++)
        if (strstr(lower, KNOWN[i].match)) { k = &KNOWN[i]; break; }

    if (k) {
        snprintf(out->name,    sizeof out->name,    "%s", k->name);
        snprintf(out->note,    sizeof out->note,    "%s", k->note);
        snprintf(out->licence, sizeof out->licence, "%s", k->licence);
    } else {
        title_from_file(out->file, out->name, sizeof out->name);
        snprintf(out->note, sizeof out->note, "%s",
                 "Not one we have measured. Times below are a guess from its size.");
        snprintf(out->licence, sizeof out->licence, "%s", "unknown");
    }

    int mb = (int)(out->bytes / (1024 * 1024));
    if (mb < 1) mb = 1;

    long tg = (long)BANDWIDTH_MB * 100 / mb;
    if (tg > TG_CAP_X100) tg = TG_CAP_X100;
    out->tg_x100 = (int)tg;

    long pp = tg * PP_RATIO_X100 / 100;
    if (pp > PP_CAP_X100) pp = PP_CAP_X100;
    out->pp_x100 = (int)pp;

    long peak = mb + OVERHEAD_MB;
    out->fit = peak > g_budget_mb - MARGIN_MB  ? PL_FIT_NO
             : peak > g_budget_mb - RESERVE_MB ? PL_FIT_TIGHT
                                               : PL_FIT_COMFORTABLE;
}

int pl_model_seconds(const pl_model_info *m, int gen_tokens) {
    if (!m || m->tg_x100 <= 0 || m->pp_x100 <= 0) return 0;
    long s = (long)NEW_TURN_TOKENS * 100 / m->pp_x100
           + (long)gen_tokens      * 100 / m->tg_x100;
    return (int)(s < 1 ? 1 : s);
}

/* --- what is actually on the device -------------------------------------- */

int pl_models_order(const void *a, const void *b) {
    const pl_model_info *x = a, *y = b;
    /* Safest first, then largest.
     *
     * Not "best first": the first entry is also what gets loaded on a device
     * with no saved choice, and a model that comfortably fits beats a better
     * one that might be killed for memory halfway through a reply. Anyone who
     * wants the better writer is one tap away, on a screen that says plainly
     * what it will cost them. Within a class, bigger is better. */
    if (x->fit != y->fit) return (int)x->fit - (int)y->fit;
    if (x->bytes != y->bytes) return x->bytes < y->bytes ? 1 : -1;
    return strcmp(x->file, y->file);
}

size_t pl_models_scan(const char *dir, pl_model_info *out, size_t max) {
    DIR *d = opendir(dir);
    if (!d) return 0;

    size_t n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || strcasecmp(dot, ".gguf") != 0) continue;

        char path[512];
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path)
            continue;

        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        pl_model_describe(e->d_name, (long)st.st_size, &out[n]);
        snprintf(out[n].path, sizeof out[n].path, "%s", path);
        n++;
    }
    closedir(d);

    qsort(out, n, sizeof *out, pl_models_order);
    return n;
}

/* --- rates this device actually achieved --------------------------------- */

static void speeds_path(const char *dir, char *out, size_t cap) {
    snprintf(out, cap, "%s/speeds.txt", dir);
}

void pl_models_load_speeds(const char *dir, pl_model_info *m, size_t n) {
    char path[512];
    speeds_path(dir, path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof line, f)) {
        char file[160];
        int tg = 0;
        if (sscanf(line, "%159s %d", file, &tg) != 2) continue;
        if (tg <= 0) continue;
        for (size_t i = 0; i < n; i++)
            if (strcmp(m[i].file, file) == 0) {
                m[i].tg_x100  = tg;
                m[i].pp_x100  = (int)((long)tg * PP_RATIO_X100 / 100);
                m[i].measured = 1;
            }
    }
    fclose(f);
}

void pl_models_record_speed(const char *dir, const char *file, int tg_x100) {
    if (!file || !*file || tg_x100 <= 0) return;

    char path[512];
    speeds_path(dir, path, sizeof path);

    /* Read, replace this model's line, write the lot back. The file has one
     * line per model, so it is never big enough to be worth doing better. */
    char keep[PL_MAX_MODELS][256];
    size_t nkeep = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f) && nkeep < PL_MAX_MODELS) {
            char other[160];
            if (sscanf(line, "%159s", other) == 1 && strcmp(other, file) == 0) continue;
            snprintf(keep[nkeep++], sizeof keep[0], "%s", line);
        }
        fclose(f);
    }

    f = fopen(path, "w");
    if (!f) return;
    for (size_t i = 0; i < nkeep; i++) fputs(keep[i], f);
    fprintf(f, "%s %d\n", file, tg_x100);
    fclose(f);
}

/* --- the remembered choice ----------------------------------------------- */

static void choice_path(const char *dir, char *out, size_t cap) {
    snprintf(out, cap, "%s/chosen.txt", dir);
}

int pl_models_load_choice(const char *dir, char *out, size_t cap) {
    char path[512];
    choice_path(dir, path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int ok = fgets(out, (int)cap, f) != NULL;
    fclose(f);
    if (!ok) return 0;
    out[strcspn(out, "\r\n")] = 0;
    return out[0] != 0;
}

void pl_models_save_choice(const char *dir, const char *file) {
    char path[512];
    choice_path(dir, path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%s\n", file ? file : "");
    fclose(f);
}
