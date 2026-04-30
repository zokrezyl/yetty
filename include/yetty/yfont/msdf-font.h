#ifndef YETTY_YFONT_MSDF_FONT_H
#define YETTY_YFONT_MSDF_FONT_H

#include <yetty/yfont/font.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create non-monospace MSDF font from .cdb file.
 * Can be used at any font size — scaling handled by shader.
 */
struct yetty_font_font_result yetty_yfont_msdf_font_create(const char *cdb_path,
                                                          const char *shader_path);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFONT_MSDF_FONT_H */
