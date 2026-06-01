/*
 * raster-font.c - Non-monospace raster (FreeType-backed) font implementation
 *
 * Mirrors msdf-font.c in spirit but sources glyph metrics and pixels directly
 * from FreeType. Uses a uniform-cell R8 atlas and the msdf-font glyph_meta_gpu
 * layout so shaders can share conventions.
 *
 * Advance measurement uses FT_Get_Advance(FT_LOAD_NO_SCALE) and scales by
 * font_size / units_per_EM — the same formula ymsdf-gen applies when baking
 * CDB advances, guaranteeing identical results between msdf-font and
 * raster-font for the same TTF.
 *
 * When created without a shader_path the font operates in metrics-only mode:
 * no atlas, no shader load, no GPU resources. Advance / measure_text remain
 * functional; get_gpu_resource_set returns an error.
 */

#include <yetty/yfont/raster-font.h>
#include <yetty/yfont/font.h>
#include <yetty/ycore/map.h>
#include <yetty/ycore/util.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/texture-format.h>
#include <yetty/ytrace/ytrace.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H

#include <stdlib.h>
#include <string.h>

#define ATLAS_INITIAL_W 1024
#define ATLAS_INITIAL_H 512
#define ATLAS_MAX_DIM 16384
#define DEFAULT_CELL_SIZE 64
#define MAP_CAPACITY 8192

/* Per-glyph GPU metadata (24 bytes, 6 floats). Matches msdf-font layout. */
struct yetty_yfont_glyph_meta_gpu {
    float size_x, size_y;       /* logical glyph size in pixels at base_size */
    float bearing_x, bearing_y; /* offset from pen position */
    float advance;              /* horizontal advance in pixels at base_size */
    float cell_idx;             /* atlas cell index; -1 if no atlas pixels */
};

struct yetty_yfont_raster_font {
    struct yetty_yfont_font base;

    /* TTF bytes owned when loaded from memory; NULL when loaded from file. */
    uint8_t *ttf_owned;
    size_t ttf_size;

    FT_Library ft_library;
    FT_Face ft_face;

    float base_size;    /* pixel size rasterized into atlas cells */
    float units_per_em; /* FreeType face->units_per_EM (float for arithmetic) */

    /* Atlas — present only in full (rendering) mode. */
    int has_atlas;
    uint8_t *atlas_pixels;
    uint32_t atlas_width;
    uint32_t atlas_height;
    uint32_t cell_size;
    uint32_t atlas_cols;
    uint32_t next_cell;

    struct yetty_yfont_glyph_meta_gpu *meta;
    uint32_t meta_capacity;
    uint32_t next_slot;

    struct yetty_ycore_map glyph_map;
    /* Inverse map (slot → cp) — populated alongside glyph_map so
     * clipboard extraction can recover the codepoint from a glyph
     * index stored in a ydraw glyph prim. */
    struct yetty_ycore_map slot_to_cp;

    /* GPU — present only in full mode. */
    struct yetty_ycore_buffer shader_code;
    struct yetty_ydraw_gpu_resource_set rs;
    int dirty;
};

/*=============================================================================
 * Helpers
 *===========================================================================*/

static float advance_pixels_at_base(const struct yetty_yfont_raster_font *f, FT_UInt gid)
{
    FT_Fixed adv_fu = 0;
    if (FT_Get_Advance(f->ft_face, gid, FT_LOAD_NO_SCALE, &adv_fu) != 0) {
        return 0.0f;
    }
    if (f->units_per_em <= 0.0f) {
        return 0.0f;
    }
    return (float)adv_fu * f->base_size / f->units_per_em;
}

static void atlas_grow(struct yetty_yfont_raster_font *f)
{
    uint32_t rows_to_add = 4;
    uint32_t new_h = f->atlas_height + rows_to_add * f->cell_size;
    if (new_h > ATLAS_MAX_DIM) {
        return;
    }

    size_t old_sz = (size_t)f->atlas_width * f->atlas_height;
    size_t new_sz = (size_t)f->atlas_width * new_h;
    uint8_t *p = realloc(f->atlas_pixels, new_sz);
    if (!p) {
        return;
    }

    memset(p + old_sz, 0, new_sz - old_sz);
    f->atlas_pixels = p;
    f->atlas_height = new_h;
}

