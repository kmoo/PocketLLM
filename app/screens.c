#include "screens.h"
#include <stdio.h>
#include <string.h>

/* Layout is written against a 1272x1696 panel and scaled to whatever we woke
 * up on: a Paperwhite is 1072x1448, an Oasis 1264x1680. Touch targets are
 * floored rather than scaled, because a finger is the same size on every
 * device -- Material asks for 48dp, about 106px at 300ppi, and e-ink has no
 * haptics and no cursor to help one correct itself. */
#define REF_W      1272
#define S(v)       ((int)((long)(v) * PL_SCREEN_W / REF_W))
#define SMIN(v, m) (S(v) < (m) ? (m) : S(v))

#define PAD        S(48)
#define TOP_H      SMIN(120, 96)
#define COMPOSER_H SMIN(140, 112)

#define BLACK  0x0D
#define INK    0x22
#define MID    0x6D
#define FAINT  0x9A
#define RULE   0xE4
#define BUBBLE 0xEF
#define WHITE  0xFF

#define F_TITLE SMIN(34, 22)
#define F_BODY  SMIN(40, 24)
#define F_SMALL SMIN(32, 20)
#define F_TINY  SMIN(26, 17)

/* ---- chrome ------------------------------------------------------------ */

static void close_button(pl_ui *ui) {
    int r = SMIN(46, 34);
    int cx = PL_SCREEN_W - PAD - r, cy = TOP_H / 2;
    pl_rect c = { cx - r, cy - r, r * 2, r * 2 };
    pl_ui_round_rect(ui, c, r, 3, 0xB4, 0);
    for (int i = -r / 2; i <= r / 2; i++) {
        pl_rect a = { cx + i - 2, cy + i - 2, 5, 5 };
        pl_rect b = { cx + i - 2, cy - i - 2, 5, 5 };
        pl_ui_fill(ui, a, INK);
        pl_ui_fill(ui, b, INK);
    }
}

int pl_hit_close(int x, int y) {
    /* Padded well past the drawn circle, because a 34px target is under half
     * what a finger needs -- but clamped to the top bar, so it cannot also
     * claim taps that belong to the scroll rail directly beneath it. */
    int r = SMIN(46, 34) + S(18);
    int cx = PL_SCREEN_W - PAD - SMIN(46, 34), cy = TOP_H / 2;
    if (y >= TOP_H) return 0;
    return x > cx - r && x < cx + r && y > cy - r;
}

/* The model label doubles as the way into the picker, so choosing a model
 * needs no chrome of its own -- and the thing you tap is the thing you are
 * changing. Drawn in a pill so it reads as a control rather than a caption. */
#define MODEL_TAB_W  S(300)

static pl_rect model_tab(void) {
    int h = SMIN(58, 44);
    pl_rect r = { PL_SCREEN_W - PAD - SMIN(46, 34) * 2 - S(20) - MODEL_TAB_W,
                  TOP_H / 2 - h / 2, MODEL_TAB_W, h };
    return r;
}

int pl_hit_model(int x, int y) {
    if (y >= TOP_H || pl_hit_close(x, y)) return 0;
    pl_rect r = model_tab();
    return x >= r.x && x < r.x + r.w;
}

static void top_bar(pl_ui *ui, const char *model_name) {
    pl_ui_text(ui, PL_FONT_SANS_BOLD, F_TITLE, PAD, TOP_H / 2 + F_TITLE / 3,
               "PocketLLM", INK);
    if (model_name && *model_name) {
        pl_rect r = model_tab();
        pl_ui_round_rect(ui, r, r.h / 2, 2, 0xD8, 0);
        int w = pl_ui_text_width(ui, PL_FONT_SANS, F_TINY, model_name);
        int cap = r.w - S(30);
        pl_ui_text(ui, PL_FONT_SANS, F_TINY,
                   r.x + (r.w - (w < cap ? w : cap)) / 2,
                   r.y + r.h / 2 + F_TINY / 3, model_name, MID);
    }
    pl_ui_hline(ui, 0, TOP_H, PL_SCREEN_W, RULE);
    close_button(ui);
}

