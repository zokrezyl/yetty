/*
 * raster-font.c - Raster font implementation (FreeType-based)
 */

#include <yetty/yfont/ms-raster-font.h>
#include <yetty/yfont/ms-font.h>
#include <yetty/yfont/font.h> /* YETTY_YFONT_LIGATURE_MAX_LEN */
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/types.h>
#include <yetty/ycore/util.h>
#include <yetty/yrender/texture-format.h>
#include <yetty/yplatform/fs.h>
#include <yetty/ytrace/ytrace.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H /* FT_Load_Sfnt_Table — feeds tables to HarfBuzz */

#ifdef YETTY_ENABLE_LIB_HARFBUZZ
#include <hb.h>
#endif

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Shorthand accessors into rs */
#define FONT_ATLAS(f) ((f)->rs.textures[0])
#define FONT_UV_BUF(f) ((f)->rs.buffers[0])
#define FONT_UVS(f) ((struct yetty_yfont_raster_glyph_uv *)(f)->rs.buffers[0].data)
#define FONT_UV_COUNT(f) ((f)->rs.buffers[0].size / sizeof(struct yetty_yfont_raster_glyph_uv))

#define RASTER_FONT_MAX_PATH 512
#define RASTER_FONT_MAX_FALLBACK_FACES 384
#define RASTER_FONT_SLOT_PADDING 1
#define RASTER_FONT_ATLAS_MAX_DIMENSION 16384
#define RASTER_FONT_ATLAS_PADDING 2
/* Glyph slot = cell + padding on each side */
#define GLYPH_SLOT_W(f) ((uint32_t)(f)->cell_width + RASTER_FONT_ATLAS_PADDING * 2)
#define GLYPH_SLOT_H(f) ((uint32_t)(f)->cell_height + RASTER_FONT_ATLAS_PADDING * 2)

static const char *FACE_SUFFIXES[] = {"-Regular.ttf", "-Bold.ttf", "-Oblique.ttf",
                                      "-BoldOblique.ttf"};

/* Per-glyph GPU record: atlas UV origin of the slot's content plus the slot
 * width in cells (CJK/emoji glyphs advance two cells and get a double-width
 * slot). 4 floats — matches the stride in ms-raster-font.wgsl and the
 * terminal text shader's raster branch. */
struct yetty_yfont_raster_glyph_uv {
    float uv_x;
    float uv_y;
    float width_cells;
    float pad;
};

struct yetty_yfont_codepoint_slot {
    uint32_t codepoint;
    uint32_t slot;
};

/* Upper bound on combining marks composited into one cluster slot. Matches
 * the grapheme-cluster capacity the terminal model captures per cell
 * (libvterm's VTERM_MAX_CHARS_PER_CELL minus the base). */
#define RASTER_FONT_MAX_MARKS 5

/* Cache entry for a composited base+marks cluster. The atlas slot holds the
 * base with each combining mark stacked over it; the key is the whole cluster
 * so identical clusters (very common in running text) reuse one slot. */
struct yetty_yfont_cluster_slot {
    uint32_t codepoint;
    uint32_t marks[RASTER_FONT_MAX_MARKS];
    uint8_t mark_count;
    uint32_t slot;
};

/* Cache entry for a shaped programming ligature. A monospace ligature font
 * keeps one glyph per input cell (the calt substitutes each character with a
 * ligature-piece glyph), so `slots[i]` is the atlas slot for cell i of the run.
 * The key is the codepoint sequence itself, so a repeated ligature (the steady
 * state — the grid re-packs every visible row each frame) is a tiny linear-scan
 * hit with no reshaping. Only populated on the dedicated ligature face. */
struct yetty_yfont_ligature_slot {
    uint32_t codepoints[YETTY_YFONT_LIGATURE_MAX_LEN];
    uint8_t count;
    uint32_t slots[YETTY_YFONT_LIGATURE_MAX_LEN];
};

struct yetty_yfont_raster_font {
    struct yetty_yfont_ms_font base;

    char fonts_dir[RASTER_FONT_MAX_PATH];
    char font_name[RASTER_FONT_MAX_PATH];
    float cell_width;
    float cell_height;
    uint32_t font_size;
    int baseline;

    /* Color (CBDT/COLR bitmap-strike) face, e.g. Noto Color Emoji: the atlas
     * is RGBA8 (4 bytes/pixel) and strike bitmaps are box-filtered into the
     * cell-sized slot at rasterize time. Monochrome faces keep the R8 atlas. */
    int color_mode;
    uint32_t atlas_bpp;

    FT_Library ft_library;
    FT_Face ft_faces[4];

    /* Fallback chain (Regular style only): when the primary faces have no
     * glyph for a codepoint, these are tried in order. Populated by a glob
     * font name (e.g. "NotoSans*" loads every NotoSans<Script>-Regular.ttf
     * in the fonts dir), so one face can cover many scripts. */
    FT_Face fallback_faces[RASTER_FONT_MAX_FALLBACK_FACES];
    size_t fallback_count;

    /* Shader code (owned) */
    struct yetty_ycore_buffer shader_code;

    /* GPU resource set — returned directly.
     * rs.textures[0] = atlas, rs.buffers[0] = glyph UVs */
    struct yetty_yrender_gpu_resource_set rs;
    int next_slot_idx;

    /* Shelf packer state */
    uint32_t shelf_x;
    uint32_t shelf_y;
    uint32_t shelf_height;
    uint32_t shelf_min_x;

    struct yetty_yfont_codepoint_slot *codepoint_slots[4];
    size_t codepoint_slots_count[4];
    size_t codepoint_slots_capacity[4];

    /* Composited base+marks clusters, keyed on the whole cluster (per style).
     * Separate from codepoint_slots so the common no-marks path is untouched. */
    struct yetty_yfont_cluster_slot *cluster_slots[4];
    size_t cluster_slots_count[4];
    size_t cluster_slots_capacity[4];

