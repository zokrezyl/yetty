/*
 * msdf-font.c - Non-monospace MSDF font implementation
 *
 * Reads .cdb files (glyph header + RGBA bitmap per glyph).
 * Uses uniform cell grid atlas — all glyphs in same-size cells.
 * UV computed from slot index; bearing/advance handle layout.
 * Implements yetty_font_font interface (non-monospace).
 * Can be used at any font size — shader scales from base_size.
 */

#include <yetty/yfont/msdf-font.h>
#include <yetty/yfont/font.h>
#include <yetty/ymsdf-gen/ymsdf-gen.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/ycdb/ycdb.h>
#include <yetty/ycore/map.h>
#include <yetty/ycore/util.h>
#include <yetty/yrender/texture-format.h>
#include <yetty/ytrace/ytrace.h>

#include <stdio.h> /* snprintf for namespace */
#include <stdlib.h>
#include <string.h>

#define ATLAS_INITIAL_W 1024
#define ATLAS_INITIAL_H 512
#define ATLAS_MAX_DIM 16384
#define ATLAS_PADDING 2
#define MAP_CAPACITY 8192

/* Per-glyph GPU metadata (24 bytes, 6 floats)
 * cell_idx stores the actual atlas cell (may differ from slot due to empty glyphs).
 */
struct yetty_yfont_glyph_meta_gpu {
    float size_x, size_y;       /* logical glyph size */
    float bearing_x, bearing_y; /* offset from pen position */
    float advance;              /* horizontal advance */
    float cell_idx;             /* atlas cell index for UV computation */
};

struct yetty_yfont_msdf_font {
    struct yetty_yfont_font base;

    struct yetty_ycdb_reader *cdb;

    uint8_t *atlas_pixels;
    uint32_t atlas_width;
    uint32_t atlas_height;

    /* Uniform cell grid */
    uint32_t cell_size;  /* uniform cell size (square) */
    uint32_t atlas_cols; /* cells per row */
    uint32_t next_cell;  /* next cell index to use */

    struct yetty_yfont_glyph_meta_gpu *meta;
    uint32_t meta_capacity;
    uint32_t next_slot;

    /* Forward (cp → slot) and inverse (slot → cp) maps. The inverse map
     * is populated alongside the forward one so clipboard extraction can
     * recover the codepoint from a glyph index stored in a ydraw glyph
     * prim. */
    struct yetty_ycore_map glyph_map;
    struct yetty_ycore_map slot_to_cp;

    float base_size; /* font size CDB was generated at */
    float pixel_range;

    /* Shader code (owned) */
    struct yetty_ycore_buffer shader_code;

    struct yetty_yrender_gpu_resource_set rs;
    int dirty;
};

/*=============================================================================
 * Atlas helpers
 *===========================================================================*/

static void atlas_grow(struct yetty_yfont_msdf_font *f)
{
    /* Add more rows */
    uint32_t rows_to_add = 4;
    uint32_t new_h = f->atlas_height + rows_to_add * f->cell_size;
    if (new_h > ATLAS_MAX_DIM) {
        return;
    }

    size_t old_sz = (size_t)f->atlas_width * f->atlas_height * 4;
    size_t new_sz = (size_t)f->atlas_width * new_h * 4;
    uint8_t *p = realloc(f->atlas_pixels, new_sz);
    if (!p) {
        return;
    }

    memset(p + old_sz, 0, new_sz - old_sz);
    f->atlas_pixels = p;
    f->atlas_height = new_h;
}

static struct uint32_result load_one(struct yetty_yfont_msdf_font *f, uint32_t cp)
{
    const uint32_t *existing = yetty_ycore_map_get(&f->glyph_map, cp);
    if (existing) {
        return YETTY_OK(uint32, *existing);
    }

    uint32_t key = cp;
    void *data = NULL;
    size_t data_len = 0;

    struct yetty_ycore_void_result res =
        yetty_ycdb_reader_get(f->cdb, &key, sizeof(key), &data, &data_len);
    if (YETTY_IS_ERR(res)) {
        return YETTY_ERR(uint32, res.error.msg);
    }
    if (!data) {
        ydebug("msdf_font: glyph cp=U+%04X not in CDB", cp);
        return YETTY_ERR(uint32, "glyph not found");
    }