static void composer(pl_ui *ui, const char *placeholder) {
    int y = PL_SCREEN_H - COMPOSER_H;
    pl_ui_hline(ui, 0, y, PL_SCREEN_W, RULE);
    int h = COMPOSER_H - S(36);
    pl_rect pill = { PAD, y + (COMPOSER_H - h) / 2, PL_SCREEN_W - PAD * 2, h };
    pl_ui_round_rect(ui, pill, h / 2, 2, 0xCF, 0);
    pl_ui_text(ui, PL_FONT_SANS, F_SMALL, pill.x + S(34),
               pill.y + h / 2 + F_SMALL / 3, placeholder, 0xB0);
}

int pl_hit_composer(int x, int y) {
    (void)x;
    return y >= PL_SCREEN_H - COMPOSER_H;
}

/* ---- chat -------------------------------------------------------------- */

int pl_hit_scroll(int x, int y) {
    if (x < PL_SCREEN_W - S(110)) return 0;
    if (y < TOP_H || y >= PL_SCREEN_H - COMPOSER_H) return 0;
    return y < (TOP_H + PL_SCREEN_H - COMPOSER_H) / 2 ? -1 : 1;
}

int pl_hit_stop(int x, int y) {
    (void)x;
    return y >= PL_SCREEN_H - COMPOSER_H;
}

/* The height of the conversation area, so the caller can page by screenfuls
 * without knowing where the chrome is. */
int pl_screen_view_height(void) { return PL_SCREEN_H - COMPOSER_H - TOP_H; }

/* Drawing and measuring are the same walk, because two walks drift apart. With
 * draw=0 nothing is rasterised and only the height comes back -- which matters:
 * pinning a streaming reply to the bottom of the screen needs the height
 * BEFORE the draw, and rendering a screenful of text twice every 450 ms on two
 * A53 cores would spend more of the budget on pixels than on tokens. */
#define D_FILL(r, g)   do { if (draw) pl_ui_fill(ui, (r), (g)); } while (0)
#define D_RECT(r, rad, t, g, f) \
    do { if (draw) pl_ui_round_rect(ui, (r), (rad), (t), (g), (f)); } while (0)

