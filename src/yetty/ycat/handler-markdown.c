/*
 * handler-markdown.c - markdown → ydraw envelope stream.
 *
 * Thin bridge from ymarkdown's chunk emit callback to ycat's emit
 * callback. Each chunk is one screen-height tile; the receiver scrolls
 * by the chunk's scene height after each envelope.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/ymarkdown/ymarkdown.h>

struct chunk_bridge_ctx {
    yetty_ycat_emit_fn emit;
    void *emit_ud;
};

static struct yetty_ycore_void_result chunk_to_ycat_emit(
    void *ud, int chunk_index, const struct yetty_ydraw_draw_list *envelope)
{
    (void)chunk_index;
    struct chunk_bridge_ctx *b = ud;
    struct yetty_ycore_void_result r = b->emit(b->emit_ud, envelope);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ycat emit failed");
    return YETTY_OK_VOID();
}

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

    struct chunk_bridge_ctx bridge = {.emit = emit, .emit_ud = emit_user_data};
    struct yetty_ymarkdown_stream_render_result r = yetty_ymarkdown_render_streaming(
        (const char *)bytes, len, NULL, 0, &mdcfg, chunk_to_ycat_emit, &bridge);
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ycore_void, "ymarkdown streaming render failed", r);
    }
    return YETTY_OK_VOID();
}