    if (data_len < sizeof(struct yetty_ymsdf_gen_glyph_header)) {
        free(data);
        return YETTY_ERR(uint32, "glyph data too small");
    }

    struct yetty_ymsdf_gen_glyph_header hdr;
    memcpy(&hdr, data, sizeof(hdr));
    const uint8_t *pixels = (const uint8_t *)data + sizeof(hdr);
    size_t pixel_bytes = (size_t)hdr.width * hdr.height * 4;

    if (data_len < sizeof(hdr) + pixel_bytes) {
        free(data);
        return YETTY_ERR(uint32, "glyph pixel data truncated");
    }

    uint32_t slot = f->next_slot;

    if (slot >= f->meta_capacity) {
        uint32_t new_cap = f->meta_capacity * 2;
        struct yetty_yfont_glyph_meta_gpu *new_meta =
            realloc(f->meta, new_cap * sizeof(struct yetty_yfont_glyph_meta_gpu));
        if (!new_meta) {
            free(data);
            return YETTY_ERR(uint32, "meta realloc failed");
        }
        f->meta = new_meta;
        f->meta_capacity = new_cap;
    }

    /* Empty glyph (space) — just metadata, no atlas pixels */
    if (hdr.width == 0 || hdr.height == 0) {
        struct yetty_yfont_glyph_meta_gpu *m = &f->meta[slot];
        memset(m, 0, sizeof(*m));
        m->advance = hdr.advance;
        m->cell_idx = -1.0f; /* No atlas cell for empty glyphs */
        f->next_slot++;
        yetty_ycore_map_put(&f->glyph_map, cp, slot);
        yetty_ycore_map_put(&f->slot_to_cp, slot, cp);
        f->dirty = 1;
        free(data);
        return YETTY_OK(uint32, slot);
    }

    /* Place in uniform cell grid */
    uint32_t cell_idx = f->next_cell;
    uint32_t col = cell_idx % f->atlas_cols;
    uint32_t row = cell_idx / f->atlas_cols;

    /* Grow atlas if needed */
    while ((row + 1) * f->cell_size > f->atlas_height) {
        atlas_grow(f);
    }

    uint32_t ax = col * f->cell_size;
    uint32_t ay = row * f->cell_size;

    /* Center glyph in cell */
    uint32_t gw = hdr.width;
    uint32_t gh = hdr.height;
    uint32_t ox = (f->cell_size - gw) / 2;
    uint32_t oy = (f->cell_size - gh) / 2;

    /* Copy pixels into atlas cell */
    for (uint32_t y = 0; y < gh; y++) {
        size_t dst = ((size_t)(ay + oy + y) * f->atlas_width + ax + ox) * 4;
        size_t src = (size_t)y * gw * 4;
        memcpy(f->atlas_pixels + dst, pixels + src, gw * 4);
    }

    f->next_cell++;

    /* Fill metadata — cell_idx used by shader for UV computation */
    struct yetty_yfont_glyph_meta_gpu *m = &f->meta[slot];
    m->size_x = hdr.size_x;
    m->size_y = hdr.size_y;
    m->bearing_x = hdr.bearing_x;
    m->bearing_y = hdr.bearing_y;
    m->advance = hdr.advance;
    m->cell_idx = (float)cell_idx;

    f->next_slot++;
    yetty_ycore_map_put(&f->glyph_map, cp, slot);
    yetty_ycore_map_put(&f->slot_to_cp, slot, cp);
    f->dirty = 1;
    free(data);
    return YETTY_OK(uint32, slot);
}

/*=============================================================================
 * Vtable
 *===========================================================================*/

static void msdf_destroy(struct yetty_yfont_font *self)
{
    struct yetty_yfont_msdf_font *font = (struct yetty_yfont_msdf_font *)self;
    if (!font) {
        return;
    }
    free(font->atlas_pixels);
    free(font->meta);
    free(font->shader_code.data);
    yetty_ycore_map_destroy(&font->glyph_map);
    yetty_ycore_map_destroy(&font->slot_to_cp);
    yetty_ycdb_reader_close(font->cdb);
    free(font);
}

static struct uint32_result msdf_get_codepoint(struct yetty_yfont_font *self, uint32_t glyph_index)
{
    struct yetty_yfont_msdf_font *f = (struct yetty_yfont_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(uint32, "font is NULL");
    }
    const uint32_t *cp = yetty_ycore_map_get(&f->slot_to_cp, glyph_index);
    if (!cp) {
        return YETTY_ERR(uint32, "unknown glyph_index");
    }
    return YETTY_OK(uint32, *cp);
}

