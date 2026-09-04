/* platform/kindle/input_evdev.c — touch from /dev/input/event1.
 *
 * The panel speaks multitouch protocol B: ABS_MT_POSITION_X/Y arrive inside a
 * slot, and a slot is released by ABS_MT_TRACKING_ID == -1. We only care about
 * the first finger, so the state machine is small: remember where the finger
 * went down, and when it lifts, decide tap or swipe.
 */
#include "../input.h"
#include "../devroot.h"

#include <linux/input.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Optional logger, set by the app. */
void (*g_in_log)(const char *fmt, ...) = 0;

struct pl_input {
    int fd;
    int cur_x, cur_y;
    int down_x, down_y;
    int down;          /* a finger is currently on the glass */
    int have_pos;
    int pending_down;  /* saw a touch-down, waiting for SYN_REPORT */
    int pending_up;

    /* A single read() returns up to 64 events. Returning as soon as one
     * gesture completes would throw away everything after it in the buffer --
     * a fast second tap would simply vanish. The buffer persists across calls
     * and is drained before the device is read again. */
    struct input_event buf[64];
    size_t n_buf, i_buf;

    /* The digitizer reports in its OWN units, which are not screen pixels.
     * Using them raw is why nothing was tappable. Bounds come from EVIOCGABS
     * and every event is interpolated onto the panel. */
    int min_x, max_x, min_y, max_y;
    int swap_xy;       /* the panel is mounted rotated relative to the display */
    int screen_w, screen_h;
    int calibrated;
};

static void read_abs_range(int fd, int code, int *lo, int *hi) {
    struct input_absinfo ai;
    if (ioctl(fd, EVIOCGABS(code), &ai) == 0 && ai.maximum > ai.minimum) {
        *lo = ai.minimum; *hi = ai.maximum;
    }
}

pl_input *pl_input_open_sized(int screen_w, int screen_h) {
    char pb[512];
    /* event1 is the touch panel; event0 is the power key. Both were named by
     * the probe, so this is not a guess. */
    int fd = open(pl_path("/dev/input/event1", pb, sizeof pb), O_RDONLY | O_NONBLOCK);
    if (fd < 0) return NULL;
    pl_input *in = calloc(1, sizeof *in);
    if (!in) { close(fd); return NULL; }
    in->fd = fd;
    in->screen_w = screen_w > 0 ? screen_w : 1272;
    in->screen_h = screen_h > 0 ? screen_h : 1696;

    /* Prefer the multitouch axes; fall back to the single-touch ones. */
    in->min_x = in->min_y = 0;
    in->max_x = in->max_y = 0;
    read_abs_range(fd, ABS_MT_POSITION_X, &in->min_x, &in->max_x);
    read_abs_range(fd, ABS_MT_POSITION_Y, &in->min_y, &in->max_y);
    if (in->max_x <= in->min_x) read_abs_range(fd, ABS_X, &in->min_x, &in->max_x);
    if (in->max_y <= in->min_y) read_abs_range(fd, ABS_Y, &in->min_y, &in->max_y);

    if (in->max_x > in->min_x && in->max_y > in->min_y) {
        in->calibrated = 1;
        int rx = in->max_x - in->min_x, ry = in->max_y - in->min_y;
        /* If the digitizer's X spans roughly the screen's HEIGHT and vice
         * versa, the panel is mounted rotated and the axes must be swapped.
         * This is common enough on Kindles that KOReader carries bug reports
         * about it. */
        int portrait_screen = in->screen_h > in->screen_w;
        int portrait_digi   = ry > rx;
        in->swap_xy = (portrait_screen != portrait_digi);
    }

    if (g_in_log)
        g_in_log("touch: x %d..%d, y %d..%d, %s, swap_xy=%d, screen %dx%d",
                 in->min_x, in->max_x, in->min_y, in->max_y,
                 in->calibrated ? "calibrated" : "NO RANGE (using raw values)",
                 in->swap_xy, in->screen_w, in->screen_h);
    return in;
}

/* Kept for callers that do not know the screen size yet. */
pl_input *pl_input_open(void) { return pl_input_open_sized(0, 0); }

/* Map a raw digitizer point onto the panel. */
static void to_screen(pl_input *in, int rx, int ry, int *ox, int *oy) {
    if (!in->calibrated) { *ox = rx; *oy = ry; return; }
    long x = rx, y = ry;
    if (in->swap_xy) { long t = x; x = y; y = t; }
    int lo_x = in->swap_xy ? in->min_y : in->min_x;
    int hi_x = in->swap_xy ? in->max_y : in->max_x;
    int lo_y = in->swap_xy ? in->min_x : in->min_y;
    int hi_y = in->swap_xy ? in->max_x : in->max_y;
    if (hi_x <= lo_x || hi_y <= lo_y) { *ox = rx; *oy = ry; return; }
    *ox = (int)((x - lo_x) * (long)(in->screen_w - 1) / (hi_x - lo_x));
    *oy = (int)((y - lo_y) * (long)(in->screen_h - 1) / (hi_y - lo_y));
    if (*ox < 0) *ox = 0; if (*ox >= in->screen_w) *ox = in->screen_w - 1;
    if (*oy < 0) *oy = 0; if (*oy >= in->screen_h) *oy = in->screen_h - 1;
}