void pl_screen_chat(pl_ui *ui, const pl_message *msgs, size_t n,
                    int scroll, int *out_height,
                    const char *model_name, int busy, int tick, int draw) {
    if (draw) pl_ui_clear(ui, WHITE);

    int y = TOP_H + S(40) - scroll;
    const int maxw = PL_SCREEN_W - PAD * 2 - S(110);   /* room for the rail */

    if (!n && draw) {
        /* Empty state. Says what this is and, more usefully, that nothing
         * leaves the device -- which is the whole reason to run it here. */
        int cy = (TOP_H + PL_SCREEN_H - COMPOSER_H) / 2;
        pl_rect a = { PAD, cy - S(120), PL_SCREEN_W - PAD * 2, S(120) };
        pl_ui_text_box(ui, PL_FONT_SERIF, SMIN(52, 32), a, SMIN(72, 44),
                       PL_ALIGN_CENTER, "Ask it anything.", INK, 1);
        pl_rect b = { PAD + S(60), cy + S(10), PL_SCREEN_W - (PAD + S(60)) * 2, S(200) };
        pl_ui_text_box(ui, PL_FONT_SANS, F_SMALL, b, SMIN(46, 30), PL_ALIGN_CENTER,
                       "A language model running on this Kindle. No wifi, no "
                       "account, nothing leaves the device.", MID, 1);
    }

    for (size_t i = 0; i < n; i++) {
        if (msgs[i].from_user) {
            /* A bubble sized to its own text, up to 78% of the width. Measured
             * first, because the bubble has to be painted before the text that
             * decides how tall it is. */
            int bw = maxw * 78 / 100;
            pl_rect probe = { 0, 0, bw - S(52), 0 };
            int hgt = pl_ui_text_box(ui, PL_FONT_SANS, F_SMALL, probe, SMIN(46, 30),
                                     PL_ALIGN_LEFT, msgs[i].text, INK, 0) + S(34);
            int tw = pl_ui_text_width(ui, PL_FONT_SANS, F_SMALL, msgs[i].text) + S(52);
            if (tw < bw) bw = tw;
            pl_rect bub = { PL_SCREEN_W - PAD - S(110) - bw, y, bw, hgt };
            D_RECT(bub, S(22), 0, BUBBLE, 1);
            pl_rect tb = { bub.x + S(26), bub.y + S(12), bub.w - S(52), bub.h - S(24) };
            pl_ui_text_box(ui, PL_FONT_SANS, F_SMALL, tb, SMIN(46, 30),
                           PL_ALIGN_LEFT, msgs[i].text, INK, draw);
            y += hgt + S(34);
        } else {
            /* The model's replies are set flush, in a serif, like the book the
             * device was built for -- not in a bubble. */
            pl_rect tb = { PAD, y, maxw, 0 };
            int end = pl_ui_text_box(ui, PL_FONT_SERIF, F_BODY, tb, SMIN(62, 38),
                                     PL_ALIGN_LEFT, msgs[i].text, BLACK, draw);
            if (msgs[i].streaming && draw) {
                /* The caret sits exactly after the last glyph, so it tracks the
                 * text as it arrives rather than guessing a position. */
                int cy = 0;
                int lw = pl_ui_text_box_tail(ui, PL_FONT_SERIF, F_BODY, tb,
                                             SMIN(62, 38), msgs[i].text, &cy);
                pl_rect car = { PAD + lw + S(8), cy - S(38), S(4), S(42) };
                pl_ui_fill(ui, car, BLACK);
            }
            y = end + S(40);
        }
    }

    if (out_height) *out_height = y + scroll - (TOP_H + S(40));
    if (!draw) return;

    /* Scrolled text runs up behind the chrome, so the bar is painted over it
     * rather than clipping every draw call. */
    pl_rect above = { 0, 0, PL_SCREEN_W, TOP_H };
    pl_ui_fill(ui, above, WHITE);
    top_bar(ui, model_name);

    /* Scroll rail: two chevrons at the right edge. Swipes on an e-ink
     * digitiser are unreliable enough that a target you cannot miss beats a
     * scrollbar you can. */
    {
        int mid = (TOP_H + PL_SCREEN_H - COMPOSER_H) / 2;
        int cx = PL_SCREEN_W - S(56);
        for (int i = 0; i <= S(18); i++) {
            pl_rect a1 = { cx - i, TOP_H + S(90) + i, S(5), S(5) };
            pl_rect a2 = { cx + i, TOP_H + S(90) + i, S(5), S(5) };
            pl_rect b1 = { cx - i, mid + S(220) - i, S(5), S(5) };
            pl_rect b2 = { cx + i, mid + S(220) - i, S(5), S(5) };
            pl_ui_fill(ui, a1, 0x9E); pl_ui_fill(ui, a2, 0x9E);
            pl_ui_fill(ui, b1, 0x9E); pl_ui_fill(ui, b2, 0x9E);
        }
    }

    if (busy) {
        /* While generating, the composer becomes progress plus a way out. Half
         * a minute of an unchanging screen reads as a crash on a device with
         * no spinner of its own, so the dots walk. */
        int cy = PL_SCREEN_H - COMPOSER_H;
        pl_rect strip = { 0, cy, PL_SCREEN_W, COMPOSER_H };
        pl_ui_fill(ui, strip, WHITE);
        pl_ui_hline(ui, 0, cy, PL_SCREEN_W, RULE);
        int dx = PAD, dy = cy + COMPOSER_H / 2;
        static const uint8_t SHADE[3] = { 0x30, 0x88, 0xC0 };
        for (int i = 0; i < 3; i++) {
            pl_rect d = { dx, dy - S(7), S(14), S(14) };
            pl_ui_round_rect(ui, d, S(7), 0, SHADE[((i - tick) % 3 + 3) % 3], 1);
            dx += S(24);
        }
        pl_ui_text(ui, PL_FONT_SANS, F_SMALL, dx + S(16), dy + F_SMALL / 3,
                   "thinking…", MID);
        const char *stop = "stop";
        int sw = pl_ui_text_width(ui, PL_FONT_SANS, F_SMALL, stop);
        pl_ui_text(ui, PL_FONT_SANS, F_SMALL, PL_SCREEN_W - PAD - sw,
                   dy + F_SMALL / 3, stop, INK);
    } else {
        composer(ui, "Type a message…");
    }
}

#undef D_FILL
#undef D_RECT

/* ---- keyboard ---------------------------------------------------------- */

