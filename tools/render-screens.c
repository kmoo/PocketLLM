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

    pl_screen_notice(ui, "No model installed.",
                     "Run tools/fetch-models.sh on a computer, then copy "
                     "model.gguf into extensions/pocketllm on this Kindle.");
    pl_ui_host_save(ui, "notice");

    pl_ui_destroy(ui);
    printf("rendered 6 screens to out/screens/\n");
    return 0;
}