/* Lookup or rasterize a glyph. Always populates metadata; only writes atlas
 * pixels when has_atlas=1 and the glyph has non-zero bitmap. */
static struct uint32_result load_one(struct yetty_yfont_raster_font *f, uint32_t cp)
{
    const uint32_t *existing = yetty_ycore_map_get(&f->glyph_map, cp);
    if (existing) {
        return YETTY_OK(uint32, *existing);
    }

    FT_UInt gid = FT_Get_Char_Index(f->ft_face, cp);
    if (gid == 0) {
        return YETTY_ERR(uint32, "glyph not found");
    }

    uint32_t slot = f->next_slot;
    if (slot >= f->meta_capacity) {
        uint32_t new_cap = f->meta_capacity * 2;
        struct yetty_yfont_glyph_meta_gpu *nm =
            realloc(f->meta, new_cap * sizeof(struct yetty_yfont_glyph_meta_gpu));
        if (!nm) {
            return YETTY_ERR(uint32, "meta realloc failed");
        }
        f->meta = nm;
        f->meta_capacity = new_cap;
    }

    struct yetty_yfont_glyph_meta_gpu *m = &f->meta[slot];
    memset(m, 0, sizeof(*m));
    m->advance = advance_pixels_at_base(f, gid);
    m->cell_idx = -1.0f;

    if (!f->has_atlas) {
        /* Metrics-only: advance only, no rasterization. */
        f->next_slot++;
        yetty_ycore_map_put(&f->glyph_map, cp, slot);
        yetty_ycore_map_put(&f->slot_to_cp, slot, cp);
        return YETTY_OK(uint32, slot);
    }

    if (FT_Set_Pixel_Sizes(f->ft_face, 0, (FT_UInt)f->base_size) != 0 ||
        FT_Load_Glyph(f->ft_face, gid, FT_LOAD_RENDER) != 0) {
        f->next_slot++;
        yetty_ycore_map_put(&f->glyph_map, cp, slot);
        yetty_ycore_map_put(&f->slot_to_cp, slot, cp);
        f->dirty = 1;
        return YETTY_OK(uint32, slot);
    }

    FT_GlyphSlot gslot = f->ft_face->glyph;
    FT_Bitmap *bmp = &gslot->bitmap;

    m->bearing_x = (float)gslot->bitmap_left;
    m->bearing_y = (float)gslot->bitmap_top;
    m->size_x = (float)bmp->width;
    m->size_y = (float)bmp->rows;

    if (bmp->width == 0 || bmp->rows == 0) {
        f->next_slot++;
        yetty_ycore_map_put(&f->glyph_map, cp, slot);
        yetty_ycore_map_put(&f->slot_to_cp, slot, cp);
        f->dirty = 1;
        return YETTY_OK(uint32, slot);
    }

    if (bmp->width > f->cell_size || bmp->rows > f->cell_size) {
        ywarn("raster_font: glyph U+%04X %ux%u exceeds cell %u; skipping pixels", cp, bmp->width,
              bmp->rows, f->cell_size);
        f->next_slot++;
        yetty_ycore_map_put(&f->glyph_map, cp, slot);
        yetty_ycore_map_put(&f->slot_to_cp, slot, cp);
        f->dirty = 1;
        return YETTY_OK(uint32, slot);
    }

    uint32_t cell_idx = f->next_cell;
    uint32_t col = cell_idx % f->atlas_cols;
    uint32_t row = cell_idx / f->atlas_cols;

    while ((row + 1) * f->cell_size > f->atlas_height) {
        atlas_grow(f);
    }

    uint32_t ax = col * f->cell_size;
    uint32_t ay = row * f->cell_size;
    uint32_t ox = (f->cell_size - bmp->width) / 2;
    uint32_t oy = (f->cell_size - bmp->rows) / 2;

