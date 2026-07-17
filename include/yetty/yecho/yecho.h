#ifndef YETTY_YECHO_YECHO_H
#define YETTY_YECHO_YECHO_H

/*
 * yecho - text with embedded glyphs and styled blocks, rendered to a
 * ydraw-core buffer (client-side only — yecho never runs in yetty's
 * process).
 *
 * Grammar:
 *   @name                       -> shader glyph by name
 *   {attrs: content}            -> styled block (text run with attrs applied)
 *   {plot; ...: f=expr; ...}    -> yplot block (function plotter)
 *   \{ \} \@ \\                 -> escaped literals
 *
 * Styled-block attributes (semicolon-separated key=value):
 *   color=#RRGGBB    text color (hex; #RGB also accepted)
 *   bg=#RRGGBB       background color (hex)
 *   style=bold       style (bold | italic | underline; combinable with '|')
 *   font-size=N      text size in pixels for this block (default = config)
 *
 * Plot-block attributes (in addition to the keyword `plot`):
 *   w=N              width in pixels (default 400)
 *   h=N              height in pixels (default 200)
 *   xrange=lo..hi    X axis range (default -3.14..3.14)
 *   yrange=lo..hi    Y axis range (default -1.5..1.5)
 *   nogrid|noaxes|nolabels — disable the corresponding overlay
 *
 * Plot-block content uses yexpr's plot syntax:
 *   f=sin(x); g=cos(x); @f.color=#FF6B6B
 *
 * Styled-block content is treated as plain text — nested glyphs/blocks
 * are not recursively expanded.
 *
 * Glyph names resolve via yetty_yfont_shader_glyph_codepoint() (PUA-B
 * range, U+100000 + local_id). Unknown names land in the doc's error list
 * and render as a literal "[?name]" placeholder.
 *
 * The library emits a yetty_ydraw_drawable_list (text spans for runs of
 * UTF-8 text, SDF box primitives for backgrounds). The frontend tool
 * wraps that buffer in the same OSC envelope ycat uses
 * (YETTY_DCS_YDRAW_BIN — see yetty_yecho_dcs_bin_emit).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * Style flags (combined with bitwise OR)
 *===========================================================================*/

enum yetty_yecho_style {
    YETTY_YECHO_STYLE_NONE = 0,
    YETTY_YECHO_STYLE_BOLD = 1u << 0,
    YETTY_YECHO_STYLE_ITALIC = 1u << 1,
    YETTY_YECHO_STYLE_UNDERLINE = 1u << 2,
};

/*=============================================================================
 * Public layout: attribute and span
 *===========================================================================*/

struct yetty_yecho_attr {
    char *key;   /* NUL-terminated, owned by the parent doc */
    char *value; /* NUL-terminated, owned by the parent doc */
};

enum yetty_yecho_span_type {
    YETTY_YECHO_SPAN_TEXT,
    YETTY_YECHO_SPAN_GLYPH,
    YETTY_YECHO_SPAN_BLOCK,
};

struct yetty_yecho_span {
    enum yetty_yecho_span_type type;

    /* TEXT:  text payload (NUL-terminated, escapes resolved)
     * GLYPH: glyph name (NUL-terminated, no leading '@')
     * BLOCK: content after ':' (NUL-terminated, escapes resolved) */
    char *text;

    /* BLOCK only: array of attrs from the left side of ':'. */
    struct yetty_yecho_attr *attrs;
    size_t attr_count;
};

/*=============================================================================
 * Parsed document — opaque, owns spans + diagnostics
 *===========================================================================*/

struct yetty_yecho_doc;

YETTY_YRESULT_DECLARE(yetty_yecho_doc_ptr, struct yetty_yecho_doc *);

/* Parse `input` (length `len`) into a document. Unknown glyph names are
 * collected in the doc's error list but do not fail the parse. */
struct yetty_yecho_doc_ptr_result yetty_yecho_parse(const char *input, size_t len);

void yetty_yecho_doc_destroy(struct yetty_yecho_doc *doc);

size_t yetty_yecho_doc_span_count(const struct yetty_yecho_doc *doc);
const struct yetty_yecho_span *yetty_yecho_doc_span(const struct yetty_yecho_doc *doc, size_t idx);

size_t yetty_yecho_doc_error_count(const struct yetty_yecho_doc *doc);
const char *yetty_yecho_doc_error(const struct yetty_yecho_doc *doc, size_t idx);

/*=============================================================================
 * Rendering — produce a ydraw-core buffer
 *===========================================================================*/

struct yetty_yecho_render_config {
    /* Cell size in pixels (sets scene_max bounds via width_cells). */
    uint32_t cell_width;
    uint32_t cell_height;
    /* Layout width in cells. 0 → 80. */
    uint32_t width_cells;
    /* Text size in pixels. 0 → cell_height. */
    float font_size;
    /* Default foreground color (RGBA, R in low byte). 0 → 0xFFE6E6E6. */
    uint32_t default_fg;
    /* Line spacing multiplier (default 1.2 if 0). */
    float line_spacing;
};

/* Render `doc` into a fresh ydraw-core buffer. Caller frees with
 * yetty_ydraw_drawable_list_destroy. */
struct yetty_ydraw_drawable_list_result yetty_yecho_render(
    const struct yetty_yecho_doc *doc, const struct yetty_yecho_render_config *config);

/* Convenience: parse + render in one call. */
struct yetty_ydraw_drawable_list_result yetty_yecho_render_string(
    const char *input, size_t len, const struct yetty_yecho_render_config *config);

/*=============================================================================
 * DCS emission — wraps a ydraw buffer in a YETTY_DCS_YDRAW_BIN envelope
 * (same wire format as yetty_ycat_dcs_bin_emit). Returns bytes written on
 * success.
 *===========================================================================*/

struct yetty_ycore_size_result yetty_yecho_dcs_bin_emit(
    const struct yetty_ydraw_drawable_list *buffer, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YECHO_YECHO_H */
