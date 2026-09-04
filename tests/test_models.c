/* What the picker tells you about each model.
 *
 * These numbers are the whole point of the screen -- someone decides whether
 * to wait a minute for a better answer based on them -- so the estimates, the
 * memory verdicts and the ordering are all checked here rather than eyeballed
 * once on a render.
 */
#include "../app/models.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int fails;
static void check(int ok, const char *what) {
    if (!ok) { printf("  FAIL %s\n", what); fails++; }
}

#define MB (1024L * 1024L)

int main(void) {
    pl_model_info m;

    /* The three we actually ship. */
    pl_model_describe("Qwen2.5-0.5B-Instruct-Q4_K_M.gguf", 380 * MB, &m);
    check(strcmp(m.name, "Qwen2.5 0.5B") == 0, "qwen is recognised");
    check(strcmp(m.licence, "Apache-2.0") == 0, "qwen licence");
    check(m.fit == PL_FIT_TIGHT, "qwen fits, but tightly");
    int qwen_s = pl_model_seconds(&m, 60);

    pl_model_describe("SmolLM2-360M-Instruct-Q4_K_M.gguf", 270 * MB, &m);
    check(strcmp(m.name, "SmolLM2 360M") == 0, "smol360 is recognised");
    check(m.fit == PL_FIT_COMFORTABLE, "smol360 fits comfortably");
    int smol360_s = pl_model_seconds(&m, 60);

    pl_model_describe("SmolLM2-135M-Instruct-Q4_K_M.gguf", 110 * MB, &m);
    check(m.fit == PL_FIT_COMFORTABLE, "smol135 fits comfortably");
    int smol135_s = pl_model_seconds(&m, 60);

    /* The ordering is the entire message of the screen: bigger is slower. If
     * this ever inverts, the picker is lying to the person reading it. */
    check(qwen_s > smol360_s, "the bigger model is slower");
    check(smol360_s > smol135_s, "and so on down");

    /* Sanity against the one measured point: a short reply on the shipped
     * model is tens of seconds, not seconds and not many minutes. */
    check(qwen_s > 10 && qwen_s < 120, "a short qwen reply is tens of seconds");
    check(smol135_s >= 1, "even the smallest takes a moment");

    /* Too big to load must be said, not hidden. */
    pl_model_describe("Qwen2.5-1.5B-Instruct-Q4_K_M.gguf", 1100 * MB, &m);
    check(m.fit == PL_FIT_NO, "a 1.1 GB model will not fit");

    /* Something nobody has measured still gets a usable entry. */
    pl_model_describe("some-random-model-q4.gguf", 200 * MB, &m);
    check(m.name[0] != 0, "an unknown model still gets a name");
    check(strstr(m.name, ".gguf") == NULL, "without the extension");
    check(pl_model_seconds(&m, 60) > 0, "and still gets an estimate");

    /* A longer reply must cost more than a short one, or "stop" makes no
     * sense as a feature. */
    pl_model_describe("SmolLM2-360M-Instruct-Q4_K_M.gguf", 270 * MB, &m);
    check(pl_model_seconds(&m, 200) > pl_model_seconds(&m, 60),
          "a long reply costs more than a short one");

    /* A zero-byte file must not divide by zero or promise infinite speed. */
    pl_model_describe("truncated.gguf", 0, &m);
    check(pl_model_seconds(&m, 60) > 0, "a zero-byte file does not divide by zero");

    /* The sort decides what loads on a device with no saved choice, so it is
     * checked with real sizes rather than with stub files. The one with room
     * to spare must come before the one that is tight, even though the tight
     * one is the better model. */
    {
        pl_model_info l[3];
        pl_model_describe("Qwen2.5-0.5B-Instruct-Q4_K_M.gguf", 380 * MB, &l[0]);
        pl_model_describe("SmolLM2-135M-Instruct-Q4_K_M.gguf", 110 * MB, &l[1]);
        pl_model_describe("SmolLM2-360M-Instruct-Q4_K_M.gguf", 270 * MB, &l[2]);
        qsort(l, 3, sizeof l[0], pl_models_order);
        check(strcmp(l[0].name, "SmolLM2 360M") == 0, "the safest large model leads");
        check(strcmp(l[1].name, "SmolLM2 135M") == 0, "then the smaller safe one");
        check(strcmp(l[2].name, "Qwen2.5 0.5B") == 0, "the tight one comes last");
    }

    /* Scanning, the remembered choice, and measured rates -- against a real
     * directory, because that is where the path handling actually goes wrong. */
    char dir[] = "/tmp/pl_models_testXXXXXX";
    if (mkdtemp(dir)) {
        char p1[512], p2[512];
        snprintf(p1, sizeof p1, "%s/SmolLM2-360M-Instruct-Q4_K_M.gguf", dir);
        snprintf(p2, sizeof p2, "%s/Qwen2.5-0.5B-Instruct-Q4_K_M.gguf", dir);
        FILE *f = fopen(p1, "wb"); if (f) { fputs("GGUF", f); fclose(f); }
        f = fopen(p2, "wb");       if (f) { fputs("GGUF", f); fclose(f); }
        f = fopen("/dev/null", "r"); if (f) fclose(f);

        pl_model_info list[PL_MAX_MODELS];
        size_t n = pl_models_scan(dir, list, PL_MAX_MODELS);
        check(n == 2, "both models are found");

        pl_models_save_choice(dir, "Qwen2.5-0.5B-Instruct-Q4_K_M.gguf");
        char back[160] = {0};
        check(pl_models_load_choice(dir, back, sizeof back), "the choice is remembered");
        check(strcmp(back, "Qwen2.5-0.5B-Instruct-Q4_K_M.gguf") == 0, "and is the right one");

        pl_models_record_speed(dir, list[0].file, 250);
        pl_models_record_speed(dir, list[1].file, 700);
        pl_models_record_speed(dir, list[0].file, 260);   /* replaces, not appends */
        pl_models_load_speeds(dir, list, n);
        check(list[0].measured && list[0].tg_x100 == 260, "the measured rate wins");
        check(list[1].measured && list[1].tg_x100 == 700, "for each model separately");

        char cmd[600];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
        if (system(cmd)) { /* the temp dir outliving the test is not a failure */ }
    }

    printf("%-20s %s\n", "models", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