#define KB_ROWS  4
#define KB_GAP   S(10)
#define KB_KEY_H SMIN(128, 96)
#define KB_H     (KB_ROWS * KB_KEY_H + (KB_ROWS + 1) * KB_GAP)

/* Two pages. A chat app is not a search box: people write question marks and
 * apostrophes and paste in numbers, and a keyboard without them forces every
 * one of those through a workaround. */
static const char *const KB_PAGE[2][3] = {
    { "qwertyuiop", "asdfghjkl",  "zxcvbnm" },
    { "1234567890", "-/:;()$&@\"", ".,?!'" }
};

/* The bottom row, in units of the same width. Space earns its double: it is
 * the most-pressed key and the one whose miss is most annoying. */
#define KB_FN 5
static const int FN_WEIGHT[KB_FN] = { 2, 2, 4, 2, 3 };

static int kb_key_rect(int page, int row, int col, pl_rect *r) {
    if (row < 0 || row >= KB_ROWS) return 0;
    int top = PL_SCREEN_H - KB_H;

    if (row == 3) {
        if (col < 0 || col >= KB_FN) return 0;
        int total = 0;
        for (int i = 0; i < KB_FN; i++) total += FN_WEIGHT[i];
        int avail = PL_SCREEN_W - KB_GAP * (KB_FN + 1);
        int x = KB_GAP;
        for (int i = 0; i < col; i++) x += avail * FN_WEIGHT[i] / total + KB_GAP;
        r->x = x;
        r->y = top + 3 * (KB_KEY_H + KB_GAP);
        r->w = avail * FN_WEIGHT[col] / total;
        r->h = KB_KEY_H;
        return 1;
    }

    const char *keys = KB_PAGE[page][row];
    size_t n = strlen(keys);
    if (col < 0 || (size_t)col >= n) return 0;

    /* Every row uses the same key width, set by the widest, and shorter rows
     * are centred. Insetting a short row without shrinking its keys is what
     * pushes the last one off the right edge -- which shipped once. */
    const int LONGEST = 10;
    int kw = (PL_SCREEN_W - KB_GAP * (LONGEST + 1)) / LONGEST;
    int row_w = (int)n * kw + ((int)n - 1) * KB_GAP;
    r->x = (PL_SCREEN_W - row_w) / 2 + col * (kw + KB_GAP);
    r->y = top + row * (KB_KEY_H + KB_GAP);
    r->w = kw; r->h = KB_KEY_H;
    return 1;
}

void pl_screen_keyboard(pl_ui *ui, const char *typed, int shift, int page) {
    pl_ui_clear(ui, WHITE);
    top_bar(ui, NULL);
    page = page ? 1 : 0;

    /* What has been typed, in a box rather than on a line: pasting a paragraph
     * is a normal thing to do here, and a single line would hide most of it. */
    int fy = TOP_H + S(36);
    int avail = PL_SCREEN_H - KB_H - fy - S(30);
    pl_rect pill = { PAD, fy, PL_SCREEN_W - PAD * 2, avail };
    pl_ui_round_rect(ui, pill, S(28), 2, 0xCF, 0);
    pl_rect tb = { pill.x + S(30), pill.y + S(26), pill.w - S(60), avail - S(52) };
    if (typed && *typed) {
        pl_ui_text_box(ui, PL_FONT_SANS, F_SMALL, tb, SMIN(46, 30),
                       PL_ALIGN_LEFT, typed, INK, 1);
        int cy = 0;
        int lw = pl_ui_text_box_tail(ui, PL_FONT_SANS, F_SMALL, tb, SMIN(46, 30),
                                     typed, &cy);
        pl_rect caret = { tb.x + lw + S(6), cy - S(30), S(4), S(34) };
        pl_ui_fill(ui, caret, BLACK);
    } else {
        pl_ui_text(ui, PL_FONT_SANS, F_SMALL, tb.x, tb.y + F_SMALL,
                   "Type or paste anything…", 0xC4);
    }

    const char *fn[KB_FN] = { page ? "ABC" : "?123", "shift", "space", "back", "send" };
    for (int row = 0; row < KB_ROWS; row++) {
        for (int col = 0; col < 12; col++) {
            pl_rect r;
            if (!kb_key_rect(page, row, col, &r)) break;

            int is_send  = (row == 3 && col == 4);
            int is_shift = (row == 3 && col == 1);
            int solid = is_send || (is_shift && shift && !page);
            pl_ui_round_rect(ui, r, S(14), 2, solid ? BLACK : 0xDC, solid);

            char label[8];
            const char *text;
            if (row == 3) text = fn[col];
            else {
                label[0] = KB_PAGE[page][row][col];
                if (shift && !page && label[0] >= 'a' && label[0] <= 'z') label[0] -= 32;
                label[1] = 0;
                text = label;
            }
            /* Shift is meaningless on the symbol page; drawn faint rather than
             * removed, so the row does not shift under your finger. */
            uint8_t fg = solid ? WHITE : (is_shift && page ? 0xB0 : INK);
            int size = row == 3 ? SMIN(30, 20) : SMIN(52, 34);
            int tw = pl_ui_text_width(ui, PL_FONT_SANS, size, text);
            pl_ui_text(ui, PL_FONT_SANS, size, r.x + (r.w - tw) / 2,
                       r.y + r.h / 2 + size / 3, text, fg);
        }
    }
}

