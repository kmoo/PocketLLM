/* platform/kindle/mtk_eink.h — the MTK e-ink kernel ABI, minimal subset.
 *
 * The 12th-gen Paperwhite's controller reports itself as "hwtcon_v2" (confirmed
 * by our own probe: fix.id == "hwtcon_v2"). It is MediaTek's, not Freescale's
 * mxcfb, so the classic Kindle einkfb/mxcfb ioctls do not apply.
 *
 * PROVENANCE, because it matters. Everything below is kernel ABI: ioctl
 * request codes and the struct layouts they take. It is the shape of an
 * interface, not an implementation -- there is no algorithm here and no way to
 * express any of it differently and still talk to the driver.
 *
 * The originating source is the Kindle's own Linux kernel, which Amazon
 * publishes under GPL-2.0; Linux UAPI headers carry the Linux-syscall-note
 * exception, which exists precisely so that userspace programs of any licence
 * may use them. This subset was written with FBInk's eink/mtk-kindle.h
 * (NiLuJe, AGPL-3.0) open alongside it, which reconstructed the same
 * declarations from those kernel sources. No FBInk code is used, copied or
 * linked -- only the ABI it and we both describe. See THIRD-PARTY.md.
 *
 * Only what we actually call is kept.
 *
 * Writing pixels into the mmap'd framebuffer changes nothing on screen by
 * itself: an update must be submitted for the panel to render it.
 */
#ifndef PL_MTK_EINK_H
#define PL_MTK_EINK_H

#include <linux/ioctl.h>
#include <stdint.h>

#define HWTCON_IOCTL_MAGIC_NUMBER 'F'

/* Waveform modes. The choice trades speed against fidelity and ghosting:
 *   DU    2-level, fastest, leaves artefacts behind    -> streaming text
 *   GL16  16-level, tuned for text on white            -> normal redraw
 *   GC16  16-level, full flash, clears ghosting        -> new screen
 *   A2    2-level, fastest of all, very lossy          -> transient UI
 */
enum {
    MTK_WAVEFORM_MODE_INIT = 0,
    MTK_WAVEFORM_MODE_DU   = 1,
    MTK_WAVEFORM_MODE_GC16 = 2,
    MTK_WAVEFORM_MODE_GL16 = 3,
    MTK_WAVEFORM_MODE_A2   = 6,
    MTK_WAVEFORM_MODE_AUTO = 257
};

#define UPDATE_MODE_PARTIAL 0x0
#define UPDATE_MODE_FULL    0x1

struct mxcfb_rect { uint32_t top, left, width, height; };

struct mxcfb_alt_buffer_data {
    uint32_t phys_addr, width, height;
    struct mxcfb_rect alt_update_region;
};

struct mxcfb_swipe_data { uint32_t direction, steps; };

/* Field order matters: this is passed straight to the driver. */
struct mxcfb_update_data_mtk {
    struct mxcfb_rect            update_region;
    uint32_t                     waveform_mode;
    uint32_t                     update_mode;
    uint32_t                     update_marker;
    int                          temp;
    unsigned int                 flags;
    int                          dither_mode;
    int                          quant_bit;
    struct mxcfb_alt_buffer_data alt_buffer_data;
    struct mxcfb_swipe_data      swipe_data;
    uint32_t                     hist_bw_waveform_mode;
    uint32_t                     hist_gray_waveform_mode;
    uint32_t                     ts_pxp;
    uint32_t                     ts_epdc;
};

struct mxcfb_update_marker_data { uint32_t update_marker, collision_test; };

#define MXCFB_SEND_UPDATE_MTK \
    _IOW(HWTCON_IOCTL_MAGIC_NUMBER, 0x2E, struct mxcfb_update_data_mtk)
#define MXCFB_WAIT_FOR_UPDATE_COMPLETE_MTK \
    _IOWR(HWTCON_IOCTL_MAGIC_NUMBER, 0x2F, struct mxcfb_update_marker_data)

#endif
