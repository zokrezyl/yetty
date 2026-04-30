/*
 * raster-font.c - Raster font implementation (FreeType-based)
 */

#include <yetty/yfont/ms-raster-font.h>
#include <yetty/yfont/ms-font.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yconfig.h>
#include <yetty/ycore/types.h>
#include <yetty/ycore/util.h>
#include <yetty/ytrace.h>
#include <webgpu/webgpu.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Shorthand accessors into rs */
#define FONT_ATLAS(f) ((f)->rs.textures[0])
#define FONT_UV_BUF(f) ((f)->rs.buffers[0])
#define FONT_UVS(f) ((struct yetty_yfont_raster_glyph_uv *)(f)->rs.buffers[0].data)
#define FONT_UV_COUNT(f) ((f)->rs.buffers[0].size / sizeof(struct yetty_yfont_raster_glyph_uv))

#define RASTER_FONT_MAX_PATH 512
#define RASTER_FONT_SLOT_PADDING 1
#define RASTER_FONT_ATLAS_MAX_DIMENSION 16384
#define RASTER_FONT_ATLAS_PADDING 2
/* Glyph slot = cell + padding on each side */
#define GLYPH_SLOT_W(f) ((uint32_t)(f)->cell_width + RASTER_FONT_ATLAS_PADDING * 2)
#define GLYPH_SLOT_H(f) ((uint32_t)(f)->cell_height + RASTER_FONT_ATLAS_PADDING * 2)

static const char *FACE_SUFFIXES[] = {"-Regular.ttf", "-Bold.ttf", "-Oblique.ttf",
                                      "-BoldOblique.ttf"};

struct yetty_yfont_raster_glyph_uv {
    float uv_x;
    float uv_y;
};

struct yetty_yfont_codepoint_slot {
    uint32_t codepoint;
    uint32_t slot;
};

struct yetty_yfont_raster_font {
    struct yetty_yfont_ms_font base;

    char fonts_dir[RASTER_FONT_MAX_PATH];
    char font_name[RASTER_FONT_MAX_PATH];
    float cell_width;
    float cell_height;
    uint32_t font_size;
    int baseline;

    FT_Library ft_library;
    FT_Face ft_faces[4];

    /* Shader code (owned) */
    struct yetty_ycore_buffer shader_code;

    /* GPU resource set — returned directly.
     * rs.textures[0] = atlas, rs.buffers[0] = glyph UVs */
    struct yetty_ypaint_core_gpu_resource_set rs;
    int next_slot_idx;

    /* Shelf packer state */
    uint32_t shelf_x;
    uint32_t shelf_y;
    uint32_t shelf_height;
    uint32_t shelf_min_x;

    struct yetty_yfont_codepoint_slot *codepoint_slots[4];
    size_t codepoint_slots_count[4];
    size_t codepoint_slots_capacity[4];

    int dirty;
};

/* Forward declarations */
static void raster_font_destroy(struct yetty_yfont_ms_font *self);
static struct pixel_size_result raster_font_get_cell_size(const struct yetty_yfont_ms_font *self);
static struct uint32_result raster_font_get_glyph_index(struct yetty_yfont_ms_font *self,
                                                        uint32_t codepoint);
static struct uint32_result raster_font_get_glyph_index_styled(struct yetty_yfont_ms_font *self,
                                                               uint32_t codepoint,
                                                               enum yetty_yfont_ms_style style);
static struct yetty_ycore_void_result raster_font_resize(struct yetty_yfont_ms_font *self,
                                                         float font_size);
static struct yetty_ycore_void_result raster_font_load_glyphs(struct yetty_yfont_ms_font *self,
                                                              const uint32_t *codepoints,
                                                              size_t count);
static struct yetty_ycore_void_result raster_font_load_basic_latin(struct yetty_yfont_ms_font *self);
static int raster_font_is_dirty(const struct yetty_yfont_ms_font *self);
static struct yetty_yrender_gpu_resource_set_result raster_font_get_gpu_resource_set(
    struct yetty_yfont_ms_font *self);

static void raster_font_update_font_size(struct yetty_yfont_raster_font *font);
static void raster_font_rasterize_all(struct yetty_yfont_raster_font *font);
static int raster_font_rasterize_glyph(struct yetty_yfont_raster_font *font, uint32_t codepoint,
                                       enum yetty_yfont_ms_style style);
static void raster_font_cleanup(struct yetty_yfont_raster_font *font);
static uint32_t raster_font_lookup_slot(struct yetty_yfont_raster_font *font, int style_idx,
                                        uint32_t codepoint);
