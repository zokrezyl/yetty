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

struct yetty_ydraw_draw_list_result yetty_ycat_handler_mermaid(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config)
{
    (void)path_hint;
    (void)config;

    struct yetty_ydiagram_buffer_result r = yetty_ydiagram_render_mermaid((const char *)bytes, len);
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ydraw_draw_list, r.error.msg);
    }
    return YETTY_OK(yetty_ydraw_draw_list, r.value);
}
