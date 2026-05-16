#ifndef YETTY_YMARKDOWN_YMARKDOWN_H
#define YETTY_YMARKDOWN_YMARKDOWN_H

/*
 * ymarkdown - render markdown text into a ydraw buffer.
 *
 * The renderer:
 *   - parses headers (#..######), inline bold (**..**), italic (*..*),
 *     bold+italic (***..***), inline code (`..`) and bullet lists
 *     ('-' or '*' at start of line)
 *   - emits text spans via yetty_ydraw_draw_list_add_text
 *   - emits code-run background rectangles as SDF Box primitives
 *   - populates the scene bounds on the buffer from the config
 *
 * The result carries the buffer ownership; caller frees it via
 * yetty_ydraw_draw_list_destroy.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/draw-list.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ymarkdown_render_config {
    uint32_t cell_width;
    uint32_t cell_height;
    uint32_t width_cells;
    uint32_t height_cells;
};

struct yetty_ymarkdown_render_output {
    struct yetty_ydraw_draw_list *buffer;
    float scene_width;
    float scene_height;
};

YETTY_YRESULT_DECLARE(yetty_ymarkdown_render, struct yetty_ymarkdown_render_output);

/* Args string honours the same flags as the C++ original:
 *   --font-size=<float>   override derived font size (default: cell_height)
 *   --line-spacing=<float> (default 1.4)
 * Unknown flags are ignored. Pass NULL / 0 to use defaults. */
struct yetty_ymarkdown_render_result yetty_ymarkdown_render(
    const char *content, size_t content_len, const char *args, size_t args_len,
    const struct yetty_ymarkdown_render_config *config);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMARKDOWN_YMARKDOWN_H */