static void raster_font_add_slot(struct yetty_yfont_raster_font *font, int style_idx, uint32_t codepoint,
                                 uint32_t slot);
static void raster_font_grow_atlas(struct yetty_yfont_raster_font *font);

static struct yetty_ycore_void_result raster_font_set_cell_size(struct yetty_yfont_ms_font *self,
                                                                struct yetty_ycore_pixel_size cell_size);

static const struct yetty_yfont_ms_font_ops raster_font_ops = {
    .destroy = raster_font_destroy,
    .get_cell_size = raster_font_get_cell_size,
    .set_cell_size = raster_font_set_cell_size,
    .get_glyph_index = raster_font_get_glyph_index,
    .get_glyph_index_styled = raster_font_get_glyph_index_styled,
    .resize = raster_font_resize,
    .load_glyphs = raster_font_load_glyphs,
    .load_basic_latin = raster_font_load_basic_latin,
    .is_dirty = raster_font_is_dirty,
    .get_gpu_resource_set = raster_font_get_gpu_resource_set,
};

/*===========================================================================
 * Helper functions
 *===========================================================================*/

static uint32_t raster_font_lookup_slot(struct yetty_yfont_raster_font *font, int style_idx, uint32_t codepoint)
{
    for (size_t i = 0; i < font->codepoint_slots_count[style_idx]; i++) {
        if (font->codepoint_slots[style_idx][i].codepoint == codepoint) {
            return font->codepoint_slots[style_idx][i].slot;
        }
    }
    return UINT32_MAX;
}

static void raster_font_add_slot(struct yetty_yfont_raster_font *font, int style_idx, uint32_t codepoint,
                                 uint32_t slot)
{
    /* Check if already exists */
    for (size_t i = 0; i < font->codepoint_slots_count[style_idx]; i++) {
        if (font->codepoint_slots[style_idx][i].codepoint == codepoint) {
            font->codepoint_slots[style_idx][i].slot = slot;
            return;
        }
    }

    /* Ensure capacity */
    if (font->codepoint_slots_count[style_idx] >= font->codepoint_slots_capacity[style_idx]) {
        font->codepoint_slots_capacity[style_idx] =
            font->codepoint_slots_capacity[style_idx]
                ? font->codepoint_slots_capacity[style_idx] * 2
                : 256;
        font->codepoint_slots[style_idx] =
            realloc(font->codepoint_slots[style_idx],
                    font->codepoint_slots_capacity[style_idx] * sizeof(struct yetty_yfont_codepoint_slot));
    }

    font->codepoint_slots[style_idx][font->codepoint_slots_count[style_idx]].codepoint = codepoint;
    font->codepoint_slots[style_idx][font->codepoint_slots_count[style_idx]].slot = slot;
    font->codepoint_slots_count[style_idx]++;
}

static void raster_font_update_font_size(struct yetty_yfont_raster_font *font)
{
    FT_Face regular_face = font->ft_faces[0];
    if (!regular_face) {
        return;
    }

    /* Set initial size to get metrics (use cell height as starting point) */
    FT_Set_Pixel_Sizes(regular_face, 0, (FT_UInt)font->cell_height);

    /* Get font metrics in pixels (at current size) */
    /* FreeType metrics are in 26.6 fixed point (divide by 64) */
    int ascender = regular_face->size->metrics.ascender >> 6;
    int descender = abs(regular_face->size->metrics.descender >> 6);
    int line_height = ascender + descender;

    /* Scale font size so line height fits in cell height (with small margin) */
    int target_height = (int)font->cell_height - 2; /* 1px margin top/bottom */
    if (line_height > 0 && target_height > 0) {
        font->font_size = (uint32_t)(font->cell_height * target_height / line_height);
    } else {
        font->font_size = (uint32_t)font->cell_height;
    }

    /* Apply the calculated font size to all faces */
    for (int i = 0; i < 4; i++) {
        if (font->ft_faces[i]) {
            FT_Set_Pixel_Sizes(font->ft_faces[i], 0, font->font_size);
        }
    }

    /* Recalculate metrics at new size */
    ascender = regular_face->size->metrics.ascender >> 6;
    descender = abs(regular_face->size->metrics.descender >> 6);

    /* Baseline position from top of cell (center the text vertically) */
    int actual_line_height = ascender + descender;
    int top_margin = ((int)font->cell_height - actual_line_height) / 2;
    font->baseline = top_margin + ascender;
}

