#ifndef YETTY_YMSOFFICE_RENDER_H
#define YETTY_YMSOFFICE_RENDER_H

/*
 * render.h - render a parsed ymsoffice document into a ydraw buffer.
 *
 * Follows the same conventions as ymarkdown: text advances by
 * 0.6 * font_size per byte (flat proportional approximation), colors use
 * the brand palette, and the scene bounds come from the caller's cell
 * geometry. The result carries buffer ownership; free it with
 * yetty_ydraw_drawable_list_destroy.
 */

#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ymsoffice/model.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ymsoffice_render_config {
    uint32_t cell_width;
    uint32_t cell_height;
    uint32_t width_cells;
    uint32_t height_cells;
};

struct yetty_ymsoffice_render_output {
    struct yetty_ydraw_drawable_list *buffer;
    float scene_width;
    float scene_height;
};

YETTY_YRESULT_DECLARE(yetty_ymsoffice_render, struct yetty_ymsoffice_render_output);

struct yetty_ymsoffice_render_result yetty_ymsoffice_render(
    const struct yetty_ymsoffice_document *document,
    const struct yetty_ymsoffice_render_config *config);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMSOFFICE_RENDER_H */
