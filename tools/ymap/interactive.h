#ifndef YMAP_INTERACTIVE_H
#define YMAP_INTERACTIVE_H

/* Interactive map session (see interactive.c). Drives a configured
 * `ymap:map` object — Ctrl-Shift-wheel zoom, drag pan, +/- keys — and
 * ships its renders to a yview figure. Returns a process exit code.
 * The object is borrowed; the caller destroys it after this returns. */

#include <stdint.h>

struct yetty_yclass_object;

int ymap_interactive_run(struct yetty_yclass_object *map_object, uint32_t width_px,
                         uint32_t height_px);

#endif /* YMAP_INTERACTIVE_H */
