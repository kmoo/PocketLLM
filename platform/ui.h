/* platform/ui.h — the drawing interface.
 *
 * This is the REAL interface. Both implementations satisfy it:
 *   platform/host    renders to a PNG at true device size, for looking at here
 *   platform/kindle  writes to /dev/fb0 and refreshes the panel
 *
 * Screens are written once against this and run in both places, so the golden
 * images we diff on this Mac are the same pixels the Kindle draws.
 *
 * The device is 8bpp greyscale: 0 = black, 255 = white. No colour, ever.
 */
#ifndef PL_UI_H
#define PL_UI_H

#include <stddef.h>
#include <stdint.h>

/* Measured on the device, not from a spec sheet (see docs/). */
/* The panel's real size, set when the UI opens. Kindles differ -- 1072x1448 on
 * a Paperwhite, 1264x1680 on an Oasis, 1272x1696 here -- so every layout is
 * written against these rather than against one device's numbers. They hold
 * the Scribe's size until a backend reports otherwise. */
extern int pl_screen_w, pl_screen_h;
#define PL_SCREEN_W pl_screen_w
#define PL_SCREEN_H pl_screen_h

typedef enum {
    PL_FONT_SERIF = 0,       /* Literata — answers, book text          */
    PL_FONT_SERIF_ITALIC,
    PL_FONT_SANS,            /* Inter — chrome, your own messages      */
    PL_FONT_SANS_BOLD,
    PL_FONT__COUNT
} pl_font;

typedef enum { PL_ALIGN_LEFT = 0, PL_ALIGN_CENTER, PL_ALIGN_RIGHT } pl_align;

/* E-ink refresh modes. Getting these wrong is the difference between crisp
 * text and a ghosted mess, so callers state intent rather than guessing. */
typedef enum {
    PL_REFRESH_NONE = 0,   /* drew into the buffer, don't show it yet     */
    PL_REFRESH_FAST,       /* partial, ~100ms, some ghosting: streaming   */
    PL_REFRESH_FULL        /* flash, ~500ms, clears ghosting: new screen  */
} pl_refresh;

typedef struct { int x, y, w, h; } pl_rect;

typedef struct pl_ui pl_ui;

/* --- surface ------------------------------------------------------------ */
void pl_ui_clear(pl_ui *ui, uint8_t grey);
void pl_ui_present(pl_ui *ui, pl_refresh how);

/* --- primitives --------------------------------------------------------- */
void pl_ui_fill(pl_ui *ui, pl_rect r, uint8_t grey);
void pl_ui_rect(pl_ui *ui, pl_rect r, int thickness, uint8_t grey);
void pl_ui_round_rect(pl_ui *ui, pl_rect r, int radius, int thickness,
                      uint8_t grey, int filled);
void pl_ui_hline(pl_ui *ui, int x, int y, int w, uint8_t grey);

/* --- text --------------------------------------------------------------- */
/* Draw one line. Returns the advance width in pixels. */
int  pl_ui_text(pl_ui *ui, pl_font f, int size_px, int x, int y,
                const char *utf8, uint8_t grey);

/* Width of `utf8` if drawn at this font and size. No drawing. */
int  pl_ui_text_width(pl_ui *ui, pl_font f, int size_px, const char *utf8);

/* Word-wrap `utf8` into a box and draw it. Returns the y just past the last
 * baseline drawn, so callers can stack blocks without measuring twice.
 * Pass draw=0 to measure only. */
int  pl_ui_text_box(pl_ui *ui, pl_font f, int size_px, pl_rect box,
                    int line_height, pl_align align, const char *utf8,
                    uint8_t grey, int draw);

/* Width of the FINAL wrapped line of `utf8` in this box, and via out_y the
 * baseline y it would occupy. This is where a streaming caret goes -- without
 * it the caret has to guess, and guessing puts it in the middle of a word. */
int  pl_ui_text_box_tail(pl_ui *ui, pl_font f, int size_px, pl_rect box,
                         int line_height, const char *utf8, int *out_y);

/* Immediate tap feedback: a filled circle flashed black-then-normal at the
 * touch point, refreshed FAST. E-ink has no haptics and no cursor, so without
 * this a tap gives no sign it registered until whatever it triggered finishes
 * -- which can be a network... here, a retrieval or a redraw -- and on a
 * device already prone to feeling slow, an unacknowledged tap reads as
 * "broken" long before it reads as "still working". Call this the instant a
 * tap is classified, before doing anything else. */
void pl_ui_tap_flash(pl_ui *ui, int x, int y);

/* --- lifecycle (implementation-specific creation lives in each backend) -- */
void pl_ui_destroy(pl_ui *ui);

#endif
