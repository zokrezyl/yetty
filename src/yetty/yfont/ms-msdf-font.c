/*
 * ms-msdf-font.c - Monospace MSDF font implementation
 *
 * Reads .cdb files (glyph header + RGBA bitmap per glyph).
 * Packs glyphs into own atlas, builds per-glyph UV metadata buffer.
 * Implements yetty_font_ms_font interface.
 */

#include <yetty/yfont/ms-msdf-font.h>
#include <yetty/yfont/ms-font.h>
#include <yetty/ymsdf-gen/ymsdf-gen.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/ycdb/ycdb.h>
#include <yetty/ycore/map.h>
#include <yetty/ycore/util.h>
#include <yetty/yrender/texture-format.h>
#include <yetty/ytrace/ytrace.h>

#include <stdlib.h>
#include <string.h>

#define ATLAS_INITIAL_W 1024
#define ATLAS_INITIAL_H 512
#define ATLAS_MAX_DIM 16384
#define ATLAS_PADDING 2
#define MAP_CAPACITY 8192

/*=============================================================================
 * Per-glyph GPU metadata (40 bytes, matches old GlyphMetadataGPU)
 *===========================================================================*/

struct yetty_yfont_glyph_meta_gpu {
    float uv_min_x, uv_min_y;
    float uv_max_x, uv_max_y;
    float size_x, size_y;
    float bearing_x, bearing_y;
    float advance;
    float _pad;
};

/*=============================================================================
 * Internal struct
 *===========================================================================*/

struct yetty_yfont_ms_msdf_font {
    struct yetty_yfont_ms_font base;

    /* One CDB reader per style (Regular, Bold, Italic, BoldItalic), indexed
     * by enum yetty_yfont_ms_style. cdb[REGULAR] is required; the others are
     * NULL when that variant isn't on disk and fall back to Regular. All
     * variants share the single atlas / meta buffer below — glyphs are keyed
     * by (style, codepoint) so the same character in different styles gets
     * distinct slots. */
    struct yetty_ycdb_reader *cdb[4];

    /* Atlas (RGBA8) */
    uint8_t *atlas_pixels;
    uint32_t atlas_width;
    uint32_t atlas_height;

    /* Shelf packer */
    uint32_t shelf_x;
    uint32_t shelf_y;
    uint32_t shelf_height;

    /* Per-glyph metadata buffer */
    struct yetty_yfont_glyph_meta_gpu *meta;
    uint32_t meta_capacity;
    uint32_t next_slot;

    /* Codepoint -> slot (forward) and slot -> codepoint (inverse).
     * The inverse map is populated alongside the forward one in load_one
     * so the font can answer get_codepoint(glyph_index) for clipboard
     * extraction without scanning the forward map. */
    struct yetty_ycore_map glyph_map;
    struct yetty_ycore_map slot_to_cp;

    /* Font sizing */
    float base_size;      /* CDB generation font size */
    float requested_size; /* user-requested font size */
    float pixel_range;

    /* Font metrics in CDB base-size units, captured as glyphs load.
	 *   max_ascent  = max(bearing_y)          — visible above baseline (padding-inflated)
	 *   max_descent = max(size_y - bearing_y) — visible below baseline (padding-inflated)
	 *   advance_cdb = horizontal pen step per glyph; identical for every
	 *                 glyph in a monospace font, so the first one wins.
	 *                 This is the designer's chosen letter spacing — what
	 *                 alacritty et al. use for cell width.
	 * max_ascent and max_descent include the msdfgen pixel_range pad on
	 * each edge; strip pixel_range per edge to get the visible extent.
	 * advance does NOT include any padding — it is a pure font metric. */
    float max_ascent;
    float max_descent;
    float advance_cdb;

    /* Cell padding (fractions of glyph dimensions). Cell wraps the glyph
	 * extent with padding on each side. */
    struct yetty_yfont_ms_padding padding;

    /* Shader code (owned) */
    struct yetty_ycore_buffer shader_code;

    /* GPU resource set */
    struct yetty_ydraw_gpu_resource_set rs;
    int dirty;
};

/*=============================================================================
 * Atlas helpers
 *===========================================================================*/

