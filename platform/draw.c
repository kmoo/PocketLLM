int pl_screen_w = 1272;
int pl_screen_h = 1696;

/* platform/draw.c — every drawing primitive, shared by both backends.
 *
 * Operates only on the pixel buffer in struct pl_ui, so the PNG we look at here
 * and the pixels the Kindle panel shows come from the same code. That is what
 * makes golden-image testing meaningful rather than decorative.
 */
/* stb_truetype's implementation defines a lot we don't call. Silence that here
 * only, so -Werror keeps its teeth everywhere else in the project. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "ui_internal.h"
#pragma GCC diagnostic pop
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pl_face_load(pl_face *f, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, 0, SEEK_SET);
    f->data = malloc((size_t)n);
    if (!f->data || fread(f->data, 1, (size_t)n, fp) != (size_t)n) {
        free(f->data); f->data = NULL; fclose(fp); return 0;
    }
    fclose(fp);
    if (!stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0))) {
        free(f->data); f->data = NULL; return 0;
    }
    f->loaded = 1;
    return 1;
}

void pl_face_free(pl_face *f) { free(f->data); f->data = NULL; f->loaded = 0; }

void pl_ui_present(pl_ui *ui, pl_refresh how) { if (ui->present) ui->present(ui, how); }

/* ---- primitives -------------------------------------------------------- */

static inline void put(pl_ui *ui, int x, int y, uint8_t g) {
    if (x < 0 || y < 0 || x >= ui->w || y >= ui->h) return;
    ui->px[(size_t)y * ui->stride + x] = g;
}
/* Alpha-blend coverage `a` (0..255) of `grey` over the existing pixel. */
static inline void blend(pl_ui *ui, int x, int y, uint8_t grey, int a) {
    if (a <= 0 || x < 0 || y < 0 || x >= ui->w || y >= ui->h) return;
    size_t i = (size_t)y * ui->stride + x;
    int dst = ui->px[i];
    ui->px[i] = (uint8_t)((grey * a + dst * (255 - a)) / 255);
}

void pl_ui_clear(pl_ui *ui, uint8_t grey) {
    for (int y = 0; y < ui->h; y++)
        memset(ui->px + (size_t)y * ui->stride, grey, (size_t)ui->w);
}

void pl_ui_fill(pl_ui *ui, pl_rect r, uint8_t grey) {
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++) put(ui, x, y, grey);
}

void pl_ui_hline(pl_ui *ui, int x, int y, int w, uint8_t grey) {
    for (int i = 0; i < w; i++) put(ui, x + i, y, grey);
}

void pl_ui_rect(pl_ui *ui, pl_rect r, int t, uint8_t grey) {
    for (int i = 0; i < t; i++) {
        pl_ui_hline(ui, r.x, r.y + i, r.w, grey);
        pl_ui_hline(ui, r.x, r.y + r.h - 1 - i, r.w, grey);
        for (int y = r.y; y < r.y + r.h; y++) {
            put(ui, r.x + i, y, grey);
            put(ui, r.x + r.w - 1 - i, y, grey);
        }
    }
}

void pl_ui_round_rect(pl_ui *ui, pl_rect r, int rad, int t, uint8_t grey, int filled) {
    /* Anti-aliased corners: on e-ink at 300ppi a jagged bubble edge is visible. */
    for (int y = 0; y < r.h; y++) {
        for (int x = 0; x < r.w; x++) {
            int dx = 0, dy = 0;
            if (x < rad)            dx = rad - x;
            else if (x >= r.w - rad) dx = x - (r.w - rad - 1);
            if (y < rad)            dy = rad - y;
            else if (y >= r.h - rad) dy = y - (r.h - rad - 1);

            double d = (dx && dy) ? (rad - __builtin_sqrt((double)(dx*dx + dy*dy))) : 1e9;
            int inside = (d > 0.5);
            int edge_a = 0;
            if (d < 1e8) {
                if (d <= -0.5) continue;                 /* outside the corner */
                if (d < 0.5) edge_a = (int)((d + 0.5) * 255);
            }

            if (filled) {
                if (inside) put(ui, r.x + x, r.y + y, grey);
                else if (edge_a) blend(ui, r.x + x, r.y + y, grey, edge_a);
            } else {
                int on_border = (x < t || y < t || x >= r.w - t || y >= r.h - t) ||
                                (d < 1e8 && d < t + 0.5);
                if (on_border && (inside || edge_a))
                    blend(ui, r.x + x, r.y + y, grey, edge_a ? edge_a : 255);
            }
        }
    }
}