int pl_keyboard_hit(int x, int y, int shift, int page) {
    page = page ? 1 : 0;
    for (int row = 0; row < KB_ROWS; row++) {
        for (int col = 0; col < 12; col++) {
            pl_rect r;
            if (!kb_key_rect(page, row, col, &r)) break;
            if (x < r.x || x >= r.x + r.w || y < r.y || y >= r.y + r.h) continue;
            if (row == 3) {
                switch (col) {
                    case 0:  return PL_KEY_PAGE;
                    case 1:  return page ? 0 : PL_KEY_SHIFT;
                    case 2:  return ' ';
                    case 3:  return '\b';
                    default: return '\n';
                }
            }
            char c = KB_PAGE[page][row][col];
            if (shift && !page && c >= 'a' && c <= 'z') c = (char)(c - 32);
            return (unsigned char)c;
        }
    }
    return 0;
}

/* ---- choosing a model -------------------------------------------------- */

#define ROW_H     SMIN(210, 158)
#define ROWS_TOP  (TOP_H + S(120))

/* How many rows the panel has room for. Below this the list is truncated and
 * says so -- a model that is installed but invisible would read as a copy that
 * failed. There is no scrolling here on purpose: the list is short by
 * construction, and a Kindle with a few hundred megabytes free has room for a
 * handful of models at
 * most before the drive is the problem. */
static size_t rows_shown(size_t n) {
    size_t room = (size_t)((PL_SCREEN_H - S(120) - ROWS_TOP) / ROW_H);
    return n < room ? n : room;
}

int pl_models_hit(int x, int y, size_t n) {
    (void)x;
    if (y < ROWS_TOP) return -1;
    int i = (y - ROWS_TOP) / ROW_H;
    return (i >= 0 && (size_t)i < rows_shown(n)) ? i : -1;
}

static void seconds_label(int secs, char *out, size_t cap) {
    /* "about 2 minutes" beats "127 s" when the point is to decide whether you
     * are willing to wait for it. */
    if (secs < 90) snprintf(out, cap, "%ds", secs);
    else           snprintf(out, cap, "%dm %02ds", secs / 60, secs % 60);
}