static void raster_font_grow_atlas(struct yetty_yfont_raster_font *font)
{
    uint32_t old_width = FONT_ATLAS(font).width;
    uint32_t old_height = FONT_ATLAS(font).height;
    uint32_t new_width = old_width;
    uint32_t new_height = old_height;

    /* Grow by one row of glyph slots */
    uint32_t grow_w = GLYPH_SLOT_W(font) * 8; /* 8 glyphs wide */
    uint32_t grow_h = GLYPH_SLOT_H(font);     /* 1 row */

    int can_grow_width = (new_width + grow_w <= RASTER_FONT_ATLAS_MAX_DIMENSION);
    int can_grow_height = (new_height + grow_h <= RASTER_FONT_ATLAS_MAX_DIMENSION);

    if (!can_grow_width && !can_grow_height) {
        yerror("Raster font atlas at maximum size %ux%u", old_width, old_height);
        return;
    }

    if (can_grow_height) {
        new_height += grow_h;
    }
    if (can_grow_width) {
        new_width += grow_w;
    }

    if (can_grow_width && !can_grow_height) {
        font->shelf_x = old_width;
        font->shelf_y = 0;
        font->shelf_height = 0;
        font->shelf_min_x = old_width;
    }

    ydebug("Growing raster font atlas from %ux%u to %ux%u", old_width, old_height, new_width,
           new_height);

    uint8_t *new_data = calloc(new_width * new_height, 1);
    if (!new_data) {
        yerror("Failed to allocate new atlas data");
        return;
    }

    for (uint32_t y = 0; y < old_height; y++) {
        memcpy(new_data + y * new_width, FONT_ATLAS(font).data + y * old_width, old_width);
    }

    free(FONT_ATLAS(font).data);
    FONT_ATLAS(font).data = new_data;
    FONT_ATLAS(font).width = new_width;
    FONT_ATLAS(font).height = new_height;

    float scale_x = (float)old_width / (float)new_width;
    float scale_y = (float)old_height / (float)new_height;
    size_t uv_count = FONT_UV_COUNT(font);
    struct yetty_yfont_raster_glyph_uv *uvs = FONT_UVS(font);
    for (size_t i = 0; i < uv_count; i++) {
        if (uvs[i].uv_x >= 0.0f) {
            if (old_width != new_width) {
                uvs[i].uv_x *= scale_x;
            }
            if (old_height != new_height) {
                uvs[i].uv_y *= scale_y;
            }
        }
    }

    font->dirty = 1;
}

static int raster_font_rasterize_glyph(struct yetty_yfont_raster_font *font, uint32_t codepoint,
                                       enum yetty_yfont_ms_style style)
{
    int face_idx = (int)style;
    FT_Face face = font->ft_faces[face_idx];

    ydebug("rasterize ENTER: cp=U+%04X style=%d face=%p", codepoint, (int)style, (void *)face);

    /* Fallback to Regular if this face not available */
    if (!face) {
        ydebug("rasterize: face[%d] NULL, fallback or fail (style=%d)", face_idx, (int)style);
        if (style != YETTY_YFONT_MS_STYLE_REGULAR) {
            return raster_font_rasterize_glyph(font, codepoint, YETTY_YFONT_MS_STYLE_REGULAR);
        }
        return 0;
    }

    FT_UInt glyph_index = FT_Get_Char_Index(face, codepoint);
    ydebug("rasterize: FT_Get_Char_Index(U+%04X) = %u on face=%p num_glyphs=%ld", codepoint,
           (unsigned)glyph_index, (void *)face, (long)face->num_glyphs);
    if (glyph_index == 0) {
        /* Fallback to Regular if glyph not in this face */
        if (style != YETTY_YFONT_MS_STYLE_REGULAR) {
            return raster_font_rasterize_glyph(font, codepoint, YETTY_YFONT_MS_STYLE_REGULAR);
        }
        ydebug("rasterize: no glyph for U+%04X and style already REGULAR, returning 0", codepoint);
        return 0;
    }

