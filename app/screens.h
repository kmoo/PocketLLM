/* app/screens.h — one function per screen, drawn against pl_ui.
 *
 * Screens are pure drawing: data in, pixels out. The same code renders to a PNG
 * on a Mac and to /dev/fb0 on the device, which is what makes the layout
 * checkable without a Kindle in hand.
 */
#ifndef PL_SCREENS_H
#define PL_SCREENS_H

#include "../platform/ui.h"
#include "ui_types.h"

/* The conversation. `scroll` is how far down it is scrolled in pixels; the
 * total height comes back through `out_height` so the caller can bound it.
 * `busy` swaps the composer for a progress line, which `tick` animates.
 *
 * `draw` = 0 measures without rasterising anything -- the same walk, so the
 * two can never disagree about where things are. Pinning a streaming reply to
 * the bottom needs the height before the draw, and on two A53 cores rendering
 * a screenful of text twice per update costs more than generating does. */
void pl_screen_chat(pl_ui *ui, const pl_message *msgs, size_t n,
                    int scroll, int *out_height,
                    const char *model_name, int busy, int tick, int draw);

/* The height of the conversation area, for paging by screenfuls. */
int pl_screen_view_height(void);

/* Keys that are not characters. Below 0x20, so they can never collide with
 * something the keyboard actually types. */
#define PL_KEY_SHIFT 1
#define PL_KEY_PAGE  2   /* letters <-> numbers and punctuation */

/* The keyboard, with whatever has been typed so far. `page` is 0 for letters,
 * 1 for symbols. */
void pl_screen_keyboard(pl_ui *ui, const char *typed, int shift, int page);

/* Which key is under a tap, or 0 -- already shifted, so the caller inserts
 * what comes back and does not re-apply the case itself. '\b' is backspace and
 * '\n' is send. Shared by the drawing and the hit test so the two cannot drift
 * apart; tests/test_layout.c checks that every drawn key is reachable. */
int  pl_keyboard_hit(int x, int y, int shift, int page);

/* Chrome hit tests. */
int  pl_hit_close(int x, int y);
int  pl_hit_composer(int x, int y);
int  pl_hit_scroll(int x, int y);    /* -1 up, 1 down, 0 neither */
int  pl_hit_stop(int x, int y);      /* the stop button, while generating */

/* A full-screen message, for a crash or a clean exit. The panel keeps whatever
 * was drawn last, so leaving it blank looks exactly like a freeze. */
void pl_screen_notice(pl_ui *ui, const char *line1, const char *line2);

#endif