    /* Ligature face: size the font so one character advance equals cell_width
     * (rather than the line box filling cell_height), so a ligature that
     * shapes to one glyph spans exactly its N cells. Set only on the dedicated
     * programming-ligature face. */
    int size_to_cell_width;
#ifdef YETTY_ENABLE_LIB_HARFBUZZ
    /* Lazily-built HarfBuzz font (hb_font_t *) over this face's SFNT tables,
     * plus the gid->slot cache for shaped ligature glyphs. void* so the struct
     * layout does not depend on the HarfBuzz headers. */
    void *hb_font;
    struct yetty_yfont_ligature_slot *ligature_slots;
    size_t ligature_slot_count;
    size_t ligature_slot_capacity;
#endif

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
static struct uint32_result raster_font_get_codepoint(struct yetty_yfont_ms_font *self,
                                                      uint32_t glyph_index);
static struct uint32_result raster_font_get_glyph_index_ligature(struct yetty_yfont_ms_font *self,
                                                                 const uint32_t *codepoints,
                                                                 size_t count, uint32_t *out_slots);
static struct yetty_ycore_void_result raster_font_resize(struct yetty_yfont_ms_font *self,
                                                         float font_size);
static struct yetty_ycore_void_result raster_font_load_glyphs(struct yetty_yfont_ms_font *self,
                                                              const uint32_t *codepoints,
                                                              size_t count);
static struct yetty_ycore_void_result raster_font_load_basic_latin(
    struct yetty_yfont_ms_font *self);
static int raster_font_is_dirty(const struct yetty_yfont_ms_font *self);
static struct yetty_yrender_gpu_resource_set_result raster_font_get_gpu_resource_set(
    struct yetty_yfont_ms_font *self);

static void raster_font_update_font_size(struct yetty_yfont_raster_font *font);
static void raster_font_rasterize_all(struct yetty_yfont_raster_font *font);
static int raster_font_rasterize_glyph(struct yetty_yfont_raster_font *font, uint32_t codepoint,
                                       const uint32_t *marks, uint8_t mark_count,
                                       enum yetty_yfont_ms_style style);
static struct uint32_result raster_font_get_glyph_index_cluster(struct yetty_yfont_ms_font *self,
                                                                uint32_t codepoint,
                                                                const uint32_t *marks,
                                                                uint8_t mark_count,
                                                                enum yetty_yfont_ms_style style);
static void raster_font_cleanup(struct yetty_yfont_raster_font *font);
static uint32_t raster_font_lookup_slot(struct yetty_yfont_raster_font *font, int style_idx,
                                        uint32_t codepoint);
static void raster_font_add_slot(struct yetty_yfont_raster_font *font, int style_idx,
                                 uint32_t codepoint, uint32_t slot);
static uint32_t raster_font_lookup_cluster(struct yetty_yfont_raster_font *font, int style_idx,
                                           uint32_t codepoint, const uint32_t *marks,
                                           uint8_t mark_count);
static void raster_font_add_cluster(struct yetty_yfont_raster_font *font, int style_idx,
                                    uint32_t codepoint, const uint32_t *marks, uint8_t mark_count,
                                    uint32_t slot);
static int raster_font_resolve_face_glyph(struct yetty_yfont_raster_font *font, uint32_t codepoint,
                                          enum yetty_yfont_ms_style style, FT_Face *out_face,
                                          FT_UInt *out_index);
static void raster_font_grow_atlas(struct yetty_yfont_raster_font *font);

static struct yetty_ycore_void_result raster_font_set_cell_size(
    struct yetty_yfont_ms_font *self, struct yetty_ycore_pixel_size cell_size);

static struct yetty_font_ms_font_result raster_font_create_named_impl(
    struct yetty_yconfig_config *config, const char *font_name, float cell_width, float cell_height,
    int size_to_cell_width);

static const struct yetty_yfont_ms_font_ops raster_font_ops = {
    .destroy = raster_font_destroy,
    .get_cell_size = raster_font_get_cell_size,
    .set_cell_size = raster_font_set_cell_size,
    .get_glyph_index = raster_font_get_glyph_index,
    .get_glyph_index_styled = raster_font_get_glyph_index_styled,
    .get_glyph_index_cluster = raster_font_get_glyph_index_cluster,
    .get_codepoint = raster_font_get_codepoint,
    .get_glyph_index_ligature = raster_font_get_glyph_index_ligature,
    .resize = raster_font_resize,
    .load_glyphs = raster_font_load_glyphs,
    .load_basic_latin = raster_font_load_basic_latin,
    .is_dirty = raster_font_is_dirty,
    .get_gpu_resource_set = raster_font_get_gpu_resource_set,
};

/*===========================================================================
 * Helper functions
 *===========================================================================*/

static uint32_t raster_font_lookup_slot(struct yetty_yfont_raster_font *font, int style_idx,
                                        uint32_t codepoint)
{
    for (size_t i = 0; i < font->codepoint_slots_count[style_idx]; i++) {
        if (font->codepoint_slots[style_idx][i].codepoint == codepoint) {
            return font->codepoint_slots[style_idx][i].slot;
        }
    }
    return UINT32_MAX;
}

static void raster_font_add_slot(struct yetty_yfont_raster_font *font, int style_idx,
                                 uint32_t codepoint, uint32_t slot)
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
        font->codepoint_slots[style_idx] = realloc(font->codepoint_slots[style_idx],
                                                   font->codepoint_slots_capacity[style_idx] *
                                                       sizeof(struct yetty_yfont_codepoint_slot));
    }

    font->codepoint_slots[style_idx][font->codepoint_slots_count[style_idx]].codepoint = codepoint;
    font->codepoint_slots[style_idx][font->codepoint_slots_count[style_idx]].slot = slot;
    font->codepoint_slots_count[style_idx]++;
}

static int cluster_slot_matches(const struct yetty_yfont_cluster_slot *entry, uint32_t codepoint,
                                const uint32_t *marks, uint8_t mark_count)
{
    if (entry->codepoint != codepoint || entry->mark_count != mark_count) {
        return 0;
    }
    for (uint8_t i = 0; i < mark_count; i++) {
        if (entry->marks[i] != marks[i]) {
            return 0;
        }
    }
    return 1;
}

static uint32_t raster_font_lookup_cluster(struct yetty_yfont_raster_font *font, int style_idx,
                                           uint32_t codepoint, const uint32_t *marks,
                                           uint8_t mark_count)
{
    for (size_t i = 0; i < font->cluster_slots_count[style_idx]; i++) {
        if (cluster_slot_matches(&font->cluster_slots[style_idx][i], codepoint, marks,
                                 mark_count)) {
            return font->cluster_slots[style_idx][i].slot;
        }
    }
    return UINT32_MAX;
}

static void raster_font_add_cluster(struct yetty_yfont_raster_font *font, int style_idx,
                                    uint32_t codepoint, const uint32_t *marks, uint8_t mark_count,
                                    uint32_t slot)
{
    if (mark_count > RASTER_FONT_MAX_MARKS) {
        mark_count = RASTER_FONT_MAX_MARKS;
    }
    for (size_t i = 0; i < font->cluster_slots_count[style_idx]; i++) {
        if (cluster_slot_matches(&font->cluster_slots[style_idx][i], codepoint, marks,
                                 mark_count)) {
            font->cluster_slots[style_idx][i].slot = slot;
            return;
        }
    }

    if (font->cluster_slots_count[style_idx] >= font->cluster_slots_capacity[style_idx]) {
        font->cluster_slots_capacity[style_idx] = font->cluster_slots_capacity[style_idx]
                                                      ? font->cluster_slots_capacity[style_idx] * 2
                                                      : 64;
        font->cluster_slots[style_idx] =
            realloc(font->cluster_slots[style_idx], font->cluster_slots_capacity[style_idx] *
                                                        sizeof(struct yetty_yfont_cluster_slot));
    }

    struct yetty_yfont_cluster_slot *entry =
        &font->cluster_slots[style_idx][font->cluster_slots_count[style_idx]];
    entry->codepoint = codepoint;
    entry->mark_count = mark_count;
    for (uint8_t i = 0; i < mark_count; i++) {
        entry->marks[i] = marks[i];
    }
    entry->slot = slot;
    font->cluster_slots_count[style_idx]++;
}

/* Resolve a codepoint to a (face, glyph index) pair: the requested style face
 * first, then the Regular face, then the fallback chain — the same order the
 * base rasterization uses. Returns 0 when no face covers the codepoint. */
static int raster_font_resolve_face_glyph(struct yetty_yfont_raster_font *font, uint32_t codepoint,
                                          enum yetty_yfont_ms_style style, FT_Face *out_face,
                                          FT_UInt *out_index)
{
    FT_Face face = font->ft_faces[(int)style];
    if (!face) {
        face = font->ft_faces[YETTY_YFONT_MS_STYLE_REGULAR];
    }
    if (!face) {
        return 0;
    }
    FT_UInt glyph_index = FT_Get_Char_Index(face, codepoint);
    if (glyph_index == 0 && face != font->ft_faces[YETTY_YFONT_MS_STYLE_REGULAR] &&
        font->ft_faces[YETTY_YFONT_MS_STYLE_REGULAR]) {
        FT_Face regular = font->ft_faces[YETTY_YFONT_MS_STYLE_REGULAR];
        FT_UInt regular_index = FT_Get_Char_Index(regular, codepoint);
        if (regular_index != 0) {
            face = regular;
            glyph_index = regular_index;
        }
    }
    if (glyph_index == 0) {
        for (size_t i = 0; i < font->fallback_count; i++) {
            FT_UInt fallback_index = FT_Get_Char_Index(font->fallback_faces[i], codepoint);
            if (fallback_index != 0) {
                face = font->fallback_faces[i];
                glyph_index = fallback_index;
                break;
            }
        }
    }
    if (glyph_index == 0) {
        return 0;
    }
    *out_face = face;
    *out_index = glyph_index;
    return 1;
}

/* Apply the font's current pixel size to one face: scalable faces get the
 * computed font_size; fixed-strike faces select the strike closest to the
 * cell height. */
static void raster_font_apply_size_to_face(struct yetty_yfont_raster_font *font, FT_Face face)
{
    if (!face) {
        return;
    }
    if (FT_IS_SCALABLE(face)) {
        FT_Set_Pixel_Sizes(face, 0, font->font_size);
        return;
    }
    if (face->num_fixed_sizes == 0) {
        return;
    }
    int best_strike = 0;
    long best_delta = LONG_MAX;
    for (int s = 0; s < face->num_fixed_sizes; s++) {
        long strike_height = face->available_sizes[s].height;
        long delta = labs(strike_height - (long)font->cell_height);
        if (delta < best_delta) {
            best_delta = delta;
            best_strike = s;
        }
    }
    FT_Select_Size(face, best_strike);
}