    int ft_err = FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER);
    if (ft_err) {
        ydebug("rasterize: FT_Load_Glyph err=%d for U+%04X glyph=%u", ft_err, codepoint,
               glyph_index);
        return 0;
    }

    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap *bitmap = &slot->bitmap;

    int style_idx = (int)style;
    uint32_t slot_idx = (uint32_t)font->next_slot_idx;

    /* Ensure UV array capacity */
    size_t needed = (slot_idx + 1) * sizeof(struct yetty_yfont_raster_glyph_uv);
    if (needed > FONT_UV_BUF(font).capacity) {
        size_t new_cap = FONT_UV_BUF(font).capacity * 2;
        if (new_cap < needed) {
            new_cap = needed;
        }
        FONT_UV_BUF(font).data = realloc(FONT_UV_BUF(font).data, new_cap);
        FONT_UV_BUF(font).capacity = new_cap;
    }
    if (needed > FONT_UV_BUF(font).size) {
        FONT_UV_BUF(font).size = needed;
    }

    struct yetty_yfont_raster_glyph_uv *uvs = FONT_UVS(font);

    if (bitmap->width == 0 || bitmap->rows == 0) {
        uvs[slot_idx].uv_x = -1.0f;
        uvs[slot_idx].uv_y = -1.0f;
        raster_font_add_slot(font, style_idx, codepoint, slot_idx);
        font->next_slot_idx++;
        return 1;
    }

    int cell_w = (int)font->cell_width;
    int cell_h = (int)font->cell_height;
    uint32_t glyph_width = (uint32_t)cell_w + RASTER_FONT_ATLAS_PADDING * 2;
    uint32_t glyph_height = (uint32_t)cell_h + RASTER_FONT_ATLAS_PADDING * 2;
    uint32_t aw = FONT_ATLAS(font).width;
    uint32_t ah = FONT_ATLAS(font).height;
    size_t atlas_size = (size_t)aw * ah;

    if (font->shelf_x + glyph_width > aw) {
        font->shelf_x = font->shelf_min_x + RASTER_FONT_ATLAS_PADDING;
        font->shelf_y += font->shelf_height + RASTER_FONT_ATLAS_PADDING;
        font->shelf_height = 0;
    }

    if (font->shelf_y + glyph_height > ah) {
        raster_font_grow_atlas(font);
        aw = FONT_ATLAS(font).width;
        ah = FONT_ATLAS(font).height;
        atlas_size = (size_t)aw * ah;
    }

    if (font->shelf_y + glyph_height > ah) {
        yerror("Atlas full, cannot fit glyph for U+%04X", codepoint);
        return 0;
    }

    uint32_t atlas_x = font->shelf_x;
    uint32_t atlas_y = font->shelf_y;
    uint8_t *pixels = FONT_ATLAS(font).data;

    /* Clear cell area */
    for (uint32_t y = 0; y < glyph_height; y++) {
        for (uint32_t x = 0; x < glyph_width; x++) {
            uint32_t dst_idx = (atlas_y + y) * aw + (atlas_x + x);
            if (dst_idx < atlas_size) {
                pixels[dst_idx] = 0;
            }
        }
    }

    /* Position glyph within cell */
    int glyph_w = (int)bitmap->width;
    int glyph_h = (int)bitmap->rows;
    int bearing_x = slot->bitmap_left;
    int bearing_y = slot->bitmap_top;

    int offset_x = bearing_x + RASTER_FONT_ATLAS_PADDING;
    if (offset_x < RASTER_FONT_ATLAS_PADDING) {
        offset_x = RASTER_FONT_ATLAS_PADDING;
    }
    if (offset_x > (int)(glyph_width - (uint32_t)glyph_w - RASTER_FONT_ATLAS_PADDING)) {
        offset_x = (int)(glyph_width - (uint32_t)glyph_w - RASTER_FONT_ATLAS_PADDING);
    }
    if (offset_x < RASTER_FONT_ATLAS_PADDING) {
        offset_x = RASTER_FONT_ATLAS_PADDING;
    }

    int offset_y = font->baseline - bearing_y + RASTER_FONT_ATLAS_PADDING;
    if (offset_y < RASTER_FONT_ATLAS_PADDING) {
        offset_y = RASTER_FONT_ATLAS_PADDING;
    }
    if (offset_y > (int)(glyph_height - (uint32_t)glyph_h - RASTER_FONT_ATLAS_PADDING)) {
        offset_y = (int)(glyph_height - (uint32_t)glyph_h - RASTER_FONT_ATLAS_PADDING);
    }
    if (offset_y < RASTER_FONT_ATLAS_PADDING) {
        offset_y = RASTER_FONT_ATLAS_PADDING;
    }

    /* Copy bitmap to atlas */
    for (int y = 0; y < glyph_h; y++) {
        for (int x = 0; x < glyph_w; x++) {
            int src_idx = y * bitmap->pitch + x;
            uint32_t dst_idx = (atlas_y + (uint32_t)offset_y + (uint32_t)y) * aw +
                               (atlas_x + (uint32_t)offset_x + (uint32_t)x);
            if (dst_idx < atlas_size) {
                pixels[dst_idx] = bitmap->buffer[src_idx];
            }
        }
    }

    /* Store UV — point to cell content start (skip padding) */
    uvs[slot_idx].uv_x = (float)(atlas_x + RASTER_FONT_ATLAS_PADDING) / (float)aw;
    uvs[slot_idx].uv_y = (float)(atlas_y + RASTER_FONT_ATLAS_PADDING) / (float)ah;
    raster_font_add_slot(font, style_idx, codepoint, slot_idx);

    /* Sample a few pixel values from the bitmap */
    uint8_t sample_vals[4] = {0};
    for (int i = 0; i < 4 && i < glyph_w * glyph_h; i++) {
        int y = i / glyph_w;
        int x = i % glyph_w;
        sample_vals[i] = bitmap->buffer[y * bitmap->pitch + x];
    }
    ydebug("rasterize U+%04X slot=%u bm=%dx%d mode=%d bear=(%d,%d) off=(%d,%d) atlas=(%u,%u) "
           "uv=(%.4f,%.4f) px=[%u,%u,%u,%u]",
           codepoint, slot_idx, glyph_w, glyph_h, bitmap->pixel_mode, bearing_x, bearing_y,
           offset_x, offset_y, atlas_x, atlas_y, uvs[slot_idx].uv_x, uvs[slot_idx].uv_y,
           sample_vals[0], sample_vals[1], sample_vals[2], sample_vals[3]);

    font->shelf_x = atlas_x + glyph_width + RASTER_FONT_ATLAS_PADDING;
    if (glyph_height > font->shelf_height) {
        font->shelf_height = glyph_height;
    }

    font->next_slot_idx++;
    font->dirty = 1;
    return 1;
}

