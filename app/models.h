/* app/models.h — which models are on the device, and what each one costs you.
 *
 * A Kindle has 512 MB and no swap, and generation is memory-bandwidth bound,
 * so the choice of model IS the choice between "answers in eight seconds and
 * says little" and "answers in a minute and says something". That trade is the
 * user's to make, not ours to make for them -- but they can only make it if
 * the numbers are in front of them.
 *
 * The estimates here are derived from one measured point on real hardware and
 * are labelled as estimates. As soon as a model has actually generated
 * something on this device, its real rate replaces the estimate.
 */
#ifndef PL_MODELS_H
#define PL_MODELS_H

#include <stddef.h>

#define PL_MAX_MODELS 12

typedef enum {
    PL_FIT_COMFORTABLE = 0,  /* room to spare                                */
    PL_FIT_TIGHT,            /* runs, with little headroom -- may be killed  */
    PL_FIT_NO                /* will not load on 512 MB                      */
} pl_fit;

typedef struct {
    char path[512];
    char file[160];
    char name[48];        /* friendly, e.g. "Qwen2.5 0.5B"                  */
    char note[96];        /* one line on what it is like to talk to         */
    char licence[24];

    long bytes;
    int  tg_x100;         /* generation, tokens/sec x100                    */
    int  pp_x100;         /* prompt evaluation, tokens/sec x100             */
    int  measured;        /* 1 if tg came from this device, 0 if estimated  */
    pl_fit fit;
} pl_model_info;

/* Everything known from a filename and a size, with no disk access. Unknown
 * files still get an entry: an unrecognised GGUF is a model someone chose to
 * put there, and refusing to list it helps nobody. */
void pl_model_describe(const char *filename, long bytes, pl_model_info *out);

/* Seconds for a reply of `gen_tokens`, including evaluating a short new turn.
 * The first reply in a conversation costs more, because the whole history has
 * to be evaluated once; after that the KV cache covers all but the newest
 * turn. See pl_model_first_reply_penalty. */
int pl_model_seconds(const pl_model_info *m, int gen_tokens);

/* The order the picker lists them in, and so which one loads when there is no
 * saved choice: most memory headroom first, then largest. Exposed because that
 * default matters enough to test. Use with qsort. */
int pl_models_order(const void *a, const void *b);

/* Every .gguf in `dir`, in that order. Returns how many were found. */
size_t pl_models_scan(const char *dir, pl_model_info *out, size_t max);

/* Replace estimates with rates this device actually achieved, if we have any.
 * Kept in <dir>/speeds.txt as "<file> <tokens-per-sec x100>" per line. */
void pl_models_load_speeds(const char *dir, pl_model_info *m, size_t n);
void pl_models_record_speed(const char *dir, const char *file, int tg_x100);

/* The chosen model, remembered in <dir>/chosen.txt across restarts. */
int  pl_models_load_choice(const char *dir, char *out, size_t cap);
void pl_models_save_choice(const char *dir, const char *file);

#endif
