/*
 * handler-markdown.c - markdown → ydraw envelope.
 *
 * Renders the whole document into a single ydraw draw list and ships it
 * as one envelope — the same path ygreeter drives via the ymarkdown ygui
 * widget.
 *
 * It deliberately does NOT use ymarkdown's screen-height streaming: that
 * mode resets each chunk to envelope-local y and relies on the receiver
 * scrolling by the envelope's declared scene height. The terminal's
 * scrolling-canvas receiver does not do that — it stacks envelopes by
 * glyph-content rows (and lands the cursor on the last occupied row), so
 * every chunk boundary dropped the trailing inter-block spacing and
 * overlapped the previous chunk's last line onto the next chunk's first
 * block (mangling headers / tables). One envelope has no boundaries.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/ymarkdown/ymarkdown.h>

struct yetty_ycore_void_result yetty_ycat_handler_markdown_streaming(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config,
    yetty_ycat_emit_fn emit, void *emit_user_data)
{
    (void)path_hint;

    if (!emit) {
        return YETTY_ERR(yetty_ycore_void, "emit callback is NULL");
    }

    struct yetty_ymarkdown_render_config mdcfg = {0};
    if (config) {
        mdcfg.cell_width = config->cell_width;
        mdcfg.cell_height = config->cell_height;
        mdcfg.width_cells = config->width_cells;
        mdcfg.height_cells = config->height_cells;
    }

    struct yetty_ymarkdown_render_result r =
        yetty_ymarkdown_render((const char *)bytes, len, NULL, 0, &mdcfg);
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ycore_void, "ymarkdown render failed", r);
    }

    struct yetty_ycore_void_result er = emit(emit_user_data, r.value.buffer);
    yetty_ydraw_drawable_list_destroy(r.value.buffer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "ycat emit failed");
    return YETTY_OK_VOID();
}
