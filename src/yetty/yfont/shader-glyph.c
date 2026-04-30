/*
 * shader-glyph.c — name->codepoint lookup for shader glyphs.
 *
 * The static table is generated at configure time by
 * gen-shader-glyph-table.py from src/yetty/yfont/glyph-shaders/0xNN-name.wgsl
 * filenames. See the .h for the full story.
 */

#include <yetty/yfont/shader-glyph.h>

#include <string.h>

#include "shader-glyph-table.gen.h" /* defines yetty_yfont_shader_glyph_table_data
                                     * and YETTY_YFONT_SHADER_GLYPH_TABLE_COUNT */

const struct yetty_yfont_shader_glyph_entry *yetty_yfont_shader_glyph_table(void)
{
    return yetty_yfont_shader_glyph_table_data;
}

size_t yetty_yfont_shader_glyph_count(void)
{
    return YETTY_YFONT_SHADER_GLYPH_TABLE_COUNT;
}

struct uint32_result yetty_yfont_shader_glyph_codepoint(const char *name)
{
    if (!name) {
        return YETTY_ERR(uint32, "shader glyph name is NULL");
    }

    /* Binary search — table is sorted by name at generation time. */
    size_t lo = 0;
    size_t hi = YETTY_YFONT_SHADER_GLYPH_TABLE_COUNT;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(name, yetty_yfont_shader_glyph_table_data[mid].name);
        if (cmp == 0) {
            return YETTY_OK(uint32, yetty_yfont_shader_glyph_table_data[mid].codepoint);
        }
        if (cmp < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return YETTY_ERR(uint32, "unknown shader glyph");
}
