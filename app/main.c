#define _POSIX_C_SOURCE 200809L
/* app/main.c — PocketLLM on the device.
 *
 * File I/O, signals and the event loop live here. Everything else -- the
 * conversation, the screens -- is platform-free and runs identically under the
 * host renderer, so this file is the only genuinely device-only part.
 */
#include "chat.h"
#include "screens.h"
#include "../platform/ui.h"
#include "../platform/input.h"
#include "../platform/devroot.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

pl_ui    *pl_ui_fb_create(const char *serif, const char *sans);
pl_input *pl_input_open_sized(int w, int h);

#define LOGPATH "/mnt/us/pocketllm.log"

/* A Kindle has no console, so anything that goes wrong is invisible unless it
 * lands on disk. Flushed every line: a crash must not lose the last one. */
static FILE *g_log;

static void logf_(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

/* Every screen begins with a clear-to-white, so a fault part-way through a
 * draw leaves a blank panel -- indistinguishable from a hang. These handlers
 * make a crash say so instead. */
static pl_ui *g_ui;

static void say(const char *l1, const char *l2) {
    if (!g_ui) return;
    pl_screen_notice(g_ui, l1, l2);
    pl_ui_present(g_ui, PL_REFRESH_FULL);
}

static void on_fatal(int sig) {
    const char *name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS"
                     : sig == SIGFPE  ? "SIGFPE"  : sig == SIGABRT ? "SIGABRT"
                     : sig == SIGILL  ? "SIGILL"  : "signal";
    logf_("FATAL: %s", name);
    say("Something went wrong.", "The details are in pocketllm.log at the top "
                                 "level of the Kindle's drive.");
    _exit(1);
}

static void install_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_fatal;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

/* --- the running app ---------------------------------------------------- */

typedef enum { MODE_CHAT, MODE_KEYBOARD } mode;

typedef struct {
    pl_ui    *ui;
    pl_input *in;
    pl_chat   chat;

    mode  screen;
    int   scroll, content_h;
    int   shift, page;
    char  typed[4096];
    size_t typed_len;

    const char *model_label;

    /* Streaming bookkeeping. */
    long  last_draw;
    int   tick;
    int   stop_requested;
} app;

static size_t view(app *a, pl_message *msgs, size_t cap) {
    return pl_chat_messages(&a->chat, msgs, cap);
}

static void clamp_scroll(app *a) {
    int max = a->content_h - pl_screen_view_height();
    if (max < 0) max = 0;
    if (a->scroll > max) a->scroll = max;
    if (a->scroll < 0)   a->scroll = 0;
}

/* Measure, pin to the bottom, then draw once. Measuring is a separate pass
 * because the height is not known until the walk is over, and a second
 * rasterising pass would cost more than the tokens it is showing. */
static void draw_chat(app *a, pl_refresh how, int pin_to_end) {
    pl_message msgs[PL_MAX_TURNS + 1];
    size_t n = view(a, msgs, sizeof msgs / sizeof msgs[0]);

    if (pin_to_end) {
        pl_screen_chat(a->ui, msgs, n, 0, &a->content_h,
                       a->model_label, a->chat.generating, a->tick, 0);
        a->scroll = a->content_h;      /* clamped down to the real maximum */
        clamp_scroll(a);
    }

    pl_screen_chat(a->ui, msgs, n, a->scroll, &a->content_h,
                   a->model_label, a->chat.generating, a->tick, 1);
    pl_ui_present(a->ui, how);
}

/* Called after every token. Two jobs: show the text arriving, and notice a tap
 * on stop. Both have to happen without stalling generation, which is why the
 * redraw is throttled and the input poll does not wait. */
static int on_token(pl_chat *c, void *user) {
    (void)c;
    app *a = user;

    pl_event ev = pl_input_next(a->in, 0);
    if (ev.kind == PL_EV_QUIT) { a->stop_requested = 2; return 0; }
    if (ev.kind == PL_EV_TAP && pl_hit_stop(ev.x, ev.y)) {
        pl_ui_tap_flash(a->ui, ev.x, ev.y);
        a->stop_requested = 1;
        return 0;
    }

    /* A partial refresh costs about 100 ms of panel time. Redrawing per token
     * would spend more of the budget pushing pixels than generating them, and
     * e-ink cannot show it that fast anyway. */
    long t = now_ms();
    if (t - a->last_draw < 450) return 1;
    a->last_draw = t;
    a->tick++;

    draw_chat(a, PL_REFRESH_FAST, 1);
    return 1;
}

static void send_message(app *a) {
    if (a->typed_len == 0) return;

    if (!pl_chat_add(&a->chat, 1, a->typed)) {
        logf_("message rejected: %zu bytes", a->typed_len);
        return;
    }
    a->typed[0] = 0;
    a->typed_len = 0;
    a->screen = MODE_CHAT;

    draw_chat(a, PL_REFRESH_FULL, 1);

    if (!pl_model_available(a->chat.model)) {
        pl_chat_add(&a->chat, 0,
                    "No model is installed. Run tools/fetch-models.sh on a "
                    "computer and copy the .gguf file into the pocketllm "
                    "folder on this Kindle.");
    } else {
        a->stop_requested = 0;
        a->last_draw = now_ms();
        long t0 = now_ms();
        int got = pl_chat_reply(&a->chat, on_token, a);
        logf_("reply: produced=%d stopped=%d in %ldms",
              got, a->stop_requested, now_ms() - t0);
        if (!got && !a->stop_requested)
            pl_chat_add(&a->chat, 0, "I could not answer that one. Try asking "
                                     "it a different way, or start a new chat.");
    }

    draw_chat(a, PL_REFRESH_FULL, 1);
}

static void key(app *a, int k) {
    switch (k) {
        case 0:            return;
        case PL_KEY_SHIFT: a->shift = !a->shift; break;
        case PL_KEY_PAGE:  a->page = !a->page; a->shift = 0; break;
        case '\n':         send_message(a); return;
        case '\b':
            if (a->typed_len) a->typed[--a->typed_len] = 0;
            break;
        default:
            if (a->typed_len + 1 < sizeof a->typed) {
                a->typed[a->typed_len++] = (char)k;
                a->typed[a->typed_len] = 0;
                a->shift = 0;      /* one capital, not caps lock */
            }
            break;
    }
    pl_screen_keyboard(a->ui, a->typed, a->shift, a->page);
    pl_ui_present(a->ui, PL_REFRESH_FAST);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "/mnt/us/extensions/pocketllm";

    char lp[512];
    g_log = fopen(pl_path(LOGPATH, lp, sizeof lp), "a");
    logf_("--- pocketllm start, dir=%s ---", dir);

    char serif[600], sans[600], gguf[600];
    snprintf(serif, sizeof serif, "%s/Literata.ttf", dir);
    snprintf(sans,  sizeof sans,  "%s/Inter.ttf",    dir);
    snprintf(gguf,  sizeof gguf,  "%s/model.gguf",   dir);

    app a;
    memset(&a, 0, sizeof a);

    a.ui = pl_ui_fb_create(serif, sans);
    if (!a.ui) { logf_("cannot open the framebuffer -- giving up"); return 1; }
    g_ui = a.ui;
    install_handlers();

    say("Loading…", "The model is about half a gigabyte, so the first screen "
                    "takes a moment.");

    a.in = pl_input_open_sized(PL_SCREEN_W, PL_SCREEN_H);
    if (!a.in) {
        /* Without touch there is no way to type, and the event loop would spin
         * at full speed on a device with no fan and a battery. Say so and stop
         * rather than sit there getting hot. */
        logf_("no touch device -- cannot run");
        say("No touchscreen found.",
            "PocketLLM needs touch input. The details are in pocketllm.log at "
            "the top level of the Kindle's drive.");
        pl_ui_destroy(a.ui);
        return 1;
    }

    /* Two threads for two cores, and a context window sized to leave the
     * weights room: at Q4 a 0.5B model is ~400 MB of the 512 MB available, and
     * KV costs roughly 19 KB per token. 2048 is about 40 MB. */
    long t0 = now_ms();
    pl_model *m = pl_model_open(gguf, 2, 2048);
    logf_("model: %s (%ldms)", m ? pl_model_name(m) : "none", now_ms() - t0);

    pl_chat_init(&a.chat, m);
    a.model_label = m ? pl_model_name(m) : "no model";
    a.screen = MODE_CHAT;

    if (!m) {
        say("No model installed.",
            "Run tools/fetch-models.sh on a computer, then copy model.gguf "
            "into extensions/pocketllm on this Kindle. Tap to continue.");
        pl_event ev;
        do { ev = pl_input_next(a.in, 30000); } while (ev.kind == PL_EV_SWIPE_UP
                                                    || ev.kind == PL_EV_SWIPE_DOWN);
    }

    draw_chat(&a, PL_REFRESH_FULL, 0);

    for (;;) {
        pl_event ev = pl_input_next(a.in, 30000);
        if (ev.kind == PL_EV_QUIT) break;
        if (ev.kind == PL_EV_NONE) continue;

        if (ev.kind == PL_EV_SWIPE_UP || ev.kind == PL_EV_SWIPE_DOWN) {
            if (a.screen != MODE_CHAT) continue;
            a.scroll += (ev.kind == PL_EV_SWIPE_UP ? 1 : -1)
                      * (pl_screen_view_height() * 3 / 4);
            clamp_scroll(&a);
            draw_chat(&a, PL_REFRESH_FAST, 0);
            continue;
        }
        if (ev.kind != PL_EV_TAP) continue;

        /* Acknowledge the touch before doing the work. E-ink has no cursor and
         * no haptics, so an unacknowledged tap reads as "broken" long before
         * it reads as "still working". */
        pl_ui_tap_flash(a.ui, ev.x, ev.y);

        if (pl_hit_close(ev.x, ev.y)) break;

        if (a.screen == MODE_KEYBOARD) {
            key(&a, pl_keyboard_hit(ev.x, ev.y, a.shift, a.page));
            /* A quit that arrived while a reply was streaming is noticed here,
             * once the generation it interrupted has unwound. */
            if (a.stop_requested == 2) break;
            continue;
        }

        int dir_ = pl_hit_scroll(ev.x, ev.y);
        if (dir_) {
            a.scroll += dir_ * (pl_screen_view_height() * 3 / 4);
            clamp_scroll(&a);
            draw_chat(&a, PL_REFRESH_FAST, 0);
            continue;
        }

        if (pl_hit_composer(ev.x, ev.y)) {
            a.screen = MODE_KEYBOARD;
            pl_screen_keyboard(a.ui, a.typed, a.shift, a.page);
            pl_ui_present(a.ui, PL_REFRESH_FULL);
        }
    }

    logf_("--- exit ---");
    say("Closed.", "Tap the PocketLLM shortcut in your library to come back.");
    pl_input_close(a.in);
    pl_model_close(m);
    pl_ui_destroy(a.ui);
    if (g_log) fclose(g_log);
    return 0;
}
