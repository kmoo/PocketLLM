/* Render every screen to a PNG at true device size.
 *
 * The same drawing code the Kindle runs, against an in-memory buffer instead
 * of /dev/fb0. This is how the layout gets looked at without the device, and
 * how a change to spacing gets reviewed before it is copied over.
 */
#include "../app/screens.h"
#include <stdio.h>
#include <string.h>

pl_ui *pl_ui_host_create(const char *serif, const char *sans, const char *out);
void   pl_ui_host_save(pl_ui *ui, const char *name);

int main(void) {
    pl_ui *ui = pl_ui_host_create("assets/fonts/Literata.ttf",
                                  "assets/fonts/Inter.ttf",
                                  "out/screens/pocketllm");
    if (!ui) { fprintf(stderr, "cannot create the host surface\n"); return 1; }

    int h;

    pl_screen_chat(ui, NULL, 0, 0, &h, "qwen2.5-0.5b", 0, 0, 1);
    pl_ui_host_save(ui, "empty");

    static const pl_message CONVO[] = {
        { 1, "What is a good way to explain recursion to someone who has never "
             "written code?", 0 },
        { 0, "Think of two mirrors facing each other. Each one shows the other, "
             "which shows the first again, and so on until the reflections are "
             "too small to see. A recursive function does the same thing: it "
             "solves a small piece of the problem and then asks itself to solve "
             "what is left, until what is left is nothing.", 0 },
        { 1, "Nice. Now do it in one sentence.", 0 },
        { 0, "A recursive function is one that solves a problem by calling "
             "itself on a smaller version of the same problem.", 0 },
    };
    pl_screen_chat(ui, CONVO, 4, 0, &h, "qwen2.5-0.5b", 0, 0, 1);
    pl_ui_host_save(ui, "chat");

    static const pl_message BUSY[] = {
        { 1, "Write me an opening line for a story set on a rainy pier.", 0 },
        { 0, "The pier had been closed since the storm, which is why", 1 },
    };
    pl_screen_chat(ui, BUSY, 2, 0, &h, "qwen2.5-0.5b", 1, 1, 1);
    pl_ui_host_save(ui, "streaming");

    pl_screen_keyboard(ui, "", 0, 0);
    pl_ui_host_save(ui, "keyboard-empty");

    pl_screen_keyboard(ui, "Explain what a monad is, but assume I only know "
                           "JavaScript and have never heard the word before.", 1, 0);
    pl_ui_host_save(ui, "keyboard-typed");

    pl_screen_keyboard(ui, "Convert 350 F to C, and show the working.", 0, 1);
    pl_ui_host_save(ui, "keyboard-symbols");

    /* The picker, showing everything ./build.sh 1gb installs plus one that is
     * too big -- the greyed row is exactly the case worth looking at. */
    pl_model_info models[6];
    pl_model_describe("SmolLM2-360M-Instruct-Q4_K_M.gguf", 270590880L, &models[0]);
    pl_model_describe("gemma-3-270m-it-qat-Q4_K_M.gguf",   253115488L, &models[1]);
    pl_model_describe("LFM2-350M-Q4_K_M.gguf",             229309376L, &models[2]);
    pl_model_describe("SmolLM2-135M-Instruct-Q4_K_M.gguf", 105454432L, &models[3]);
    pl_model_describe("Qwen2.5-0.5B-Instruct-Q4_K_M.gguf", 397808192L, &models[4]);
    pl_model_describe("Qwen2.5-1.5B-Instruct-Q4_K_M.gguf", 1153433600L, &models[5]);
    models[0].measured = 1;        /* one row showing a real, timed rate */
    pl_screen_models(ui, models, 6, 0);
    pl_ui_host_save(ui, "models");

    pl_screen_notice(ui, "No model installed.",
                     "Run tools/fetch-models.sh on a computer, then copy "
                     "model.gguf into extensions/pocketllm on this Kindle.");
    pl_ui_host_save(ui, "notice");

    pl_ui_destroy(ui);
    printf("rendered 6 screens to out/screens/\n");
    return 0;
}
