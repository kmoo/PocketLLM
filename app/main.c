#define _POSIX_C_SOURCE 200809L
/* app/main.c — PocketLLM on the device.
 *
 * File I/O, signals and the event loop live here. Everything else -- the
 * conversation, the screens -- is platform-free and runs identically under the
 * host renderer, so this file is the only genuinely device-only part.
 */
#include "chat.h"
#include "models.h"
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

/* What the kernel says it can still lend us, in MB.
 *
 * MemAvailable, not MemTotal: the reader framework is running underneath and
 * its pages are not ours to take. On a 12th-generation Paperwhite that is
 * ~550 MB of 1 GB -- and the gap between those two numbers is exactly why this
 * is read rather than assumed. Returns 0 if it cannot be read, and the
 * catalogue keeps its measured default. */
static long available_mb(void) {
    char pb[512];
    FILE *f = fopen(pl_path("/proc/meminfo", pb, sizeof pb), "r");
    if (!f) return 0;

    long kb = 0, total = 0;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "MemAvailable: %ld kB", &kb) == 1) break;
        sscanf(line, "MemTotal: %ld kB", &total);
    }
    fclose(f);

    /* Older kernels predate MemAvailable. Half of total is a poor estimate,
     * but it is the right side of poor: it under-promises. */
    if (!kb && total) kb = total / 2;
    return kb / 1024;
}

/* --- the running app ---------------------------------------------------- */

typedef enum { MODE_CHAT, MODE_KEYBOARD, MODE_MODELS } mode;

typedef struct {
    pl_ui    *ui;
    pl_input *in;
    pl_chat   chat;

    mode  screen;
    int   scroll, content_h;
    int   shift, page;
    char  typed[4096];
    size_t typed_len;

    /* Every .gguf found beside the binary, and which one is loaded. */
    pl_model_info models[PL_MAX_MODELS];
    size_t        n_models;
    int           current;          /* index into models, or -1 */
    char          dir[512];
    const char   *model_label;

    /* Streaming bookkeeping. */
    long  last_draw, gen_started;
    int   tick;
    int   stop_requested;
} app;

/* Load one model by index, replacing whatever is loaded now.
 *
 * Closed before the new one is opened, always: two sets of weights do not fit
 * in what a Kindle has free, and the failure mode of trying is the kernel
 * killing the app
 * rather than an error we could report. */
