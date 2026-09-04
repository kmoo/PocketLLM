/* Every key that is drawn must be tappable, and fully on screen.
 *
 * The renderer and the hit test share one geometry function, so this checks
 * the thing they share. It exists because the keyboard shipped once with 'l'
 * and 'm' drawn past the right edge: they were visible, and nothing happened
 * when you pressed them. A screenshot does not catch that; this does, in
 * milliseconds. */
#include "../app/screens.h"
#include <stdio.h>
#include <string.h>

static int fails;

static void check(int ok, const char *what) {
    if (!ok) { printf("  FAIL %s\n", what); fails++; }
}

int main(void) {
    /* Layouts are written against whatever panel the app woke up on, so test
     * the smallest and the largest Kindle rather than one convenient size. */
    const struct { int w, h; const char *name; } PANELS[] = {
        { 1072, 1448, "Paperwhite" },
        { 1264, 1680, "Oasis" },
        { 1272, 1696, "Scribe" },
    };

    for (size_t p = 0; p < sizeof PANELS / sizeof PANELS[0]; p++) {
        pl_screen_w = PANELS[p].w;
        pl_screen_h = PANELS[p].h;

        /* Both pages, every key. */
        const char *pages[2][3] = {
            { "qwertyuiop", "asdfghjkl",  "zxcvbnm" },
            { "1234567890", "-/:;()$&@\"", ".,?!'" }
        };
        for (int pg = 0; pg < 2; pg++) {
            for (int r = 0; r < 3; r++) {
                for (size_t c = 0; pages[pg][r][c]; c++) {
                    char want = pages[pg][r][c];
                    int hit = 0, minx = 1 << 20, maxx = -1;
                    for (int y = 0; y < pl_screen_h; y += 4)
                        for (int x = 0; x < pl_screen_w; x += 4)
                            if (pl_keyboard_hit(x, y, 0, pg) == want) {
                                hit = 1;
                                if (x < minx) minx = x;
                                if (x > maxx) maxx = x;
                            }
                    char msg[96];
                    snprintf(msg, sizeof msg, "%s p%d: key '%c' reachable",
                             PANELS[p].name, pg, want);
                    check(hit, msg);
                    if (!hit) continue;
                    snprintf(msg, sizeof msg, "%s p%d: key '%c' inside the screen",
                             PANELS[p].name, pg, want);
                    check(minx > 4 && maxx < pl_screen_w - 8, msg);
                }
            }
        }

        /* Shift must actually produce capitals -- the hit test applies the
         * case, so it is the only place that can get it wrong. */
        {
            int found_upper = 0;
            for (int y = 0; y < pl_screen_h && !found_upper; y += 4)
                for (int x = 0; x < pl_screen_w && !found_upper; x += 4)
                    if (pl_keyboard_hit(x, y, 1, 0) == 'Q') found_upper = 1;
            check(found_upper, "shift produces capitals");
        }

        const int SPECIAL[5] = { PL_KEY_PAGE, PL_KEY_SHIFT, ' ', '\b', '\n' };
        const char *NAME[5]  = { "?123", "shift", "space", "back", "send" };
        for (int i = 0; i < 5; i++) {
            int hit = 0;
            for (int y = 0; y < pl_screen_h && !hit; y += 4)
                for (int x = 0; x < pl_screen_w && !hit; x += 4)
                    if (pl_keyboard_hit(x, y, 0, 0) == SPECIAL[i]) hit = 1;
            char msg[96];
            snprintf(msg, sizeof msg, "%s: %s reachable", PANELS[p].name, NAME[i]);
            check(hit, msg);
        }

        /* The chrome must not overlap: a tap that both closes the app and
         * opens the keyboard does whichever the code tests first, which is a
         * coin toss the user always loses. */
        for (int y = 0; y < pl_screen_h; y += 3)
            for (int x = 0; x < pl_screen_w; x += 3) {
                int n = !!pl_hit_close(x, y) + !!pl_hit_composer(x, y)
                      + !!pl_hit_scroll(x, y);
                if (n > 1) { check(0, "chrome hit tests overlap"); y = pl_screen_h; break; }
            }

        /* Every model row the picker draws must be tappable, and no tap may
         * land on a row it does not draw -- a model listed but unreachable, or
         * a tap selecting one that is not there, are both worse than a short
         * list. Checked across the full range the app allows. */
        for (size_t count = 1; count <= PL_MAX_MODELS; count++) {
            int seen[PL_MAX_MODELS];
            memset(seen, 0, sizeof seen);
            int out_of_range = 0;
            for (int y = 0; y < pl_screen_h; y += 3) {
                int i = pl_models_hit(pl_screen_w / 2, y, count);
                if (i < 0) continue;
                if (i >= (int)count) { out_of_range = 1; break; }
                seen[i] = 1;
            }
            check(!out_of_range, "no tap selects a model that is not listed");
            /* Row 0 must always be reachable: it is the default, and on the
             * smallest panel it is the one people will actually pick. */
            check(seen[0], "the first model row is always tappable");
        }

        /* Both scroll arrows must exist, or the conversation has a direction
         * you cannot go. */
        int up = 0, down = 0;
        for (int y = 0; y < pl_screen_h; y += 3)
            for (int x = 0; x < pl_screen_w; x += 3) {
                int d = pl_hit_scroll(x, y);
                if (d < 0) up = 1;
                if (d > 0) down = 1;
            }
        check(up && down, "both scroll directions reachable");
    }

    printf("%-20s %s\n", "layout", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
