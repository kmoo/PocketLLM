/* platform/devroot.h — device path indirection.
 *
 * Every device path the app touches goes through pl_path(). On the Kindle
 * POCKETLLM_DEV_ROOT is unset and paths pass through unchanged. Under the
 * simulator it points at a fake device tree, so the *same ARM binary* we ship
 * can be exercised on this Mac against a fake /dev/fb0, /proc/meminfo and
 * /mnt/us.
 *
 * Why not LD_PRELOAD (which is how qemu-kindle does it): LD_PRELOAD needs a
 * dynamic loader, and our binary is static on purpose. Path indirection is the
 * approach that survives static linking -- and it is explicit and testable
 * rather than magic.
 */
#ifndef PL_DEVROOT_H
#define PL_DEVROOT_H

#include <stddef.h>

/* Resolve an absolute device path against POCKETLLM_DEV_ROOT (if set).
 * Writes into `buf` and returns it; returns `abs` unchanged when no root is
 * configured. Never fails; on overflow it falls back to `abs`. */
const char *pl_path(const char *abs, char *buf, size_t buflen);

/* 1 when running against a simulated device tree. */
int pl_is_simulated(void);

#endif
