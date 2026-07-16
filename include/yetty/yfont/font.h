#ifndef YETTY_YFONT_FONT_H
#define YETTY_YFONT_FONT_H

/*
 * yetty_font_font - Non-monospace font interface
 *
 * Used by ydraw text spans. No fixed cell size.
 * Each text span specifies its own font size — shader scales.
 * Font provides gpu_resource_set (atlas + glyph metadata).
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrender/gpu-resource-set.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yfont_font;

/* Font style */
enum yetty_yfont_style {
    YETTY_YFONT_STYLE_REGULAR = 0,
    YETTY_YFONT_STYLE_BOLD = 1,
    YETTY_YFONT_STYLE_ITALIC = 2,
    YETTY_YFONT_STYLE_BOLD_ITALIC = 3,
};

YETTY_YRESULT_DECLARE(yetty_font_font, struct yetty_yfont_font *);

/*
 * Complex-script shaping support.
 *
 * Scripts whose correct rendering needs OpenType shaping (contextual joining,
 * glyph reordering, conjunct formation, mark positioning) rather than the
 * one-codepoint-one-glyph model. A run of same-class codepoints is handed to
 * the shaper as a unit. YETTY_YFONT_SHAPING_NONE codepoints stay on the fast
 * per-codepoint path.
 */
enum yetty_yfont_shaping_script {
    YETTY_YFONT_SHAPING_NONE = 0,
    YETTY_YFONT_SHAPING_ARABIC,  /* Arabic + Syriac + Thaana + N'Ko: cursive joining */
    YETTY_YFONT_SHAPING_INDIC,   /* Devanagari..Malayalam, Sinhala: reorder + conjuncts */
    YETTY_YFONT_SHAPING_BRAHMIC, /* Thai, Lao, Khmer, Myanmar, Tibetan: mark positioning */
};

/* Classify a codepoint for run detection. HarfBuzz-free — always available,
 * so callers can detect shaping runs even in builds without the shaper. */
enum yetty_yfont_shaping_script yetty_yfont_shaping_script_for_codepoint(uint32_t codepoint);

/* Longest ligature (in a monospace programming font) supported by the table. */
#define YETTY_YFONT_LIGATURE_MAX_LEN 4u

/* Programming-ligature detection for the terminal grid. Returns the number of
 * codepoints of the longest known programming ligature that begins at
 * codepoints[pos] (e.g. 2 for "=>", 3 for "===", 4 for "<==>"), or 0 if none.
 * HarfBuzz-free and deterministic — the same table is consulted by the grid
 * glyph-suppression pass and the SDF shaping pass so they agree, without
 * communicating, on exactly which cells a ligature covers. The set is
 * punctuation-only, so it never triggers on ordinary words or numbers. */
size_t yetty_yfont_ligature_length_at(const uint32_t *codepoints, size_t count, size_t pos);

/* One shaped output glyph. Positions are in pixels at the font's base_size,
 * matching struct yetty_yfont_glyph_meta_gpu conventions, so the ydraw
 * free-position path can place them directly and scale by font_size/base_size. */
struct yetty_yfont_shaped_glyph {
    uint32_t gid;     /* OpenType glyph id in this face — feed to get_glyph_index_by_gid */
    float x_offset;   /* GPOS x offset from the pen, px at base_size */
    float y_offset;   /* GPOS y offset from the pen, px at base_size (up positive) */
    float x_advance;  /* horizontal advance, px at base_size */
    uint32_t cluster; /* source codepoint index within the run */
};

/* Font ops */
struct yetty_yfont_font_ops {
    void (*destroy)(struct yetty_yfont_font *self);

    /* Glyph lookup — loads on demand, returns glyph index */
    struct uint32_result (*get_glyph_index)(struct yetty_yfont_font *self, uint32_t codepoint);
    struct uint32_result (*get_glyph_index_styled)(struct yetty_yfont_font *self,
                                                   uint32_t codepoint,
                                                   enum yetty_yfont_style style);
    /* Resolve an OpenType glyph id (from the shaper) to an atlas slot,
     * rasterizing by glyph index rather than by codepoint. This is the
     * (glyph_id, face) atlas key: the face is this font object, so the key
     * reduces to the gid. Optional — NULL when the backend has no live
     * rasterizer (MSDF/CDB is codepoint-keyed and cannot render arbitrary
     * gids). */
    struct uint32_result (*get_glyph_index_by_gid)(struct yetty_yfont_font *self, uint32_t gid);
    /* Shape a run of codepoints belonging to one script/direction using the
     * face's OpenType tables. Script, direction and language are inferred from
     * the run content. Writes up to out_cap shaped glyphs into out and returns
     * the count actually produced (may differ from count: ligatures reduce it,
     * decompositions raise it). Optional — NULL when the backend has no
     * shaper (build without HarfBuzz, or a non-FreeType backend). */
    struct uint32_result (*shape_run)(struct yetty_yfont_font *self, const uint32_t *codepoints,
                                      size_t count, struct yetty_yfont_shaped_glyph *out,
                                      size_t out_cap);
    /* Inverse of get_glyph_index — given an atlas glyph index the font
     * previously handed out, recover the codepoint that produced it.
     * Used by selection / clipboard so glyph prims (which store
     * glyph_index, not codepoint) can be reconstructed back into UTF-8.
     * Returns an error result for an unknown glyph_index. */
    struct uint32_result (*get_codepoint)(struct yetty_yfont_font *self, uint32_t glyph_index);

    /* Glyph loading */
    struct yetty_ycore_void_result (*load_glyphs)(struct yetty_yfont_font *self,
                                                  const uint32_t *codepoints, size_t count);
    struct yetty_ycore_void_result (*load_basic_latin)(struct yetty_yfont_font *self);

    /* Horizontal advance for a single codepoint at the given pixel size.
	 * Must not require glyph rasterization, atlas placement, or shader.
	 * Units: pixels at the requested font_size. */
    struct float_result (*get_advance)(struct yetty_yfont_font *self, uint32_t codepoint,
                                       float font_size);

    /* Width of a UTF-8 byte range at the given pixel size.
	 * Must not require glyph rasterization, atlas placement, or shader.
	 * Implementations may override to handle kerning/shaping; the default
	 * behaviour is to sum get_advance() over codepoints. */
    struct float_result (*measure_text)(struct yetty_yfont_font *self, const char *utf8, size_t len,
                                        float font_size);

    /* Base size the CDB was generated at */
    float (*get_base_size)(const struct yetty_yfont_font *self);

    /* Dirty tracking */
    int (*is_dirty)(const struct yetty_yfont_font *self);

    /* GPU resources — clears dirty internally */
    struct yetty_yrender_gpu_resource_set_result (*get_gpu_resource_set)(
        struct yetty_yfont_font *self);
};

/* Font base */
struct yetty_yfont_font {
    const struct yetty_yfont_font_ops *ops;
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFONT_FONT_H */