static void atlas_grow(struct yetty_yfont_ms_msdf_font *f)
{
    uint32_t new_h = f->atlas_height + 512;
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

static struct uint32_result load_one(struct yetty_yfont_ms_msdf_font *f, uint32_t cp,
                                     enum yetty_yfont_ms_style style)
{
    if ((int)style < 0 || (int)style > 3) {
        style = YETTY_YFONT_MS_STYLE_REGULAR;
    }
    /* Variant not on disk → render that style with the Regular face. */
    struct yetty_ycdb_reader *cdb = f->cdb[style] ? f->cdb[style] : f->cdb[YETTY_YFONT_MS_STYLE_REGULAR];
    if (!f->cdb[style]) {
        style = YETTY_YFONT_MS_STYLE_REGULAR;
    }

    /* Glyphs are keyed by (style, codepoint): the same character in two
     * styles occupies two atlas slots. cp is ≤ 0x10FFFF so it fits in the
     * low 24 bits, leaving the top byte for the style. */
    uint32_t map_key = ((uint32_t)style << 24) | (cp & 0x00FFFFFFu);

    const uint32_t *existing = yetty_ycore_map_get(&f->glyph_map, map_key);
    if (existing) {
        return YETTY_OK(uint32, *existing);
    }

    /* Read from the style's CDB */
    uint32_t key = cp;
    void *data = NULL;
    size_t data_len = 0;

    struct yetty_ycore_void_result res =
        yetty_ycdb_reader_get(cdb, &key, sizeof(key), &data, &data_len);
    if (YETTY_IS_ERR(res)) {
        return YETTY_ERR(uint32, res.error.msg);
    }
    if (!data) {
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
    ydebug("ms_msdf load_one cp=0x%04X w=%u h=%u size=(%.2f,%.2f) bear=(%.2f,%.2f) adv=%.2f", cp,
           hdr.width, hdr.height, hdr.size_x, hdr.size_y, hdr.bearing_x, hdr.bearing_y,
           hdr.advance);

    if (data_len < sizeof(hdr) + pixel_bytes) {
        free(data);
        return YETTY_ERR(uint32, "glyph pixel data truncated");
    }

    /* Track vertical extents (padding-inflated) so the shader can derive
	 * the baseline. */
    if (hdr.size_y > 0.0f) {
        float ascent = hdr.bearing_y;               /* above baseline */
        float descent = hdr.size_y - hdr.bearing_y; /* below baseline */
        if (ascent > f->max_ascent) {
            f->max_ascent = ascent;
        }
        if (descent > f->max_descent) {
            f->max_descent = descent;
        }
    }
    /* Capture advance from the first glyph that has one — monospace, so
	 * every glyph reports the same value. */
    if (f->advance_cdb == 0.0f && hdr.advance > 0.0f) {
        f->advance_cdb = hdr.advance;
    }

    /* Allocate slot */
    uint32_t slot = f->next_slot;

    /* Grow metadata buffer if needed */
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

    /* Set glyph_cell_size uniform from first real glyph */
    if (hdr.width > 0 && hdr.height > 0 && f->rs.uniforms[1].vec2[0] == 0.0f) {
        f->rs.uniforms[1].vec2[0] = hdr.size_x;
        f->rs.uniforms[1].vec2[1] = hdr.size_y;
    }

    /* Empty glyph (space etc) — just metadata, no atlas pixels */
    if (hdr.width == 0 || hdr.height == 0) {
        struct yetty_yfont_glyph_meta_gpu *m = &f->meta[slot];
        memset(m, 0, sizeof(*m));
        m->advance = hdr.advance;
        f->next_slot++;

        if (yetty_ycore_map_put(&f->glyph_map, map_key, slot) < 0) {
            free(data);
            return YETTY_ERR(uint32, "map full");
        }
        /* Inverse map: slot → cp, for clipboard extraction. Ignore failure;
         * the forward map already succeeded, and reverse lookup will just
         * fall back to "unknown glyph". */
        yetty_ycore_map_put(&f->slot_to_cp, slot, cp);
        f->dirty = 1;
        free(data);
        return YETTY_OK(uint32, slot);
    }

    /* Shelf-pack into atlas */
    uint32_t gw = hdr.width;
    uint32_t gh = hdr.height;

    if (f->shelf_x + gw + ATLAS_PADDING > f->atlas_width) {
        /* Next shelf */
        f->shelf_x = ATLAS_PADDING;
        f->shelf_y += f->shelf_height + ATLAS_PADDING;
        f->shelf_height = 0;
    }

    while (f->shelf_y + gh + ATLAS_PADDING > f->atlas_height) {
        atlas_grow(f);
    }

    uint32_t ax = f->shelf_x;
    uint32_t ay = f->shelf_y;

    /* Copy pixels into atlas */
    for (uint32_t y = 0; y < gh; y++) {
        size_t dst = ((size_t)(ay + y) * f->atlas_width + ax) * 4;
        size_t src = (size_t)y * gw * 4;
        memcpy(f->atlas_pixels + dst, pixels + src, gw * 4);
    }

    f->shelf_x = ax + gw + ATLAS_PADDING;
    if (gh > f->shelf_height) {
        f->shelf_height = gh;
    }

    /* Fill metadata */
    struct yetty_yfont_glyph_meta_gpu *m = &f->meta[slot];
    m->uv_min_x = (float)ax / (float)f->atlas_width;
    m->uv_min_y = (float)ay / (float)f->atlas_height;
    m->uv_max_x = (float)(ax + gw) / (float)f->atlas_width;
    m->uv_max_y = (float)(ay + gh) / (float)f->atlas_height;
    m->size_x = hdr.size_x;
    m->size_y = hdr.size_y;
    m->bearing_x = hdr.bearing_x;
    m->bearing_y = hdr.bearing_y;
    m->advance = hdr.advance;
    m->_pad = 0;

    f->next_slot++;

    if (yetty_ycore_map_put(&f->glyph_map, map_key, slot) < 0) {
        free(data);
        return YETTY_ERR(uint32, "map full");
    }
    yetty_ycore_map_put(&f->slot_to_cp, slot, cp);

    f->dirty = 1;
    free(data);
    return YETTY_OK(uint32, slot);
}

/*=============================================================================
 * Vtable
 *===========================================================================*/

static void ms_msdf_destroy(struct yetty_yfont_ms_font *self)
{
    struct yetty_yfont_ms_msdf_font *font = (struct yetty_yfont_ms_msdf_font *)self;
    if (!font) {
        return;
    }
    free(font->atlas_pixels);
    free(font->meta);
    free(font->shader_code.data);
    yetty_ycore_map_destroy(&font->glyph_map);
    yetty_ycore_map_destroy(&font->slot_to_cp);
    for (int i = 0; i < 4; i++) {
        if (font->cdb[i]) {
            yetty_ycdb_reader_close(font->cdb[i]);
        }
    }
    free(font);
}

static struct uint32_result ms_msdf_get_codepoint(struct yetty_yfont_ms_font *self,
                                                  uint32_t glyph_index)
{
    struct yetty_yfont_ms_msdf_font *f = (struct yetty_yfont_ms_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(uint32, "font is NULL");
    }
    const uint32_t *cp = yetty_ycore_map_get(&f->slot_to_cp, glyph_index);
    if (!cp) {
        return YETTY_ERR(uint32, "unknown glyph_index");
    }
    return YETTY_OK(uint32, *cp);
}

static struct pixel_size_result ms_msdf_get_cell_size(const struct yetty_yfont_ms_font *self)
{
    const struct yetty_yfont_ms_msdf_font *f = (const struct yetty_yfont_ms_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(pixel_size, "font is NULL");
    }
    float visible_h_cdb = (f->max_ascent + f->max_descent) - 2.0f * f->pixel_range;
    if (visible_h_cdb <= 0.0f || f->advance_cdb <= 0.0f) {
        return YETTY_ERR(pixel_size, "font metrics not yet known");
    }
    /* font.size is the visible glyph extent in pixels (height). Cell
	 * width = advance × same scale — i.e. the font designer's chosen
	 * letter spacing, matching alacritty and other mainstream terminals. */
    float scale = f->requested_size / visible_h_cdb;
    float glyph_h = f->requested_size;
    float glyph_w = f->advance_cdb * scale;
    struct yetty_ycore_pixel_size sz;
    sz.height = glyph_h * (1.0f + f->padding.top + f->padding.bottom);
    sz.width = glyph_w * (1.0f + f->padding.left + f->padding.right);
    return YETTY_OK(pixel_size, sz);
}

static struct uint32_result ms_msdf_get_glyph_index(struct yetty_yfont_ms_font *self, uint32_t cp)
{
    struct yetty_yfont_ms_msdf_font *f = (struct yetty_yfont_ms_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(uint32, "font is NULL");
    }
    return load_one(f, cp, YETTY_YFONT_MS_STYLE_REGULAR);
}

static struct uint32_result ms_msdf_get_glyph_index_styled(struct yetty_yfont_ms_font *self,
                                                           uint32_t cp,
                                                           enum yetty_yfont_ms_style style)
{
    struct yetty_yfont_ms_msdf_font *f = (struct yetty_yfont_ms_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(uint32, "font is NULL");
    }
    return load_one(f, cp, style);
}

static struct yetty_ycore_void_result ms_msdf_resize(struct yetty_yfont_ms_font *self,
                                                     float font_size)
{
    struct yetty_yfont_ms_msdf_font *f = (struct yetty_yfont_ms_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(yetty_ycore_void, "font is NULL");
    }
    f->requested_size = font_size;
    f->dirty = 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ms_msdf_load_glyphs(struct yetty_yfont_ms_font *self,
                                                          const uint32_t *cps, size_t count)
{
    struct yetty_yfont_ms_msdf_font *f = (struct yetty_yfont_ms_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(yetty_ycore_void, "font is NULL");
    }
    for (size_t i = 0; i < count; i++) {
        load_one(f, cps[i], YETTY_YFONT_MS_STYLE_REGULAR);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ms_msdf_load_basic_latin(struct yetty_yfont_ms_font *self)
{
    struct yetty_yfont_ms_msdf_font *f = (struct yetty_yfont_ms_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(yetty_ycore_void, "font is NULL");
    }
    for (uint32_t cp = 0x20; cp <= 0x7E; cp++) {
        load_one(f, cp, YETTY_YFONT_MS_STYLE_REGULAR);
    }
    return YETTY_OK_VOID();
}

static int ms_msdf_is_dirty(const struct yetty_yfont_ms_font *self)
{
    return ((const struct yetty_yfont_ms_msdf_font *)self)->dirty;
}

static struct yetty_yrender_gpu_resource_set_result ms_msdf_get_gpu_resource_set(
    struct yetty_yfont_ms_font *self)
{
    struct yetty_yfont_ms_msdf_font *f = (struct yetty_yfont_ms_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(yetty_yrender_gpu_resource_set, "font is NULL");
    }

    if (f->dirty) {
        /* Update atlas texture */
        f->rs.textures[0].data = f->atlas_pixels;
        f->rs.textures[0].width = f->atlas_width;
        f->rs.textures[0].height = f->atlas_height;
        f->rs.textures[0].dirty = 1;

        /* Update metadata buffer */
        f->rs.buffers[0].data = (uint8_t *)f->meta;
        f->rs.buffers[0].size = (size_t)f->next_slot * sizeof(struct yetty_yfont_glyph_meta_gpu);
        f->rs.buffers[0].dirty = 1;

        /* Compute pixel-domain placement values for the shader.
		 *
		 * The CDB stores bearing_y and size_y with msdfgen's antialiasing
		 * padding baked in (pixel_range CDB-units on each edge of every
		 * glyph). So max_ascent = true_ascent + pixel_range and
		 * max_descent = true_descent + pixel_range, and the visible glyph
		 * extent in CDB-units is (max_ascent + max_descent - 2*pixel_range).
		 *
		 * scale: maps CDB-units to cell pixels so that the visible glyph
		 * extent matches font.size in pixels. With padding.top/bottom = 0
		 * the cell wraps the visible extent tightly — tallest ascender at
		 * cell top, lowest descender at cell bottom. The CDB-padding region
		 * extends slightly outside the cell, which is fine: it only carries
		 * MSDF antialiasing data and is sampled out of bounds in the shader.
		 *
		 * baseline_y: distance in pixels from cell top to the baseline. The
		 * pixel_range subtraction undoes the CDB padding so the visible top
		 * of the tallest ascender lands at top_pad_px, not below it.
		 */
        float pad_cdb = f->pixel_range;
        float visible_h_cdb = (f->max_ascent + f->max_descent) - 2.0f * pad_cdb;
        float scale = (visible_h_cdb > 0.0f) ? f->requested_size / visible_h_cdb
                                             : f->requested_size / f->base_size;
        float glyph_w = f->advance_cdb * scale;
        float top_pad_px = f->requested_size * f->padding.top;
        float left_pad_px = glyph_w * f->padding.left;
        float baseline_y = top_pad_px + (f->max_ascent - pad_cdb) * scale;

        f->rs.uniforms[0].f32 = f->pixel_range;
        f->rs.uniforms[1].f32 = scale;
        f->rs.uniforms[2].f32 = baseline_y;
        f->rs.uniforms[3].f32 = left_pad_px;

        f->dirty = 0;
    }

    return YETTY_OK(yetty_yrender_gpu_resource_set, &f->rs);
}

static struct yetty_ycore_void_result ms_msdf_set_cell_size(struct yetty_yfont_ms_font *self,
                                                            struct yetty_ycore_pixel_size cell_size)
{
    struct yetty_yfont_ms_msdf_font *f = (struct yetty_yfont_ms_msdf_font *)self;
    if (!f) {
        return YETTY_ERR(yetty_ycore_void, "font is NULL");
    }
    if (cell_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "invalid cell height");
    }
    /* Back out the requested glyph height from the cell height; ignore the
	 * caller's width — cell width is derived from font geometry, not from
	 * what the caller asks for. */
    float h_div = 1.0f + f->padding.top + f->padding.bottom;
    f->requested_size = cell_size.height / h_div;
    f->dirty = 1;
    return YETTY_OK_VOID();
}

static const struct yetty_yfont_ms_font_ops ms_msdf_ops = {
    .destroy = ms_msdf_destroy,
    .get_cell_size = ms_msdf_get_cell_size,
    .set_cell_size = ms_msdf_set_cell_size,
    .get_glyph_index = ms_msdf_get_glyph_index,
    .get_glyph_index_styled = ms_msdf_get_glyph_index_styled,
    .get_codepoint = ms_msdf_get_codepoint,
    .resize = ms_msdf_resize,
    .load_glyphs = ms_msdf_load_glyphs,
    .load_basic_latin = ms_msdf_load_basic_latin,
    .is_dirty = ms_msdf_is_dirty,
    .get_gpu_resource_set = ms_msdf_get_gpu_resource_set,
};

/*=============================================================================
 * Create
 *===========================================================================*/

static void close_all_cdb(struct yetty_yfont_ms_msdf_font *font)
{
    for (int i = 0; i < 4; i++) {
        if (font->cdb[i]) {
            yetty_ycdb_reader_close(font->cdb[i]);
            font->cdb[i] = NULL;
        }
    }
}

/* Open a style variant by swapping the "-Regular.cdb" suffix of the regular
 * path for `suffix`. Returns NULL (not an error) when the path doesn't follow
 * the convention or the variant file is absent — callers fall back to the
 * regular face. */
static struct yetty_ycdb_reader *open_style_variant(const char *regular_path, const char *suffix)
{
    const char *tail = "-Regular.cdb";
    size_t plen = strlen(regular_path);
    size_t tlen = strlen(tail);
    if (plen < tlen || strcmp(regular_path + plen - tlen, tail) != 0) {
        return NULL;
    }
    size_t base = plen - tlen;
    char path[1024];
    if (base + strlen(suffix) + 1 > sizeof(path)) {
        return NULL;
    }
    memcpy(path, regular_path, base);
    strcpy(path + base, suffix);

    struct yetty_ycdb_reader_result r = yetty_ycdb_reader_open(path);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    ydebug("ms_msdf_font: loaded style variant %s", path);
    return r.value;
}

struct yetty_font_ms_font_result yetty_yfont_ms_msdf_font_create(
    const char *cdb_path, const char *shader_path, float font_size,
    struct yetty_yfont_ms_padding padding)
{
    if (!cdb_path) {
        return YETTY_ERR(yetty_font_ms_font, "cdb_path is NULL");
    }
    if (!shader_path) {
        return YETTY_ERR(yetty_font_ms_font, "shader_path is NULL");
    }
    if (font_size <= 0.0f) {
        return YETTY_ERR(yetty_font_ms_font, "font_size must be > 0");
    }

    ydebug("ms_msdf_font: opening %s, shader %s", cdb_path, shader_path);

    /* Load shader from file */
    struct yetty_ycore_buffer_result shader_res = yetty_ycore_read_file(shader_path);
    if (YETTY_IS_ERR(shader_res)) {
        return YETTY_ERR(yetty_font_ms_font, shader_res.error.msg);
    }

    struct yetty_ycdb_reader_result cdb_res = yetty_ycdb_reader_open(cdb_path);
    if (YETTY_IS_ERR(cdb_res)) {
        free(shader_res.value.data);
        return YETTY_ERR(yetty_font_ms_font, cdb_res.error.msg);
    }

    struct yetty_yfont_ms_msdf_font *font = calloc(1, sizeof(struct yetty_yfont_ms_msdf_font));
    if (!font) {
        free(shader_res.value.data);
        yetty_ycdb_reader_close(cdb_res.value);
        return YETTY_ERR(yetty_font_ms_font, "allocation failed");
    }

    font->shader_code = shader_res.value;

    font->base.ops = &ms_msdf_ops;
    font->cdb[YETTY_YFONT_MS_STYLE_REGULAR] = cdb_res.value;
    /* Bold / italic / bold-italic faces share the regular's atlas + metadata;
     * absent variants stay NULL and fall back to regular at lookup time. */
    font->cdb[YETTY_YFONT_MS_STYLE_BOLD] = open_style_variant(cdb_path, "-Bold.cdb");
    font->cdb[YETTY_YFONT_MS_STYLE_ITALIC] = open_style_variant(cdb_path, "-Oblique.cdb");
    font->cdb[YETTY_YFONT_MS_STYLE_BOLD_ITALIC] = open_style_variant(cdb_path, "-BoldOblique.cdb");
    font->requested_size = font_size;
    font->base_size = 32.0f; /* TODO: read from CDB or config */
    font->pixel_range = 4.0f;
    font->padding = padding;
    font->dirty = 1;

    /* Init atlas */
    font->atlas_width = ATLAS_INITIAL_W;
    font->atlas_height = ATLAS_INITIAL_H;
    size_t atlas_bytes = (size_t)font->atlas_width * font->atlas_height * 4;
    font->atlas_pixels = calloc(atlas_bytes, 1);
    if (!font->atlas_pixels) {
        free(font->shader_code.data);
        close_all_cdb(font);
        free(font);
        return YETTY_ERR(yetty_font_ms_font, "atlas allocation failed");
    }

    /* Init shelf packer */
    font->shelf_x = ATLAS_PADDING;
    font->shelf_y = ATLAS_PADDING;

    /* Init metadata buffer */
    font->meta_capacity = 256;
    font->meta = calloc(font->meta_capacity, sizeof(struct yetty_yfont_glyph_meta_gpu));
    if (!font->meta) {
        free(font->atlas_pixels);
        free(font->shader_code.data);
        close_all_cdb(font);
        free(font);
        return YETTY_ERR(yetty_font_ms_font, "meta allocation failed");
    }
    font->next_slot = 1; /* slot 0 = empty/space */

    /* Init forward and inverse glyph maps. The inverse is sized identically
     * since every (cp → slot) pair stored in the forward map also gets
     * a (slot → cp) entry here. */
    if (yetty_ycore_map_init(&font->glyph_map, MAP_CAPACITY) < 0) {
        free(font->meta);
        free(font->atlas_pixels);
        free(font->shader_code.data);
        close_all_cdb(font);
        free(font);
        return YETTY_ERR(yetty_font_ms_font, "map init failed");
    }
    if (yetty_ycore_map_init(&font->slot_to_cp, MAP_CAPACITY) < 0) {
        yetty_ycore_map_destroy(&font->glyph_map);
        free(font->meta);
        free(font->atlas_pixels);
        free(font->shader_code.data);
        close_all_cdb(font);
        free(font);
        return YETTY_ERR(yetty_font_ms_font, "inverse map init failed");
    }

    /* GPU resource set */
    strncpy(font->rs.namespace, "ms_msdf_font", YETTY_YRENDER_NAME_MAX - 1);

    /* Texture: RGBA8 atlas */
    font->rs.texture_count = 1;
    struct yetty_yrender_texture *tex = &font->rs.textures[0];
    strncpy(tex->name, "texture", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(tex->wgsl_type, "texture_2d<f32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    strncpy(tex->sampler_name, "sampler", YETTY_YRENDER_NAME_MAX - 1);
    tex->format = YETTY_YRENDER_TEXTURE_FORMAT_RGBA8_UNORM;
    tex->sampler_filter = YETTY_YRENDER_FILTER_LINEAR;

    /* Buffer: per-glyph metadata */
    font->rs.buffer_count = 1;
    struct yetty_yrender_buffer *buf = &font->rs.buffers[0];
    strncpy(buf->name, "buffer", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(buf->wgsl_type, "array<f32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    buf->readonly = 1;

    /* Uniforms for shader */
    font->rs.uniform_count = 4;
    strncpy(font->rs.uniforms[0].name, "pixel_range", YETTY_YRENDER_NAME_MAX - 1);
    font->rs.uniforms[0].type = YETTY_YRENDER_UNIFORM_F32;
    font->rs.uniforms[0].f32 = font->pixel_range;

    /* scale: pixels per CDB-unit. Lets the shader convert per-glyph
	 * (size, bearing) — stored in CDB-units — to cell pixels. */
    strncpy(font->rs.uniforms[1].name, "scale", YETTY_YRENDER_NAME_MAX - 1);
    font->rs.uniforms[1].type = YETTY_YRENDER_UNIFORM_F32;
    font->rs.uniforms[1].f32 = 0.0f;

    /* baseline_y: distance in pixels from cell top to the font baseline.
	 * Computed CPU-side from padding.top + (max_ascent - pixel_range)*scale,
	 * where the pixel_range subtraction strips the MSDF antialiasing pad
	 * baked into bearing_y by the CDB generator. */
    strncpy(font->rs.uniforms[2].name, "baseline_y", YETTY_YRENDER_NAME_MAX - 1);
    font->rs.uniforms[2].type = YETTY_YRENDER_UNIFORM_F32;
    font->rs.uniforms[2].f32 = 0.0f;

    /* glyph_left: distance in pixels from cell left to the glyph origin.
	 * Computed CPU-side from padding.left * glyph_width. */
    strncpy(font->rs.uniforms[3].name, "glyph_left", YETTY_YRENDER_NAME_MAX - 1);
    font->rs.uniforms[3].type = YETTY_YRENDER_UNIFORM_F32;
    font->rs.uniforms[3].f32 = 0.0f;

    yetty_yrender_shader_code_set(&font->rs.shader, (const char *)font->shader_code.data,
                                  font->shader_code.size);

    /* Pre-load Basic Latin so the max_ascent / max_descent / max_glyph_w
	 * extents are settled before the first frame. Otherwise the scale and
	 * cell size would visibly shift as glyphs (especially descenders like
	 * underscore, or wide glyphs) load on demand. */
    for (uint32_t cp = 0x20; cp <= 0x7E; cp++) {
        load_one(font, cp, YETTY_YFONT_MS_STYLE_REGULAR);
    }
    if (font->max_ascent <= 0.0f || font->advance_cdb <= 0.0f) {
        free(font->meta);
        free(font->atlas_pixels);
        free(font->shader_code.data);
        yetty_ycore_map_destroy(&font->glyph_map);
        yetty_ycore_map_destroy(&font->slot_to_cp);
        close_all_cdb(font);
        free(font);
        return YETTY_ERR(yetty_font_ms_font, "failed to determine font metrics");
    }

    yinfo("ms_msdf_font: created from %s, size=%.0f, "
          "ascent=%.2f, descent=%.2f, advance=%.2f",
          cdb_path, font_size, font->max_ascent, font->max_descent, font->advance_cdb);

    return YETTY_OK(yetty_font_ms_font, &font->base);
}