/* ---- text -------------------------------------------------------------- */

/* Minimal UTF-8 decode; returns codepoint and advances *i. */
static int next_cp(const char *s, size_t *i) {
    unsigned char c = (unsigned char)s[*i];
    if (c < 0x80) { (*i)++; return c; }
    if ((c & 0xE0) == 0xC0) { int cp = ((c&0x1F)<<6)|(s[*i+1]&0x3F); *i += 2; return cp; }
    if ((c & 0xF0) == 0xE0) { int cp = ((c&0x0F)<<12)|((s[*i+1]&0x3F)<<6)|(s[*i+2]&0x3F); *i += 3; return cp; }
    if ((c & 0xF8) == 0xF0) { int cp = ((c&0x07)<<18)|((s[*i+1]&0x3F)<<12)|((s[*i+2]&0x3F)<<6)|(s[*i+3]&0x3F); *i += 4; return cp; }
    (*i)++; return '?';
}

int pl_ui_text(pl_ui *ui, pl_font fi, int size, int x, int y,
               const char *utf8, uint8_t grey) {
    pl_face *f = &ui->fonts[fi];
    if (!f->loaded || !utf8) return 0;
    float scale = stbtt_ScaleForPixelHeight(&f->info, (float)size);

    size_t i = 0; float penf = (float)x; int prev = 0;
    while (utf8[i]) {
        int cp = next_cp(utf8, &i);
        if (prev) penf += stbtt_GetCodepointKernAdvance(&f->info, prev, cp) * scale;
        int pen = (int)(penf + 0.5f);

        int aw, lsb;
        stbtt_GetCodepointHMetrics(&f->info, cp, &aw, &lsb);
        if (cp != ' ') {
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&f->info, cp, scale, scale, &x0, &y0, &x1, &y1);
            int gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0) {
                unsigned char *bmp = malloc((size_t)gw * gh);
                stbtt_MakeCodepointBitmap(&f->info, bmp, gw, gh, gw, scale, scale, cp);
                for (int gy = 0; gy < gh; gy++)
                    for (int gx = 0; gx < gw; gx++)
                        /* x0 from GetCodepointBitmapBox already includes lsb --
                         * adding it again shifts glyphs right and swallows spaces. */
                        blend(ui, pen + x0 + gx, y + y0 + gy,
                              grey, bmp[(size_t)gy*gw + gx]);
                free(bmp);
            }
        }
        penf += aw * scale;
        prev = cp;
    }
    return (int)(penf + 0.5f) - x;
}

int pl_ui_text_width(pl_ui *ui, pl_font fi, int size, const char *utf8) {
    pl_face *f = &ui->fonts[fi];
    if (!f->loaded || !utf8) return 0;
    float scale = stbtt_ScaleForPixelHeight(&f->info, (float)size);
    size_t i = 0; float w = 0; int prev = 0;
    while (utf8[i]) {
        int cp = next_cp(utf8, &i), aw, lsb;
        if (prev) w += stbtt_GetCodepointKernAdvance(&f->info, prev, cp) * scale;
        stbtt_GetCodepointHMetrics(&f->info, cp, &aw, &lsb);
        w += aw * scale;
        prev = cp;
    }
    return (int)(w + 0.5f);
}