/* Ascender/descender (in pixels, at the face's current ppem) used to fit a
 * face's glyphs into the cell.
 *
 * font_size is derived so the face's line box fills the cell height. That is
 * fine for Latin, but broad-coverage faces bake generous leading into their
 * vertical metrics — NotoSansCJK carries ascent 1160 / descent -288 per 1000
 * upm (1.45x the em), and the Noto fallback faces used for Thai, Devanagari,
 * Arabic, Hebrew, Bengali, Tamil, … are worse still — so fitting THAT box to
 * the cell shrinks the visible glyphs to a fraction of the cell and every
 * Asian / complex script renders tiny next to Latin text.
 *
 * Guard against it: when the hhea box overflows the em, prefer the
 * typographic (OS/2) box for a truer ascent:descent split, then clamp the
 * total to the em. The em is the design body the glyphs are drawn against, so
 * clamping sizes them to fill the cell rather than shrinking them to fit stray
 * leading. Faces whose box already fits the em (Latin, most scripts) are
 * untouched. The clamp is unconditional because some faces set the typo box
 * as large as hhea — without it they would still render tiny. */
static void raster_font_face_metrics(FT_Face face, int *ascender_out, int *descender_out)
{
    int ascender = (int)(face->size->metrics.ascender >> 6);
    int descender = (int)labs(face->size->metrics.descender >> 6);
    int em_px = (int)(face->size->metrics.y_ppem);

    if (em_px <= 0 || ascender + descender <= em_px) {
        *ascender_out = ascender;
        *descender_out = descender;
        return;
    }

    TT_OS2 *os2 = (TT_OS2 *)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    FT_UShort upm = face->units_per_EM;
    if (os2 && upm && (os2->sTypoAscender != 0 || os2->sTypoDescender != 0)) {
        long ppem = (long)face->size->metrics.y_ppem;
        int typo_ascender = (int)((long)os2->sTypoAscender * ppem / upm);
        int typo_descender = (int)labs((long)os2->sTypoDescender * ppem / upm);
        if (typo_ascender + typo_descender > 0) {
            ascender = typo_ascender;
            descender = typo_descender;
        }
    }

    int total = ascender + descender;
    if (total > em_px && total > 0) {
        ascender = ascender * em_px / total;
        descender = descender * em_px / total;
    }

    *ascender_out = ascender;
    *descender_out = descender;
}

