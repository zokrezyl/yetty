/*
 * handler-msoffice.c - docx/xlsx/pptx → ydraw envelope.
 *
 * One handler serves all three OOXML types: ymsoffice sniffs the container
 * kind from its part names, so a mis-detected extension still renders the
 * right way. The whole document ships as a single envelope, same as the
 * markdown handler (the terminal's scrolling-canvas receiver stacks
 * envelopes by glyph rows; one envelope avoids chunk-boundary artifacts).
 */

#include <yetty/ycat/ycat.h>

#include <yetty/ymsoffice/msoffice.h>
#include <yetty/ymsoffice/render.h>

struct yetty_ydraw_drawable_list_result yetty_ycat_handler_msoffice(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config)
{
    (void)path_hint;

    struct yetty_ymsoffice_document_ptr_result parse_res = yetty_ymsoffice_parse(bytes, len);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, parse_res, "ymsoffice parse failed");
    struct yetty_ymsoffice_document *document = parse_res.value;

    struct yetty_ymsoffice_render_config render_config = {0};
    if (config) {
        render_config.cell_width = config->cell_width;
        render_config.cell_height = config->cell_height;
        render_config.width_cells = config->width_cells;
        render_config.height_cells = config->height_cells;
    }

    struct yetty_ymsoffice_render_result render_res =
        yetty_ymsoffice_render(document, &render_config);
    yetty_ymsoffice_document_destroy(document);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, render_res, "ymsoffice render failed");

    return YETTY_OK(yetty_ydraw_drawable_list, render_res.value.buffer);
}