void pl_screen_models(pl_ui *ui, const pl_model_info *m, size_t n, int current) {
    pl_ui_clear(ui, WHITE);
    top_bar(ui, NULL);

    pl_ui_text(ui, PL_FONT_SERIF, SMIN(46, 30), PAD, TOP_H + S(72),
               "Choose a model", BLACK);

    if (!n) {
        pl_rect b = { PAD, TOP_H + S(180), PL_SCREEN_W - PAD * 2, S(300) };
        pl_ui_text_box(ui, PL_FONT_SANS, F_SMALL, b, SMIN(46, 30), PL_ALIGN_LEFT,
                       "No models found. Run build.sh on a computer and copy "
                       "the models folder into extensions/pocketllm.", MID, 1);
        return;
    }

    size_t shown = rows_shown(n);
    for (size_t i = 0; i < shown; i++) {
        int top = ROWS_TOP + (int)i * ROW_H;

        int chosen = ((int)i == current);
        if (chosen) {
            /* The one in use is filled rather than ticked: on e-ink a small
             * mark at arm's length is easy to miss, a block of tone is not. */
            pl_rect band = { PAD - S(18), top + S(6),
                             PL_SCREEN_W - (PAD - S(18)) * 2, ROW_H - S(20) };
            pl_ui_round_rect(ui, band, S(18), 0, 0xF0, 1);
        }

        /* A model too big to load is listed, dimmed, and says why -- rather
         * than hidden, which would look like the file failed to copy. */
        int dead = (m[i].fit == PL_FIT_NO);
        uint8_t head = dead ? 0xA6 : BLACK;
        uint8_t body = dead ? 0xBA : MID;

        pl_ui_text(ui, PL_FONT_SANS_BOLD, SMIN(38, 25), PAD, top + S(52),
                   m[i].name, head);

        char right[48], sub[160];
        if (dead) {
            snprintf(right, sizeof right, "%s", "too big");
        } else {
            char t[24];
            seconds_label(pl_model_seconds(&m[i], 60), t, sizeof t);
            snprintf(right, sizeof right, "~%s", t);
        }
        int rw = pl_ui_text_width(ui, PL_FONT_SANS, SMIN(40, 26), right);
        pl_ui_text(ui, PL_FONT_SANS, SMIN(40, 26), PL_SCREEN_W - PAD - rw,
                   top + S(52), right, head);

        if (!dead) {
            const char *unit = m[i].measured ? "a short reply, measured here"
                                             : "a short reply, estimated";
            int uw = pl_ui_text_width(ui, PL_FONT_SANS, F_TINY, unit);
            pl_ui_text(ui, PL_FONT_SANS, F_TINY, PL_SCREEN_W - PAD - uw,
                       top + S(88), unit, body);
        }

        snprintf(sub, sizeof sub, "%ld MB  ·  %s%s",
                 m[i].bytes / (1024 * 1024), m[i].licence,
                 m[i].fit == PL_FIT_TIGHT ? "  ·  tight on memory" : "");
        pl_ui_text(ui, PL_FONT_SANS, F_TINY, PAD, top + S(88), sub, body);

        pl_rect nb = { PAD, top + S(104), PL_SCREEN_W - PAD * 2 - S(340), S(80) };
        pl_ui_text_box(ui, PL_FONT_SERIF, SMIN(32, 21), nb, SMIN(42, 28),
                       PL_ALIGN_LEFT,
                       dead ? "Needs more memory than this Kindle has."
                            : m[i].note, body, 1);

        if (i + 1 < shown) pl_ui_hline(ui, PAD, top + ROW_H - S(8),
                                       PL_SCREEN_W - PAD * 2, RULE);
    }

    if (shown < n) {
        char more[80];
        snprintf(more, sizeof more,
                 "%zu more installed than fit on this screen.", n - shown);
        pl_ui_text(ui, PL_FONT_SANS, F_TINY, PAD,
                   ROWS_TOP + (int)shown * ROW_H + S(40), more, FAINT);
    }

    /* The one number that surprises people: the first reply in a conversation
     * evaluates the whole history, every later one only the newest turn. */
    pl_rect foot = { PAD, PL_SCREEN_H - S(110), PL_SCREEN_W - PAD * 2, S(90) };
    pl_ui_text_box(ui, PL_FONT_SANS, F_TINY, foot, SMIN(38, 25), PL_ALIGN_LEFT,
                   "The first reply after switching takes longer — the whole "
                   "conversation has to be read in once.", FAINT, 1);
}

/* ---- notice ------------------------------------------------------------ */

void pl_screen_notice(pl_ui *ui, const char *line1, const char *line2) {
    pl_ui_clear(ui, WHITE);
    pl_rect a = { PAD, PL_SCREEN_H / 2 - S(130), PL_SCREEN_W - PAD * 2, S(130) };
    pl_ui_text_box(ui, PL_FONT_SERIF, SMIN(56, 34), a, SMIN(76, 46),
                   PL_ALIGN_CENTER, line1, BLACK, 1);
    pl_rect b = { PAD, PL_SCREEN_H / 2 + S(20), PL_SCREEN_W - PAD * 2, S(180) };
    pl_ui_text_box(ui, PL_FONT_SANS, F_SMALL, b, SMIN(50, 32),
                   PL_ALIGN_CENTER, line2, MID, 1);
}