size_t pl_input_drain(pl_input *in) {
    if (!in) return 0;
    size_t dropped = 0;
    /* Bounded: a stuck device must not turn this into a spin. */
    for (int i = 0; i < 256; i++) {
        pl_event ev = pl_input_next(in, 0);
        if (ev.kind == PL_EV_NONE) break;
        dropped++;
    }
    /* A finger still down has no completed gesture to report, but leaving the
     * flag set would pair it with the next release and invent a tap. */
    in->down = in->pending_down = in->pending_up = 0;
    return dropped;
}

void pl_input_close(pl_input *in) {
    if (!in) return;
    if (in->fd >= 0) close(in->fd);
    free(in);
}

/* Below this, a movement is a tap rather than a swipe. At 300 ppi a finger
 * wobbles ~20px even when the user believes they held still. */
#define TAP_SLOP 60

/* Protocol B arrives in a strict order, and it matters:
 *
 *     ABS_MT_SLOT, ABS_MT_TRACKING_ID, ABS_MT_POSITION_X,
 *     ABS_MT_POSITION_Y, SYN_REPORT
 *
 * The tracking id comes FIRST, before the position it belongs to. Capturing
 * the touch-down point when the id arrives therefore records the coordinates
 * of the PREVIOUS touch, and the apparent movement is the distance between two
 * separate taps -- hundreds of pixels, so every tap was classified as a swipe
 * and nothing was clickable.
 *
 * The fix is to treat SYN_REPORT as the only point at which state is
 * consistent: mark the finger as newly down, then latch its position at the
 * next sync. */
pl_event pl_input_next(pl_input *in, int timeout_ms) {
    pl_event ev = { PL_EV_NONE, 0, 0 };
    if (!in) return ev;

    /* Drain what is already buffered before asking the device for more. */
    if (in->i_buf >= in->n_buf) {
        struct pollfd p = { in->fd, POLLIN, 0 };
        if (poll(&p, 1, timeout_ms) <= 0) return ev;
        ssize_t n = read(in->fd, in->buf, sizeof in->buf);
        if (n < (ssize_t)sizeof in->buf[0]) return ev;
        in->n_buf = (size_t)n / sizeof in->buf[0];
        in->i_buf = 0;
    }

    struct input_event *e = in->buf;
    for (size_t i = in->i_buf; i < in->n_buf; i++) {
        in->i_buf = i + 1;
        if (e[i].type == EV_ABS) {
            switch (e[i].code) {
                case ABS_MT_POSITION_X: case ABS_X:
                    in->cur_x = e[i].value; in->have_pos = 1; break;
                case ABS_MT_POSITION_Y: case ABS_Y:
                    in->cur_y = e[i].value; in->have_pos = 1; break;
                case ABS_MT_TRACKING_ID:
                    if (e[i].value == -1) in->pending_up = 1;
                    else                  in->pending_down = 1;
                    break;
                default: break;
            }
        } else if (e[i].type == EV_KEY && e[i].code == BTN_TOUCH) {
            if (e[i].value) in->pending_down = 1;
            else            in->pending_up = 1;
        } else if (e[i].type == EV_SYN && e[i].code == SYN_REPORT) {
            /* Everything is consistent here, and only here. */
            if (in->pending_down && !in->down) {
                in->down = 1;
                in->down_x = in->cur_x;
                in->down_y = in->cur_y;
                if (g_in_log) g_in_log("  down at raw (%d,%d)", in->down_x, in->down_y);
            }
            in->pending_down = 0;

            if (in->pending_up) {
                in->pending_up = 0;
                if (in->down) {
                    in->down = 0;
                    int sx, sy, dsx, dsy;
                    to_screen(in, in->cur_x, in->cur_y, &sx, &sy);
                    to_screen(in, in->down_x, in->down_y, &dsx, &dsy);
                    int adx = sx - dsx < 0 ? dsx - sx : sx - dsx;
                    int ady = sy - dsy < 0 ? dsy - sy : sy - dsy;

                    if (adx < TAP_SLOP && ady < TAP_SLOP) {
                        ev.kind = PL_EV_TAP; ev.x = sx; ev.y = sy;
                    } else if (ady > adx) {
                        ev.kind = sy < dsy ? PL_EV_SWIPE_UP : PL_EV_SWIPE_DOWN;
                        ev.x = sx; ev.y = sy;
                    }
                    if (g_in_log)
                        g_in_log("  up at raw (%d,%d) -> screen (%d,%d), moved %d,%d -> %s",
                                 in->cur_x, in->cur_y, sx, sy, adx, ady,
                                 ev.kind == PL_EV_TAP ? "TAP" :
                                 ev.kind == PL_EV_NONE ? "ignored" : "swipe");
                    if (ev.kind != PL_EV_NONE) return ev;
                }
            }
        }
    }
    return ev;
}