static void raster_font_rasterize_all(struct yetty_yfont_raster_font *font)
{
    size_t atlas_size = (size_t)FONT_ATLAS(font).width * FONT_ATLAS(font).height;
    memset(FONT_ATLAS(font).data, 0, atlas_size);

    /* Reset shelf packer */
    font->shelf_x = RASTER_FONT_ATLAS_PADDING;
    font->shelf_y = RASTER_FONT_ATLAS_PADDING;
    font->shelf_height = 0;
    font->shelf_min_x = 0;

    /* Reserve slot 0 for empty/space */
    font->next_slot_idx = 1;
    FONT_UVS(font)[0].uv_x = -1.0f;
    FONT_UVS(font)[0].uv_y = -1.0f;
    FONT_UV_BUF(font).size = sizeof(struct yetty_yfont_raster_glyph_uv);

    for (int i = 0; i < 4; i++) {
        font->codepoint_slots_count[i] = 0;
        raster_font_add_slot(font, i, 0x20, 0); /* Map space to slot 0 */
    }

    for (int s = 0; s < 4; s++) {
        for (size_t i = 0; i < font->codepoint_slots_count[s]; i++) {
            uint32_t cp = font->codepoint_slots[s][i].codepoint;
            if (cp == 0x20) {
                continue; /* space already reserved */
            }
            if (!raster_font_rasterize_glyph(font, cp, (enum yetty_yfont_ms_style)s)) {
                ywarn("Failed to re-rasterize glyph U+%04X style %d", cp, s);
            }
        }
    }

    font->dirty = 1;
}

static void raster_font_cleanup(struct yetty_yfont_raster_font *font)
{
    for (int i = 0; i < 4; i++) {
        if (font->ft_faces[i]) {
            FT_Done_Face(font->ft_faces[i]);
            font->ft_faces[i] = NULL;
        }
    }
    if (font->ft_library) {
        FT_Done_FreeType(font->ft_library);
        font->ft_library = NULL;
    }
    free(FONT_ATLAS(font).data);
    FONT_ATLAS(font).data = NULL;
    free(FONT_UV_BUF(font).data);
    FONT_UV_BUF(font).data = NULL;
    free(font->shader_code.data);
    font->shader_code.data = NULL;
    for (int i = 0; i < 4; i++) {
        free(font->codepoint_slots[i]);
        font->codepoint_slots[i] = NULL;
    }
}

/*===========================================================================
 * Create
 *===========================================================================*/

