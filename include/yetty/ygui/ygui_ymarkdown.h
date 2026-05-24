#ifndef YETTY_YGUI_YGUI_YMARKDOWN_H
#define YETTY_YGUI_YGUI_YMARKDOWN_H

/*
 * ygui_ymarkdown — Markdown widget for ygui.
 *
 * Builds a draw_list with ymarkdown and hands it to a RICH-style widget
 * that translates every primitive by its resolved (x, y) at render
 * time. The widget owns the produced buffer.
 *
 * Widget (w, h) defines the rendering pane size; the markdown renderer
 * derives cell-grid metrics from it.
 *
 * NULL is returned on failure (file missing, OOM).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_engine;
struct yetty_ygui_widget;

struct yetty_ygui_widget *yetty_ygui_engine_ymarkdown_from_file(struct yetty_ygui_engine *engine,
                                                                const char *id, float x, float y,
                                                                float w, float h, const char *path);

struct yetty_ygui_widget *yetty_ygui_engine_ymarkdown_from_buffer(struct yetty_ygui_engine *engine,
                                                                  const char *id, float x, float y,
                                                                  float w, float h,
                                                                  const uint8_t *data, size_t len);

/* Build a widget rendering the default markdown bundled into ygui_ymarkdown
 * itself (the repo's README.md, embedded at library-build time). Lets
 * tools render a sample without shipping a separate .md file. Returns
 * NULL on failure or when the library was built without an asset. */
struct yetty_ygui_widget *yetty_ygui_engine_ymarkdown_default(struct yetty_ygui_engine *engine,
                                                              const char *id, float x, float y,
                                                              float w, float h);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_YGUI_YMARKDOWN_H */
