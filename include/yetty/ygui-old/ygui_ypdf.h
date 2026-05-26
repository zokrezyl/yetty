#ifndef YETTY_YGUI_OLD_YGUI_YPDF_H
#define YETTY_YGUI_OLD_YGUI_YPDF_H

/*
 * ygui_ypdf — PDF viewer widget for ygui.
 *
 * The widget owns N per-page ydraw-core sub-buffers (one per PDF page,
 * built via yetty_ypdf_render_pdf_streaming) plus a font-header buffer
 * that carries the full FONT prims referenced anywhere in the document.
 *
 * On every render the widget emits:
 *   1. the font header (so any visible-page TEXT_SPAN resolves its
 *      font_id even when the page that originally introduced the FONT
 *      is currently scrolled off-screen);
 *   2. the primitives of pages whose [abs_y, abs_y + h] band overlaps
 *      the viewport [scroll_y, scroll_y + widget_h], translated by
 *      (widget_origin + page_abs_y - scroll_y).
 *
 * Pages outside the viewport are skipped entirely — emission cost is
 * O(visible pages), not O(document pages). A 200-page document only
 * ships ~1–2 pages' worth of prims per frame.
 *
 * Wheel events are absorbed by the widget's on_scroll vtable handler.
 * For keyboard / scrollbar navigation, use the scroll API below.
 *
 * Two constructors (file path / raw buffer); pdfio only opens by
 * filename, so the buffer variant writes to mkstemp + unlinks.
 *
 * NULL is returned on any failure.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_old_engine;
struct yetty_ygui_old_widget;

struct yetty_ygui_old_widget *yetty_ygui_old_engine_ypdf_from_file(
    struct yetty_ygui_old_engine *engine, const char *id, float x, float y, float w, float h,
    const char *path);

struct yetty_ygui_old_widget *yetty_ygui_old_engine_ypdf_from_buffer(
    struct yetty_ygui_old_engine *engine, const char *id, float x, float y, float w, float h,
    const uint8_t *data, size_t len);

/* Default sample PDF embedded into ygui_ypdf. Returns NULL on failure
 * or when the library was built without an asset. */
struct yetty_ygui_old_widget *yetty_ygui_old_engine_ypdf_default(
    struct yetty_ygui_old_engine *engine, const char *id, float x, float y, float w, float h);

/*=============================================================================
 * Scroll API
 *
 * All measurements are in document pixels (the same unit ypdf uses for
 * page sizes). scroll_y of 0 puts the document top flush with the
 * widget top; scroll_y of `content_height - viewport_height` puts the
 * document bottom flush with the widget bottom (the widget clamps to
 * this range).
 *===========================================================================*/

/* Set absolute scroll position. Clamped to [0, max_scroll]. */
void yetty_ygui_old_widget_ypdf_scroll_to(struct yetty_ygui_old_widget *widget, float y);

/* Add `dy` to the current scroll position (positive = scroll DOWN). */
void yetty_ygui_old_widget_ypdf_scroll_by(struct yetty_ygui_old_widget *widget, float dy);

/* Current scroll position. */
float yetty_ygui_old_widget_ypdf_get_scroll(const struct yetty_ygui_old_widget *widget);

/* Document height (sum of page heights + inter-page gaps). */
float yetty_ygui_old_widget_ypdf_content_height(const struct yetty_ygui_old_widget *widget);

/* Live widget viewport height (post-layout). 0 until the first layout pass. */
float yetty_ygui_old_widget_ypdf_viewport_height(const struct yetty_ygui_old_widget *widget);

/* max_scroll = max(0, content_height - viewport_height). Useful for
 * driving a scrollbar (value = scroll_y / max_scroll, when max > 0). */
float yetty_ygui_old_widget_ypdf_max_scroll(const struct yetty_ygui_old_widget *widget);

/* Number of pages in the document. */
int yetty_ygui_old_widget_ypdf_page_count(const struct yetty_ygui_old_widget *widget);

typedef void (*yetty_ygui_old_ypdf_scroll_change_fn)(struct yetty_ygui_old_widget *widget,
                                                     float scroll_y, float max_scroll,
                                                     void *userdata);

/* Fired AFTER scroll_y mutates (via wheel, the scroll API above, or a
 * future on_key handler). A bound scrollbar typically translates
 * (scroll_y / max_scroll) into its 0..1 value. */
void yetty_ygui_old_widget_ypdf_on_scroll_change(struct yetty_ygui_old_widget *widget,
                                                 yetty_ygui_old_ypdf_scroll_change_fn cb,
                                                 void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_OLD_YGUI_YPDF_H */