int pl_ui_text_box(pl_ui *ui, pl_font fi, int size, pl_rect box, int lh,
                   pl_align align, const char *utf8, uint8_t grey, int draw) {
    if (!utf8) return box.y;
    int y = box.y;
    const char *p = utf8;

    while (*p) {
        /* Greedily fit words, breaking at the last space that fits. */
        const char *line_start = p, *last_fit = NULL, *scan = p;
        char buf[1024];
        while (*scan) {
            while (*scan == ' ') scan++;
            const char *word_end = scan;
            while (*word_end && *word_end != ' ' && *word_end != '\n') word_end++;
            size_t len = (size_t)(word_end - line_start);
            if (len >= sizeof buf) break;
            memcpy(buf, line_start, len); buf[len] = 0;
            if (pl_ui_text_width(ui, fi, size, buf) > box.w && last_fit) break;
            last_fit = word_end;
            scan = word_end;
            if (*scan == '\n') break;
        }
        if (!last_fit) last_fit = scan;

        size_t len = (size_t)(last_fit - line_start);
        if (len >= sizeof buf) len = sizeof buf - 1;
        memcpy(buf, line_start, len); buf[len] = 0;

        if (draw) {
            int w = pl_ui_text_width(ui, fi, size, buf);
            int x = box.x;
            if (align == PL_ALIGN_CENTER) x = box.x + (box.w - w) / 2;
            else if (align == PL_ALIGN_RIGHT) x = box.x + box.w - w;
            pl_ui_text(ui, fi, size, x, y + size, buf, grey);
        }
        y += lh;

        p = last_fit;
        while (*p == ' ' || *p == '\n') p++;
        if (box.h > 0 && y + lh > box.y + box.h) break;
    }
    return y;
}


/* Re-walks the same greedy wrap as pl_ui_text_box and reports only the last
 * line. Duplicating the walk is deliberate: threading an out-param through the
 * drawing path would complicate the common case for one caller's benefit. */
int pl_ui_text_box_tail(pl_ui *ui, pl_font fi, int size, pl_rect box, int lh,
                        const char *utf8, int *out_y) {
    if (!utf8) { if (out_y) *out_y = box.y; return 0; }
    int y = box.y, last_w = 0;
    const char *p = utf8;

    while (*p) {
        const char *line_start = p, *last_fit = NULL, *scan = p;
        char buf[1024];
        while (*scan) {
            while (*scan == ' ') scan++;
            const char *word_end = scan;
            while (*word_end && *word_end != ' ' && *word_end != '\n') word_end++;
            size_t len = (size_t)(word_end - line_start);
            if (len >= sizeof buf) break;
            memcpy(buf, line_start, len); buf[len] = 0;
            if (pl_ui_text_width(ui, fi, size, buf) > box.w && last_fit) break;
            last_fit = word_end;
            scan = word_end;
            if (*scan == '\n') break;
        }
        if (!last_fit) last_fit = scan;

        size_t len = (size_t)(last_fit - line_start);
        if (len >= sizeof buf) len = sizeof buf - 1;
        memcpy(buf, line_start, len); buf[len] = 0;
        last_w = pl_ui_text_width(ui, fi, size, buf);

        if (out_y) *out_y = y + size;
        y += lh;
        p = last_fit;
        while (*p == ' ' || *p == '\n') p++;
        if (box.h > 0 && y + lh > box.y + box.h) break;
    }
    return last_w;
}

/* For diagnostics: hand back the visible buffer so a caller can save it. */
uint8_t *pl_ui_pixels(pl_ui *ui, int *w, int *h, int *stride) {
    if (!ui) return 0;
    if (w) *w = ui->w;
    if (h) *h = ui->h;
    if (stride) *stride = ui->stride;
    return ui->px;
}

void pl_ui_tap_flash(pl_ui *ui, int x, int y) {
    if (!ui) return;
    /* ~4mm at 300ppi: big enough to see under a fingertip, small enough not
     * to obscure whatever is being tapped. */
    const int R = 46;
    pl_rect r = { x - R, y - R, R * 2, R * 2 };
    pl_ui_round_rect(ui, r, R, 0, 0x0D, 1);
    pl_ui_present(ui, PL_REFRESH_FAST);
}
