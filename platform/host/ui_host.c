/* platform/host/ui_host.c — pl_ui backed by an in-memory greyscale buffer,
 * written out as a PNG at true device size.
 *
 * Same interface the Kindle backend implements, so a screen written once
 * renders identically in both places. This is what makes UI work possible
 * without the device in hand.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ui_internal.h"

typedef struct { char out_path[512]; } host_state;
static void host_present(pl_ui *ui, pl_refresh how);

pl_ui *pl_ui_host_create(const char *serif_ttf, const char *sans_ttf,
                         const char *out_path) {
    pl_ui *ui = calloc(1, sizeof *ui);
    if (!ui) return NULL;
    ui->w = PL_SCREEN_W; ui->h = PL_SCREEN_H; ui->stride = PL_SCREEN_W;
    ui->px = malloc((size_t)ui->stride * ui->h);
    host_state *st = calloc(1, sizeof *st);
    if (!ui->px || !st) { free(ui->px); free(st); free(ui); return NULL; }
    memset(ui->px, 0xFF, (size_t)ui->stride * ui->h);
    snprintf(st->out_path, sizeof st->out_path, "%s", out_path ? out_path : "out/screen");
    ui->backend = st;
    ui->present = host_present;

    /* Italic and bold come from the same variable font files for now; the real
     * app will carry static instances. Good enough to lay screens out. */
    pl_face_load(&ui->fonts[PL_FONT_SERIF],        serif_ttf);
    pl_face_load(&ui->fonts[PL_FONT_SERIF_ITALIC], serif_ttf);
    pl_face_load(&ui->fonts[PL_FONT_SANS],         sans_ttf);
    pl_face_load(&ui->fonts[PL_FONT_SANS_BOLD],    sans_ttf);
    return ui;
}

void pl_ui_destroy(pl_ui *ui) {
    if (!ui) return;
    for (int i = 0; i < PL_FONT__COUNT; i++) pl_face_free(&ui->fonts[i]);
    free(ui->backend); free(ui->px); free(ui);
}

/* ---- PNG output --------------------------------------------------------
 * Written by hand rather than pulled in as a dependency. We only have an
 * inflater, not a deflater, so IDAT uses stored (uncompressed) deflate
 * blocks -- valid zlib, and file size does not matter for dev screenshots.
 * CRC-32 is the standard reflected polynomial, computed on the fly rather
 * than from a table -- a screenshot is not a hot path. */
static uint32_t pl_crc32(const unsigned char *d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1)));
    }
    return c ^ 0xFFFFFFFFu;
}

static void be32(unsigned char *p, uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}

static void chunk(FILE *f, const char *tag, const unsigned char *data, size_t len) {
    unsigned char hdr[8];
    be32(hdr, (uint32_t)len);
    memcpy(hdr + 4, tag, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);

    unsigned char *crcbuf = malloc(len + 4);
    memcpy(crcbuf, tag, 4);
    if (len) memcpy(crcbuf + 4, data, len);
    unsigned char c[4];
    be32(c, pl_crc32(crcbuf, len + 4));
    fwrite(c, 1, 4, f);
    free(crcbuf);
}

static void write_png(pl_ui *ui, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);

    unsigned char ihdr[13];
    be32(ihdr, (uint32_t)ui->w); be32(ihdr + 4, (uint32_t)ui->h);
    ihdr[8] = 8;    /* bit depth  */
    ihdr[9] = 0;    /* greyscale  */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, sizeof ihdr);

    /* Raw scanlines, each prefixed with filter type 0. */
    size_t raw_len = (size_t)(ui->w + 1) * ui->h;
    unsigned char *raw = malloc(raw_len);
    for (int y = 0; y < ui->h; y++) {
        raw[(size_t)y * (ui->w + 1)] = 0;
        memcpy(raw + (size_t)y * (ui->w + 1) + 1, ui->px + (size_t)y * ui->stride, (size_t)ui->w);
    }

    /* zlib stream: header, stored deflate blocks, adler32. */
    size_t cap = raw_len + (raw_len / 65535 + 1) * 5 + 6;
    unsigned char *z = malloc(cap);
    size_t zn = 0;
    z[zn++] = 0x78; z[zn++] = 0x01;
    size_t off = 0;
    while (off < raw_len) {
        size_t n = raw_len - off; if (n > 65535) n = 65535;
        int final = (off + n >= raw_len);
        z[zn++] = (unsigned char)(final ? 1 : 0);
        z[zn++] = (unsigned char)(n & 0xFF);        z[zn++] = (unsigned char)(n >> 8);
        z[zn++] = (unsigned char)(~n & 0xFF);       z[zn++] = (unsigned char)((~n >> 8) & 0xFF);
        memcpy(z + zn, raw + off, n); zn += n; off += n;
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_len; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    be32(z + zn, (b << 16) | a); zn += 4;

    chunk(f, "IDAT", z, zn);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw); free(z);
}

static void host_present(pl_ui *ui, pl_refresh how) {
    (void)how;   /* no panel here; the refresh mode is exercised on device */
    char path[600];
    snprintf(path, sizeof path, "%s.png", ((host_state *)ui->backend)->out_path);
    write_png(ui, path);
}

/* Present under an explicit name, for rendering a set of screens in one run. */
void pl_ui_host_save(pl_ui *ui, const char *name) {
    char path[700];
    snprintf(path, sizeof path, "%s-%s.png", ((host_state *)ui->backend)->out_path, name);
    write_png(ui, path);
}
