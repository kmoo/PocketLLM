/* platform/ui_internal.h — shared by both backends, private to platform/.
 *
 * All drawing happens in platform/draw.c against this struct, so the host and
 * Kindle backends produce byte-identical pixels. A backend only supplies the
 * buffer and decides what present() means.
 */
#ifndef PL_UI_INTERNAL_H
#define PL_UI_INTERNAL_H

#include "ui.h"
#include "../vendor/stb/stb_truetype.h"

typedef struct {
    unsigned char  *data;
    stbtt_fontinfo  info;
    int             loaded;
} pl_face;

struct pl_ui {
    uint8_t *px;         /* w*h bytes, 8bpp grey; may be an mmap of /dev/fb0 */
    int      w, h;
    int      stride;     /* bytes per row; equals w on host, line_length on device */
    pl_face  fonts[PL_FONT__COUNT];

    void   (*present)(struct pl_ui *, pl_refresh);
    void    *backend;    /* backend-private state */
};

int  pl_face_load(pl_face *f, const char *path);
void pl_face_free(pl_face *f);

#endif