static int load_model(app *a, int idx) {
    if (a->chat.model) { pl_model_close(a->chat.model); a->chat.model = NULL; }
    a->current = -1;
    a->model_label = "no model";

    if (idx < 0 || (size_t)idx >= a->n_models) return 0;

    long t0 = now_ms();
    pl_model *m = pl_model_open(a->models[idx].path, 2, 2048);
    logf_("load %s: %s in %ldms", a->models[idx].file, m ? "ok" : "FAILED",
          now_ms() - t0);
    if (!m) return 0;

    a->chat.model  = m;
    a->current     = idx;
    a->model_label = a->models[idx].name;
    pl_models_save_choice(a->dir, a->models[idx].file);
    return 1;
}

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
    long t = now_ms();

    pl_event ev = pl_input_next(a->in, 0);
    if (ev.kind == PL_EV_QUIT) { a->stop_requested = 2; return 0; }

    /* A tap in the first moments of a reply is the tail of the tap that asked
     * for it, not a decision to abandon it -- nobody changes their mind in
     * half a second, and on e-ink the "stop" button has not finished being
     * drawn yet. Together with the drain in send_message this is what stopped
     * replies being cut off after four tokens. */
    if (ev.kind == PL_EV_TAP && t - a->gen_started > 700
        && pl_hit_stop(ev.x, ev.y)) {
        pl_ui_tap_flash(a->ui, ev.x, ev.y);
        a->stop_requested = 1;
        logf_("stopped by the reader");
        return 0;
    }

    /* A partial refresh costs about 100 ms of panel time. Redrawing per token
     * would spend more of the budget pushing pixels than generating them, and
     * e-ink cannot show it that fast anyway. */
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
                    "No model is loaded. Run ./build.sh on a computer and copy "
                    "the pocketllm folder into extensions on this Kindle — or "
                    "tap the model name at the top to choose one that is "
                    "already here.");
    } else {
        /* Everything queued right now is the send tap still arriving. It must
         * not be read as a stop for the reply it just asked for. */
        size_t stale = pl_input_drain(a->in);
        if (stale) logf_("dropped %zu queued event(s) from the send tap", stale);

        a->stop_requested = 0;
        a->last_draw = a->gen_started = now_ms();
        long t0 = now_ms();
        int got = pl_chat_reply(&a->chat, on_token, a);
        long ms = now_ms() - t0;
        logf_("reply: produced=%d stopped=%d in %ldms", got, a->stop_requested, ms);

        /* Replace the estimate with what this device actually did. Only a
         * reply long enough to average out the loading and the first token
         * says anything useful about the rate. */
        if (got >= 20 && ms > 0 && a->current >= 0) {
            int tg_x100 = (int)((long)got * 100000 / ms);
            a->models[a->current].tg_x100  = tg_x100;
            a->models[a->current].measured = 1;
            pl_models_record_speed(a->dir, a->models[a->current].file, tg_x100);
        }

        if (!got && !a->stop_requested)
            pl_chat_add(&a->chat, 0, "I could not answer that one. Try asking "
                                     "it a different way, or start a new chat.");
    }

    /* Taps that landed during a half-minute wait were aimed at a screen that
     * no longer exists. Replaying them afterwards reopens the keyboard or
     * jumps to the model list for no reason the reader can see. */
    pl_input_drain(a->in);
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

    char serif[600], sans[600];
    snprintf(serif, sizeof serif, "%s/Literata.ttf", dir);
    snprintf(sans,  sizeof sans,  "%s/Inter.ttf",    dir);

    app a;
    memset(&a, 0, sizeof a);
    a.current = -1;
    a.model_label = "no model";
    snprintf(a.dir, sizeof a.dir, "%s", dir);

    a.ui = pl_ui_fb_create(serif, sans);
    if (!a.ui) { logf_("cannot open the framebuffer -- giving up"); return 1; }
    g_ui = a.ui;
    install_handlers();

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

    long mem = available_mb();
    if (mem) pl_models_set_budget(mem);
    logf_("memory: %ld MB available (budget %ld MB)", mem, pl_models_budget());

    /* What is installed, and which of them this device has actually timed. */
    char models_dir[560];
    snprintf(models_dir, sizeof models_dir, "%s/models", dir);
    a.n_models = pl_models_scan(models_dir, a.models, PL_MAX_MODELS);
    if (!a.n_models)   /* older layouts kept the .gguf beside the binary */
        a.n_models = pl_models_scan(dir, a.models, PL_MAX_MODELS);
    pl_models_load_speeds(dir, a.models, a.n_models);
    logf_("found %zu model(s) in %s", a.n_models, models_dir);

    pl_chat_init(&a.chat, NULL);
    a.screen = MODE_CHAT;

    if (!a.n_models) {
        say("No models installed.",
            "Run ./build.sh on a computer, then copy the pocketllm folder "
            "into extensions on this Kindle.");
        pl_event ev;
        do { ev = pl_input_next(a.in, 60000); }
        while (ev.kind == PL_EV_SWIPE_UP || ev.kind == PL_EV_SWIPE_DOWN);
    } else {
        /* Whatever was chosen last time, if it is still installed; otherwise
         * the first that will load, which the sort has already made the one
         * with the most memory headroom rather than the cleverest. */
        int pick = -1;
        char want[160];
        if (pl_models_load_choice(dir, want, sizeof want))
            for (size_t i = 0; i < a.n_models; i++)
                if (strcmp(a.models[i].file, want) == 0) { pick = (int)i; break; }
        if (pick < 0)
            for (size_t i = 0; i < a.n_models; i++)
                if (a.models[i].fit != PL_FIT_NO) { pick = (int)i; break; }

        if (pick >= 0) {
            char line[160];
            snprintf(line, sizeof line,
                     "Reading %s off the drive. This part is slow; talking to "
                     "it is not.", a.models[pick].name);
            say("Loading…", line);
            if (!load_model(&a, pick))
                say("That model would not load.",
                    "Tap to carry on, then tap the model name at the top to "
                    "choose a different one.");
        }
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

        if (pl_hit_close(ev.x, ev.y)) {
            /* From a sub-screen the X goes back; from the chat it leaves. */
            if (a.screen == MODE_CHAT) break;
            a.screen = MODE_CHAT;
            draw_chat(&a, PL_REFRESH_FULL, 0);
            continue;
        }

        if (a.screen == MODE_MODELS) {
            int pick = pl_models_hit(ev.x, ev.y, a.n_models);
            if (pick < 0) continue;
            if (a.models[pick].fit == PL_FIT_NO) continue;  /* listed, not loadable */
            if (pick != a.current) {
                char line[160];
                snprintf(line, sizeof line, "Reading %s off the drive.",
                         a.models[pick].name);
                say("Switching…", line);
                if (!load_model(&a, pick))
                    say("That model would not load.",
                        "Tap to carry on and pick a different one.");
            }
            a.screen = MODE_CHAT;
            draw_chat(&a, PL_REFRESH_FULL, 0);
            continue;
        }

        if (a.screen == MODE_KEYBOARD) {
            key(&a, pl_keyboard_hit(ev.x, ev.y, a.shift, a.page));
            /* A quit that arrived while a reply was streaming is noticed here,
             * once the generation it interrupted has unwound. */
            if (a.stop_requested == 2) break;
            continue;
        }

        if (pl_hit_model(ev.x, ev.y)) {
            a.screen = MODE_MODELS;
            pl_screen_models(a.ui, a.models, a.n_models, a.current);
            pl_ui_present(a.ui, PL_REFRESH_FULL);
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
    pl_model_close(a.chat.model);
    pl_ui_destroy(a.ui);
    if (g_log) fclose(g_log);
    return 0;
}