    for (uint32_t y = 0; y < bmp->rows; y++) {
        size_t dst = (size_t)(ay + oy + y) * f->atlas_width + ax + ox;
        const uint8_t *src = bmp->buffer + (size_t)y * bmp->pitch;
        memcpy(f->atlas_pixels + dst, src, bmp->width);
    }

    m->cell_idx = (float)cell_idx;
    f->next_cell++;
    f->next_slot++;
    yetty_ycore_map_put(&f->glyph_map, cp, slot);
    yetty_ycore_map_put(&f->slot_to_cp, slot, cp);
    f->dirty = 1;
    return YETTY_OK(uint32, slot);
}

/*=============================================================================
 * Vtable
 *===========================================================================*/

static void raster_destroy(struct yetty_yfont_font *self)
{
    struct yetty_yfont_raster_font *f = (struct yetty_yfont_raster_font *)self;
    if (!f) {
        return;
    }

    if (f->ft_face) {
        FT_Done_Face(f->ft_face);
    }
    if (f->ft_library) {
        FT_Done_FreeType(f->ft_library);
    }
    free(f->ttf_owned);
    free(f->atlas_pixels);
    free(f->meta);
    free(f->shader_code.data);
    yetty_ycore_map_destroy(&f->glyph_map);
    yetty_ycore_map_destroy(&f->slot_to_cp);
    free(f);
}

static struct uint32_result raster_get_glyph_index(struct yetty_yfont_font *self, uint32_t cp)
{
    struct yetty_yfont_raster_font *f = (struct yetty_yfont_raster_font *)self;
    if (!f) {
        return YETTY_ERR(uint32, "font is NULL");
    }
    return load_one(f, cp);
}

static struct uint32_result raster_get_codepoint(struct yetty_yfont_font *self,
                                                 uint32_t glyph_index)
{
    struct yetty_yfont_raster_font *f = (struct yetty_yfont_raster_font *)self;
    if (!f) {
        return YETTY_ERR(uint32, "font is NULL");
    }
    const uint32_t *cp = yetty_ycore_map_get(&f->slot_to_cp, glyph_index);
    if (!cp) {
        return YETTY_ERR(uint32, "unknown glyph_index");
    }
    return YETTY_OK(uint32, *cp);
}

static struct uint32_result raster_get_glyph_index_styled(struct yetty_yfont_font *self,
                                                          uint32_t cp, enum yetty_yfont_style style)
{
    (void)style;
    return raster_get_glyph_index(self, cp);
}