static struct uint32_result msdf_get_glyph_index(struct yetty_yfont_font *self, uint32_t cp)
{
    struct yetty_yfont_msdf_font *f = (struct yetty_yfont_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(uint32, "font is NULL");
    }
    return load_one(f, cp);
}

static struct uint32_result msdf_get_glyph_index_styled(struct yetty_yfont_font *self, uint32_t cp,
                                                        enum yetty_yfont_style style)
{
    (void)style;
    return msdf_get_glyph_index(self, cp);
}

static struct yetty_ycore_void_result msdf_load_glyphs(struct yetty_yfont_font *self,
                                                       const uint32_t *cps, size_t count)
{
    struct yetty_yfont_msdf_font *f = (struct yetty_yfont_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(yetty_ycore_void, "font is NULL");
    }
    for (size_t i = 0; i < count; i++) {
        load_one(f, cps[i]);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result msdf_load_basic_latin(struct yetty_yfont_font *self)
{
    struct yetty_yfont_msdf_font *f = (struct yetty_yfont_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(yetty_ycore_void, "font is NULL");
    }
    for (uint32_t cp = 0x20; cp <= 0x7E; cp++) {
        load_one(f, cp);
    }
    return YETTY_OK_VOID();
}

static float msdf_get_base_size(const struct yetty_yfont_font *self)
{
    const struct yetty_yfont_msdf_font *f = (const struct yetty_yfont_msdf_font *)self;
    return f ? f->base_size : 32.0f;
}

/* Read only the glyph header from CDB (no atlas allocation). */
static struct float_result msdf_read_advance_units(struct yetty_yfont_msdf_font *f, uint32_t cp)
{
    uint32_t key = cp;
    void *data = NULL;
    size_t data_len = 0;

    struct yetty_ycore_void_result res =
        yetty_ycdb_reader_get(f->cdb, &key, sizeof(key), &data, &data_len);
    if (YETTY_IS_ERR(res)) {
        return YETTY_ERR(float, res.error.msg);
    }
    if (!data) {
        return YETTY_ERR(float, "glyph not found");
    }
    if (data_len < sizeof(struct yetty_ymsdf_gen_glyph_header)) {
        free(data);
        return YETTY_ERR(float, "glyph data too small");
    }

    struct yetty_ymsdf_gen_glyph_header hdr;
    memcpy(&hdr, data, sizeof(hdr));
    free(data);
    return YETTY_OK(float, hdr.advance);
}

static struct float_result msdf_get_advance(struct yetty_yfont_font *self, uint32_t cp,
                                            float font_size)
{
    struct yetty_yfont_msdf_font *f = (struct yetty_yfont_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(float, "font is NULL");
    }
    if (f->base_size <= 0.0f) {
        return YETTY_ERR(float, "invalid base size");
    }

    struct float_result adv = msdf_read_advance_units(f, cp);
    if (YETTY_IS_ERR(adv)) {
        return adv;
    }

    /* CDB stores advance in pixels at base_size. Scale to requested size. */
    return YETTY_OK(float, adv.value *font_size / f->base_size);
}

static struct float_result msdf_measure_text(struct yetty_yfont_font *self, const char *utf8,
                                             size_t len, float font_size)
{
    struct yetty_yfont_msdf_font *f = (struct yetty_yfont_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(float, "font is NULL");
    }
    if (!utf8) {
        return YETTY_ERR(float, "utf8 is NULL");
    }
    if (f->base_size <= 0.0f) {
        return YETTY_ERR(float, "invalid base size");
    }

    const uint8_t *p = (const uint8_t *)utf8;
    const uint8_t *end = p + len;
    float total = 0.0f;

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

        struct float_result adv = msdf_read_advance_units(f, cp);
        if (YETTY_IS_OK(adv)) {
            total += adv.value;
        }
    }

    return YETTY_OK(float, total *font_size / f->base_size);
}

static int msdf_is_dirty(const struct yetty_yfont_font *self)
{
    return ((const struct yetty_yfont_msdf_font *)self)->dirty;
}

static struct yetty_yrender_gpu_resource_set_result msdf_get_gpu_resource_set(
    struct yetty_yfont_font *self)
{
    struct yetty_yfont_msdf_font *f = (struct yetty_yfont_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(yetty_yrender_gpu_resource_set, "font is NULL");
    }

    if (f->dirty) {
        f->rs.textures[0].data = f->atlas_pixels;
        f->rs.textures[0].width = f->atlas_width;
        f->rs.textures[0].height = f->atlas_height;
        f->rs.textures[0].dirty = 1;

        f->rs.buffers[0].data = (uint8_t *)f->meta;
        f->rs.buffers[0].size = (size_t)f->next_slot * sizeof(struct yetty_yfont_glyph_meta_gpu);
        f->rs.buffers[0].dirty = 1;

        f->rs.uniforms[0].f32 = f->pixel_range;
        f->rs.uniforms[1].f32 = f->base_size;
        f->rs.uniforms[2].u32 = f->cell_size;
        f->rs.uniforms[3].u32 = f->atlas_cols;

        f->dirty = 0;
    }

    return YETTY_OK(yetty_yrender_gpu_resource_set, &f->rs);
}

static const struct yetty_yfont_font_ops msdf_font_ops = {
    .destroy = msdf_destroy,
    .get_codepoint = msdf_get_codepoint,
    .get_glyph_index = msdf_get_glyph_index,
    .get_glyph_index_styled = msdf_get_glyph_index_styled,
    .load_glyphs = msdf_load_glyphs,
    .load_basic_latin = msdf_load_basic_latin,
    .get_advance = msdf_get_advance,
    .measure_text = msdf_measure_text,
    .get_base_size = msdf_get_base_size,
    .is_dirty = msdf_is_dirty,
    .get_gpu_resource_set = msdf_get_gpu_resource_set,
};

/*=============================================================================
 * Create
 *===========================================================================*/

#define DEFAULT_CELL_SIZE 64

struct yetty_font_font_result yetty_yfont_msdf_font_create(const char *cdb_path,
                                                           const char *shader_path,
                                                           const char *namespace)
{
    if (!cdb_path) {
        return YETTY_ERR(yetty_font_font, "cdb_path is NULL");
    }
    if (!shader_path) {
        return YETTY_ERR(yetty_font_font, "shader_path is NULL");
    }
    if (!namespace || !*namespace) {
        namespace = "msdf_font";
    }

    ydebug("msdf_font: opening %s, shader %s", cdb_path, shader_path);

    /* Load shader from file */
    struct yetty_ycore_buffer_result shader_res = yetty_ycore_read_file(shader_path);
    if (YETTY_IS_ERR(shader_res)) {
        return YETTY_ERR(yetty_font_font, shader_res.error.msg);
    }

    struct yetty_ycdb_reader_result cdb_res = yetty_ycdb_reader_open(cdb_path);
    if (YETTY_IS_ERR(cdb_res)) {
        free(shader_res.value.data);
        return YETTY_ERR(yetty_font_font, cdb_res.error.msg);
    }

    struct yetty_yfont_msdf_font *font = calloc(1, sizeof(struct yetty_yfont_msdf_font));
    if (!font) {
        free(shader_res.value.data);
        yetty_ycdb_reader_close(cdb_res.value);
        return YETTY_ERR(yetty_font_font, "allocation failed");
    }

    font->shader_code = shader_res.value;
    font->base.ops = &msdf_font_ops;
    font->cdb = cdb_res.value;
    font->base_size = 32.0f; /* TODO: read from CDB metadata */
    font->pixel_range = 4.0f;
    font->dirty = 1;

    /* Uniform cell grid */
    font->cell_size = DEFAULT_CELL_SIZE;
    font->atlas_width = ATLAS_INITIAL_W;
    font->atlas_cols = font->atlas_width / font->cell_size;
    font->atlas_height = ATLAS_INITIAL_H;
    font->next_cell = 0;

    size_t atlas_bytes = (size_t)font->atlas_width * font->atlas_height * 4;
    font->atlas_pixels = calloc(atlas_bytes, 1);
    if (!font->atlas_pixels) {
        free(font->shader_code.data);
        yetty_ycdb_reader_close(font->cdb);
        free(font);
        return YETTY_ERR(yetty_font_font, "atlas allocation failed");
    }

    font->meta_capacity = 256;
    font->meta = calloc(font->meta_capacity, sizeof(struct yetty_yfont_glyph_meta_gpu));
    if (!font->meta) {
        free(font->atlas_pixels);
        free(font->shader_code.data);
        yetty_ycdb_reader_close(font->cdb);
        free(font);
        return YETTY_ERR(yetty_font_font, "meta allocation failed");
    }
    font->next_slot = 1;

    if (yetty_ycore_map_init(&font->glyph_map, MAP_CAPACITY) < 0) {
        free(font->meta);
        free(font->atlas_pixels);
        free(font->shader_code.data);
        yetty_ycdb_reader_close(font->cdb);
        free(font);
        return YETTY_ERR(yetty_font_font, "map init failed");
    }
    if (yetty_ycore_map_init(&font->slot_to_cp, MAP_CAPACITY) < 0) {
        yetty_ycore_map_destroy(&font->glyph_map);
        free(font->meta);
        free(font->atlas_pixels);
        free(font->shader_code.data);
        yetty_ycdb_reader_close(font->cdb);
        free(font);
        return YETTY_ERR(yetty_font_font, "inverse map init failed");
    }

    /* GPU resource set. The namespace must be unique among msdf-font
     * instances attached to the same layer — the merged WGSL would
     * otherwise have duplicate uniform fields, duplicate helper functions
     * and duplicate `<ns>_buffer_offset` constants. Caller picks the
     * namespace from a stable identifier (PDF: FNV1a hash of TTF bytes;
     * default font: family name). The shader source uses `__NS__` as a
     * placeholder; the pipeline binder substitutes it at shader-merge
     * time. See shader_append + generate_wgsl_bindings in pipeline.c. */
    snprintf(font->rs.namespace, YETTY_YRENDER_NAME_MAX, "msdf_%s", namespace);

    font->rs.texture_count = 1;
    struct yetty_yrender_texture *tex = &font->rs.textures[0];
    strncpy(tex->name, "texture", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(tex->wgsl_type, "texture_2d<f32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    strncpy(tex->sampler_name, "sampler", YETTY_YRENDER_NAME_MAX - 1);
    tex->format = YETTY_YRENDER_TEXTURE_FORMAT_RGBA8_UNORM;
    tex->sampler_filter = YETTY_YRENDER_FILTER_LINEAR;

    font->rs.buffer_count = 1;
    struct yetty_yrender_buffer *buf = &font->rs.buffers[0];
    strncpy(buf->name, "buffer", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(buf->wgsl_type, "array<f32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    buf->readonly = 1;

    font->rs.uniform_count = 5;
    strncpy(font->rs.uniforms[0].name, "pixel_range", YETTY_YRENDER_NAME_MAX - 1);
    font->rs.uniforms[0].type = YETTY_YRENDER_UNIFORM_F32;
    font->rs.uniforms[0].f32 = font->pixel_range;

    strncpy(font->rs.uniforms[1].name, "base_size", YETTY_YRENDER_NAME_MAX - 1);
    font->rs.uniforms[1].type = YETTY_YRENDER_UNIFORM_F32;
    font->rs.uniforms[1].f32 = font->base_size;

    strncpy(font->rs.uniforms[2].name, "cell_size", YETTY_YRENDER_NAME_MAX - 1);
    font->rs.uniforms[2].type = YETTY_YRENDER_UNIFORM_U32;
    font->rs.uniforms[2].u32 = font->cell_size;

    strncpy(font->rs.uniforms[3].name, "atlas_cols", YETTY_YRENDER_NAME_MAX - 1);
    font->rs.uniforms[3].type = YETTY_YRENDER_UNIFORM_U32;
    font->rs.uniforms[3].u32 = font->atlas_cols;

    /* Atlas sub-region (u_min, v_min, u_max, v_max) inside the packed
     * mega-atlas. Filled in by the binder at bind-group time — it sees a
     * VEC4 uniform named `<texname>_region` and writes the texture's
     * normalised position into it. We only need to declare it here. */
    strncpy(font->rs.uniforms[4].name, "texture_region", YETTY_YRENDER_NAME_MAX - 1);
    font->rs.uniforms[4].type = YETTY_YRENDER_UNIFORM_VEC4;

    yetty_yrender_shader_code_set(&font->rs.shader, (const char *)font->shader_code.data,
                                  font->shader_code.size);

    yinfo("msdf_font: created from %s, base_size=%.0f", cdb_path, font->base_size);

    return YETTY_OK(yetty_font_font, &font->base);
}
