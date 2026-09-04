/* platform/kindle/ui_fb.c — pl_ui backed by the real framebuffer.
 *
 * Same interface, same drawing code (platform/draw.c) as the host backend, so
 * a screen written once produces identical pixels here and in the PNGs we look
 * at on the Mac. The only thing this file adds is: where the pixels live, and
 * what "present" means on e-ink.
 */
#include "../ui_internal.h"
#include "mtk_eink.h"
#include "../devroot.h"

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Optional logger, set by the app. There is no console on a Kindle, so
 * anything this layer discovers has to be written down or it is lost. */
void (*g_fb_log)(const char *fmt, ...) = 0;

typedef struct {
    int      fd;
    void    *map;
    size_t   map_len;
    size_t   pan_offset;   /* which half of a double-buffered panel is visible */
    uint32_t marker;
    int      partials_since_full;   /* ghosting accumulates; count them */
    int      simulated;             /* fb0 is a plain file: skip the ioctls */
} fb_state;

/* E-ink truth: a partial update is fast but leaves a faint trace of what was
 * there before. Streaming an answer means many partials in a row, so we force
 * a full flash periodically. Chosen empirically-ish; tune on device. */
#define FULL_REFRESH_EVERY 12

static void fb_present(pl_ui *ui, pl_refresh how) {
    fb_state *st = ui->backend;
    if (how == PL_REFRESH_NONE) return;

    if (how == PL_REFRESH_FAST && ++st->partials_since_full >= FULL_REFRESH_EVERY)
        how = PL_REFRESH_FULL;
    if (how == PL_REFRESH_FULL) st->partials_since_full = 0;

    if (st->simulated) return;   /* nothing to submit to; the file IS the output */

    struct mxcfb_update_data_mtk u;
    memset(&u, 0, sizeof u);
    u.update_region.top    = 0;
    u.update_region.left   = 0;
    u.update_region.width  = (uint32_t)ui->w;
    u.update_region.height = (uint32_t)ui->h;
    u.update_marker        = ++st->marker;
    u.temp                 = 0x18;          /* room temperature; driver default */

    if (how == PL_REFRESH_FULL) {
        /* Full flash: inverts the panel once, which is what actually clears
         * accumulated ghosting. Costs ~500ms and is visibly disruptive, so it
         * is reserved for screen changes. */
        u.waveform_mode = MTK_WAVEFORM_MODE_GC16;
        u.update_mode   = UPDATE_MODE_FULL;
    } else {
        /* GL16 rather than DU: DU is 2-level, so antialiased type turns to
         * mush. GL16 keeps greys and is still fast enough to stream into. */
        u.waveform_mode = MTK_WAVEFORM_MODE_GL16;
        u.update_mode   = UPDATE_MODE_PARTIAL;
    }

    if (ioctl(st->fd, MXCFB_SEND_UPDATE_MTK, &u) < 0) return;

    /* Wait for a full refresh to land so the next draw doesn't race it. Partial
     * updates are deliberately fire-and-forget: waiting on every one would make
     * streaming text stutter. */
    if (how == PL_REFRESH_FULL) {
        struct mxcfb_update_marker_data m = { u.update_marker, 0 };
        ioctl(st->fd, MXCFB_WAIT_FOR_UPDATE_COMPLETE_MTK, &m);
    }
}

pl_ui *pl_ui_fb_create(const char *serif_ttf, const char *sans_ttf) {
    char pb[512];
    const char *dev = pl_path("/dev/fb0", pb, sizeof pb);

    int fd = open(dev, O_RDWR);
    if (fd < 0) { fprintf(stderr, "fb: cannot open %s (%s)\n", dev, strerror(errno)); return NULL; }

    pl_ui *ui = calloc(1, sizeof *ui);
    fb_state *st = calloc(1, sizeof *st);
    if (!ui || !st) { free(ui); free(st); close(fd); return NULL; }
    st->fd = fd;
    st->simulated = pl_is_simulated();

    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    if (!st->simulated &&
        (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0)) {
        fprintf(stderr, "fb: screeninfo ioctls failed (%s)\n", strerror(errno));
        free(ui); free(st); close(fd); return NULL;
    }

    if (st->simulated) {
        /* Simulator: /dev/fb0 is a regular file of the right size, so take the
         * geometry we already measured off the device. */
        ui->w = PL_SCREEN_W; ui->h = PL_SCREEN_H; ui->stride = PL_SCREEN_W;
        st->map_len = (size_t)ui->stride * ui->h;
    } else {
        ui->w      = (int)var.xres;
        ui->h      = (int)var.yres;
        /* Layouts read these, so the app fits whatever panel it woke up on. */
        pl_screen_w = ui->w;
        pl_screen_h = ui->h;
        ui->stride = (int)fix.line_length;
        st->map_len = fix.smem_len;
        /* The panel is double-buffered: yres_virtual is twice yres. Which half
         * is actually on screen is var.yoffset, and drawing at offset 0 while
         * the second page is visible means drawing into a buffer nobody sees.
         * This was the "renders weirdly" bug. */
        st->pan_offset = (size_t)var.yoffset * fix.line_length + var.xoffset;
        if (g_fb_log)
            g_fb_log("fb: %ux%u visible, %ux%u virtual, %ubpp, grayscale=%u, "
                     "rotate=%u, stride=%u, yoffset=%u -> byte offset %zu",
                     var.xres, var.yres, var.xres_virtual, var.yres_virtual,
                     var.bits_per_pixel, var.grayscale, var.rotate,
                     fix.line_length, var.yoffset, st->pan_offset);

        if (var.bits_per_pixel != 8) {
            /* Measured 8bpp grayscale on the 12th gen. Anything else means an
             * assumption is wrong, and drawing blind would corrupt the panel. */
            fprintf(stderr, "fb: expected 8bpp, got %u\n", var.bits_per_pixel);
            free(ui); free(st); close(fd); return NULL;
        }
    }

    st->map = mmap(NULL, st->map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (st->map == MAP_FAILED) {
        fprintf(stderr, "fb: mmap failed (%s)\n", strerror(errno));
        free(ui); free(st); close(fd); return NULL;
    }

    ui->px      = (uint8_t *)st->map + st->pan_offset;
    ui->backend = st;
    ui->present = fb_present;

    pl_face_load(&ui->fonts[PL_FONT_SERIF],        serif_ttf);
    pl_face_load(&ui->fonts[PL_FONT_SERIF_ITALIC], serif_ttf);
    pl_face_load(&ui->fonts[PL_FONT_SANS],         sans_ttf);
    pl_face_load(&ui->fonts[PL_FONT_SANS_BOLD],    sans_ttf);
    return ui;
}

void pl_ui_destroy(pl_ui *ui) {
    if (!ui) return;
    fb_state *st = ui->backend;
    for (int i = 0; i < PL_FONT__COUNT; i++) pl_face_free(&ui->fonts[i]);
    if (st) {
        if (st->map && st->map != MAP_FAILED) munmap(st->map, st->map_len);
        if (st->fd >= 0) close(st->fd);
        free(st);
    }
    free(ui);
}