struct yetty_font_ms_font_result yetty_yfont_ms_raster_font_create(struct yetty_yconfig *config,
                                                                  float cell_width,
                                                                  float cell_height)
{
    const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    const char *font_family = config->ops->font_family(config);
    if (!font_family || strcmp(font_family, "default") == 0) {
        font_family = "DejaVuSansMNerdFontMono";
    }

    /* Load shader from file */
    char shader_path[RASTER_FONT_MAX_PATH];
    snprintf(shader_path, sizeof(shader_path), "%s/ms-raster-font.wgsl", shaders_dir);
    struct yetty_ycore_buffer_result shader_res = yetty_ycore_read_file(shader_path);
    if (YETTY_IS_ERR(shader_res)) {
        return YETTY_ERR(yetty_font_ms_font, shader_res.error.msg);
    }

    struct yetty_yfont_raster_font *font = calloc(1, sizeof(struct yetty_yfont_raster_font));
    if (!font) {
        free(shader_res.value.data);
        return YETTY_ERR(yetty_font_ms_font, "failed to allocate raster font");
    }

    font->shader_code = shader_res.value;

    font->base.ops = &raster_font_ops;
    strncpy(font->fonts_dir, fonts_dir, RASTER_FONT_MAX_PATH - 1);
    strncpy(font->font_name, font_family, RASTER_FONT_MAX_PATH - 1);
    font->cell_width = cell_width;
    font->cell_height = cell_height;

    /* Resource set: namespace + atlas texture + UV buffer */
    strncpy(font->rs.namespace, "raster_font", YETTY_YRENDER_NAME_MAX - 1);
    font->rs.texture_count = 1;
    font->rs.buffer_count = 1;

    /* Start with space for ~16 glyphs (one row) */
    FONT_ATLAS(font).width = GLYPH_SLOT_W(font) * 16;
    FONT_ATLAS(font).height = GLYPH_SLOT_H(font);
    FONT_ATLAS(font).format = WGPUTextureFormat_R8Unorm;
    FONT_ATLAS(font).sampler_filter = WGPUFilterMode_Linear;
    strncpy(FONT_ATLAS(font).name, "texture", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(FONT_ATLAS(font).wgsl_type, "texture_2d<f32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    strncpy(FONT_ATLAS(font).sampler_name, "sampler", YETTY_YRENDER_NAME_MAX - 1);

    size_t atlas_bytes = (size_t)FONT_ATLAS(font).width * FONT_ATLAS(font).height;
    FONT_ATLAS(font).data = calloc(atlas_bytes, 1);
    if (!FONT_ATLAS(font).data) {
        free(font);
        return YETTY_ERR(yetty_font_ms_font, "Failed to allocate atlas");
    }

    strncpy(FONT_UV_BUF(font).name, "buffer", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(FONT_UV_BUF(font).wgsl_type, "array<f32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    FONT_UV_BUF(font).readonly = 1;
    FONT_UV_BUF(font).capacity = 256 * sizeof(struct yetty_yfont_raster_glyph_uv);
    FONT_UV_BUF(font).data = malloc(FONT_UV_BUF(font).capacity);
    if (!FONT_UV_BUF(font).data) {
        raster_font_cleanup(font);
        free(font);
        return YETTY_ERR(yetty_font_ms_font, "Failed to allocate glyph UVs");
    }

    FONT_UVS(font)[0].uv_x = -1.0f;
    FONT_UVS(font)[0].uv_y = -1.0f;
    FONT_UV_BUF(font).size = sizeof(struct yetty_yfont_raster_glyph_uv);
    font->next_slot_idx = 1;

    yetty_yrender_shader_code_set(&font->rs.shader, (const char *)font->shader_code.data,
                                  font->shader_code.size);

    /* FreeType */
    if (FT_Init_FreeType(&font->ft_library)) {
        raster_font_cleanup(font);
        free(font);
        return YETTY_ERR(yetty_font_ms_font, "Failed to initialize FreeType");
    }

    for (int i = 0; i < 4; i++) {
        char path[RASTER_FONT_MAX_PATH * 2];
        snprintf(path, sizeof(path), "%s/%s%s", font->fonts_dir, font->font_name, FACE_SUFFIXES[i]);
        if (FT_New_Face(font->ft_library, path, 0, &font->ft_faces[i])) {
            if (i == 0) {
                raster_font_cleanup(font);
                free(font);
                return YETTY_ERR(yetty_font_ms_font, "Failed to load font");
            }
            font->ft_faces[i] = NULL;
        }
    }

    raster_font_update_font_size(font);

    /* Shelf packer */
    font->shelf_x = RASTER_FONT_ATLAS_PADDING;
    font->shelf_y = RASTER_FONT_ATLAS_PADDING;
    font->shelf_height = 0;
    font->shelf_min_x = 0;

    /* Codepoint slot maps */
    for (int i = 0; i < 4; i++) {
        font->codepoint_slots_capacity[i] = 256;
        font->codepoint_slots[i] =
            malloc(font->codepoint_slots_capacity[i] * sizeof(struct yetty_yfont_codepoint_slot));
        if (!font->codepoint_slots[i]) {
            raster_font_cleanup(font);
            free(font);
            return YETTY_ERR(yetty_font_ms_font, "Failed to allocate codepoint slots");
        }
        font->codepoint_slots_count[i] = 0;
        raster_font_add_slot(font, i, 0x20, 0);
    }

    ydebug("RasterFont: %s cell=%dx%d fontSize=%d atlas=%ux%u", font->font_name, (int)cell_width,
           (int)cell_height, font->font_size, FONT_ATLAS(font).width, FONT_ATLAS(font).height);

    return YETTY_OK(yetty_font_ms_font, &font->base);
}

/*===========================================================================
 * Ops implementation
 *===========================================================================*/

static void raster_font_destroy(struct yetty_yfont_ms_font *self)
{
    struct yetty_yfont_raster_font *font = container_of(self, struct yetty_yfont_raster_font, base);
    raster_font_cleanup(font);
    free(font);
}

static struct pixel_size_result raster_font_get_cell_size(const struct yetty_yfont_ms_font *self)
{
    const struct yetty_yfont_raster_font *font = container_of(self, struct yetty_yfont_raster_font, base);
    struct yetty_ycore_pixel_size sz;
    sz.width = font->cell_width;
    sz.height = font->cell_height;
    return YETTY_OK(pixel_size, sz);
}

static struct uint32_result raster_font_get_glyph_index(struct yetty_yfont_ms_font *self,
                                                        uint32_t codepoint)
{
    return raster_font_get_glyph_index_styled(self, codepoint, YETTY_YFONT_MS_STYLE_REGULAR);
}

static struct uint32_result raster_font_get_glyph_index_styled(struct yetty_yfont_ms_font *self,
                                                               uint32_t codepoint,
                                                               enum yetty_yfont_ms_style style)
{
    struct yetty_yfont_raster_font *font = container_of(self, struct yetty_yfont_raster_font, base);

    if (codepoint == 0x20) {
        return YETTY_OK(uint32, 0);
    }

    int style_idx = (int)style;
    uint32_t slot = raster_font_lookup_slot(font, style_idx, codepoint);
    if (slot != UINT32_MAX) {
        return YETTY_OK(uint32, slot);
    }

    if (raster_font_rasterize_glyph(font, codepoint, (enum yetty_yfont_ms_style)style)) {
        slot = raster_font_lookup_slot(font, style_idx, codepoint);
        if (slot != UINT32_MAX) {
            return YETTY_OK(uint32, slot);
        }
    }

    if (style != YETTY_YFONT_MS_STYLE_REGULAR) {
        return raster_font_get_glyph_index_styled(self, codepoint, YETTY_YFONT_MS_STYLE_REGULAR);
    }

    return YETTY_ERR(uint32, "glyph not found");
}

static struct yetty_ycore_void_result raster_font_resize(struct yetty_yfont_ms_font *self,
                                                         float font_size)
{
    struct yetty_yfont_raster_font *font = container_of(self, struct yetty_yfont_raster_font, base);
    (void)font_size;
    /* TODO: implement resize — recalculate cell size from font_size, re-rasterize */
    raster_font_update_font_size(font);
    raster_font_rasterize_all(font);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result raster_font_set_cell_size(struct yetty_yfont_ms_font *self,
                                                                struct yetty_ycore_pixel_size cell_size)
{
    struct yetty_yfont_raster_font *font = container_of(self, struct yetty_yfont_raster_font, base);
    if (cell_size.width <= 0.0f || cell_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "invalid cell size");
    }

    /* Clamp — silly huge cells blow the atlas. */
    float cw = cell_size.width;
    float ch = cell_size.height;
    if (ch > 128.0f) {
        ch = 128.0f;
    }
    if (cw > 256.0f) {
        cw = 256.0f;
    }

    font->cell_width = cw;
    font->cell_height = ch;

    /* Re-derive font_size from the new cell_height and re-rasterize every
     * already-loaded glyph. The atlas grows on demand if needed. */
    raster_font_update_font_size(font);
    raster_font_rasterize_all(font);
    ydebug("raster_font: set_cell_size %.1fx%.1f font_size=%u", cw, ch, (unsigned)font->font_size);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result raster_font_load_glyphs(struct yetty_yfont_ms_font *self,
                                                              const uint32_t *codepoints,
                                                              size_t count)
{
    struct yetty_yfont_raster_font *font = container_of(self, struct yetty_yfont_raster_font, base);

    /* Load glyphs for all available styles */
    for (int style_idx = 0; style_idx < 4; style_idx++) {
        if (!font->ft_faces[style_idx]) {
            continue;
        }
        enum yetty_yfont_ms_style style = (enum yetty_yfont_ms_style)style_idx;

        for (size_t i = 0; i < count; i++) {
            uint32_t codepoint = codepoints[i];
            if (raster_font_lookup_slot(font, style_idx, codepoint) != UINT32_MAX) {
                continue;
            }

            if (!raster_font_rasterize_glyph(font, codepoint, style)) {
                /* Not a warning for non-Regular - fallback handles it */
                if (style == YETTY_YFONT_MS_STYLE_REGULAR) {
                    ywarn("Failed to rasterize glyph for U+%04X", codepoint);
                }
                continue;
            }
        }
    }

    font->dirty = 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result raster_font_load_basic_latin(struct yetty_yfont_ms_font *self)
{
    /* Collect codepoints */
    uint32_t codepoints[1024];
    size_t count = 0;

    /* Basic Latin (ASCII printable: space to tilde) */
    for (uint32_t cp = 0x20; cp <= 0x7E && count < 1024; cp++) {
        codepoints[count++] = cp;
    }

    /* Latin-1 Supplement (0xA0-0xFF) */
    for (uint32_t cp = 0xA0; cp <= 0xFF && count < 1024; cp++) {
        codepoints[count++] = cp;
    }

    /* Latin Extended-A (0x100-0x17F) */
    for (uint32_t cp = 0x100; cp <= 0x17F && count < 1024; cp++) {
        codepoints[count++] = cp;
    }

    /* Box Drawing (0x2500-0x257F) */
    for (uint32_t cp = 0x2500; cp <= 0x257F && count < 1024; cp++) {
        codepoints[count++] = cp;
    }

    /* Block Elements (0x2580-0x259F) */
    for (uint32_t cp = 0x2580; cp <= 0x259F && count < 1024; cp++) {
        codepoints[count++] = cp;
    }

    /* Common math/programming symbols */
    const uint32_t extra_symbols[] = {
        0x2190, 0x2191, 0x2192, 0x2193, /* arrows */
        0x2194, 0x2195, 0x21D0, 0x21D2, /* double arrows */
        0x2200, 0x2203, 0x2205, 0x2208, /* math */
        0x2260, 0x2264, 0x2265, 0x2227, 0x2228, 0x2229,
        0x222A, 0x2248, 0x221E, 0x2022, 0x2026, 0x00B7,
    };
    for (size_t i = 0; i < sizeof(extra_symbols) / sizeof(extra_symbols[0]) && count < 1024; i++) {
        codepoints[count++] = extra_symbols[i];
    }

    return raster_font_load_glyphs(self, codepoints, count);
}

static int raster_font_is_dirty(const struct yetty_yfont_ms_font *self)
{
    const struct yetty_yfont_raster_font *font = container_of(self, struct yetty_yfont_raster_font, base);
    return font->dirty;
}

static struct yetty_yrender_gpu_resource_set_result raster_font_get_gpu_resource_set(
    struct yetty_yfont_ms_font *self)
{
    struct yetty_yfont_raster_font *font = container_of(self, struct yetty_yfont_raster_font, base);

    ydebug("raster_font_get_gpu_resource_set: atlas=%ux%u buffer_size=%zu dirty=%d",
           FONT_ATLAS(font).width, FONT_ATLAS(font).height, FONT_UV_BUF(font).size, font->dirty);

    /* Dump non-zero pixels in first glyph slot area for debugging */
    if (font->dirty && FONT_ATLAS(font).data) {
        uint8_t *px = FONT_ATLAS(font).data;
        uint32_t aw = FONT_ATLAS(font).width;
        int non_zero = 0;
        for (uint32_t y = 0; y < 24 && y < FONT_ATLAS(font).height; y++) {
            for (uint32_t x = 0; x < 16 && x < aw; x++) {
                if (px[y * aw + x] > 0) {
                    non_zero++;
                }
            }
        }
        ydebug("  atlas first slot (0-16, 0-24): %d non-zero pixels", non_zero);
    }

    if (font->dirty) {
        FONT_ATLAS(font).dirty = 1;
        FONT_UV_BUF(font).dirty = 1;
        font->dirty = 0;
    }

    return YETTY_OK(yetty_yrender_gpu_resource_set, &font->rs);
}
