#ifndef YETTY_YREADME_YREADME_H
#define YETTY_YREADME_YREADME_H

/*
 * yreadme — render README-style content (Markdown today) into a ydraw
 * buffer. A thin convenience layer over ymarkdown that adds:
 *
 *   - file-path entry point (yreadme_render_from_file) — slurps the
 *     file and forwards to the buffer-based render.
 *   - default render config sized for a typical pane (no caller-supplied
 *     cell grid required); callers may still pass an explicit config.
 *
 * The caller owns the returned buffer and frees it via
 * yetty_ydraw_draw_list_destroy.
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ymarkdown/ymarkdown.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a config tuned for a `pane_w x pane_h` pixel surface. The
 * default cell metrics (8x16) are an approximation; ymarkdown derives
 * its font size from cell_height. */
struct yetty_ymarkdown_render_config yetty_yreadme_default_config(float pane_w, float pane_h);

/* Render markdown bytes. Equivalent to yetty_ymarkdown_render with NULL
 * args. `config` may be NULL — defaults to yreadme_default_config(800, 600). */
struct yetty_ymarkdown_render_result yetty_yreadme_render_from_buffer(
    const uint8_t *data, size_t len,
    const struct yetty_ymarkdown_render_config *config);

/* Slurp `path` and render. Returns the same result shape as the buffer
 * variant; error is populated when the file cannot be opened or read. */
struct yetty_ymarkdown_render_result yetty_yreadme_render_from_file(
    const char *path, const struct yetty_ymarkdown_render_config *config);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YREADME_YREADME_H */