static struct yetty_ycore_void_result raster_load_glyphs(struct yetty_yfont_font *self,
                                                         const uint32_t *cps, size_t count)
{
    struct yetty_yfont_raster_font *f = (struct yetty_yfont_raster_font *)self;
    if (!f) {
        return YETTY_ERR(yetty_ycore_void, "font is NULL");
    }
    for (size_t i = 0; i < count; i++) {
        load_one(f, cps[i]);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result raster_load_basic_latin(struct yetty_yfont_font *self)
{
    struct yetty_yfont_raster_font *f = (struct yetty_yfont_raster_font *)self;
    if (!f) {
        return YETTY_ERR(yetty_ycore_void, "font is NULL");
    }
    for (uint32_t cp = 0x20; cp <= 0x7E; cp++) {
        load_one(f, cp);
    }
    return YETTY_OK_VOID();
}

static struct float_result raster_get_advance(struct yetty_yfont_font *self, uint32_t cp,
                                              float font_size)
{
    struct yetty_yfont_raster_font *f = (struct yetty_yfont_raster_font *)self;
    if (!f) {
        return YETTY_ERR(float, "font is NULL");
    }
    if (f->units_per_em <= 0.0f) {
        return YETTY_ERR(float, "invalid units_per_EM");
    }

    FT_UInt gid = FT_Get_Char_Index(f->ft_face, cp);
    if (gid == 0) {
        return YETTY_ERR(float, "glyph not found");
    }

    FT_Fixed adv_fu = 0;
    if (FT_Get_Advance(f->ft_face, gid, FT_LOAD_NO_SCALE, &adv_fu) != 0) {
        return YETTY_ERR(float, "FT_Get_Advance failed");
    }

    return YETTY_OK(float, (float)adv_fu *font_size / f->units_per_em);
}

static struct float_result raster_measure_text(struct yetty_yfont_font *self, const char *utf8,
                                               size_t len, float font_size)
{
    struct yetty_yfont_raster_font *f = (struct yetty_yfont_raster_font *)self;
    if (!f) {
        return YETTY_ERR(float, "font is NULL");
    }
    if (!utf8) {
        return YETTY_ERR(float, "utf8 is NULL");
    }
    if (f->units_per_em <= 0.0f) {
        return YETTY_ERR(float, "invalid units_per_EM");
    }

    const uint8_t *p = (const uint8_t *)utf8;
    const uint8_t *end = p + len;
    FT_Pos total_fu = 0;

    while (p < end) {
        uint32_t cp = 0;
        uint8_t b = *p;
        if ((b & 0x80) == 0) {
            cp = b;
            p += 1;
        } else if ((b & 0xE0) == 0xC0 && p + 1 < end) {
            cp = ((uint32_t)(b & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if ((b & 0xF0) == 0xE0 && p + 2 < end) {
            cp = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
        } else if ((b & 0xF8) == 0xF0 && p + 3 < end) {
            cp = ((uint32_t)(b & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
                 ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            p += 4;
        } else {
            p += 1;
            continue;
        }

        FT_UInt gid = FT_Get_Char_Index(f->ft_face, cp);
        if (gid == 0) {
            continue;
        }
        FT_Fixed adv_fu = 0;
        if (FT_Get_Advance(f->ft_face, gid, FT_LOAD_NO_SCALE, &adv_fu) == 0) {
            total_fu += adv_fu;
        }
    }

    return YETTY_OK(float, (float)total_fu *font_size / f->units_per_em);
}

static float raster_get_base_size(const struct yetty_yfont_font *self)
{
    const struct yetty_yfont_raster_font *f = (const struct yetty_yfont_raster_font *)self;
    return f ? f->base_size : 32.0f;
}

static int raster_is_dirty(const struct yetty_yfont_font *self)
{
    const struct yetty_yfont_raster_font *f = (const struct yetty_yfont_raster_font *)self;
    return f ? f->dirty : 0;
}

static struct yetty_yrender_gpu_resource_set_result raster_get_gpu_resource_set(
    struct yetty_yfont_font *self)
{
    struct yetty_yfont_raster_font *f = (struct yetty_yfont_raster_font *)self;
    if (!f) {
        return YETTY_ERR(yetty_yrender_gpu_resource_set, "font is NULL");
    }
    if (!f->has_atlas) {
        return YETTY_ERR(yetty_yrender_gpu_resource_set,
                         "font is in metrics-only mode (no shader loaded)");
    }

    if (f->dirty) {
        f->rs.textures[0].data = f->atlas_pixels;
        f->rs.textures[0].width = f->atlas_width;
        f->rs.textures[0].height = f->atlas_height;
        f->rs.textures[0].dirty = 1;

        f->rs.buffers[0].data = (uint8_t *)f->meta;
        f->rs.buffers[0].size = (size_t)f->next_slot * sizeof(struct yetty_yfont_glyph_meta_gpu);
        f->rs.buffers[0].dirty = 1;

        f->rs.uniforms[0].f32 = f->base_size;
        f->rs.uniforms[1].u32 = f->cell_size;
        f->rs.uniforms[2].u32 = f->atlas_cols;

        f->dirty = 0;
    }

    return YETTY_OK(yetty_yrender_gpu_resource_set, &f->rs);
}

static const struct yetty_yfont_font_ops raster_font_ops = {
    .destroy = raster_destroy,
    .get_codepoint = raster_get_codepoint,
    .get_glyph_index = raster_get_glyph_index,
    .get_glyph_index_styled = raster_get_glyph_index_styled,
    .load_glyphs = raster_load_glyphs,
    .load_basic_latin = raster_load_basic_latin,
    .get_advance = raster_get_advance,
    .measure_text = raster_measure_text,
    .get_base_size = raster_get_base_size,
    .is_dirty = raster_is_dirty,
    .get_gpu_resource_set = raster_get_gpu_resource_set,
};

/*=============================================================================
 * Construction
 *===========================================================================*/

/* Common init after ft_face is set up. shader_path may be NULL (metrics-only). */
static struct yetty_font_font_result raster_font_finalise(struct yetty_yfont_raster_font *f,
                                                          const char *shader_path)
{
    f->base.ops = &raster_font_ops;
    f->units_per_em = (float)f->ft_face->units_per_EM;
    if (f->base_size <= 0.0f) {
        f->base_size = 32.0f;
    }

    if (yetty_ycore_map_init(&f->glyph_map, MAP_CAPACITY) < 0) {
        raster_destroy(&f->base);
        return YETTY_ERR(yetty_font_font, "map init failed");
    }
    if (yetty_ycore_map_init(&f->slot_to_cp, MAP_CAPACITY) < 0) {
        raster_destroy(&f->base);
        return YETTY_ERR(yetty_font_font, "inverse map init failed");
    }

    f->meta_capacity = 256;
    f->meta = calloc(f->meta_capacity, sizeof(struct yetty_yfont_glyph_meta_gpu));
    if (!f->meta) {
        raster_destroy(&f->base);
        return YETTY_ERR(yetty_font_font, "meta allocation failed");
    }
    f->next_slot = 1; /* slot 0 reserved for empty/unresolved */

    if (!shader_path) {
        /* Metrics-only mode. */
        yinfo("raster_font: metrics-only (base_size=%.0f, upem=%.0f)", f->base_size,
              f->units_per_em);
        return YETTY_OK(yetty_font_font, &f->base);
    }

    /* Full mode: load shader + atlas + GPU resource set. */
    struct yetty_ycore_buffer_result sres = yetty_ycore_read_file(shader_path);
    if (YETTY_IS_ERR(sres)) {
        raster_destroy(&f->base);
        return YETTY_ERR(yetty_font_font, sres.error.msg);
    }
    f->shader_code = sres.value;

    f->has_atlas = 1;
    f->dirty = 1;
    f->cell_size = DEFAULT_CELL_SIZE;
    f->atlas_width = ATLAS_INITIAL_W;
    f->atlas_cols = f->atlas_width / f->cell_size;
    f->atlas_height = ATLAS_INITIAL_H;
    f->next_cell = 0;

    size_t atlas_bytes = (size_t)f->atlas_width * f->atlas_height;
    f->atlas_pixels = calloc(atlas_bytes, 1);
    if (!f->atlas_pixels) {
        raster_destroy(&f->base);
        return YETTY_ERR(yetty_font_font, "atlas allocation failed");
    }

    /* Set freetype pixel size for rasterization. */
    if (FT_Set_Pixel_Sizes(f->ft_face, 0, (FT_UInt)f->base_size) != 0) {
        raster_destroy(&f->base);
        return YETTY_ERR(yetty_font_font, "FT_Set_Pixel_Sizes failed");
    }

    /* GPU resource set */
    strncpy(f->rs.namespace, "raster_font", YETTY_YRENDER_NAME_MAX - 1);

    f->rs.texture_count = 1;
    struct yetty_yrender_texture *tex = &f->rs.textures[0];
    strncpy(tex->name, "texture", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(tex->wgsl_type, "texture_2d<f32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    strncpy(tex->sampler_name, "sampler", YETTY_YRENDER_NAME_MAX - 1);
    tex->format = YETTY_YRENDER_TEXTURE_FORMAT_R8_UNORM;
    tex->sampler_filter = YETTY_YRENDER_FILTER_LINEAR;

    f->rs.buffer_count = 1;
    struct yetty_yrender_buffer *buf = &f->rs.buffers[0];
    strncpy(buf->name, "buffer", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(buf->wgsl_type, "array<f32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    buf->readonly = 1;

    f->rs.uniform_count = 3;
    strncpy(f->rs.uniforms[0].name, "base_size", YETTY_YRENDER_NAME_MAX - 1);
    f->rs.uniforms[0].type = YETTY_YRENDER_UNIFORM_F32;
    f->rs.uniforms[0].f32 = f->base_size;

    strncpy(f->rs.uniforms[1].name, "cell_size", YETTY_YRENDER_NAME_MAX - 1);
    f->rs.uniforms[1].type = YETTY_YRENDER_UNIFORM_U32;
    f->rs.uniforms[1].u32 = f->cell_size;

    strncpy(f->rs.uniforms[2].name, "atlas_cols", YETTY_YRENDER_NAME_MAX - 1);
    f->rs.uniforms[2].type = YETTY_YRENDER_UNIFORM_U32;
    f->rs.uniforms[2].u32 = f->atlas_cols;

    yetty_yrender_shader_code_set(&f->rs.shader, (const char *)f->shader_code.data,
                                  f->shader_code.size);

    yinfo("raster_font: base_size=%.0f upem=%.0f atlas=%ux%u shader=%s", f->base_size,
          f->units_per_em, f->atlas_width, f->atlas_height, shader_path);
    return YETTY_OK(yetty_font_font, &f->base);
}

struct yetty_font_font_result yetty_yfont_raster_font_create_from_file(const char *ttf_path,
                                                                       const char *shader_path,
                                                                       float base_size)
{
    if (!ttf_path) {
        return YETTY_ERR(yetty_font_font, "ttf_path is NULL");
    }

    struct yetty_yfont_raster_font *f = calloc(1, sizeof(struct yetty_yfont_raster_font));
    if (!f) {
        return YETTY_ERR(yetty_font_font, "allocation failed");
    }
    f->base_size = base_size;

    if (FT_Init_FreeType(&f->ft_library) != 0) {
        free(f);
        return YETTY_ERR(yetty_font_font, "FT_Init_FreeType failed");
    }
    if (FT_New_Face(f->ft_library, ttf_path, 0, &f->ft_face) != 0) {
        FT_Done_FreeType(f->ft_library);
        free(f);
        return YETTY_ERR(yetty_font_font, "FT_New_Face failed");
    }

    return raster_font_finalise(f, shader_path);
}

struct yetty_font_font_result yetty_yfont_raster_font_create_from_data(const uint8_t *ttf_data,
                                                                       size_t ttf_size,
                                                                       const char *name,
                                                                       const char *shader_path,
                                                                       float base_size)
{
    (void)name;
    if (!ttf_data || ttf_size == 0) {
        return YETTY_ERR(yetty_font_font, "ttf_data is empty");
    }

    struct yetty_yfont_raster_font *f = calloc(1, sizeof(struct yetty_yfont_raster_font));
    if (!f) {
        return YETTY_ERR(yetty_font_font, "allocation failed");
    }
    f->base_size = base_size;

    /* FreeType doesn't copy the input bytes, so we own a copy. */
    f->ttf_owned = malloc(ttf_size);
    if (!f->ttf_owned) {
        free(f);
        return YETTY_ERR(yetty_font_font, "ttf copy failed");
    }
    memcpy(f->ttf_owned, ttf_data, ttf_size);
    f->ttf_size = ttf_size;

    if (FT_Init_FreeType(&f->ft_library) != 0) {
        free(f->ttf_owned);
        free(f);
        return YETTY_ERR(yetty_font_font, "FT_Init_FreeType failed");
    }
    if (FT_New_Memory_Face(f->ft_library, f->ttf_owned, (FT_Long)f->ttf_size, 0, &f->ft_face) !=
        0) {
        FT_Done_FreeType(f->ft_library);
        free(f->ttf_owned);
        free(f);
        return YETTY_ERR(yetty_font_font, "FT_New_Memory_Face failed");
    }

    return raster_font_finalise(f, shader_path);
}
