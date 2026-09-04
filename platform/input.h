/* platform/input.h — touch, abstracted the same way drawing is.
 *
 * The device reports a multitouch protocol-B stream on /dev/input/event1
 * (confirmed by the probe: "pt_mt"). The host backend replays a scripted list
 * of taps instead, so screen flows can be exercised without the device.
 */
#ifndef PL_INPUT_H
#define PL_INPUT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    PL_EV_NONE = 0,
    PL_EV_TAP,        /* finger down then up, near enough the same place */
    PL_EV_SWIPE_UP,
    PL_EV_SWIPE_DOWN,
    PL_EV_QUIT
} pl_event_kind;

typedef struct {
    pl_event_kind kind;
    int x, y;
} pl_event;

typedef struct pl_input pl_input;

/* Waits up to timeout_ms for an event. Returns PL_EV_NONE on timeout, which is
 * how the app stays responsive while streaming an answer. */
pl_event pl_input_next(pl_input *in, int timeout_ms);

/* Throw away everything queued, and return how much was thrown.
 *
 * This exists because a tap that starts an action is still arriving while the
 * action runs. Tapping "send" put a tap in the queue at the bottom of the
 * screen; the generating screen puts "stop" in that same place; and the first
 * poll during generation read the send tap as a stop and killed the reply
 * after four tokens. Whatever is queued when a long operation begins belongs
 * to the gesture that began it. */
size_t pl_input_drain(pl_input *in);
void     pl_input_close(pl_input *in);

#endif
