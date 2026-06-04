/*
 * handler-mermaid.c — Mermaid diagram → ydraw buffer.
 *
 * Thin wrapper around yetty_ydiagram_render_mermaid. ycat doesn't carry
 * a font_id-aware measure callback (the canvas's default font measures
 * at receive time), so we use the convenience entry without one — node
 * boxes will fall back to the 0.6 × font_size × char_count heuristic.
 * A future change should plumb the configured font through ycat_config
 * so this handler can pass a real measure_text in.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/ydiagram/ydiagram.h>

struct yetty_ydraw_drawable_list_result yetty_ycat_handler_mermaid(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config)
{
    (void)path_hint;
    (void)config;

    /* ycat is cat-like: the diagram must flow inline at the cursor, not clear
     * the pane and jump to the origin. Render with clear_canvas = false (the
     * standalone ydiagram tool and ygui widgets keep the full-redraw default). */
    struct yetty_ydiagram_render_options opts = yetty_ydiagram_default_render_options();
    opts.clear_canvas = false;

    struct yetty_ydiagram_buffer_result r =
        yetty_ydiagram_render_mermaid_full((const char *)bytes, len, NULL, &opts, NULL, NULL);
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ydraw_drawable_list, r.error.msg);
    }
    return YETTY_OK(yetty_ydraw_drawable_list, r.value);
}