static void raster_font_update_font_size(struct yetty_yfont_raster_font *font)
{
    FT_Face regular_face = font->ft_faces[0];
    if (!regular_face) {
        return;
    }

    if (!FT_IS_SCALABLE(regular_face)) {
        /* Fixed-strike face (color emoji): select the strike closest to the
         * cell height on every face; bitmaps are scaled into the cell box at
         * rasterize time, so the exact strike only affects quality. */
        for (int i = 0; i < 4; i++) {
            FT_Face face = font->ft_faces[i];
            if (!face || face->num_fixed_sizes == 0) {
                continue;
            }
            int best_strike = 0;
            long best_delta = LONG_MAX;
            for (int s = 0; s < face->num_fixed_sizes; s++) {
                long strike_height = face->available_sizes[s].height;
                long delta = labs(strike_height - (long)font->cell_height);
                if (delta < best_delta) {
                    best_delta = delta;
                    best_strike = s;
                }
            }
            FT_Select_Size(face, best_strike);
        }
        font->font_size = (uint32_t)font->cell_height;
        font->baseline = (int)font->cell_height - 1;
        for (size_t i = 0; i < font->fallback_count; i++) {
            raster_font_apply_size_to_face(font, font->fallback_faces[i]);
        }
        return;
    }

    /* Set initial size to get metrics (use cell height as starting point) */
    FT_Set_Pixel_Sizes(regular_face, 0, (FT_UInt)font->cell_height);

    /* Get font metrics in pixels (at current size). raster_font_face_metrics
     * substitutes the typographic box for faces (CJK) whose hhea line box
     * overflows the em, so their glyphs are not shrunk to fit stray leading. */
    int ascender, descender;
    raster_font_face_metrics(regular_face, &ascender, &descender);
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
    for (size_t i = 0; i < font->fallback_count; i++) {
        raster_font_apply_size_to_face(font, font->fallback_faces[i]);
    }

    /* Ligature face: override the line-box sizing so one character advance
     * equals the cell width. A monospace ligature's advance is then N cell
     * widths, so the shaped glyph spans exactly its N cells and following grid
     * text stays cell-aligned — the same effect the cell grid gives ordinary
     * glyphs, achieved by matching the reference advance rather than clipping. */
    if (font->size_to_cell_width) {
        FT_Set_Pixel_Sizes(regular_face, 0, (FT_UInt)font->cell_height);
        FT_UInt ref_gid = FT_Get_Char_Index(regular_face, 'M');
        float advance_px = 0.0f;
        if (ref_gid && FT_Load_Glyph(regular_face, ref_gid, FT_LOAD_DEFAULT) == 0) {
            advance_px = (float)regular_face->glyph->advance.x / 64.0f;
        }
        if (advance_px > 0.0f) {
            font->font_size =
                (uint32_t)(font->cell_width * (float)font->cell_height / advance_px + 0.5f);
        }
        if (font->font_size == 0) {
            font->font_size = (uint32_t)font->cell_height;
        }
        for (int i = 0; i < 4; i++) {
            if (font->ft_faces[i]) {
                FT_Set_Pixel_Sizes(font->ft_faces[i], 0, font->font_size);
            }
        }
    }

    /* Recalculate metrics at new size (same typographic-box substitution). */
    raster_font_face_metrics(regular_face, &ascender, &descender);

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

    /* Grow width by a batch of slots; grow height geometrically (double,
     * at least one slot row, clamped to the max dimension) — per-row growth
     * makes bulk glyph loads (an emoji sweep) do O(rows) full-atlas copies
     * + UV rescales. */
    uint32_t grow_w = GLYPH_SLOT_W(font) * 8; /* 8 glyphs wide */
    uint32_t grow_h = old_height > GLYPH_SLOT_H(font) ? old_height : GLYPH_SLOT_H(font);
    if (old_height + grow_h > RASTER_FONT_ATLAS_MAX_DIMENSION) {
        grow_h = RASTER_FONT_ATLAS_MAX_DIMENSION - old_height; /* 0 = maxed out */
    }

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

    uint32_t bpp = font->atlas_bpp ? font->atlas_bpp : 1u;
    uint8_t *new_data = calloc((size_t)new_width * new_height, bpp);
    if (!new_data) {
        yerror("Failed to allocate new atlas data");
        return;
    }

    for (uint32_t y = 0; y < old_height; y++) {
        memcpy(new_data + (size_t)y * new_width * bpp,
               FONT_ATLAS(font).data + (size_t)y * old_width * bpp, (size_t)old_width * bpp);
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
                                       const uint32_t *marks, uint8_t mark_count,
                                       enum yetty_yfont_ms_style style)
{
    int face_idx = (int)style;
    FT_Face face = font->ft_faces[face_idx];

    ydebug("rasterize ENTER: cp=U+%04X marks=%u style=%d face=%p", codepoint, (unsigned)mark_count,
           (int)style, (void *)face);

    /* Fallback to Regular if this face not available */
    if (!face) {
        ydebug("rasterize: face[%d] NULL, fallback or fail (style=%d)", face_idx, (int)style);
        if (style != YETTY_YFONT_MS_STYLE_REGULAR) {
            return raster_font_rasterize_glyph(font, codepoint, marks, mark_count,
                                               YETTY_YFONT_MS_STYLE_REGULAR);
        }
        return 0;
    }

    FT_UInt glyph_index = FT_Get_Char_Index(face, codepoint);
    ydebug("rasterize: FT_Get_Char_Index(U+%04X) = %u on face=%p num_glyphs=%ld", codepoint,
           (unsigned)glyph_index, (void *)face, (long)face->num_glyphs);
    if (glyph_index == 0) {
        /* Fallback to Regular if glyph not in this face */
        if (style != YETTY_YFONT_MS_STYLE_REGULAR) {
            return raster_font_rasterize_glyph(font, codepoint, marks, mark_count,
                                               YETTY_YFONT_MS_STYLE_REGULAR);
        }
        /* Walk the fallback chain — first face that covers the codepoint wins. */
        for (size_t i = 0; i < font->fallback_count; i++) {
            FT_UInt fallback_index = FT_Get_Char_Index(font->fallback_faces[i], codepoint);
            if (fallback_index != 0) {
                face = font->fallback_faces[i];
                glyph_index = fallback_index;
                break;
            }
        }
        if (glyph_index == 0) {
            ydebug("rasterize: no glyph for U+%04X in primary or %zu fallbacks", codepoint,
                   font->fallback_count);
            return 0;
        }
    }

    FT_Int32 load_flags = FT_LOAD_RENDER;
    if (font->color_mode) {
        load_flags |= FT_LOAD_COLOR;
    }
    int ft_err = FT_Load_Glyph(face, glyph_index, load_flags);
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

    /* An empty base with no marks is a real (blank) slot. With marks present
     * we fall through and composite them over the (cleared) slot even when the
     * base contributes no pixels. */
    if ((bitmap->width == 0 || bitmap->rows == 0) && mark_count == 0) {
        uvs[slot_idx].uv_x = -1.0f;
        uvs[slot_idx].uv_y = -1.0f;
        uvs[slot_idx].width_cells = 1.0f;
        raster_font_add_slot(font, style_idx, codepoint, slot_idx);
        font->next_slot_idx++;
        return 1;
    }

    int cell_w = (int)font->cell_width;
    int cell_h = (int)font->cell_height;
    /* Double-width glyphs (CJK, emoji) advance two cells — give them a
     * double-width atlas slot so the full glyph is kept. Fixed-strike color
     * faces report the advance at strike scale; normalize to cell scale. */
    float advance_px = (float)slot->advance.x / 64.0f;
    if (font->color_mode && face->size && face->size->metrics.y_ppem > 0) {
        advance_px *= font->cell_height / (float)face->size->metrics.y_ppem;
    }
    uint32_t width_cells = advance_px > font->cell_width * 1.5f ? 2u : 1u;
    uint32_t glyph_width = (uint32_t)cell_w * width_cells + RASTER_FONT_ATLAS_PADDING * 2;
    uint32_t glyph_height = (uint32_t)cell_h + RASTER_FONT_ATLAS_PADDING * 2;
    uint32_t aw = FONT_ATLAS(font).width;
    uint32_t ah = FONT_ATLAS(font).height;
    size_t atlas_size = (size_t)aw * ah;

    if (font->shelf_x + glyph_width > aw) {
        font->shelf_x = font->shelf_min_x + RASTER_FONT_ATLAS_PADDING;
        font->shelf_y += font->shelf_height + RASTER_FONT_ATLAS_PADDING;
        font->shelf_height = 0;
    }

    /* Grow until the new shelf row fits: a single grow step can land
     * PADDING pixels short right after a shelf wrap (row stride is
     * shelf_height + PADDING, growth is whole slot rows). Progress is
     * checked so a maxed-out atlas still terminates. */
    while (font->shelf_y + glyph_height > ah) {
        uint32_t old_width = aw;
        uint32_t old_height = ah;
        raster_font_grow_atlas(font);
        aw = FONT_ATLAS(font).width;
        ah = FONT_ATLAS(font).height;
        atlas_size = (size_t)aw * ah;
        if (aw == old_width && ah == old_height) {
            yerror("Atlas full, cannot fit glyph for U+%04X", codepoint);
            return 0;
        }
    }

    uint32_t atlas_x = font->shelf_x;
    uint32_t atlas_y = font->shelf_y;
    uint8_t *pixels = FONT_ATLAS(font).data;
    uint32_t bpp = font->atlas_bpp ? font->atlas_bpp : 1u;

    /* Clear cell area */
    for (uint32_t y = 0; y < glyph_height; y++) {
        for (uint32_t x = 0; x < glyph_width; x++) {
            uint32_t dst_idx = (atlas_y + y) * aw + (atlas_x + x);
            if (dst_idx < atlas_size) {
                memset(pixels + (size_t)dst_idx * bpp, 0, bpp);
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
    if (font->color_mode && bitmap->pixel_mode == FT_PIXEL_MODE_BGRA) {
        /* Color strike (premultiplied BGRA): box-filter into the slot's
         * content box, preserving aspect ratio, centered. Output is straight
         * (un-premultiplied) RGBA — the shader blends rgb with the alpha. */
        uint32_t box_w = (uint32_t)cell_w * width_cells;
        uint32_t box_h = (uint32_t)cell_h;
        float scale_w = (float)box_w / (float)glyph_w;
        float scale_h = (float)box_h / (float)glyph_h;
        float scale = scale_w < scale_h ? scale_w : scale_h;
        uint32_t out_w = (uint32_t)((float)glyph_w * scale);
        uint32_t out_h = (uint32_t)((float)glyph_h * scale);
        if (out_w == 0) {
            out_w = 1;
        }
        if (out_h == 0) {
            out_h = 1;
        }
        uint32_t box_off_x = RASTER_FONT_ATLAS_PADDING + (box_w - out_w) / 2;
        uint32_t box_off_y = RASTER_FONT_ATLAS_PADDING + (box_h - out_h) / 2;
        for (uint32_t y = 0; y < out_h; y++) {
            uint32_t src_y0 = (uint32_t)((float)y / scale);
            uint32_t src_y1 = (uint32_t)((float)(y + 1) / scale);
            if (src_y1 <= src_y0) {
                src_y1 = src_y0 + 1;
            }
            if (src_y1 > (uint32_t)glyph_h) {
                src_y1 = (uint32_t)glyph_h;
            }
            for (uint32_t x = 0; x < out_w; x++) {
                uint32_t src_x0 = (uint32_t)((float)x / scale);
                uint32_t src_x1 = (uint32_t)((float)(x + 1) / scale);
                if (src_x1 <= src_x0) {
                    src_x1 = src_x0 + 1;
                }
                if (src_x1 > (uint32_t)glyph_w) {
                    src_x1 = (uint32_t)glyph_w;
                }
                uint32_t sum_b = 0, sum_g = 0, sum_r = 0, sum_a = 0, samples = 0;
                for (uint32_t sy = src_y0; sy < src_y1; sy++) {
                    const uint8_t *row = bitmap->buffer + (size_t)sy * (size_t)bitmap->pitch;
                    for (uint32_t sx = src_x0; sx < src_x1; sx++) {
                        const uint8_t *texel = row + (size_t)sx * 4u;
                        sum_b += texel[0];
                        sum_g += texel[1];
                        sum_r += texel[2];
                        sum_a += texel[3];
                        samples++;
                    }
                }
                if (samples == 0) {
                    continue;
                }
                uint32_t avg_a = sum_a / samples;
                uint32_t avg_r = sum_r / samples;
                uint32_t avg_g = sum_g / samples;
                uint32_t avg_b = sum_b / samples;
                if (avg_a > 0) {
                    /* premultiplied → straight */
                    avg_r = avg_r * 255u / avg_a;
                    avg_g = avg_g * 255u / avg_a;
                    avg_b = avg_b * 255u / avg_a;
                    if (avg_r > 255u) {
                        avg_r = 255u;
                    }
                    if (avg_g > 255u) {
                        avg_g = 255u;
                    }
                    if (avg_b > 255u) {
                        avg_b = 255u;
                    }
                }
                uint32_t dst_idx = (atlas_y + box_off_y + y) * aw + (atlas_x + box_off_x + x);
                if (dst_idx < atlas_size) {
                    uint8_t *dst = pixels + (size_t)dst_idx * 4u;
                    dst[0] = (uint8_t)avg_r;
                    dst[1] = (uint8_t)avg_g;
                    dst[2] = (uint8_t)avg_b;
                    dst[3] = (uint8_t)avg_a;
                }
            }
        }
    } else if (font->color_mode) {
        /* Grayscale glyph in a color font — white with coverage alpha. */
        for (int y = 0; y < glyph_h; y++) {
            for (int x = 0; x < glyph_w; x++) {
                int src_idx = y * bitmap->pitch + x;
                uint32_t dst_idx = (atlas_y + (uint32_t)offset_y + (uint32_t)y) * aw +
                                   (atlas_x + (uint32_t)offset_x + (uint32_t)x);
                if (dst_idx < atlas_size) {
                    uint8_t *dst = pixels + (size_t)dst_idx * 4u;
                    dst[0] = 255u;
                    dst[1] = 255u;
                    dst[2] = 255u;
                    dst[3] = bitmap->buffer[src_idx];
                }
            }
        }
    } else {
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
    }

    /* Store UV — point to cell content start (skip padding) */
    uvs[slot_idx].uv_x = (float)(atlas_x + RASTER_FONT_ATLAS_PADDING) / (float)aw;
    uvs[slot_idx].uv_y = (float)(atlas_y + RASTER_FONT_ATLAS_PADDING) / (float)ah;
    uvs[slot_idx].width_cells = (float)width_cells;
    if (mark_count > 0) {
        raster_font_add_cluster(font, style_idx, codepoint, marks, mark_count, slot_idx);
    } else {
        raster_font_add_slot(font, style_idx, codepoint, slot_idx);
    }

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

    /* Interim combining-mark rendering: stack each captured mark over the base
     * within this same slot. A mark carries its own FreeType bearing, which
     * positions it relative to the pen origin the base was drawn from, so we
     * render each and max-blend its coverage into the cell. Correct for
     * stacking diacritics (Latin/Greek/Cyrillic/Hebrew/Vietnamese); Arabic
     * joining and Indic reordering need a shaper. The base bitmap has already
     * been consumed above, so re-loading faces here is safe. Skipped for BGRA
     * colour strikes (emoji), whose clusters are handled by presentation
     * selectors rather than stacked marks. */
    if (mark_count > 0 && !(font->color_mode && bitmap->pixel_mode == FT_PIXEL_MODE_BGRA)) {
        for (uint8_t mark_idx = 0; mark_idx < mark_count && mark_idx < RASTER_FONT_MAX_MARKS;
             mark_idx++) {
            FT_Face mark_face = NULL;
            FT_UInt mark_glyph = 0;
            if (!raster_font_resolve_face_glyph(font, marks[mark_idx], style, &mark_face,
                                                &mark_glyph)) {
                continue;
            }
            if (FT_Load_Glyph(mark_face, mark_glyph, FT_LOAD_RENDER)) {
                continue;
            }
            FT_GlyphSlot mark_slot = mark_face->glyph;
            FT_Bitmap *mark_bitmap = &mark_slot->bitmap;
            /* Only grayscale marks complex cleanly into the coverage atlas. */
            if (mark_bitmap->pixel_mode != FT_PIXEL_MODE_GRAY || mark_bitmap->width == 0 ||
                mark_bitmap->rows == 0) {
                continue;
            }
            int mark_w = (int)mark_bitmap->width;
            int mark_h = (int)mark_bitmap->rows;

            int mark_off_x = mark_slot->bitmap_left + RASTER_FONT_ATLAS_PADDING;
            if (mark_off_x < RASTER_FONT_ATLAS_PADDING) {
                mark_off_x = RASTER_FONT_ATLAS_PADDING;
            }
            if (mark_off_x > (int)(glyph_width - (uint32_t)mark_w - RASTER_FONT_ATLAS_PADDING)) {
                mark_off_x = (int)(glyph_width - (uint32_t)mark_w - RASTER_FONT_ATLAS_PADDING);
            }
            if (mark_off_x < RASTER_FONT_ATLAS_PADDING) {
                mark_off_x = RASTER_FONT_ATLAS_PADDING;
            }

            int mark_off_y = font->baseline - mark_slot->bitmap_top + RASTER_FONT_ATLAS_PADDING;
            if (mark_off_y < RASTER_FONT_ATLAS_PADDING) {
                mark_off_y = RASTER_FONT_ATLAS_PADDING;
            }
            if (mark_off_y > (int)(glyph_height - (uint32_t)mark_h - RASTER_FONT_ATLAS_PADDING)) {
                mark_off_y = (int)(glyph_height - (uint32_t)mark_h - RASTER_FONT_ATLAS_PADDING);
            }
            if (mark_off_y < RASTER_FONT_ATLAS_PADDING) {
                mark_off_y = RASTER_FONT_ATLAS_PADDING;
            }

            for (int y = 0; y < mark_h; y++) {
                if (mark_off_y + y >= (int)glyph_height) {
                    break;
                }
                for (int x = 0; x < mark_w; x++) {
                    if (mark_off_x + x >= (int)glyph_width) {
                        break;
                    }
                    uint8_t src = mark_bitmap->buffer[y * mark_bitmap->pitch + x];
                    if (src == 0) {
                        continue;
                    }
                    uint32_t dst_idx = (atlas_y + (uint32_t)mark_off_y + (uint32_t)y) * aw +
                                       (atlas_x + (uint32_t)mark_off_x + (uint32_t)x);
                    if (dst_idx >= atlas_size) {
                        continue;
                    }
                    if (font->color_mode) {
                        uint8_t *dst = pixels + (size_t)dst_idx * 4u;
                        if (src > dst[3]) {
                            dst[0] = 255u;
                            dst[1] = 255u;
                            dst[2] = 255u;
                            dst[3] = src;
                        }
                    } else if (src > pixels[dst_idx]) {
                        pixels[dst_idx] = src;
                    }
                }
            }
        }
    }

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
    size_t atlas_size = (size_t)FONT_ATLAS(font).width * FONT_ATLAS(font).height *
                        (font->atlas_bpp ? font->atlas_bpp : 1u);
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
    FONT_UVS(font)[0].width_cells = 1.0f;
    FONT_UV_BUF(font).size = sizeof(struct yetty_yfont_raster_glyph_uv);

    for (int i = 0; i < 4; i++) {
        font->codepoint_slots_count[i] = 0;
        font->cluster_slots_count[i] = 0;       /* stale cluster slots die with the atlas */
        raster_font_add_slot(font, i, 0x20, 0); /* Map space to slot 0 */
    }
#ifdef YETTY_ENABLE_LIB_HARFBUZZ
    /* Shaped ligature slots reference atlas positions the reset just cleared —
     * drop the cache so they re-rasterize on next use at the new cell size. */
    font->ligature_slot_count = 0;
#endif

    for (int s = 0; s < 4; s++) {
        for (size_t i = 0; i < font->codepoint_slots_count[s]; i++) {
            uint32_t cp = font->codepoint_slots[s][i].codepoint;
            if (cp == 0x20) {
                continue; /* space already reserved */
            }
            if (!raster_font_rasterize_glyph(font, cp, NULL, 0, (enum yetty_yfont_ms_style)s)) {
                ywarn("Failed to re-rasterize glyph U+%04X style %d", cp, s);
            }
        }
    }

    font->dirty = 1;
}

#ifdef YETTY_ENABLE_LIB_HARFBUZZ
/* HarfBuzz table-reference callback: hands a raw SFNT table from the FreeType
 * face to HarfBuzz. Standalone FT<->HB bridge — HarfBuzz reads cmap/GSUB/GDEF
 * directly from these blobs and never touches FreeType outlines (rasterization
 * stays with FreeType), so the two libraries are decoupled. */
static hb_blob_t *raster_font_hb_reference_table(hb_face_t *hb_face, hb_tag_t tag, void *user_data)
{
    (void)hb_face;
    FT_Face ft_face = (FT_Face)user_data;
    FT_ULong length = 0;
    if (FT_Load_Sfnt_Table(ft_face, tag, 0, NULL, &length) != 0 || length == 0) {
        return NULL;
    }
    FT_Byte *buffer = malloc(length);
    if (!buffer) {
        return NULL;
    }
    if (FT_Load_Sfnt_Table(ft_face, tag, 0, buffer, &length) != 0) {
        free(buffer);
        return NULL;
    }
    return hb_blob_create((const char *)buffer, (unsigned)length, HB_MEMORY_MODE_WRITABLE, buffer,
                          free);
}

/* Lazily build (and cache) the HarfBuzz font over the regular face's SFNT
 * tables. Only GSUB substitution is used (to collapse an operator run to its
 * ligature glyph id), so the hb scale is irrelevant and left at the default. */
static hb_font_t *raster_font_hb_font_get(struct yetty_yfont_raster_font *font)
{
    if (font->hb_font) {
        return (hb_font_t *)font->hb_font;
    }
    FT_Face ft_face = font->ft_faces[YETTY_YFONT_MS_STYLE_REGULAR];
    if (!ft_face) {
        return NULL;
    }
    hb_face_t *hb_face = hb_face_create_for_tables(raster_font_hb_reference_table, ft_face, NULL);
    if (!hb_face) {
        return NULL;
    }
    hb_face_set_index(hb_face, (unsigned)(ft_face->face_index & 0xFFFF));
    hb_face_set_upem(hb_face, (unsigned)ft_face->units_per_EM);
    if (hb_face_get_glyph_count(hb_face) == 0) {
        hb_face_destroy(hb_face);
        return NULL;
    }
    hb_font_t *hb_font = hb_font_create(hb_face);
    hb_face_destroy(hb_face); /* hb_font retains a reference */
    if (!hb_font) {
        return NULL;
    }
    font->hb_font = hb_font;
    return hb_font;
}

/* Rasterize a shaped glyph id (a ligature component) at a forced width of
 * `width_cells` cells into the R8 atlas. Mirrors the monochrome path of
 * raster_font_rasterize_glyph but keys on the gid rather than a codepoint (the
 * component glyph has no cmap entry) and fixes the slot width. Fira Code's
 * pieces are each one cell wide, so callers pass width_cells = 1. Ligature
 * faces are never color. Returns the new slot index. */
static struct uint32_result raster_font_rasterize_ligature_gid(struct yetty_yfont_raster_font *font,
                                                               FT_UInt gid, uint32_t width_cells)
{
    FT_Face face = font->ft_faces[YETTY_YFONT_MS_STYLE_REGULAR];
    if (!face) {
        return YETTY_ERR(uint32, "ligature face has no regular style");
    }
    if (FT_Load_Glyph(face, gid, FT_LOAD_RENDER) != 0) {
        return YETTY_ERR(uint32, "FT_Load_Glyph failed for ligature gid");
    }
    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap *bitmap = &slot->bitmap;

    uint32_t slot_idx = (uint32_t)font->next_slot_idx;
    size_t needed = (slot_idx + 1) * sizeof(struct yetty_yfont_raster_glyph_uv);
    if (needed > FONT_UV_BUF(font).capacity) {
        size_t new_cap = FONT_UV_BUF(font).capacity * 2;
        if (new_cap < needed) {
            new_cap = needed;
        }
        void *grown = realloc(FONT_UV_BUF(font).data, new_cap);
        if (!grown) {
            return YETTY_ERR(uint32, "ligature UV realloc failed");
        }
        FONT_UV_BUF(font).data = grown;
        FONT_UV_BUF(font).capacity = new_cap;
    }
    if (needed > FONT_UV_BUF(font).size) {
        FONT_UV_BUF(font).size = needed;
    }
    struct yetty_yfont_raster_glyph_uv *uvs = FONT_UVS(font);

    int cell_w = (int)font->cell_width;
    int cell_h = (int)font->cell_height;
    uint32_t glyph_width = (uint32_t)cell_w * width_cells + RASTER_FONT_ATLAS_PADDING * 2;
    uint32_t glyph_height = (uint32_t)cell_h + RASTER_FONT_ATLAS_PADDING * 2;
    uint32_t aw = FONT_ATLAS(font).width;
    uint32_t ah = FONT_ATLAS(font).height;
    size_t atlas_size = (size_t)aw * ah;

    if (font->shelf_x + glyph_width > aw) {
        font->shelf_x = font->shelf_min_x + RASTER_FONT_ATLAS_PADDING;
        font->shelf_y += font->shelf_height + RASTER_FONT_ATLAS_PADDING;
        font->shelf_height = 0;
    }
    while (font->shelf_y + glyph_height > ah) {
        uint32_t old_width = aw;
        uint32_t old_height = ah;
        raster_font_grow_atlas(font);
        aw = FONT_ATLAS(font).width;
        ah = FONT_ATLAS(font).height;
        atlas_size = (size_t)aw * ah;
        if (aw == old_width && ah == old_height) {
            return YETTY_ERR(uint32, "atlas full, cannot fit ligature glyph");
        }
    }

    uint32_t atlas_x = font->shelf_x;
    uint32_t atlas_y = font->shelf_y;
    uint8_t *pixels = FONT_ATLAS(font).data;

    for (uint32_t y = 0; y < glyph_height; y++) {
        for (uint32_t x = 0; x < glyph_width; x++) {
            uint32_t dst_idx = (atlas_y + y) * aw + (atlas_x + x);
            if (dst_idx < atlas_size) {
                pixels[dst_idx] = 0u;
            }
        }
    }

    int glyph_w = (int)bitmap->width;
    int glyph_h = (int)bitmap->rows;

    int offset_x = slot->bitmap_left + RASTER_FONT_ATLAS_PADDING;
    if (offset_x < RASTER_FONT_ATLAS_PADDING) {
        offset_x = RASTER_FONT_ATLAS_PADDING;
    }
    if (glyph_w > 0 &&
        offset_x > (int)(glyph_width - (uint32_t)glyph_w - RASTER_FONT_ATLAS_PADDING)) {
        offset_x = (int)(glyph_width - (uint32_t)glyph_w - RASTER_FONT_ATLAS_PADDING);
    }
    if (offset_x < RASTER_FONT_ATLAS_PADDING) {
        offset_x = RASTER_FONT_ATLAS_PADDING;
    }

    int offset_y = font->baseline - slot->bitmap_top + RASTER_FONT_ATLAS_PADDING;
    if (offset_y < RASTER_FONT_ATLAS_PADDING) {
        offset_y = RASTER_FONT_ATLAS_PADDING;
    }
    if (glyph_h > 0 &&
        offset_y > (int)(glyph_height - (uint32_t)glyph_h - RASTER_FONT_ATLAS_PADDING)) {
        offset_y = (int)(glyph_height - (uint32_t)glyph_h - RASTER_FONT_ATLAS_PADDING);
    }
    if (offset_y < RASTER_FONT_ATLAS_PADDING) {
        offset_y = RASTER_FONT_ATLAS_PADDING;
    }

    /* Copy coverage bitmap into the slot, clipped to the slot box (a ligature
     * antialiasing overhang must not bleed into the neighbouring slot). */
    for (int y = 0; y < glyph_h; y++) {
        if (offset_y + y >= (int)glyph_height) {
            break;
        }
        for (int x = 0; x < glyph_w; x++) {
            if (offset_x + x >= (int)glyph_width) {
                break;
            }
            uint32_t dst_idx = (atlas_y + (uint32_t)offset_y + (uint32_t)y) * aw +
                               (atlas_x + (uint32_t)offset_x + (uint32_t)x);
            if (dst_idx < atlas_size) {
                pixels[dst_idx] = bitmap->buffer[y * bitmap->pitch + x];
            }
        }
    }

    uvs[slot_idx].uv_x = (float)(atlas_x + RASTER_FONT_ATLAS_PADDING) / (float)aw;
    uvs[slot_idx].uv_y = (float)(atlas_y + RASTER_FONT_ATLAS_PADDING) / (float)ah;
    uvs[slot_idx].width_cells = (float)width_cells;
    uvs[slot_idx].pad = 0.0f;

    font->shelf_x = atlas_x + glyph_width + RASTER_FONT_ATLAS_PADDING;
    if (glyph_height > font->shelf_height) {
        font->shelf_height = glyph_height;
    }

    font->next_slot_idx++;
    font->dirty = 1;
    return YETTY_OK(uint32, slot_idx);
}

static struct uint32_result raster_font_get_glyph_index_ligature(struct yetty_yfont_ms_font *self,
                                                                 const uint32_t *codepoints,
                                                                 size_t count, uint32_t *out_slots)
{
    struct yetty_yfont_raster_font *font = container_of(self, struct yetty_yfont_raster_font, base);
    if (!codepoints || !out_slots || count < 2u || count > YETTY_YFONT_LIGATURE_MAX_LEN) {
        return YETTY_ERR(uint32, "invalid ligature run");
    }

    /* Steady-state fast path: the same ligature reappears every frame (the grid
     * re-packs each visible row), so serve a known sequence from the cache
     * without touching HarfBuzz or allocating a shaping buffer. */
    for (size_t i = 0; i < font->ligature_slot_count; i++) {
        const struct yetty_yfont_ligature_slot *entry = &font->ligature_slots[i];
        if (entry->count != count) {
            continue;
        }
        int same = 1;
        for (size_t k = 0; k < count; k++) {
            if (entry->codepoints[k] != codepoints[k]) {
                same = 0;
                break;
            }
        }
        if (same) {
            for (size_t k = 0; k < count; k++) {
                out_slots[k] = entry->slots[k];
            }
            return YETTY_OK(uint32, (uint32_t)count);
        }
    }

    hb_font_t *hb_font = raster_font_hb_font_get(font);
    if (!hb_font) {
        return YETTY_ERR(uint32, "harfbuzz font unavailable for ligature face");
    }

    hb_buffer_t *buffer = hb_buffer_create();
    if (!buffer || !hb_buffer_allocation_successful(buffer)) {
        if (buffer) {
            hb_buffer_destroy(buffer);
        }
        return YETTY_ERR(uint32, "hb_buffer_create failed");
    }
    for (size_t i = 0; i < count; i++) {
        hb_buffer_add(buffer, (hb_codepoint_t)codepoints[i], (unsigned)i);
    }
    hb_buffer_set_content_type(buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);
    hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
    hb_buffer_set_script(buffer, HB_SCRIPT_LATIN);
    /* Default features apply liga/calt — the programming ligatures live in the
     * face's calt table. */
    hb_shape(hb_font, buffer, NULL, 0);

    unsigned glyph_count = 0;
    hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
    /* Monospace ligature fonts substitute each cell's character with a
     * ligature-piece glyph and keep the glyph count equal to the cell count
     * (each piece advances one cell). If the run did not shape one-glyph-per-
     * cell, treat it as non-ligating and let the grid draw the characters
     * individually. */
    if (!infos || glyph_count != count) {
        hb_buffer_destroy(buffer);
        return YETTY_ERR(uint32, "run does not shape to one glyph per cell");
    }
    uint32_t gids[YETTY_YFONT_LIGATURE_MAX_LEN];
    for (size_t i = 0; i < count; i++) {
        gids[i] = infos[i].codepoint; /* after shaping, .codepoint holds the gid */
    }
    hb_buffer_destroy(buffer);

    /* Rasterize each cell's component glyph (width 1) into the atlas. */
    uint32_t new_slots[YETTY_YFONT_LIGATURE_MAX_LEN];
    for (size_t i = 0; i < count; i++) {
        if (gids[i] == 0u) {
            return YETTY_ERR(uint32, "ligature component resolved to notdef");
        }
        struct uint32_result slot_res =
            raster_font_rasterize_ligature_gid(font, (FT_UInt)gids[i], 1u);
        if (YETTY_IS_ERR(slot_res)) {
            return slot_res;
        }
        new_slots[i] = slot_res.value;
    }

    if (font->ligature_slot_count >= font->ligature_slot_capacity) {
        size_t new_cap = font->ligature_slot_capacity ? font->ligature_slot_capacity * 2 : 32;
        struct yetty_yfont_ligature_slot *grown =
            realloc(font->ligature_slots, new_cap * sizeof(struct yetty_yfont_ligature_slot));
        if (!grown) {
            return YETTY_ERR(uint32, "ligature slot cache realloc failed");
        }
        font->ligature_slots = grown;
        font->ligature_slot_capacity = new_cap;
    }
    struct yetty_yfont_ligature_slot *new_entry = &font->ligature_slots[font->ligature_slot_count];
    for (size_t k = 0; k < count; k++) {
        new_entry->codepoints[k] = codepoints[k];
        new_entry->slots[k] = new_slots[k];
        out_slots[k] = new_slots[k];
    }
    new_entry->count = (uint8_t)count;
    font->ligature_slot_count++;
    return YETTY_OK(uint32, (uint32_t)count);
}
#else  /* !YETTY_ENABLE_LIB_HARFBUZZ */
static struct uint32_result raster_font_get_glyph_index_ligature(struct yetty_yfont_ms_font *self,
                                                                 const uint32_t *codepoints,
                                                                 size_t count, uint32_t *out_slots)
{
    (void)self;
    (void)codepoints;
    (void)count;
    (void)out_slots;
    return YETTY_ERR(uint32, "ligature shaping requires HarfBuzz");
}
#endif /* YETTY_ENABLE_LIB_HARFBUZZ */

static void raster_font_cleanup(struct yetty_yfont_raster_font *font)
{
#ifdef YETTY_ENABLE_LIB_HARFBUZZ
    if (font->hb_font) {
        hb_font_destroy((hb_font_t *)font->hb_font);
        font->hb_font = NULL;
    }
    free(font->ligature_slots);
    font->ligature_slots = NULL;
    font->ligature_slot_count = 0;
    font->ligature_slot_capacity = 0;
#endif
    for (int i = 0; i < 4; i++) {
        if (font->ft_faces[i]) {
            FT_Done_Face(font->ft_faces[i]);
            font->ft_faces[i] = NULL;
        }
    }
    for (size_t i = 0; i < font->fallback_count; i++) {
        if (font->fallback_faces[i]) {
            FT_Done_Face(font->fallback_faces[i]);
            font->fallback_faces[i] = NULL;
        }
    }
    font->fallback_count = 0;
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
        free(font->cluster_slots[i]);
        font->cluster_slots[i] = NULL;
    }
}

static int raster_font_name_compare(const void *left, const void *right)
{
    const char *const *left_name = left;
    const char *const *right_name = right;
    return strcmp(*left_name, *right_name);
}

/* Load faces for a glob font name ("NotoSans*"): every
 * <prefix><anything>-Regular.ttf in the fonts dir, sorted by name. The first
 * match becomes the primary face, the rest form the fallback chain — one
 * configured face can cover every script the staged fonts cover. Color
 * faces are skipped (they need the RGBA8 atlas; use a dedicated range). */
static struct yetty_ycore_void_result raster_font_load_glob_faces(
    struct yetty_yfont_raster_font *font)
{
    static const char REGULAR_SUFFIX[] = "-Regular.ttf";
    const size_t suffix_len = sizeof(REGULAR_SUFFIX) - 1;

    char prefix[RASTER_FONT_MAX_PATH];
    snprintf(prefix, sizeof(prefix), "%s", font->font_name);
    char *star = strchr(prefix, '*');
    if (star) {
        *star = '\0';
    }
    size_t prefix_len = strlen(prefix);

    struct yetty_yplatform_dir *fonts_dir_handle = yetty_yplatform_dir_open(font->fonts_dir);
    if (!fonts_dir_handle) {
        return YETTY_ERR(yetty_ycore_void, "fonts dir not readable for glob font");
    }
    char *file_names[RASTER_FONT_MAX_FALLBACK_FACES + 1];
    size_t file_count = 0;
    struct yetty_yplatform_dir_entry entry;
    while (yetty_yplatform_dir_next(fonts_dir_handle, &entry) &&
           file_count < RASTER_FONT_MAX_FALLBACK_FACES + 1) {
        size_t name_len = strlen(entry.name);
        if (name_len <= suffix_len) {
            continue;
        }
        if (strncmp(entry.name, prefix, prefix_len) != 0) {
            continue;
        }
        if (strcmp(entry.name + name_len - suffix_len, REGULAR_SUFFIX) != 0) {
            continue;
        }
        char *copy = strdup(entry.name);
        if (copy) {
            file_names[file_count++] = copy;
        }
    }
    yetty_yplatform_dir_close(fonts_dir_handle);
    if (file_count == 0) {
        return YETTY_ERR(yetty_ycore_void, "no fonts match glob font name");
    }
    qsort(file_names, file_count, sizeof(char *), raster_font_name_compare);

    for (size_t i = 0; i < file_count; i++) {
        char path[RASTER_FONT_MAX_PATH * 2];
        snprintf(path, sizeof(path), "%s/%s", font->fonts_dir, file_names[i]);
        FT_Face face = NULL;
        if (FT_New_Face(font->ft_library, path, 0, &face)) {
            free(file_names[i]);
            continue;
        }
        if (FT_HAS_COLOR(face)) {
            FT_Done_Face(face);
            free(file_names[i]);
            continue;
        }
        if (!font->ft_faces[0]) {
            font->ft_faces[0] = face;
        } else if (font->fallback_count < RASTER_FONT_MAX_FALLBACK_FACES) {
            font->fallback_faces[font->fallback_count++] = face;
        } else {
            FT_Done_Face(face);
        }
        free(file_names[i]);
    }
    if (!font->ft_faces[0]) {
        return YETTY_ERR(yetty_ycore_void, "glob font matched files but none loaded");
    }
    ydebug("RasterFont glob '%s': primary + %zu fallback faces", font->font_name,
           font->fallback_count);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Create
 *===========================================================================*/

struct yetty_font_ms_font_result yetty_yfont_ms_raster_font_create(
    struct yetty_yconfig_config *config, float cell_width, float cell_height)
{
    const char *font_family = config->ops->font_family(config);
    return yetty_yfont_ms_raster_font_create_named(config, font_family, cell_width, cell_height);
}

struct yetty_font_ms_font_result yetty_yfont_ms_raster_font_create_named(
    struct yetty_yconfig_config *config, const char *font_name, float cell_width, float cell_height)
{
    return raster_font_create_named_impl(config, font_name, cell_width, cell_height, 0);
}

struct yetty_font_ms_font_result yetty_yfont_ms_raster_font_create_ligature(
    struct yetty_yconfig_config *config, const char *font_name, float cell_width, float cell_height)
{
    return raster_font_create_named_impl(config, font_name, cell_width, cell_height, 1);
}

static struct yetty_font_ms_font_result raster_font_create_named_impl(
    struct yetty_yconfig_config *config, const char *font_name, float cell_width, float cell_height,
    int size_to_cell_width)
{
    const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    const char *font_family = font_name;
    if (!font_family || !font_family[0] || strcmp(font_family, "default") == 0) {
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
    FONT_ATLAS(font).format = YETTY_YRENDER_TEXTURE_FORMAT_R8_UNORM;
    FONT_ATLAS(font).sampler_filter = YETTY_YRENDER_FILTER_LINEAR;
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
    FONT_UVS(font)[0].width_cells = 1.0f;
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

    if (strchr(font->font_name, '*')) {
        struct yetty_ycore_void_result glob_res = raster_font_load_glob_faces(font);
        if (YETTY_IS_ERR(glob_res)) {
            raster_font_cleanup(font);
            free(font);
            return YETTY_ERR(yetty_font_ms_font, "Failed to load glob font", glob_res);
        }
    } else {
        for (int i = 0; i < 4; i++) {
            char path[RASTER_FONT_MAX_PATH * 2];
            snprintf(path, sizeof(path), "%s/%s%s", font->fonts_dir, font->font_name,
                     FACE_SUFFIXES[i]);
            if (FT_New_Face(font->ft_library, path, 0, &font->ft_faces[i])) {
                if (i == 0) {
                    /* Single-file faces (e.g. NotoColorEmoji.ttf) carry no style
                     * suffix — fall back to the bare name for Regular. */
                    snprintf(path, sizeof(path), "%s/%s.ttf", font->fonts_dir, font->font_name);
                    if (FT_New_Face(font->ft_library, path, 0, &font->ft_faces[0]) == 0) {
                        continue;
                    }
                    raster_font_cleanup(font);
                    free(font);
                    return YETTY_ERR(yetty_font_ms_font, "Failed to load font");
                }
                font->ft_faces[i] = NULL;
            }
        }
    }

    if (FT_HAS_COLOR(font->ft_faces[0])) {
        /* Color bitmap face (emoji): switch the atlas to RGBA8. */
        font->color_mode = 1;
        font->atlas_bpp = 4;
        FONT_ATLAS(font).format = YETTY_YRENDER_TEXTURE_FORMAT_RGBA8_UNORM;
        size_t atlas_pixels = (size_t)FONT_ATLAS(font).width * FONT_ATLAS(font).height;
        free(FONT_ATLAS(font).data);
        FONT_ATLAS(font).data = calloc(atlas_pixels, 4);
        if (!FONT_ATLAS(font).data) {
            raster_font_cleanup(font);
            free(font);
            return YETTY_ERR(yetty_font_ms_font, "Failed to allocate color atlas");
        }
    } else {
        font->atlas_bpp = 1;
    }

    font->size_to_cell_width = size_to_cell_width;
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
    const struct yetty_yfont_raster_font *font =
        container_of(self, struct yetty_yfont_raster_font, base);
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

    if (raster_font_rasterize_glyph(font, codepoint, NULL, 0, (enum yetty_yfont_ms_style)style)) {
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

static struct uint32_result raster_font_get_glyph_index_cluster(struct yetty_yfont_ms_font *self,
                                                                uint32_t codepoint,
                                                                const uint32_t *marks,
                                                                uint8_t mark_count,
                                                                enum yetty_yfont_ms_style style)
{
    /* No marks → the plain per-codepoint slot; keeps the common path off the
     * cluster cache entirely. */
    if (mark_count == 0 || marks == NULL) {
        return raster_font_get_glyph_index_styled(self, codepoint, style);
    }

    struct yetty_yfont_raster_font *font = container_of(self, struct yetty_yfont_raster_font, base);
    if (mark_count > RASTER_FONT_MAX_MARKS) {
        mark_count = RASTER_FONT_MAX_MARKS;
    }

    int style_idx = (int)style;
    uint32_t slot = raster_font_lookup_cluster(font, style_idx, codepoint, marks, mark_count);
    if (slot != UINT32_MAX) {
        return YETTY_OK(uint32, slot);
    }

    if (raster_font_rasterize_glyph(font, codepoint, marks, mark_count, style)) {
        slot = raster_font_lookup_cluster(font, style_idx, codepoint, marks, mark_count);
        if (slot != UINT32_MAX) {
            return YETTY_OK(uint32, slot);
        }
    }

    /* The styled face may have resolved the base under Regular; look there. */
    if (style != YETTY_YFONT_MS_STYLE_REGULAR) {
        return raster_font_get_glyph_index_cluster(self, codepoint, marks, mark_count,
                                                   YETTY_YFONT_MS_STYLE_REGULAR);
    }

    /* Last resort: the bare base glyph rather than nothing (marks dropped). */
    return raster_font_get_glyph_index_styled(self, codepoint, style);
}

/* Inverse — scan the per-style codepoint_slots arrays for a slot match.
 * Style isn't preserved through the cell buffer, so we search all four
 * style arrays. Each glyph_index is unique across styles for a given
 * codepoint, so the first hit wins. Linear scan is fine here: it only
 * runs during selection extraction (a few hundred cells, not per-frame). */
static struct uint32_result raster_font_get_codepoint(struct yetty_yfont_ms_font *self,
                                                      uint32_t glyph_index)
{
    struct yetty_yfont_raster_font *font = container_of(self, struct yetty_yfont_raster_font, base);
    if (glyph_index == 0) {
        return YETTY_OK(uint32, 0x20); /* slot 0 is the reserved space cell */
    }
    for (int s = 0; s < 4; s++) {
        for (size_t i = 0; i < font->codepoint_slots_count[s]; i++) {
            if (font->codepoint_slots[s][i].slot == glyph_index) {
                return YETTY_OK(uint32, font->codepoint_slots[s][i].codepoint);
            }
        }
    }
    return YETTY_ERR(uint32, "unknown glyph_index");
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

static struct yetty_ycore_void_result raster_font_set_cell_size(
    struct yetty_yfont_ms_font *self, struct yetty_ycore_pixel_size cell_size)
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

            if (!raster_font_rasterize_glyph(font, codepoint, NULL, 0, style)) {
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
    const struct yetty_yfont_raster_font *font =
        container_of(self, struct yetty_yfont_raster_font, base);
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
        /* Bump generation with dirty so every binder embedding this shared
         * font atlas re-uploads independently — see the buffer/texture
         * `generation` contract in yrender/types.h. */
        FONT_ATLAS(font).dirty = 1;
        FONT_ATLAS(font).generation++;
        FONT_UV_BUF(font).dirty = 1;
        FONT_UV_BUF(font).generation++;
        font->dirty = 0;
    }

    return YETTY_OK(yetty_yrender_gpu_resource_set, &font->rs);
}
