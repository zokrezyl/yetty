/*
 * yrich:ydoc — paragraph-flow rich text document (+ its element kinds).
 *
 * Three classes live in this TU:
 *   ydoc          — parent@yrich:document; owns paragraph/image alias arrays
 *   paragraph     — parent@yrich:element; editable text block
 *   inline_image  — parent@yrich:element; placeholder image box
 *
 * Layout is naive — paragraphs stack vertically. The element objects are
 * owned by the document base's element list; the alias arrays here only
 * provide in-flow ordering.
 *
 * This TU deliberately does NOT include its own generated header
 * `yetty/yrich/ydoc.h`; the accessors/downcasts the appended ydoc.gen.c
 * defines are forward-declared below. The base-class headers (document.h,
 * element.h) ARE included — they are foreign generated artifacts.
 */

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <yetty/yrich/yrich-command.h>
#include <yetty/yrich/yrich-operation.h>
#include <yetty/yrich/yrich-selection.h>
#include <yetty/yrich/yrich-types.h>

#include <yetty/yrich/document.h>
#include <yetty/yrich/element.h>

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yfont/font.h> /* metrics-only glyph advances for proportional layout */
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <stdio.h> /* snprintf for list markers */
#include <stdlib.h>
#include <string.h>

#define YDOC_DEFAULT_PAGE_WIDTH 600.0f
#define YDOC_DEFAULT_MARGIN 20.0f
#define YDOC_DEFAULT_LINE_HEIGHT 20.0f
/* Default line-spacing multiplier (line_height = font_size * line_spacing).
 * The single source of truth — every site derives line_height from the
 * paragraph's line_spacing field, which starts here. */
#define YDOC_DEFAULT_LINE_SPACING 1.4f

/* Accessors / downcasts / factories defined in the appended ydoc.gen.c. */
YETTY_YRESULT_DECLARE(yetty_yrich_ydoc_ptr, struct yetty_yrich_ydoc *);
YETTY_YRESULT_DECLARE(yetty_yrich_paragraph_ptr, struct yetty_yrich_paragraph *);
YETTY_YRESULT_DECLARE(yetty_yrich_inline_image_ptr, struct yetty_yrich_inline_image *);
struct yetty_yclass_ptr_result yetty_yrich_ydoc_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_paragraph_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_inline_image_class_get(void);
struct yetty_yrich_ydoc_ptr_result yetty_yrich_ydoc_from(struct yetty_yclass_object *obj);
struct yetty_yrich_paragraph_ptr_result yetty_yrich_paragraph_from(struct yetty_yclass_object *obj);
struct yetty_yrich_inline_image_ptr_result yetty_yrich_inline_image_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_paragraph_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_inline_image_create(struct yetty_yclass_ctx *ctx);

/* Exposed setter defined below, used by the add_paragraph factory. */
struct yetty_ycore_void_result yetty_yrich_paragraph_set_text(struct yetty_yclass_object *obj,
                                                              const char *text, size_t len);

/* Exposed toggle defined below, used by the checklist-gutter mouse path. */
struct yetty_ycore_void_result yetty_yrich_ydoc_toggle_checked(struct yetty_yclass_object *obj);

/*===========================================================================
 * Paragraph
 *=========================================================================*/

struct YETTY_ANNOTATE("class@yrich:paragraph") YETTY_ANNOTATE("parent@yrich:element")
    yetty_yrich_paragraph {
    struct yetty_yrich_rect bounds;

    char *text; /* owned, NUL-terminated */
    size_t text_len;
    struct yetty_yrich_text_style style; /* base style; runs override ranges */

    /* Sorted, non-overlapping spans whose format/color differ from the
	 * base style. Maintained by the attribute engine below. */
    struct yetty_yrich_text_run *runs;
    size_t run_count;
    size_t run_capacity;

    float line_height;      /* derived: font_size * line_spacing */
    float line_spacing;     /* multiplier: 1.0 single … 2.0 double (default 1.4) */
    float indent;           /* left indent in px (Increase/Decrease indent) */
    float space_before;     /* extra vertical gap above the paragraph (px) */
    float space_after;      /* extra vertical gap below the paragraph (px) */
    uint32_t heading_level; /* 0 = body text, 1..6 = semantic heading level */
    uint32_t halign;        /* enum yetty_yrich_halign */

    /* Lists: 0 none, 1 bullet, 2 numbered, 3 checklist. list_ordinal is
     * computed during relayout (the position among consecutive numbered
     * siblings); list_checked is the checklist item's checkbox state.
     * list_level is the 0-based nesting depth (Tab/Shift-Tab). */
    uint32_t list_kind;
    int list_ordinal;
    int list_checked;
    uint32_t list_level;

    /* Block kind: 0 = normal text paragraph, 1 = horizontal-rule divider
     * (rendered as a line, no text, never a caret target), 2 = table (a grid
     * of text cells). Insert > Horizontal rule / Table create these. */
    uint32_t block_kind;

    /* Table (block_kind == 2): a rows x cols grid of owned cell strings, plus
     * the cell being edited (row*cols + col, or -1). */
    uint32_t table_rows;
    uint32_t table_cols;
    char **table_cells;
    int32_t table_active_cell;

    /* Named bookmark anchoring this paragraph (owned, NULL = none). Insert >
     * Bookmark sets it; Edit > Go to bookmark navigates to it. */
    char *bookmark;

    /* Metrics-only font for proportional layout; NULL falls back to the fixed
     * 0.6-em advance. Set from the owning ydoc during relayout. */
    const struct yetty_yfont_font *metrics_font;

    /* Render-time: draw the middot/pilcrow marks for spaces and paragraph
     * ends. Mirrored from the owning ydoc during relayout. */
    int show_nonprinting;

    /* Which styled font faces are registered on the render ygrid, as a bitmask
     * (bit 0 = bold at slot 1, bit 1 = italic at slot 2, bit 2 = bold-italic at
     * slot 3). Mirrored from the owning ydoc during relayout; drives per-run
     * font_id selection so a run only references a styled slot that exists. */
    uint32_t styled_font_mask;

    int editing;
    int selected; /* render-time: part of a document-wide selection (select-all) */
    int32_t cursor_pos;
    int32_t sel_start;
    int32_t sel_end;
};

/*---------------------------------------------------------------------------
 * Word wrap — the paragraph lays its text out as wrapped lines inside
 * bounds.w. Character advance is approximated as font_size * 0.6 (the
 * convention the cell/textinput widgets use); the same metric drives
 * rendering, caret drawing and click-to-caret mapping so they agree.
 *-------------------------------------------------------------------------*/

struct paragraph_line {
    size_t start; /* first byte of the line */
    size_t end;   /* one past the last byte drawn on the line */
    size_t next;  /* where the following line starts (separator consumed) */
};

enum ydoc_list_kind {
    YDOC_LIST_NONE = 0,
    YDOC_LIST_BULLET = 1,
    YDOC_LIST_NUMBERED = 2,
    YDOC_LIST_CHECK = 3,
};

enum ydoc_block_kind {
    YDOC_BLOCK_TEXT = 0,
    YDOC_BLOCK_DIVIDER = 1,    /* horizontal rule */
    YDOC_BLOCK_TABLE = 2,      /* grid of text cells */
    YDOC_BLOCK_PAGE_BREAK = 3, /* forced page break marker */
};

/* Render ygrid font slots: slot 0 is the Regular face (paragraph base), 1/2/3
 * are the styled faces the host registers. Kept in lock-step with the slot
 * assignment in the ygrid factory and the mask below. */
enum ydoc_font_slot {
    YDOC_FONT_SLOT_REGULAR = 0,
    YDOC_FONT_SLOT_BOLD = 1,
    YDOC_FONT_SLOT_ITALIC = 2,
    YDOC_FONT_SLOT_BOLD_ITALIC = 3,
};

/* Bits of paragraph->styled_font_mask: which styled faces are actually present
 * on the render ygrid (so a run never references an unregistered slot). */
enum ydoc_styled_font_bit {
    YDOC_STYLED_BOLD = 1u << 0,
    YDOC_STYLED_ITALIC = 1u << 1,
    YDOC_STYLED_BOLD_ITALIC = 1u << 2,
};

/* Non-caret-target rule-like blocks (a click redirects, typing is ignored). */
static inline int ydoc_block_is_rule_like(uint32_t block_kind)
{
    return block_kind == YDOC_BLOCK_DIVIDER || block_kind == YDOC_BLOCK_PAGE_BREAK;
}

/* Vertical box a horizontal-rule divider paragraph occupies; the line is
 * drawn across its middle. */
#define YDOC_DIVIDER_HEIGHT 18.0f
/* A page-break marker occupies a taller gap with a dashed line. */
#define YDOC_PAGE_BREAK_HEIGHT 40.0f
/* Visual lines the caret jumps per PageUp / PageDown. */
#define YDOC_PAGE_STEP_LINES 10

/* Table cell geometry. */
#define YDOC_TABLE_CELL_HEIGHT 26.0f
#define YDOC_TABLE_CELL_PAD 5.0f

/* Left indent (px) applied to a list paragraph's text, leaving room for the
 * bullet/number marker drawn in the gutter. */
#define YDOC_LIST_INDENT 26.0f
/* One Increase/Decrease-indent step, in px. */
#define YDOC_INDENT_STEP 36.0f

static float paragraph_char_width(const struct yetty_yrich_paragraph *paragraph)
{
    return paragraph->style.font_size * 0.6f;
}

/* Font size at byte `index` — the covering run's size, else the base style. */
static float paragraph_font_size_at(const struct yetty_yrich_paragraph *paragraph, size_t index)
{
    for (size_t i = 0; i < paragraph->run_count; i++) {
        const struct yetty_yrich_text_run *run = &paragraph->runs[i];
        if ((size_t)run->start <= index && index < (size_t)run->end) {
            return run->style.font_size;
        }
    }
    return paragraph->style.font_size;
}

/* Pixel width of the byte range [from, to) using the paragraph's metrics font;
 * falls back to the fixed 0.6-em advance when no font is attached, so wrap,
 * caret, render and hit-test stay self-consistent either way. */
/* Advance width of [from, to), summed over maximal same-font-size slices so
 * mixed per-run sizes measure correctly. Every layout consumer (wrap, caret,
 * hit-test, render x) goes through here, so they all agree. */
static float paragraph_measure(const struct yetty_yrich_paragraph *paragraph, size_t from,
                               size_t to)
{
    if (to > paragraph->text_len) {
        to = paragraph->text_len;
    }
    if (to <= from) {
        return 0.0f;
    }
    int can_shape = paragraph->metrics_font && paragraph->metrics_font->ops &&
                    paragraph->metrics_font->ops->measure_text;
    float total = 0.0f;
    size_t slice = from;
    while (slice < to) {
        float font_size = paragraph_font_size_at(paragraph, slice);
        size_t slice_end = slice + 1;
        while (slice_end < to && paragraph_font_size_at(paragraph, slice_end) == font_size) {
            slice_end++;
        }
        int measured = 0;
        if (can_shape) {
            struct float_result width = paragraph->metrics_font->ops->measure_text(
                (struct yetty_yfont_font *)paragraph->metrics_font, paragraph->text + slice,
                slice_end - slice, font_size);
            if (!YETTY_IS_ERR(width)) {
                total += width.value;
                measured = 1;
            }
        }
        if (!measured) {
            total += (float)(slice_end - slice) * font_size * 0.6f;
        }
        slice = slice_end;
    }
    return total;
}

/* Compute the line starting at `start` — greedy break at the last space
 * that fits, hard break when a word exceeds the width, explicit '\n'
 * always ends a line. Width is measured in pixels (proportional). */
static void paragraph_line_from(const struct yetty_yrich_paragraph *paragraph, size_t start,
                                struct paragraph_line *out_line)
{
    float max_width = paragraph->bounds.w;
    size_t len = paragraph->text_len;
    const char *text = paragraph->text;
    size_t pos = start;
    size_t last_space = 0;
    int have_space = 0;

    while (pos < len) {
        if (text[pos] == '\n') {
            out_line->start = start;
            out_line->end = pos;
            out_line->next = pos + 1;
            return;
        }
        /* Break when the glyph at `pos` would overflow the box; always keep at
         * least one character per line to guarantee progress. */
        if (pos > start && paragraph_measure(paragraph, start, pos + 1) > max_width) {
            if (have_space && last_space > start) {
                out_line->start = start;
                out_line->end = last_space;
                out_line->next = last_space + 1;
            } else {
                out_line->start = start;
                out_line->end = pos;
                out_line->next = pos;
            }
            return;
        }
        if (text[pos] == ' ') {
            last_space = pos;
            have_space = 1;
        }
        pos++;
    }
    out_line->start = start;
    out_line->end = len;
    out_line->next = len;
}

/* Visit every line. The visitor returns non-zero to stop early. A trailing
 * separator ('\n' or a wrap-consumed space at the very end) yields one
 * final empty line so the caret can sit there. */
typedef int (*paragraph_line_visitor)(const struct paragraph_line *line, size_t line_index,
                                      void *userdata);

static void paragraph_for_each_line(const struct yetty_yrich_paragraph *paragraph,
                                    paragraph_line_visitor visit, void *userdata)
{
    size_t len = paragraph->text_len;
    size_t start = 0;
    size_t line_index = 0;
    for (;;) {
        struct paragraph_line line;
        paragraph_line_from(paragraph, start, &line);
        if (visit(&line, line_index, userdata)) {
            return;
        }
        line_index++;
        if (line.next >= len) {
            if (line.next > line.end && line.next == len) {
                struct paragraph_line tail = {len, len, len};
                visit(&tail, line_index, userdata);
            }
            return;
        }
        start = line.next;
    }
}

static int count_line_visitor(const struct paragraph_line *line, size_t line_index, void *userdata)
{
    (void)line;
    (void)line_index;
    size_t *count = userdata;
    (*count)++;
    return 0;
}

static size_t paragraph_line_count(const struct yetty_yrich_paragraph *paragraph)
{
    size_t count = 0;
    paragraph_for_each_line(paragraph, count_line_visitor, &count);
    return count ? count : 1;
}

static void paragraph_recompute_height(struct yetty_yrich_paragraph *paragraph)
{
    if (paragraph->block_kind == YDOC_BLOCK_DIVIDER) {
        paragraph->line_height = YDOC_DIVIDER_HEIGHT;
        paragraph->bounds.h = YDOC_DIVIDER_HEIGHT;
        return;
    }
    if (paragraph->block_kind == YDOC_BLOCK_PAGE_BREAK) {
        paragraph->line_height = YDOC_PAGE_BREAK_HEIGHT;
        paragraph->bounds.h = YDOC_PAGE_BREAK_HEIGHT;
        return;
    }
    if (paragraph->block_kind == YDOC_BLOCK_TABLE) {
        paragraph->line_height = YDOC_TABLE_CELL_HEIGHT;
        paragraph->bounds.h = (float)paragraph->table_rows * YDOC_TABLE_CELL_HEIGHT;
        return;
    }
    /* Line height fits the tallest run so a larger per-run size never overlaps
     * the line above. */
    float max_font_size = paragraph->style.font_size;
    for (size_t i = 0; i < paragraph->run_count; i++) {
        if (paragraph->runs[i].style.font_size > max_font_size) {
            max_font_size = paragraph->runs[i].style.font_size;
        }
    }
    paragraph->line_height = max_font_size * paragraph->line_spacing;
    paragraph->bounds.h = (float)paragraph_line_count(paragraph) * paragraph->line_height;
}

/* Caret index → (line, column). */
struct caret_place {
    size_t index;
    size_t line_index;
    size_t column;
    size_t line_start;
    size_t line_len;
    int found;
};

static int caret_place_visitor(const struct paragraph_line *line, size_t line_index, void *userdata)
{
    struct caret_place *place = userdata;
    /* A line owns the caret when it sits before the next line's start;
	 * the final line also owns the end-of-text position. */
    if (place->index < line->next || line->next >= line->end) {
        if (place->index >= line->start) {
            size_t column =
                place->index >= line->end ? line->end - line->start : place->index - line->start;
            place->line_index = line_index;
            place->column = column;
            place->line_start = line->start;
            place->line_len = line->end - line->start;
            place->found = 1;
            if (place->index < line->next) {
                return 1;
            }
        }
    }
    return 0;
}

static void paragraph_caret_place(const struct yetty_yrich_paragraph *paragraph, int32_t index,
                                  struct caret_place *out_place)
{
    memset(out_place, 0, sizeof(*out_place));
    out_place->index = index < 0 ? 0 : (size_t)index;
    paragraph_for_each_line(paragraph, caret_place_visitor, out_place);
    if (!out_place->found) {
        out_place->line_index = 0;
        out_place->column = 0;
    }
}

/* (line, column) → caret index, clamped to the line's content. */
struct caret_target {
    size_t line_index;
    size_t column;
    size_t result;
    size_t last_line_index;
    size_t last_line_start;
    size_t last_line_len;
    int found;
};

static int caret_target_visitor(const struct paragraph_line *line, size_t line_index,
                                void *userdata)
{
    struct caret_target *target = userdata;
    target->last_line_index = line_index;
    target->last_line_start = line->start;
    target->last_line_len = line->end - line->start;
    if (line_index == target->line_index) {
        size_t column = target->column;
        size_t line_len = line->end - line->start;
        if (column > line_len) {
            column = line_len;
        }
        target->result = line->start + column;
        target->found = 1;
        return 1;
    }
    return 0;
}

static int32_t paragraph_caret_at(const struct yetty_yrich_paragraph *paragraph, size_t line_index,
                                  size_t column)
{
    struct caret_target target = {0};
    target.line_index = line_index;
    target.column = column;
    paragraph_for_each_line(paragraph, caret_target_visitor, &target);
    if (!target.found) {
        /* Past the last line — clamp to the end of the final line. */
        size_t column_clamped = column > target.last_line_len ? target.last_line_len : column;
        return (int32_t)(target.last_line_start + column_clamped);
    }
    return (int32_t)target.result;
}

/* Fetch line number `line_index` (clamped to the last line). */
struct line_lookup {
    size_t line_index;
    struct paragraph_line line;
    size_t found_index;
};

static int line_lookup_visitor(const struct paragraph_line *line, size_t line_index, void *userdata)
{
    struct line_lookup *lookup = userdata;
    lookup->line = *line;
    lookup->found_index = line_index;
    return line_index >= lookup->line_index;
}

static void paragraph_line_at(const struct yetty_yrich_paragraph *paragraph, size_t line_index,
                              struct paragraph_line *out_line)
{
    struct line_lookup lookup = {0};
    lookup.line_index = line_index;
    paragraph_for_each_line(paragraph, line_lookup_visitor, &lookup);
    *out_line = lookup.line;
}

/* Per-line horizontal metrics. `x_offset` is the left offset of the line
 * inside the paragraph box (centre/right alignment); `space_extra` is the
 * extra advance added to every space glyph when the line is justified.
 * Render, caret placement, and hit-testing all derive x positions from the
 * same metrics so they agree. */
struct line_metrics {
    float x_offset;
    float space_extra;
};

static size_t paragraph_count_spaces(const struct yetty_yrich_paragraph *paragraph, size_t from,
                                     size_t to)
{
    size_t count = 0;
    for (size_t i = from; i < to && i < paragraph->text_len; i++) {
        if (paragraph->text[i] == ' ') {
            count++;
        }
    }
    return count;
}

static void paragraph_line_metrics(const struct yetty_yrich_paragraph *paragraph, size_t line_start,
                                   size_t line_end, struct line_metrics *out_metrics)
{
    out_metrics->x_offset = 0.0f;
    out_metrics->space_extra = 0.0f;
    float text_width = paragraph_measure(paragraph, line_start, line_end);
    float slack = paragraph->bounds.w - text_width;
    if (slack <= 0.0f) {
        return;
    }
    switch (paragraph->halign) {
    case YETTY_YRICH_HALIGN_CENTER:
        out_metrics->x_offset = slack * 0.5f;
        return;
    case YETTY_YRICH_HALIGN_RIGHT:
        out_metrics->x_offset = slack;
        return;
    case YETTY_YRICH_HALIGN_JUSTIFY: {
        /* Only soft-wrapped lines stretch: the line must have broken at a
         * space separator. The paragraph's final line and lines ended by an
         * explicit '\n' keep their natural width. */
        if (line_end >= paragraph->text_len || paragraph->text[line_end] != ' ') {
            return;
        }
        size_t space_count = paragraph_count_spaces(paragraph, line_start, line_end);
        if (space_count > 0) {
            out_metrics->space_extra = slack / (float)space_count;
        }
        return;
    }
    default:
        return;
    }
}

/* Advance width of the line prefix [line_start, upto) under `metrics` —
 * the natural measure plus the justify stretch of every space in it. */
static float paragraph_line_prefix_width(const struct yetty_yrich_paragraph *paragraph,
                                         size_t line_start, size_t upto,
                                         const struct line_metrics *metrics)
{
    float width = paragraph_measure(paragraph, line_start, upto);
    if (metrics->space_extra > 0.0f) {
        width += metrics->space_extra * (float)paragraph_count_spaces(paragraph, line_start, upto);
    }
    return width;
}

/*---------------------------------------------------------------------------
 * Attribute engine — per-character format/color resolved from the base
 * style plus the run list. Mutations decompress to a per-char array,
 * edit it, and recompress to runs; O(text_len), trivial at typing rates.
 *-------------------------------------------------------------------------*/

struct char_attrs {
    uint32_t format;
    uint32_t color;
    uint32_t bg_color; /* highlight; 0 = transparent */
    float font_size;   /* per-run size; base style size when no run covers it */
    uint32_t link_id;  /* hyperlink target id (0 = none) */
};

static int char_attrs_equal(struct char_attrs a, struct char_attrs b)
{
    return a.format == b.format && a.color == b.color && a.bg_color == b.bg_color &&
           a.font_size == b.font_size && a.link_id == b.link_id;
}

static struct char_attrs paragraph_base_attrs(const struct yetty_yrich_paragraph *paragraph)
{
    struct char_attrs attrs = {paragraph->style.format, paragraph->style.color,
                               paragraph->style.bg_color, paragraph->style.font_size, 0};
    return attrs;
}

static struct char_attrs paragraph_attrs_at(const struct yetty_yrich_paragraph *paragraph,
                                            size_t index)
{
    struct char_attrs attrs = paragraph_base_attrs(paragraph);
    for (size_t i = 0; i < paragraph->run_count; i++) {
        const struct yetty_yrich_text_run *run = &paragraph->runs[i];
        if ((size_t)run->start <= index && index < (size_t)run->end) {
            attrs.format = run->style.format;
            attrs.color = run->style.color;
            attrs.bg_color = run->style.bg_color;
            attrs.font_size = run->style.font_size;
            attrs.link_id = run->link_id;
            return attrs;
        }
    }
    return attrs;
}

static struct char_attrs *paragraph_attrs_decompress(const struct yetty_yrich_paragraph *paragraph,
                                                     size_t len)
{
    struct char_attrs *attrs = malloc((len ? len : 1) * sizeof(*attrs));
    if (!attrs) {
        return NULL;
    }
    for (size_t i = 0; i < len; i++) {
        attrs[i] = paragraph_attrs_at(paragraph, i);
    }
    return attrs;
}

/* Rebuild the run list from a per-char array: maximal spans whose attrs
 * differ from the base style become runs. */
static struct yetty_ycore_void_result paragraph_attrs_recompress(
    struct yetty_yrich_paragraph *paragraph, const struct char_attrs *attrs, size_t len)
{
    struct char_attrs base = paragraph_base_attrs(paragraph);
    size_t span_count = 0;
    size_t i = 0;
    while (i < len) {
        if (char_attrs_equal(attrs[i], base)) {
            i++;
            continue;
        }
        size_t span_end = i + 1;
        while (span_end < len && char_attrs_equal(attrs[span_end], attrs[i])) {
            span_end++;
        }
        span_count++;
        i = span_end;
    }

    if (span_count > paragraph->run_capacity) {
        struct yetty_yrich_text_run *new_runs =
            realloc(paragraph->runs, span_count * sizeof(*new_runs));
        if (!new_runs) {
            return YETTY_ERR(yetty_ycore_void, "paragraph runs: grow failed");
        }
        paragraph->runs = new_runs;
        paragraph->run_capacity = span_count;
    }

    size_t run_index = 0;
    i = 0;
    while (i < len) {
        if (char_attrs_equal(attrs[i], base)) {
            i++;
            continue;
        }
        size_t span_end = i + 1;
        while (span_end < len && char_attrs_equal(attrs[span_end], attrs[i])) {
            span_end++;
        }
        struct yetty_yrich_text_run *run = &paragraph->runs[run_index++];
        run->start = (int32_t)i;
        run->end = (int32_t)span_end;
        run->style = paragraph->style;
        run->style.format = attrs[i].format;
        run->style.color = attrs[i].color;
        run->style.bg_color = attrs[i].bg_color;
        run->style.font_size = attrs[i].font_size;
        run->link_id = attrs[i].link_id;
        i = span_end;
    }
    paragraph->run_count = run_index;
    return YETTY_OK_VOID();
}

/* Toggle `format_flag` across [lo, hi): if every char already carries the
 * flag, clear it; otherwise set it (the Google-docs toggle rule). */
/* Overwrite the format flags, colour, highlight, and font size of every char
 * in [lo,hi) with those of `style` (paint-format apply). */
static struct yetty_ycore_void_result paragraph_apply_style_range(
    struct yetty_yrich_paragraph *paragraph, size_t lo, size_t hi,
    const struct yetty_yrich_text_style *style)
{
    if (hi > paragraph->text_len) {
        hi = paragraph->text_len;
    }
    if (lo >= hi) {
        return YETTY_OK_VOID();
    }
    struct char_attrs *attrs = paragraph_attrs_decompress(paragraph, paragraph->text_len);
    if (!attrs) {
        return YETTY_ERR(yetty_ycore_void, "apply_style_range: attrs alloc failed");
    }
    for (size_t i = lo; i < hi; i++) {
        attrs[i].format = style->format;
        attrs[i].color = style->color;
        attrs[i].bg_color = style->bg_color;
        attrs[i].font_size = style->font_size;
    }
    struct yetty_ycore_void_result recompress_res =
        paragraph_attrs_recompress(paragraph, attrs, paragraph->text_len);
    free(attrs);
    return recompress_res;
}

/* Stamp `link_id` onto every char in [lo, hi) (0 clears the link). Runs split on
 * link boundaries so the linked span is maximal; rides the same decompress /
 * recompress path as every other per-char attribute, so text edits, split, and
 * merge shift links automatically. */
static struct yetty_ycore_void_result paragraph_apply_link_range(
    struct yetty_yrich_paragraph *paragraph, size_t lo, size_t hi, uint32_t link_id)
{
    if (hi > paragraph->text_len) {
        hi = paragraph->text_len;
    }
    if (lo >= hi) {
        return YETTY_OK_VOID();
    }
    struct char_attrs *attrs = paragraph_attrs_decompress(paragraph, paragraph->text_len);
    if (!attrs) {
        return YETTY_ERR(yetty_ycore_void, "apply_link_range: attrs alloc failed");
    }
    for (size_t i = lo; i < hi; i++) {
        attrs[i].link_id = link_id;
    }
    struct yetty_ycore_void_result recompress_res =
        paragraph_attrs_recompress(paragraph, attrs, paragraph->text_len);
    free(attrs);
    return recompress_res;
}

static struct yetty_ycore_void_result paragraph_apply_format_range(
    struct yetty_yrich_paragraph *paragraph, size_t lo, size_t hi, uint32_t format_flag)
{
    if (hi > paragraph->text_len) {
        hi = paragraph->text_len;
    }
    if (lo >= hi) {
        return YETTY_OK_VOID();
    }
    struct char_attrs *attrs = paragraph_attrs_decompress(paragraph, paragraph->text_len);
    if (!attrs) {
        return YETTY_ERR(yetty_ycore_void, "apply_format_range: attrs alloc failed");
    }
    int all_have = 1;
    for (size_t i = lo; i < hi; i++) {
        if (!(attrs[i].format & format_flag)) {
            all_have = 0;
            break;
        }
    }
    for (size_t i = lo; i < hi; i++) {
        if (all_have) {
            attrs[i].format &= ~format_flag;
        } else {
            attrs[i].format |= format_flag;
        }
    }
    struct yetty_ycore_void_result recompress_res =
        paragraph_attrs_recompress(paragraph, attrs, paragraph->text_len);
    free(attrs);
    return recompress_res;
}

static struct yetty_ycore_void_result paragraph_apply_color_range(
    struct yetty_yrich_paragraph *paragraph, size_t lo, size_t hi, uint32_t color)
{
    if (hi > paragraph->text_len) {
        hi = paragraph->text_len;
    }
    if (lo >= hi) {
        return YETTY_OK_VOID();
    }
    struct char_attrs *attrs = paragraph_attrs_decompress(paragraph, paragraph->text_len);
    if (!attrs) {
        return YETTY_ERR(yetty_ycore_void, "apply_color_range: attrs alloc failed");
    }
    for (size_t i = lo; i < hi; i++) {
        attrs[i].color = color;
    }
    struct yetty_ycore_void_result recompress_res =
        paragraph_attrs_recompress(paragraph, attrs, paragraph->text_len);
    free(attrs);
    return recompress_res;
}

/* Set the highlight (background) color across [lo, hi). 0 = clear highlight. */
static struct yetty_ycore_void_result paragraph_apply_bgcolor_range(
    struct yetty_yrich_paragraph *paragraph, size_t lo, size_t hi, uint32_t bg_color)
{
    if (hi > paragraph->text_len) {
        hi = paragraph->text_len;
    }
    if (lo >= hi) {
        return YETTY_OK_VOID();
    }
    struct char_attrs *attrs = paragraph_attrs_decompress(paragraph, paragraph->text_len);
    if (!attrs) {
        return YETTY_ERR(yetty_ycore_void, "apply_bgcolor_range: attrs alloc failed");
    }
    for (size_t i = lo; i < hi; i++) {
        attrs[i].bg_color = bg_color;
    }
    struct yetty_ycore_void_result recompress_res =
        paragraph_attrs_recompress(paragraph, attrs, paragraph->text_len);
    free(attrs);
    return recompress_res;
}

static float clamp_font_size(float font_size)
{
    if (font_size < 6.0f) {
        return 6.0f;
    }
    if (font_size > 96.0f) {
        return 96.0f;
    }
    return font_size;
}

/* Adjust the per-character font size across [lo, hi) by `delta` (absolute==0) or
 * set it to `absolute` (delta ignored), clamped to [6, 96]. Enables mixed sizes
 * within a paragraph. */
static struct yetty_ycore_void_result paragraph_apply_fontsize_range(
    struct yetty_yrich_paragraph *paragraph, size_t lo, size_t hi, float delta, float absolute)
{
    if (hi > paragraph->text_len) {
        hi = paragraph->text_len;
    }
    if (lo >= hi) {
        return YETTY_OK_VOID();
    }
    struct char_attrs *attrs = paragraph_attrs_decompress(paragraph, paragraph->text_len);
    if (!attrs) {
        return YETTY_ERR(yetty_ycore_void, "apply_fontsize_range: attrs alloc failed");
    }
    for (size_t i = lo; i < hi; i++) {
        attrs[i].font_size =
            clamp_font_size(absolute > 0.0f ? absolute : attrs[i].font_size + delta);
    }
    struct yetty_ycore_void_result recompress_res =
        paragraph_attrs_recompress(paragraph, attrs, paragraph->text_len);
    free(attrs);
    return recompress_res;
}

/* Clear character formatting across [lo, hi): drop all format flags, reset text
 * color to the default, and remove any highlight — reverting the range to plain
 * body text (Google Docs "clear formatting"). */
static struct yetty_ycore_void_result paragraph_clear_format_range(
    struct yetty_yrich_paragraph *paragraph, size_t lo, size_t hi)
{
    if (hi > paragraph->text_len) {
        hi = paragraph->text_len;
    }
    if (lo >= hi) {
        return YETTY_OK_VOID();
    }
    struct char_attrs *attrs = paragraph_attrs_decompress(paragraph, paragraph->text_len);
    if (!attrs) {
        return YETTY_ERR(yetty_ycore_void, "clear_format_range: attrs alloc failed");
    }
    for (size_t i = lo; i < hi; i++) {
        attrs[i].format = YETTY_YRICH_FMT_NONE;
        attrs[i].color = YETTY_YRICH_COLOR_BLACK;
        attrs[i].bg_color = YETTY_YRICH_COLOR_TRANSPARENT;
    }
    struct yetty_ycore_void_result recompress_res =
        paragraph_attrs_recompress(paragraph, attrs, paragraph->text_len);
    free(attrs);
    return recompress_res;
}

/* Run-list maintenance across text edits: inserted chars inherit the
 * style of the character to their left (base style at offset 0). */
static struct yetty_ycore_void_result paragraph_attrs_on_insert(
    struct yetty_yrich_paragraph *paragraph, size_t old_len, size_t position, size_t count)
{
    if (paragraph->run_count == 0) {
        return YETTY_OK_VOID(); /* uniform paragraph stays uniform */
    }
    struct char_attrs *old_attrs = paragraph_attrs_decompress(paragraph, old_len);
    if (!old_attrs) {
        return YETTY_ERR(yetty_ycore_void, "attrs_on_insert: alloc failed");
    }
    size_t new_len = old_len + count;
    struct char_attrs *new_attrs = malloc((new_len ? new_len : 1) * sizeof(*new_attrs));
    if (!new_attrs) {
        free(old_attrs);
        return YETTY_ERR(yetty_ycore_void, "attrs_on_insert: alloc failed");
    }
    struct char_attrs inherited =
        position > 0 ? old_attrs[position - 1] : paragraph_base_attrs(paragraph);
    for (size_t i = 0; i < position; i++) {
        new_attrs[i] = old_attrs[i];
    }
    for (size_t i = 0; i < count; i++) {
        new_attrs[position + i] = inherited;
    }
    for (size_t i = position; i < old_len; i++) {
        new_attrs[i + count] = old_attrs[i];
    }
    free(old_attrs);
    struct yetty_ycore_void_result recompress_res =
        paragraph_attrs_recompress(paragraph, new_attrs, new_len);
    free(new_attrs);
    return recompress_res;
}

static struct yetty_ycore_void_result paragraph_attrs_on_delete(
    struct yetty_yrich_paragraph *paragraph, size_t old_len, size_t position, size_t count)
{
    if (paragraph->run_count == 0) {
        return YETTY_OK_VOID();
    }
    struct char_attrs *old_attrs = paragraph_attrs_decompress(paragraph, old_len);
    if (!old_attrs) {
        return YETTY_ERR(yetty_ycore_void, "attrs_on_delete: alloc failed");
    }
    size_t new_len = old_len - count;
    for (size_t i = position; i < new_len; i++) {
        old_attrs[i] = old_attrs[i + count];
    }
    struct yetty_ycore_void_result recompress_res =
        paragraph_attrs_recompress(paragraph, old_attrs, new_len);
    free(old_attrs);
    return recompress_res;
}

/* Document-space point → caret index inside the paragraph. */
static int32_t paragraph_caret_from_point(const struct yetty_yrich_paragraph *paragraph, float x,
                                          float y)
{
    float line_height = paragraph->line_height > 0.0f ? paragraph->line_height : 1.0f;
    float relative_y = y - paragraph->bounds.y;
    size_t line_count = paragraph_line_count(paragraph);
    long line_index = (long)(relative_y / line_height);
    if (line_index < 0) {
        line_index = 0;
    }
    if ((size_t)line_index >= line_count) {
        line_index = (long)line_count - 1;
    }
    struct paragraph_line line;
    paragraph_line_at(paragraph, (size_t)line_index, &line);
    struct line_metrics metrics;
    paragraph_line_metrics(paragraph, line.start, line.end, &metrics);
    float relative_x = x - paragraph->bounds.x - metrics.x_offset;
    if (relative_x <= 0.0f) {
        return paragraph_caret_at(paragraph, (size_t)line_index, 0);
    }
    /* Walk the line's glyphs, choosing the boundary nearest the click. */
    size_t column = 0;
    for (size_t byte = line.start; byte < line.end; byte++) {
        float left = paragraph_line_prefix_width(paragraph, line.start, byte, &metrics);
        float right = paragraph_line_prefix_width(paragraph, line.start, byte + 1, &metrics);
        if (relative_x < (left + right) * 0.5f) {
            break;
        }
        column = byte + 1 - line.start;
    }
    return paragraph_caret_at(paragraph, (size_t)line_index, column);
}

YETTY_ANNOTATE("override@yrich:paragraph:constructor")
static struct yetty_ycore_void_result paragraph_constructor(struct yetty_yclass_object *obj)
{
    struct yetty_ycore_void_result super_res =
        yetty_yrich_super_void(obj, yetty_yrich_paragraph_class_get().value,
                               (yetty_yclass_method_id_t)yetty_yrich_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, super_res, "paragraph_constructor: super");
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_constructor: data_get");
    struct yetty_yrich_paragraph *paragraph = data_res.value;
    paragraph->bounds.h = YDOC_DEFAULT_LINE_HEIGHT;
    paragraph->style = yetty_yrich_text_style_default();
    paragraph->line_spacing = YDOC_DEFAULT_LINE_SPACING;
    paragraph->line_height = YDOC_DEFAULT_LINE_HEIGHT;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yrich:paragraph:element_destroy")
static struct yetty_ycore_void_result paragraph_destroy(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_destroy: data_get");
    free(data_res.value->text);
    free(data_res.value->runs);
    free(data_res.value->bookmark);
    if (data_res.value->table_cells) {
        uint32_t cells = data_res.value->table_rows * data_res.value->table_cols;
        for (uint32_t i = 0; i < cells; i++) {
            free(data_res.value->table_cells[i]);
        }
        free(data_res.value->table_cells);
    }
    return yetty_yrich_super_void(obj, yetty_yrich_paragraph_class_get().value,
                                  (yetty_yclass_method_id_t)yetty_yrich_element_destroy);
}

YETTY_ANNOTATE("override@yrich:paragraph:element_bounds")
static struct yetty_ycore_void_result paragraph_bounds(struct yetty_yclass_object *obj,
                                                       struct yetty_yrich_rect *out_bounds)
{
    if (!out_bounds) {
        return YETTY_ERR(yetty_ycore_void, "paragraph_bounds: NULL out_bounds");
    }
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_bounds: data_get");
    *out_bounds = data_res.value->bounds;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yrich:paragraph:element_is_editable")
static struct yetty_ycore_int_result paragraph_is_editable(struct yetty_yclass_object *obj)
{
    (void)obj;
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("override@yrich:paragraph:element_begin_edit")
static struct yetty_ycore_void_result paragraph_begin_edit(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_begin_edit: data_get");
    struct yetty_yrich_paragraph *paragraph = data_res.value;
    paragraph->editing = 1;
    paragraph->cursor_pos = (int32_t)paragraph->text_len;
    paragraph->sel_start = 0;
    paragraph->sel_end = (int32_t)paragraph->text_len;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yrich:paragraph:element_end_edit")
static struct yetty_ycore_void_result paragraph_end_edit(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_end_edit: data_get");
    struct yetty_yrich_paragraph *paragraph = data_res.value;
    paragraph->editing = 0;
    paragraph->sel_start = paragraph->sel_end = paragraph->cursor_pos;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yrich:paragraph:element_is_editing")
static struct yetty_ycore_int_result paragraph_is_editing(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "paragraph_is_editing: data_get");
    return YETTY_OK(yetty_ycore_int, data_res.value->editing);
}

/* A URL may begin only after a non-alphanumeric character (or at the start). */
static int ydoc_url_boundary_char(char code)
{
    return !((code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z') ||
             (code >= '0' && code <= '9'));
}

/* Length of the URL token starting at byte `at` (bounded by `limit`), or 0 if
 * none. Matches http://, https://, www.; extends to the next space. */
static size_t paragraph_url_at(const struct yetty_yrich_paragraph *paragraph, size_t at,
                               size_t limit)
{
    if (at >= limit) {
        return 0;
    }
    if (at > 0 && !ydoc_url_boundary_char(paragraph->text[at - 1])) {
        return 0;
    }
    const char *text = paragraph->text + at;
    size_t remaining = limit - at;
    int is_url = 0;
    if (remaining >= 8 && memcmp(text, "https://", 8) == 0) {
        is_url = 1;
    } else if (remaining >= 7 && memcmp(text, "http://", 7) == 0) {
        is_url = 1;
    } else if (remaining >= 4 && memcmp(text, "www.", 4) == 0) {
        is_url = 1;
    }
    if (!is_url) {
        return 0;
    }
    size_t end = at;
    while (end < limit && paragraph->text[end] != ' ') {
        end++;
    }
    return end - at;
}

/* Per-line render pass shared state. Layers: layer+0 selection boxes,
 * layer+1 text (twice for bold), layer+2 underline/strike, layer+3 caret. */
struct paragraph_render_pass {
    const struct yetty_yrich_paragraph *paragraph;
    struct yetty_ydraw_drawable_list *drawable_list;
    uint32_t layer;
    struct yetty_ycore_void_result status;
};

/* Bound to the fixed `paragraph_line_visitor` typedef signature, so it cannot
 * return a Result. The Result-returning ydraw calls inside have nowhere to
 * propagate to; their errors are absorbed into pass->status and surfaced at the
 * paragraph_for_each_line caller. */
YETTY_EXTERNAL_CALLBACK
static int paragraph_render_line_visitor(const struct paragraph_line *line, size_t line_index,
                                         void *userdata)
{
    struct paragraph_render_pass *pass = userdata;
    const struct yetty_yrich_paragraph *paragraph = pass->paragraph;
    float line_top = paragraph->bounds.y + (float)line_index * paragraph->line_height;
    float baseline = line_top + paragraph->style.font_size;
    struct line_metrics metrics;
    paragraph_line_metrics(paragraph, line->start, line->end, &metrics);
    float line_left = paragraph->bounds.x + metrics.x_offset;

    /* Selection wash behind the text — for the editing paragraph's own range,
     * or any paragraph caught in a document-wide selection. */
    if ((paragraph->editing || paragraph->selected) && paragraph->sel_start != paragraph->sel_end) {
        int32_t selection_lo =
            paragraph->sel_start < paragraph->sel_end ? paragraph->sel_start : paragraph->sel_end;
        int32_t selection_hi =
            paragraph->sel_start < paragraph->sel_end ? paragraph->sel_end : paragraph->sel_start;
        size_t overlap_lo = (size_t)selection_lo > line->start ? (size_t)selection_lo : line->start;
        size_t overlap_hi = (size_t)selection_hi < line->end ? (size_t)selection_hi : line->end;
        if (overlap_lo < overlap_hi) {
            float x0 = line_left +
                       paragraph_line_prefix_width(paragraph, line->start, overlap_lo, &metrics);
            float x1 = line_left +
                       paragraph_line_prefix_width(paragraph, line->start, overlap_hi, &metrics);
            struct yetty_ysdf_box wash = {
                .center_x = (x0 + x1) * 0.5f,
                .center_y = line_top + paragraph->line_height * 0.5f,
                .half_width = (x1 - x0) * 0.5f,
                .half_height = paragraph->line_height * 0.5f,
                .corner_radius = 0.0f,
            };
            struct yetty_ycore_void_result wash_res = yetty_ydraw_drawable_list_add_cmd_add_box(
                pass->drawable_list, 0, pass->layer, YETTY_YRICH_RGBA(120, 170, 255, 110), 0, 0.0f,
                &wash);
            if (YETTY_IS_ERR(wash_res)) {
                pass->status =
                    YETTY_ERR(yetty_ycore_void, "paragraph_render: selection wash", wash_res);
                return 1;
            }
        }
    }

    /* Text — one draw per maximal same-style segment of the line. */
    size_t segment_start = line->start;
    while (segment_start < line->end) {
        struct char_attrs attrs = paragraph_attrs_at(paragraph, segment_start);
        /* An explicit hyperlink paints in the brand link colour with an
         * underline (segments already break on link_id, so this is uniform
         * across the segment). */
        if (attrs.link_id != 0) {
            attrs.color = YETTY_YRICH_RGBA(60, 120, 220, 255);
            attrs.format |= YETTY_YRICH_FMT_UNDERLINE;
        }
        int segment_is_space = paragraph->text[segment_start] == ' ';
        size_t segment_end = segment_start + 1;
        while (segment_end < line->end &&
               char_attrs_equal(paragraph_attrs_at(paragraph, segment_end), attrs)) {
            /* On a justified line every space is stretched, so a single
             * add_text must not span a space — split segments at each
             * space/non-space boundary and position them individually. */
            if (metrics.space_extra > 0.0f &&
                (paragraph->text[segment_end] == ' ') != segment_is_space) {
                break;
            }
            segment_end++;
        }
        size_t segment_len = segment_end - segment_start;
        float segment_x = line_left + paragraph_line_prefix_width(paragraph, line->start,
                                                                  segment_start, &metrics);
        float segment_end_x =
            line_left + paragraph_line_prefix_width(paragraph, line->start, segment_end, &metrics);

        /* Highlight (background) wash, behind the glyphs. */
        if (attrs.bg_color != YETTY_YRICH_COLOR_TRANSPARENT) {
            struct yetty_ysdf_box highlight = {
                .center_x = (segment_x + segment_end_x) * 0.5f,
                .center_y = line_top + paragraph->line_height * 0.5f,
                .half_width = (segment_end_x - segment_x) * 0.5f,
                .half_height = paragraph->line_height * 0.5f,
                .corner_radius = 0.0f,
            };
            struct yetty_ycore_void_result highlight_res =
                yetty_ydraw_drawable_list_add_cmd_add_box(pass->drawable_list, 0, pass->layer,
                                                          attrs.bg_color, 0, 0.0f, &highlight);
            if (YETTY_IS_ERR(highlight_res)) {
                pass->status =
                    YETTY_ERR(yetty_ycore_void, "paragraph_render: highlight", highlight_res);
                return 1;
            }
        }

        struct yetty_ycore_buffer text = {
            .data = (uint8_t *)(paragraph->text + segment_start),
            .size = segment_len,
            .capacity = segment_len,
        };
        /* Super/subscript: smaller glyphs on a raised/lowered baseline. The
         * advance is left at the base metric (the run keeps its column width),
         * which is close enough without a full metrics pass. */
        float segment_font_size = attrs.font_size;
        float segment_baseline = baseline;
        if (attrs.format & YETTY_YRICH_FMT_SUPERSCRIPT) {
            segment_baseline = baseline - segment_font_size * 0.34f;
            segment_font_size *= 0.72f;
        } else if (attrs.format & YETTY_YRICH_FMT_SUBSCRIPT) {
            segment_baseline = baseline + segment_font_size * 0.16f;
            segment_font_size *= 0.72f;
        }
        /* Pick the styled face for this segment. Bold/italic map to font slots
         * 1/2/3 when the host registered them (styled_font_mask); otherwise fall
         * back to the base face and, for bold, a synthetic sub-pixel re-draw.
         * Styled slots only apply to the base (slot-0) face — a custom per-run
         * family (non-zero base font_id) is left untouched. */
        int want_bold = (attrs.format & YETTY_YRICH_FMT_BOLD) != 0;
        int want_italic = (attrs.format & YETTY_YRICH_FMT_ITALIC) != 0;
        int32_t segment_font_id = paragraph->style.font_id;
        int synthetic_bold = want_bold;
        if (paragraph->style.font_id == YDOC_FONT_SLOT_REGULAR && (want_bold || want_italic)) {
            uint32_t mask = paragraph->styled_font_mask;
            if (want_bold && want_italic && (mask & YDOC_STYLED_BOLD_ITALIC)) {
                segment_font_id = YDOC_FONT_SLOT_BOLD_ITALIC;
                synthetic_bold = 0;
            } else if (want_bold && !want_italic && (mask & YDOC_STYLED_BOLD)) {
                segment_font_id = YDOC_FONT_SLOT_BOLD;
                synthetic_bold = 0;
            } else if (want_italic && !want_bold && (mask & YDOC_STYLED_ITALIC)) {
                segment_font_id = YDOC_FONT_SLOT_ITALIC;
            } else if (want_bold && want_italic && (mask & YDOC_STYLED_ITALIC)) {
                /* No combined face: use the italic face and keep synthetic bold. */
                segment_font_id = YDOC_FONT_SLOT_ITALIC;
            } else if (want_bold && want_italic && (mask & YDOC_STYLED_BOLD)) {
                /* No italic face: use the bold face (loses the slant). */
                segment_font_id = YDOC_FONT_SLOT_BOLD;
                synthetic_bold = 0;
            }
        }
        struct yetty_ycore_void_result text_res = yetty_ydraw_drawable_list_add_text(
            pass->drawable_list, segment_x, segment_baseline, &text, segment_font_size, attrs.color,
            pass->layer + 1, segment_font_id, 0.0f);
        if (YETTY_IS_ERR(text_res)) {
            pass->status = YETTY_ERR(yetty_ycore_void, "paragraph_render: add_text", text_res);
            return 1;
        }
        if (synthetic_bold) {
            /* Poor man's bold — re-draw with a sub-pixel x offset. */
            struct yetty_ycore_void_result bold_res = yetty_ydraw_drawable_list_add_text(
                pass->drawable_list, segment_x + 0.6f, segment_baseline, &text, segment_font_size,
                attrs.color, pass->layer + 1, segment_font_id, 0.0f);
            if (YETTY_IS_ERR(bold_res)) {
                pass->status = YETTY_ERR(yetty_ycore_void, "paragraph_render: bold pass", bold_res);
                return 1;
            }
        }
        if (attrs.format & YETTY_YRICH_FMT_UNDERLINE) {
            struct yetty_ysdf_segment underline = {
                .start_x = segment_x,
                .start_y = baseline + 2.0f,
                .end_x = segment_end_x,
                .end_y = baseline + 2.0f,
            };
            struct yetty_ycore_void_result deco_res = yetty_ydraw_drawable_list_add_cmd_add_segment(
                pass->drawable_list, 0, pass->layer + 2, 0, attrs.color, 1.0f, &underline);
            if (YETTY_IS_ERR(deco_res)) {
                pass->status = YETTY_ERR(yetty_ycore_void, "paragraph_render: underline", deco_res);
                return 1;
            }
        }
        /* A checked checklist item renders struck through, matching the
         * word-processor convention; the model text style is untouched. */
        if ((attrs.format & YETTY_YRICH_FMT_STRIKE) ||
            (paragraph->list_kind == YDOC_LIST_CHECK && paragraph->list_checked)) {
            struct yetty_ysdf_segment strike = {
                .start_x = segment_x,
                .start_y = baseline - paragraph->style.font_size * 0.32f,
                .end_x = segment_end_x,
                .end_y = baseline - paragraph->style.font_size * 0.32f,
            };
            struct yetty_ycore_void_result deco_res = yetty_ydraw_drawable_list_add_cmd_add_segment(
                pass->drawable_list, 0, pass->layer + 2, 0, attrs.color, 1.0f, &strike);
            if (YETTY_IS_ERR(deco_res)) {
                pass->status = YETTY_ERR(yetty_ycore_void, "paragraph_render: strike", deco_res);
                return 1;
            }
        }
        segment_start = segment_end;
    }

    /* Auto-linked URLs: overdraw any http(s):// or www. token on the line in
     * the brand accent colour with an underline. Render-time styling (the link
     * target is the token text); an editable link store is a follow-up. */
    for (size_t byte = line->start; byte < line->end;) {
        size_t url_len = paragraph_url_at(paragraph, byte, line->end);
        if (url_len == 0) {
            byte++;
            continue;
        }
        size_t url_end = byte + url_len;
        uint32_t link_color = YETTY_YRICH_RGBA(60, 120, 220, 255);
        float url_x =
            line_left + paragraph_line_prefix_width(paragraph, line->start, byte, &metrics);
        float url_end_x =
            line_left + paragraph_line_prefix_width(paragraph, line->start, url_end, &metrics);
        struct yetty_ycore_buffer url_text = {
            .data = (uint8_t *)(paragraph->text + byte),
            .size = url_len,
            .capacity = url_len,
        };
        struct yetty_ycore_void_result url_res = yetty_ydraw_drawable_list_add_text(
            pass->drawable_list, url_x, baseline, &url_text, paragraph->style.font_size, link_color,
            pass->layer + 2, paragraph->style.font_id, 0.0f);
        if (YETTY_IS_ERR(url_res)) {
            pass->status = YETTY_ERR(yetty_ycore_void, "paragraph_render: link text", url_res);
            return 1;
        }
        struct yetty_ysdf_segment underline = {
            .start_x = url_x,
            .start_y = baseline + 2.0f,
            .end_x = url_end_x,
            .end_y = baseline + 2.0f,
        };
        struct yetty_ycore_void_result deco_res = yetty_ydraw_drawable_list_add_cmd_add_segment(
            pass->drawable_list, 0, pass->layer + 2, 0, link_color, 1.0f, &underline);
        if (YETTY_IS_ERR(deco_res)) {
            pass->status =
                YETTY_ERR(yetty_ycore_void, "paragraph_render: link underline", deco_res);
            return 1;
        }
        byte = url_end;
    }

    /* Nonprinting marks (SDF, font-independent): a filled dot centred in every
     * space cell, and a small filled bar after the paragraph's final line to
     * stand in for the pilcrow / paragraph end. */
    if (paragraph->show_nonprinting) {
        uint32_t mark_color = YETTY_YRICH_RGBA(150, 150, 150, 200);
        float mid_y = line_top + paragraph->line_height * 0.5f;
        for (size_t byte = line->start; byte < line->end; byte++) {
            if (paragraph->text[byte] != ' ') {
                continue;
            }
            float left =
                line_left + paragraph_line_prefix_width(paragraph, line->start, byte, &metrics);
            float right =
                line_left + paragraph_line_prefix_width(paragraph, line->start, byte + 1, &metrics);
            struct yetty_ysdf_box dot = {
                .center_x = (left + right) * 0.5f,
                .center_y = mid_y,
                .half_width = 1.4f,
                .half_height = 1.4f,
                .corner_radius = 1.4f,
            };
            struct yetty_ycore_void_result dot_res = yetty_ydraw_drawable_list_add_cmd_add_box(
                pass->drawable_list, 0, pass->layer + 2, mark_color, 0, 0.0f, &dot);
            if (YETTY_IS_ERR(dot_res)) {
                pass->status = YETTY_ERR(yetty_ycore_void, "paragraph_render: space dot", dot_res);
                return 1;
            }
        }
        int is_last_line = line->next >= line->end;
        if (is_last_line) {
            float end_x = line_left +
                          paragraph_line_prefix_width(paragraph, line->start, line->end, &metrics);
            struct yetty_ysdf_box bar = {
                .center_x = end_x + 3.0f,
                .center_y = mid_y,
                .half_width = 1.2f,
                .half_height = paragraph->line_height * 0.32f,
                .corner_radius = 0.5f,
            };
            struct yetty_ycore_void_result bar_res = yetty_ydraw_drawable_list_add_cmd_add_box(
                pass->drawable_list, 0, pass->layer + 2, mark_color, 0, 0.0f, &bar);
            if (YETTY_IS_ERR(bar_res)) {
                pass->status = YETTY_ERR(yetty_ycore_void, "paragraph_render: para end", bar_res);
                return 1;
            }
        }
    }
    return 0;
}

YETTY_ANNOTATE("override@yrich:paragraph:element_render")
static struct yetty_ycore_void_result paragraph_render(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *drawable_list,
    uint32_t layer, int selected)
{
    (void)selected; /* element-set selection unused — text selection paints */
    if (!drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "paragraph_render: NULL drawable list");
    }
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_render: data_get");
    struct yetty_yrich_paragraph *paragraph = data_res.value;

    /* Horizontal rule: a single line across the content width at the box's
     * vertical middle. No text, marker, selection wash, or caret. */
    if (paragraph->block_kind == YDOC_BLOCK_DIVIDER) {
        float rule_y = paragraph->bounds.y + paragraph->bounds.h * 0.5f;
        struct yetty_ysdf_segment rule = {
            .start_x = paragraph->bounds.x,
            .start_y = rule_y,
            .end_x = paragraph->bounds.x + paragraph->bounds.w,
            .end_y = rule_y,
        };
        return yetty_ydraw_drawable_list_add_cmd_add_segment(
            drawable_list, 0, layer + 1, 0, YETTY_YRICH_RGBA(160, 160, 160, 255), 1.0f, &rule);
    }

    /* Page break: a dashed line across the box's middle (a marker; real
     * pagination lands in Phase 3). */
    if (paragraph->block_kind == YDOC_BLOCK_PAGE_BREAK) {
        float mid_y = paragraph->bounds.y + paragraph->bounds.h * 0.5f;
        uint32_t dash_color = YETTY_YRICH_RGBA(120, 140, 180, 255);
        float dash = 10.0f;
        float gap = 6.0f;
        for (float x = paragraph->bounds.x; x < paragraph->bounds.x + paragraph->bounds.w;
             x += dash + gap) {
            float end = x + dash;
            if (end > paragraph->bounds.x + paragraph->bounds.w) {
                end = paragraph->bounds.x + paragraph->bounds.w;
            }
            struct yetty_ysdf_segment seg = {
                .start_x = x, .start_y = mid_y, .end_x = end, .end_y = mid_y};
            struct yetty_ycore_void_result seg_res = yetty_ydraw_drawable_list_add_cmd_add_segment(
                drawable_list, 0, layer + 1, 0, dash_color, 1.0f, &seg);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, seg_res, "paragraph_render: page break");
        }
        return YETTY_OK_VOID();
    }

    /* Table: a grid of cells with borders and per-cell text. */
    if (paragraph->block_kind == YDOC_BLOCK_TABLE && paragraph->table_rows > 0 &&
        paragraph->table_cols > 0) {
        uint32_t border_color = YETTY_YRICH_RGBA(120, 130, 130, 255);
        float cell_w = paragraph->bounds.w / (float)paragraph->table_cols;
        float cell_h = YDOC_TABLE_CELL_HEIGHT;
        float origin_x = paragraph->bounds.x;
        float origin_y = paragraph->bounds.y;
        float table_w = cell_w * (float)paragraph->table_cols;
        float table_h = cell_h * (float)paragraph->table_rows;

        /* Active-cell highlight behind the grid. */
        if (paragraph->table_active_cell >= 0 &&
            (uint32_t)paragraph->table_active_cell <
                paragraph->table_rows * paragraph->table_cols) {
            uint32_t active = (uint32_t)paragraph->table_active_cell;
            uint32_t row = active / paragraph->table_cols;
            uint32_t col = active % paragraph->table_cols;
            struct yetty_ysdf_box wash = {
                .center_x = origin_x + ((float)col + 0.5f) * cell_w,
                .center_y = origin_y + ((float)row + 0.5f) * cell_h,
                .half_width = cell_w * 0.5f,
                .half_height = cell_h * 0.5f,
                .corner_radius = 0.0f,
            };
            struct yetty_ycore_void_result wash_res = yetty_ydraw_drawable_list_add_cmd_add_box(
                drawable_list, 0, layer, YETTY_YRICH_RGBA(120, 170, 255, 60), 0, 0.0f, &wash);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, wash_res, "paragraph_render: table active");
        }

        /* Grid lines. */
        for (uint32_t r = 0; r <= paragraph->table_rows; r++) {
            struct yetty_ysdf_segment line = {
                .start_x = origin_x,
                .start_y = origin_y + (float)r * cell_h,
                .end_x = origin_x + table_w,
                .end_y = origin_y + (float)r * cell_h,
            };
            struct yetty_ycore_void_result line_res = yetty_ydraw_drawable_list_add_cmd_add_segment(
                drawable_list, 0, layer + 1, 0, border_color, 1.0f, &line);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, line_res, "paragraph_render: table row line");
        }
        for (uint32_t c = 0; c <= paragraph->table_cols; c++) {
            struct yetty_ysdf_segment line = {
                .start_x = origin_x + (float)c * cell_w,
                .start_y = origin_y,
                .end_x = origin_x + (float)c * cell_w,
                .end_y = origin_y + table_h,
            };
            struct yetty_ycore_void_result line_res = yetty_ydraw_drawable_list_add_cmd_add_segment(
                drawable_list, 0, layer + 1, 0, border_color, 1.0f, &line);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, line_res, "paragraph_render: table col line");
        }

        /* Cell text. */
        for (uint32_t r = 0; r < paragraph->table_rows; r++) {
            for (uint32_t c = 0; c < paragraph->table_cols; c++) {
                const char *cell = paragraph->table_cells
                                       ? paragraph->table_cells[r * paragraph->table_cols + c]
                                       : NULL;
                if (!cell || cell[0] == '\0') {
                    continue;
                }
                struct yetty_ycore_buffer text = {
                    .data = (uint8_t *)cell,
                    .size = strlen(cell),
                    .capacity = strlen(cell),
                };
                float text_x = origin_x + (float)c * cell_w + YDOC_TABLE_CELL_PAD;
                float text_baseline = origin_y + (float)r * cell_h + cell_h * 0.5f +
                                      paragraph->style.font_size * 0.35f;
                struct yetty_ycore_void_result text_res = yetty_ydraw_drawable_list_add_text(
                    drawable_list, text_x, text_baseline, &text, paragraph->style.font_size,
                    paragraph->style.color, layer + 2, paragraph->style.font_id, 0.0f);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "paragraph_render: table cell");
            }
        }
        return YETTY_OK_VOID();
    }

    /* Checklist marker in the gutter — an SDF checkbox (outlined; a tick is
     * drawn inside when checked), so no glyph coverage is required. */
    if (paragraph->list_kind == YDOC_LIST_CHECK) {
        float box_half = paragraph->style.font_size * 0.32f;
        float box_center_x = paragraph->bounds.x - YDOC_LIST_INDENT + 6.0f + box_half;
        float box_center_y = paragraph->bounds.y + paragraph->style.font_size * 0.55f;
        struct yetty_ysdf_box checkbox = {
            .center_x = box_center_x,
            .center_y = box_center_y,
            .half_width = box_half,
            .half_height = box_half,
            .corner_radius = 2.0f,
        };
        struct yetty_ycore_void_result box_res = yetty_ydraw_drawable_list_add_cmd_add_box(
            drawable_list, 0, layer + 1, YETTY_YRICH_COLOR_TRANSPARENT, paragraph->style.color,
            1.5f, &checkbox);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, box_res, "paragraph_render: checkbox");
        if (paragraph->list_checked) {
            /* Tick: short down-stroke then long up-stroke. */
            struct yetty_ysdf_segment tick_down = {
                .start_x = box_center_x - box_half * 0.55f,
                .start_y = box_center_y + box_half * 0.05f,
                .end_x = box_center_x - box_half * 0.15f,
                .end_y = box_center_y + box_half * 0.5f,
            };
            struct yetty_ysdf_segment tick_up = {
                .start_x = box_center_x - box_half * 0.15f,
                .start_y = box_center_y + box_half * 0.5f,
                .end_x = box_center_x + box_half * 0.6f,
                .end_y = box_center_y - box_half * 0.45f,
            };
            struct yetty_ycore_void_result tick_res = yetty_ydraw_drawable_list_add_cmd_add_segment(
                drawable_list, 0, layer + 2, 0, paragraph->style.color, 1.5f, &tick_down);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, tick_res, "paragraph_render: tick down");
            tick_res = yetty_ydraw_drawable_list_add_cmd_add_segment(
                drawable_list, 0, layer + 2, 0, paragraph->style.color, 1.5f, &tick_up);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, tick_res, "paragraph_render: tick up");
        }
    } else if (paragraph->list_kind != YDOC_LIST_NONE) {
        /* Bullet / number marker in the gutter. */
        char marker[16];
        size_t marker_len;
        if (paragraph->list_kind == YDOC_LIST_NUMBERED) {
            int written = snprintf(marker, sizeof(marker), "%d.", paragraph->list_ordinal);
            marker_len = written > 0 ? (size_t)written : 0;
        } else {
            /* Bullet glyph cycles by nesting depth, like a word processor:
             * • (filled) → ◦ (hollow) → ▪ (square). */
            marker[0] = (char)0xE2;
            switch (paragraph->list_level % 3) {
            case 1:
                marker[1] = (char)0x97;
                marker[2] = (char)0xA6; /* U+25E6 WHITE BULLET */
                break;
            case 2:
                marker[1] = (char)0x96;
                marker[2] = (char)0xAA; /* U+25AA BLACK SMALL SQUARE */
                break;
            default:
                marker[1] = (char)0x80;
                marker[2] = (char)0xA2; /* U+2022 BULLET */
                break;
            }
            marker_len = 3;
        }
        if (marker_len > 0) {
            struct yetty_ycore_buffer marker_buf = {
                .data = (uint8_t *)marker,
                .size = marker_len,
                .capacity = sizeof(marker),
            };
            float marker_x = paragraph->bounds.x - YDOC_LIST_INDENT + 6.0f;
            float marker_baseline = paragraph->bounds.y + paragraph->style.font_size;
            struct yetty_ycore_void_result marker_res = yetty_ydraw_drawable_list_add_text(
                drawable_list, marker_x, marker_baseline, &marker_buf, paragraph->style.font_size,
                paragraph->style.color, layer + 1, paragraph->style.font_id, 0.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, marker_res, "paragraph_render: list marker");
        }
    }

    struct paragraph_render_pass pass = {
        .paragraph = paragraph,
        .drawable_list = drawable_list,
        .layer = layer,
        .status = YETTY_OK_VOID(),
    };
    paragraph_for_each_line(paragraph, paragraph_render_line_visitor, &pass);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pass.status, "paragraph_render: line pass");

    /* Caret. */
    if (paragraph->editing) {
        struct caret_place place;
        paragraph_caret_place(paragraph, paragraph->cursor_pos, &place);
        struct line_metrics caret_metrics;
        paragraph_line_metrics(paragraph, place.line_start, place.line_start + place.line_len,
                               &caret_metrics);
        float caret_x =
            paragraph->bounds.x + caret_metrics.x_offset +
            paragraph_line_prefix_width(paragraph, place.line_start,
                                        place.line_start + place.column, &caret_metrics);
        float caret_top = paragraph->bounds.y + (float)place.line_index * paragraph->line_height;
        struct yetty_ysdf_segment caret = {
            .start_x = caret_x,
            .start_y = caret_top + 1.0f,
            .end_x = caret_x,
            .end_y = caret_top + paragraph->line_height - 1.0f,
        };
        struct yetty_ycore_void_result caret_res = yetty_ydraw_drawable_list_add_cmd_add_segment(
            drawable_list, 0, layer + 3, 0, YETTY_YRICH_RGBA(20, 90, 220, 255), 1.5f, &caret);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, caret_res, "paragraph_render: caret");
    }
    return YETTY_OK_VOID();
}

/* Raw text mutators — shared by the element-level edit slots and the
 * undoable TEXT_INSERT / TEXT_DELETE operation application. */
static struct yetty_ycore_void_result paragraph_text_insert_at(
    struct yetty_yrich_paragraph *paragraph, int32_t position, const char *text, size_t len)
{
    if (!text || len == 0) {
        return YETTY_OK_VOID();
    }
    int32_t pos = position;
    if (pos < 0) {
        pos = 0;
    }
    if ((size_t)pos > paragraph->text_len) {
        pos = (int32_t)paragraph->text_len;
    }
    size_t new_len = paragraph->text_len + len;
    char *new_buf = realloc(paragraph->text, new_len + 1);
    if (!new_buf) {
        return YETTY_ERR(yetty_ycore_void, "paragraph text insert: grow failed");
    }
    memmove(new_buf + pos + len, new_buf + pos, paragraph->text_len - (size_t)pos);
    memcpy(new_buf + pos, text, len);
    new_buf[new_len] = '\0';
    size_t old_len = paragraph->text_len;
    paragraph->text = new_buf;
    paragraph->text_len = new_len;
    paragraph->cursor_pos = pos + (int32_t)len;
    paragraph->sel_start = paragraph->sel_end = paragraph->cursor_pos;
    struct yetty_ycore_void_result attrs_res =
        paragraph_attrs_on_insert(paragraph, old_len, (size_t)pos, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, attrs_res, "paragraph text insert: runs");
    paragraph_recompute_height(paragraph);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result paragraph_text_delete_range(
    struct yetty_yrich_paragraph *paragraph, int32_t position, int32_t length)
{
    if (length <= 0 || paragraph->text_len == 0) {
        return YETTY_OK_VOID();
    }
    int32_t pos = position;
    if (pos < 0) {
        pos = 0;
    }
    if ((size_t)pos >= paragraph->text_len) {
        return YETTY_OK_VOID();
    }
    size_t avail = paragraph->text_len - (size_t)pos;
    size_t count = (size_t)length < avail ? (size_t)length : avail;
    memmove(paragraph->text + pos, paragraph->text + pos + count,
            paragraph->text_len - (size_t)pos - count);
    size_t old_len = paragraph->text_len;
    paragraph->text_len -= count;
    paragraph->text[paragraph->text_len] = '\0';
    paragraph->cursor_pos = pos;
    paragraph->sel_start = paragraph->sel_end = pos;
    struct yetty_ycore_void_result attrs_res =
        paragraph_attrs_on_delete(paragraph, old_len, (size_t)pos, count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, attrs_res, "paragraph text delete: runs");
    paragraph_recompute_height(paragraph);
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yrich:paragraph:element_insert_text")
static struct yetty_ycore_void_result paragraph_insert_text(struct yetty_yclass_object *obj,
                                                            struct yetty_ycore_buffer text)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_insert_text: data_get");
    return paragraph_text_insert_at(data_res.value, data_res.value->cursor_pos,
                                    (const char *)text.data, text.size);
}

YETTY_ANNOTATE("override@yrich:paragraph:element_delete_sel")
static struct yetty_ycore_void_result paragraph_delete_sel(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_delete_sel: data_get");
    struct yetty_yrich_paragraph *paragraph = data_res.value;
    if (paragraph->cursor_pos > 0) {
        return paragraph_text_delete_range(paragraph, paragraph->cursor_pos - 1, 1);
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_text(struct yetty_yclass_object *obj,
                                                              const char *text, size_t len)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_text: data_get");
    struct yetty_yrich_paragraph *paragraph = data_res.value;
    char *buf = malloc(len + 1);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "paragraph_set_text: text alloc failed");
    }
    if (len > 0) {
        memcpy(buf, text, len);
    }
    buf[len] = '\0';
    free(paragraph->text);
    paragraph->text = buf;
    paragraph->text_len = len;
    paragraph->cursor_pos = (int32_t)len;
    paragraph->run_count = 0; /* full replacement resets range styling */
    paragraph_recompute_height(paragraph);
    return YETTY_OK_VOID();
}

/* Style setters — the data slice is private to this TU; the yaml loader and
 * future bindings reach the style through these. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_font_size(struct yetty_yclass_object *obj,
                                                                   float font_size)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_font_size: data_get");
    struct yetty_yrich_paragraph *paragraph = data_res.value;
    if (font_size > 0.0f) {
        paragraph->style.font_size = font_size;
        paragraph->line_height = font_size * paragraph->line_spacing;
        paragraph_recompute_height(paragraph);
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_color(struct yetty_yclass_object *obj,
                                                               uint32_t color)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_color: data_get");
    data_res.value->style.color = color;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_format(struct yetty_yclass_object *obj,
                                                                uint32_t format)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_format: data_get");
    data_res.value->style.format = format;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * InlineImage
 *=========================================================================*/

struct YETTY_ANNOTATE("class@yrich:inline_image") YETTY_ANNOTATE("parent@yrich:element")
    yetty_yrich_inline_image {
    struct yetty_yrich_rect bounds;
    char *source;   /* owned base64/path, may be NULL */
    char *alt_text; /* owned, may be NULL */
    char *caption;  /* owned, may be NULL */
    uint32_t align; /* enum yetty_yrich_halign */
};

YETTY_ANNOTATE("override@yrich:inline_image:constructor")
static struct yetty_ycore_void_result inline_image_constructor(struct yetty_yclass_object *obj)
{
    struct yetty_ycore_void_result super_res =
        yetty_yrich_super_void(obj, yetty_yrich_inline_image_class_get().value,
                               (yetty_yclass_method_id_t)yetty_yrich_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, super_res, "inline_image_constructor: super");
    struct yetty_yrich_inline_image_ptr_result data_res = yetty_yrich_inline_image_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "inline_image_constructor: data_get");
    data_res.value->align = YETTY_YRICH_HALIGN_CENTER;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yrich:inline_image:element_destroy")
static struct yetty_ycore_void_result inline_image_destroy(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_inline_image_ptr_result data_res = yetty_yrich_inline_image_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "inline_image_destroy: data_get");
    free(data_res.value->source);
    free(data_res.value->alt_text);
    free(data_res.value->caption);
    return yetty_yrich_super_void(obj, yetty_yrich_inline_image_class_get().value,
                                  (yetty_yclass_method_id_t)yetty_yrich_element_destroy);
}

YETTY_ANNOTATE("override@yrich:inline_image:element_bounds")
static struct yetty_ycore_void_result inline_image_bounds(struct yetty_yclass_object *obj,
                                                          struct yetty_yrich_rect *out_bounds)
{
    if (!out_bounds) {
        return YETTY_ERR(yetty_ycore_void, "inline_image_bounds: NULL out_bounds");
    }
    struct yetty_yrich_inline_image_ptr_result data_res = yetty_yrich_inline_image_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "inline_image_bounds: data_get");
    *out_bounds = data_res.value->bounds;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yrich:inline_image:element_render")
static struct yetty_ycore_void_result inline_image_render(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *drawable_list,
    uint32_t layer, int selected)
{
    if (!drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "inline_image_render: NULL drawable list");
    }
    struct yetty_yrich_inline_image_ptr_result data_res = yetty_yrich_inline_image_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "inline_image_render: data_get");
    struct yetty_yrich_inline_image *image = data_res.value;

    /* Placeholder until image atlasing lands — render a stroked box where
	 * the image would appear. */
    struct yetty_ysdf_box body = {
        .center_x = image->bounds.x + image->bounds.w * 0.5f,
        .center_y = image->bounds.y + image->bounds.h * 0.5f,
        .half_width = image->bounds.w * 0.5f,
        .half_height = image->bounds.h * 0.5f,
        .corner_radius = 4.0f,
    };
    uint32_t border =
        selected ? YETTY_YRICH_RGBA(0, 100, 200, 255) : YETTY_YRICH_RGBA(150, 150, 150, 255);
    struct yetty_ycore_void_result body_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        drawable_list, 0, layer, YETTY_YRICH_RGBA(245, 245, 245, 255), border, 1.0f, &body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "inline_image_render: body box add failed");

    if (image->caption) {
        size_t caption_len = strlen(image->caption);
        struct yetty_ycore_buffer text = {
            .data = (uint8_t *)image->caption,
            .size = caption_len,
            .capacity = caption_len,
        };
        struct yetty_ycore_void_result caption_res = yetty_ydraw_drawable_list_add_text(
            drawable_list, image->bounds.x, image->bounds.y + image->bounds.h + 14.0f, &text, 12.0f,
            YETTY_YRICH_COLOR_BLACK, layer + 1, 0, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, caption_res,
                            "inline_image_render: caption add_text failed");
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Document
 *=========================================================================*/

struct YETTY_ANNOTATE("class@yrich:ydoc") YETTY_ANNOTATE("include@yetty/yrich/yrich-types.h")
    YETTY_ANNOTATE("parent@yrich:document") yetty_yrich_ydoc {
    float page_width;
    float margin;

    /* Paragraph order — these alias the element list, never owning. */
    struct yetty_yclass_object **paragraphs;
    size_t paragraph_count;
    size_t paragraph_capacity;

    /* Inline images — also alias. */
    struct yetty_yclass_object **images;
    size_t image_count;
    size_t image_capacity;

    /* Where File > Save writes — owned, may be NULL (untitled). */
    char *source_path;

    /* Hyperlink table: paragraph runs reference a link by id; this maps the id
     * to its owned URL string. Ids are runtime-only (never serialized — YAML
     * denormalizes the URL onto each run and re-interns on load). Interning
     * dedups identical URLs. Unreferenced entries are harmless until the doc
     * is destroyed. */
    struct ydoc_link_entry {
        uint32_t id;
        char *url;
    } *links;
    size_t link_count;
    size_t link_capacity;
    uint32_t next_link_id;

    /* Metrics-only font propagated to paragraphs for proportional layout. */
    const struct yetty_yfont_font *metrics_font;

    /* Document-wide selection (Ctrl+A). Cleared by any caret placement/edit. */
    int select_all;

    /* View preference: show nonprinting characters (space middots + pilcrows).
     * Not persisted (a view setting, not document content). */
    int show_nonprinting;

    /* Styled-face availability bitmask (see the paragraph field). Set by the
     * host once it has registered the bold/italic/bold-italic faces on the
     * render ygrid; mirrored to paragraphs during relayout. 0 = Regular only. */
    uint32_t styled_font_mask;

    /* Paint-format clipboard: a captured character style applied to the next
     * selection. Not persisted. */
    struct yetty_yrich_text_style paint_style;
    int paint_has;

    /* Cross-paragraph text selection (drag / shift-click spanning
     * paragraphs), projected onto per-paragraph washes during relayout.
     * lo/hi are alias indices in DOCUMENT order (lo < hi); the selection
     * covers paragraph lo from lo_off to its end, every paragraph strictly
     * between fully, and paragraph hi from 0 to hi_off. The anchor/focus
     * orientation lives in the document selection object; these fields are
     * derived render/apply state. Cleared by any single-paragraph caret. */
    int sel_span_active;
    size_t sel_span_lo;
    size_t sel_span_hi;
    int32_t sel_span_lo_off;
    int32_t sel_span_hi_off;

    /* Word-granularity selection (double-click, then drag). While armed, a
     * drag snaps the focus end to whole-word boundaries and keeps the whole
     * anchor word selected — exactly the "double-click, then drag by word"
     * gesture every editor has. The anchor is a fixed [lo,hi] byte extent in
     * paragraph sel_word_anchor_index. Cleared by any fresh single click. */
    int sel_word_drag;
    size_t sel_word_anchor_index;
    int32_t sel_word_anchor_lo;
    int32_t sel_word_anchor_hi;
};

YETTY_ANNOTATE("override@yrich:ydoc:constructor")
static struct yetty_ycore_void_result ydoc_constructor(struct yetty_yclass_object *obj)
{
    struct yetty_ycore_void_result super_res = yetty_yrich_super_void(
        obj, yetty_yrich_ydoc_class_get().value, (yetty_yclass_method_id_t)yetty_yrich_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, super_res, "ydoc_constructor: super");
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_constructor: data_get");
    data_res.value->page_width = YDOC_DEFAULT_PAGE_WIDTH;
    data_res.value->margin = YDOC_DEFAULT_MARGIN;
    data_res.value->next_link_id = 1; /* 0 reserved for "no link" */
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yrich:ydoc:document_destroy")
static struct yetty_ycore_void_result ydoc_destroy(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_destroy: data_get");
    free(data_res.value->paragraphs);
    free(data_res.value->images);
    free(data_res.value->source_path);
    for (size_t i = 0; i < data_res.value->link_count; i++) {
        free(data_res.value->links[i].url);
    }
    free(data_res.value->links);
    /* The document base frees the elements and the object itself. */
    return yetty_yrich_super_void(obj, yetty_yrich_ydoc_class_get().value,
                                  (yetty_yclass_method_id_t)yetty_yrich_document_destroy);
}

/* Return the id for `url` (length `url_len`), reusing an existing entry with the
 * same URL or allocating a fresh one. Returns 0 on empty input or allocation
 * failure (0 == "no link"). */
static uint32_t ydoc_link_intern(struct yetty_yrich_ydoc *ydoc, const char *url, size_t url_len)
{
    if (!url || url_len == 0) {
        return 0;
    }
    for (size_t i = 0; i < ydoc->link_count; i++) {
        if (strlen(ydoc->links[i].url) == url_len &&
            memcmp(ydoc->links[i].url, url, url_len) == 0) {
            return ydoc->links[i].id;
        }
    }
    if (ydoc->link_count == ydoc->link_capacity) {
        size_t new_capacity = ydoc->link_capacity ? ydoc->link_capacity * 2 : 4;
        struct ydoc_link_entry *grown = realloc(ydoc->links, new_capacity * sizeof(*grown));
        if (!grown) {
            return 0;
        }
        ydoc->links = grown;
        ydoc->link_capacity = new_capacity;
    }
    char *copy = malloc(url_len + 1);
    if (!copy) {
        return 0;
    }
    memcpy(copy, url, url_len);
    copy[url_len] = '\0';
    struct ydoc_link_entry *entry = &ydoc->links[ydoc->link_count++];
    entry->id = ydoc->next_link_id++;
    entry->url = copy;
    return entry->id;
}

/* URL string for `link_id`, or NULL if the id is 0 or unknown. */
static const char *ydoc_link_url(const struct yetty_yrich_ydoc *ydoc, uint32_t link_id)
{
    if (link_id == 0) {
        return NULL;
    }
    for (size_t i = 0; i < ydoc->link_count; i++) {
        if (ydoc->links[i].id == link_id) {
            return ydoc->links[i].url;
        }
    }
    return NULL;
}

YETTY_ANNOTATE("override@yrich:ydoc:document_content_width")
static struct yetty_ycore_float_result ydoc_content_width(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "ydoc_content_width: data_get");
    return YETTY_OK(yetty_ycore_float, data_res.value->page_width);
}

YETTY_ANNOTATE("override@yrich:ydoc:document_content_height")
static struct yetty_ycore_float_result ydoc_content_height(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "ydoc_content_height: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;
    float height = ydoc->margin;
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, paragraph_res, "ydoc_content_height: paragraph");
        height += paragraph_res.value->bounds.h;
    }
    height += ydoc->margin;
    return YETTY_OK(yetty_ycore_float, height);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_set_page_width(struct yetty_yclass_object *obj,
                                                               float width)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_page_width: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;
    ydoc->page_width = width;
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, paragraph_res, "ydoc_set_page_width: paragraph");
        paragraph_res.value->bounds.w = width - 2.0f * ydoc->margin;
    }
    return yetty_yrich_document_mark_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_set_margin(struct yetty_yclass_object *obj,
                                                           float margin)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_margin: data_get");
    data_res.value->margin = margin;
    return yetty_yrich_document_mark_dirty(obj);
}

static struct yetty_ycore_void_result paragraph_list_push(struct yetty_yrich_ydoc *ydoc,
                                                          struct yetty_yclass_object *paragraph_obj)
{
    if (ydoc->paragraph_count == ydoc->paragraph_capacity) {
        size_t new_cap = ydoc->paragraph_capacity ? ydoc->paragraph_capacity * 2 : 8;
        struct yetty_yclass_object **new_arr =
            realloc(ydoc->paragraphs, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return YETTY_ERR(yetty_ycore_void, "ydoc: paragraph alias array grow failed");
        }
        ydoc->paragraphs = new_arr;
        ydoc->paragraph_capacity = new_cap;
    }
    ydoc->paragraphs[ydoc->paragraph_count++] = paragraph_obj;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_add_paragraph(
    struct yetty_yclass_object *obj, const char *text, size_t text_len)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, data_res, "ydoc_add_paragraph: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;

    float y = ydoc->margin;
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        struct yetty_yrich_paragraph_ptr_result existing_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, existing_res, "ydoc_add_paragraph: paragraph");
        y += existing_res.value->bounds.h;
    }
    float content_width = ydoc->page_width - 2.0f * ydoc->margin;

    struct yetty_yclass_object_ptr_result create_res = yetty_yrich_paragraph_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, create_res, "ydoc_add_paragraph: create");
    struct yetty_yclass_object *paragraph_obj = create_res.value;
    struct yetty_yrich_paragraph_ptr_result paragraph_res =
        yetty_yrich_paragraph_from(paragraph_obj);
    if (YETTY_IS_ERR(paragraph_res)) {
        struct yetty_ycore_void_result destroy_res = yetty_yrich_element_destroy(paragraph_obj);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_add_paragraph: data_get", paragraph_res);
    }
    struct yetty_yrich_paragraph *paragraph = paragraph_res.value;
    paragraph->bounds.x = ydoc->margin;
    paragraph->bounds.y = y;
    paragraph->bounds.w = content_width;

    struct yetty_yrich_element_id_result id_res = yetty_yrich_document_next_id(obj);
    if (YETTY_IS_OK(id_res)) {
        struct yetty_ycore_void_result set_id_res =
            yetty_yrich_element_set_id(paragraph_obj, id_res.value);
        if (YETTY_IS_ERR(set_id_res)) {
            yetty_ycore_error_destroy(set_id_res.error);
        }
    } else {
        yetty_ycore_error_destroy(id_res.error);
    }

    if (text && text_len > 0) {
        struct yetty_ycore_void_result text_res =
            yetty_yrich_paragraph_set_text(paragraph_obj, text, text_len);
        if (YETTY_IS_ERR(text_res)) {
            struct yetty_ycore_void_result destroy_res = yetty_yrich_element_destroy(paragraph_obj);
            if (YETTY_IS_ERR(destroy_res)) {
                yetty_ycore_error_destroy(destroy_res.error);
            }
            return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_add_paragraph: set_text", text_res);
        }
    }

    struct yetty_ycore_void_result add_res = yetty_yrich_document_add_element(obj, paragraph_obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, add_res, "ydoc_add_paragraph: add_element");

    struct yetty_ycore_void_result push_res = paragraph_list_push(ydoc, paragraph_obj);
    if (YETTY_IS_ERR(push_res)) {
        /* The element list owns the paragraph; an out-of-sync alias array
		 * would corrupt the layout, so unwind and surface the failure. */
        struct yetty_yrich_element_id_result unwind_id_res =
            yetty_yrich_element_id_value(paragraph_obj);
        if (YETTY_IS_OK(unwind_id_res)) {
            struct yetty_ycore_void_result remove_res =
                yetty_yrich_document_remove_element(obj, unwind_id_res.value);
            if (YETTY_IS_ERR(remove_res)) {
                yetty_ycore_error_destroy(remove_res.error);
            }
        } else {
            yetty_ycore_error_destroy(unwind_id_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_add_paragraph: alias push", push_res);
    }
    return YETTY_OK(yetty_yclass_object_ptr, paragraph_obj);
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_paragraph_at(struct yetty_yclass_object *obj,
                                                                    int32_t index)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, data_res, "ydoc_paragraph_at: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;
    if (index < 0 || (size_t)index >= ydoc->paragraph_count) {
        /* Out-of-range is a successful "no such paragraph" query result. */
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    return YETTY_OK(yetty_yclass_object_ptr, ydoc->paragraphs[index]);
}

static struct yetty_ycore_void_result image_list_push(struct yetty_yrich_ydoc *ydoc,
                                                      struct yetty_yclass_object *image_obj)
{
    if (ydoc->image_count == ydoc->image_capacity) {
        size_t new_cap = ydoc->image_capacity ? ydoc->image_capacity * 2 : 4;
        struct yetty_yclass_object **new_arr = realloc(ydoc->images, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return YETTY_ERR(yetty_ycore_void, "ydoc: image alias array grow failed");
        }
        ydoc->images = new_arr;
        ydoc->image_capacity = new_cap;
    }
    ydoc->images[ydoc->image_count++] = image_obj;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_insert_image(struct yetty_yclass_object *obj,
                                                                    int32_t paragraph_index,
                                                                    float width, float height)
{
    (void)paragraph_index; /* future: anchor to paragraph */
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, data_res, "ydoc_insert_image: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;

    float y = ydoc->margin;
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, paragraph_res, "ydoc_insert_image: paragraph");
        y += paragraph_res.value->bounds.h;
    }

    struct yetty_yclass_object_ptr_result create_res = yetty_yrich_inline_image_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, create_res, "ydoc_insert_image: create");
    struct yetty_yclass_object *image_obj = create_res.value;
    struct yetty_yrich_inline_image_ptr_result image_res = yetty_yrich_inline_image_from(image_obj);
    if (YETTY_IS_ERR(image_res)) {
        struct yetty_ycore_void_result destroy_res = yetty_yrich_element_destroy(image_obj);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_insert_image: data_get", image_res);
    }
    struct yetty_yrich_inline_image *image = image_res.value;
    image->bounds.x = ydoc->margin;
    image->bounds.y = y;
    image->bounds.w = width;
    image->bounds.h = height;

    struct yetty_yrich_element_id_result id_res = yetty_yrich_document_next_id(obj);
    if (YETTY_IS_OK(id_res)) {
        struct yetty_ycore_void_result set_id_res =
            yetty_yrich_element_set_id(image_obj, id_res.value);
        if (YETTY_IS_ERR(set_id_res)) {
            yetty_ycore_error_destroy(set_id_res.error);
        }
    } else {
        yetty_ycore_error_destroy(id_res.error);
    }

    struct yetty_ycore_void_result add_res = yetty_yrich_document_add_element(obj, image_obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, add_res, "ydoc_insert_image: add_element");

    struct yetty_ycore_void_result push_res = image_list_push(ydoc, image_obj);
    if (YETTY_IS_ERR(push_res)) {
        struct yetty_yrich_element_id_result unwind_id_res =
            yetty_yrich_element_id_value(image_obj);
        if (YETTY_IS_OK(unwind_id_res)) {
            struct yetty_ycore_void_result remove_res =
                yetty_yrich_document_remove_element(obj, unwind_id_res.value);
            if (YETTY_IS_ERR(remove_res)) {
                yetty_ycore_error_destroy(remove_res.error);
            }
        } else {
            yetty_ycore_error_destroy(unwind_id_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ydoc_insert_image: alias push", push_res);
    }
    return YETTY_OK(yetty_yclass_object_ptr, image_obj);
}

/*===========================================================================
 * Interactive editing.
 *
 * The caret/selection model is the document base's text selection: the
 * active paragraph is the SEL_TEXT element, `start` is the selection
 * anchor and `end` is the caret. The paragraph mirrors that state in its
 * own editing/cursor/sel fields so rendering stays element-local.
 * Character inserts and deletes go through TEXT_INSERT / TEXT_DELETE
 * operations wrapped in op-commands, so Ctrl+Z / Ctrl+Y work; structural
 * edits (paragraph split / merge) apply directly.
 *=========================================================================*/

static struct yetty_ycore_void_result ydoc_relayout(struct yetty_yrich_ydoc *ydoc)
{
    float content_width = ydoc->page_width - 2.0f * ydoc->margin;
    if (content_width < 8.0f) {
        content_width = 8.0f;
    }
    float y = ydoc->margin;
    /* Per-nesting-level ordinal counters for numbered lists. A numbered item at
     * level L takes the next ordinal at L and resets every deeper level; any
     * non-numbered paragraph breaks the run and clears all counters. */
    enum { YDOC_LIST_MAX_LEVEL = 7 };
    int level_ordinals[YDOC_LIST_MAX_LEVEL + 1] = {0};
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, paragraph_res, "ydoc_relayout: paragraph");
        struct yetty_yrich_paragraph *paragraph = paragraph_res.value;
        paragraph->metrics_font = ydoc->metrics_font;
        paragraph->show_nonprinting = ydoc->show_nonprinting;
        paragraph->styled_font_mask = ydoc->styled_font_mask;
        uint32_t level = paragraph->list_level;
        if (level > YDOC_LIST_MAX_LEVEL) {
            level = YDOC_LIST_MAX_LEVEL;
        }
        if (paragraph->list_kind == YDOC_LIST_NUMBERED) {
            level_ordinals[level]++;
            for (uint32_t deeper = level + 1; deeper <= YDOC_LIST_MAX_LEVEL; deeper++) {
                level_ordinals[deeper] = 0;
            }
            paragraph->list_ordinal = level_ordinals[level];
        } else {
            for (uint32_t reset = 0; reset <= YDOC_LIST_MAX_LEVEL; reset++) {
                level_ordinals[reset] = 0;
            }
            paragraph->list_ordinal = 0;
        }
        float list_indent =
            paragraph->list_kind != YDOC_LIST_NONE ? YDOC_LIST_INDENT * (float)(level + 1) : 0.0f;
        float indent = list_indent + paragraph->indent;
        y += paragraph->space_before;
        /* Project a document-wide or cross-paragraph selection onto each
         * paragraph so the existing per-paragraph wash paints it. */
        if (ydoc->select_all) {
            paragraph->selected = 1;
            paragraph->sel_start = 0;
            paragraph->sel_end = (int32_t)paragraph->text_len;
        } else if (ydoc->sel_span_active && i >= ydoc->sel_span_lo && i <= ydoc->sel_span_hi) {
            paragraph->selected = 1;
            paragraph->sel_start = i == ydoc->sel_span_lo ? ydoc->sel_span_lo_off : 0;
            paragraph->sel_end =
                i == ydoc->sel_span_hi ? ydoc->sel_span_hi_off : (int32_t)paragraph->text_len;
        } else {
            paragraph->selected = 0;
        }
        paragraph->bounds.x = ydoc->margin + indent;
        paragraph->bounds.w = content_width - indent;
        paragraph->bounds.y = y;
        paragraph_recompute_height(paragraph);
        y += paragraph->bounds.h;
        y += paragraph->space_after;
    }
    return YETTY_OK_VOID();
}

/* Attach a metrics-only font used for proportional layout (wrap, caret, render,
 * hit-test). Passing NULL reverts to the fixed 0.6-em fallback. Exported;
 * the app wires it from its loaded font after building the editor. */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_metrics_font(
    struct yetty_yclass_object *obj, const struct yetty_yfont_font *font)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_metrics_font: data_get");
    data_res.value->metrics_font = font;
    return ydoc_relayout(data_res.value);
}

/* Declare which styled faces the render ygrid has registered, as a bitmask
 * (bit 0 = bold at font slot 1, bit 1 = italic at slot 2, bit 2 = bold-italic at
 * slot 3). The host calls this after wiring the styled fonts so ydoc renders
 * bold/italic runs with the real face instead of the synthetic-bold fallback. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_set_styled_font_mask(
    struct yetty_yclass_object *obj, uint32_t mask)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_styled_font_mask: data_get");
    data_res.value->styled_font_mask = mask;
    return ydoc_relayout(data_res.value);
}

/* Toggle the space-middot / paragraph-pilcrow marks (a view preference, not
 * document content). Exposed; wired to Format > Show nonprinting characters. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_toggle_nonprinting(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_toggle_nonprinting: data_get");
    data_res.value->show_nonprinting = !data_res.value->show_nonprinting;
    struct yetty_ycore_void_result relayout_res = ydoc_relayout(data_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc_toggle_nonprinting: relayout");
    return yetty_yrich_document_mark_dirty(obj);
}

/* Select the whole document (Ctrl+A). Formatting ops then apply to every
 * paragraph; cleared by any caret placement. Exported for the key handler +
 * tests. */
struct yetty_ycore_void_result yetty_yrich_ydoc_select_all(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_select_all: data_get");
    data_res.value->select_all = 1;
    return ydoc_relayout(data_res.value);
}

/* The paragraph holding the caret, resolved from the document selection. */
struct ydoc_active_paragraph {
    struct yetty_yclass_object *paragraph_obj;
    struct yetty_yrich_paragraph *paragraph;
    size_t alias_index;
    yetty_yrich_element_id id;
    int32_t anchor;
    int32_t caret;
};

static int ydoc_alias_index_of(const struct yetty_yrich_ydoc *ydoc,
                               const struct yetty_yclass_object *paragraph_obj, size_t *out_index)
{
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        if (ydoc->paragraphs[i] == paragraph_obj) {
            *out_index = i;
            return 1;
        }
    }
    return 0;
}

/* Resolves the caret-holding paragraph. The Result's value is the found flag
 * (1 = active paragraph populated, 0 = no text selection / not found); a real
 * downstream failure propagates as an error. */
static struct yetty_ycore_int_result ydoc_active_paragraph_get(
    struct yetty_yclass_object *obj, struct yetty_yrich_ydoc *ydoc,
    struct ydoc_active_paragraph *out_active)
{
    struct yetty_yrich_selection_ptr_result selection_res = yetty_yrich_document_selection(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, selection_res, "ydoc_active_paragraph_get: selection");
    struct yetty_yrich_selection *selection = selection_res.value;
    if (!selection || selection->kind != YETTY_YRICH_SEL_TEXT) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    /* For a cross-paragraph span the caret-holding paragraph is the FOCUS
     * end; single-paragraph callers then see a collapsed caret there (the
     * anchor offset belongs to a different paragraph and would be
     * meaningless here). Span-aware operations read ydoc->sel_span_*. */
    int spans = selection->u.text.focus_element_id != selection->u.text.element_id;
    yetty_yrich_element_id active_id =
        spans ? selection->u.text.focus_element_id : selection->u.text.element_id;
    struct yetty_yclass_object_ptr_result paragraph_obj_res =
        yetty_yrich_document_find(obj, active_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, paragraph_obj_res, "ydoc_active_paragraph_get: find");
    struct yetty_yclass_object *paragraph_obj = paragraph_obj_res.value;
    if (!paragraph_obj) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_yrich_paragraph_ptr_result paragraph_res =
        yetty_yrich_paragraph_from(paragraph_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, paragraph_res, "ydoc_active_paragraph_get: data_get");
    out_active->paragraph_obj = paragraph_obj;
    out_active->paragraph = paragraph_res.value;
    out_active->id = active_id;
    out_active->anchor = spans ? selection->u.text.end : selection->u.text.start;
    out_active->caret = selection->u.text.end;
    if (!ydoc_alias_index_of(ydoc, paragraph_obj, &out_active->alias_index)) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

static int32_t clamp_caret(const struct yetty_yrich_paragraph *paragraph, int32_t index)
{
    if (index < 0) {
        return 0;
    }
    if ((size_t)index > paragraph->text_len) {
        return (int32_t)paragraph->text_len;
    }
    return index;
}

/* Put the caret (anchor..caret) into `paragraph_obj`, mirroring the state
 * into the paragraph and the document selection, and end the edit on a
 * previously active paragraph if it differs. */
static struct yetty_ycore_void_result ydoc_set_caret(struct yetty_yclass_object *obj,
                                                     struct yetty_yclass_object *paragraph_obj,
                                                     int32_t anchor, int32_t caret)
{
    /* Placing a caret collapses any document-wide or cross-paragraph
     * selection; a still-projected span must also drop its washes. */
    struct yetty_yrich_ydoc_ptr_result clear_res = yetty_yrich_ydoc_from(obj);
    if (!YETTY_IS_ERR(clear_res)) {
        clear_res.value->select_all = 0;
        if (clear_res.value->sel_span_active) {
            clear_res.value->sel_span_active = 0;
            struct yetty_ycore_void_result relayout_res = ydoc_relayout(clear_res.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc_set_caret: span clear");
        }
    }
    struct yetty_yrich_selection_ptr_result selection_res = yetty_yrich_document_selection(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, selection_res, "ydoc_set_caret: selection");
    struct yetty_yrich_selection *selection = selection_res.value;
    if (selection && selection->kind == YETTY_YRICH_SEL_TEXT) {
        struct yetty_yclass_object_ptr_result previous_obj_res =
            yetty_yrich_document_find(obj, selection->u.text.element_id);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, previous_obj_res, "ydoc_set_caret: find");
        struct yetty_yclass_object *previous_obj = previous_obj_res.value;
        if (previous_obj && previous_obj != paragraph_obj) {
            struct yetty_yrich_paragraph_ptr_result previous_res =
                yetty_yrich_paragraph_from(previous_obj);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, previous_res,
                                "ydoc_set_caret: previous data_get");
            previous_res.value->editing = 0;
            previous_res.value->sel_start = previous_res.value->sel_end =
                previous_res.value->cursor_pos;
        }
    }

    struct yetty_yrich_paragraph_ptr_result paragraph_res =
        yetty_yrich_paragraph_from(paragraph_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, paragraph_res, "ydoc_set_caret: data_get");
    struct yetty_yrich_paragraph *paragraph = paragraph_res.value;
    anchor = clamp_caret(paragraph, anchor);
    caret = clamp_caret(paragraph, caret);
    paragraph->editing = 1;
    paragraph->cursor_pos = caret;
    paragraph->sel_start = anchor;
    paragraph->sel_end = caret;

    struct yetty_yrich_element_id_result id_res = yetty_yrich_element_id_value(paragraph_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "ydoc_set_caret: element id");
    if (selection) {
        yetty_yrich_selection_select_text(selection, id_res.value, anchor, caret);
    }
    return yetty_yrich_document_mark_dirty(obj);
}

/* Text selection spanning paragraphs: the anchor stays at (anchor_obj,
 * anchor), the focus caret at (focus_obj, focus). Falls back to the
 * single-paragraph caret when both ends share a paragraph. The span is
 * projected onto per-paragraph washes by relayout. */
static struct yetty_ycore_void_result ydoc_set_caret_range(struct yetty_yclass_object *obj,
                                                           struct yetty_yclass_object *anchor_obj,
                                                           int32_t anchor,
                                                           struct yetty_yclass_object *focus_obj,
                                                           int32_t focus)
{
    if (anchor_obj == focus_obj) {
        return ydoc_set_caret(obj, anchor_obj, anchor, focus);
    }
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_caret_range: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;

    struct yetty_yrich_paragraph_ptr_result anchor_res = yetty_yrich_paragraph_from(anchor_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, anchor_res, "ydoc_set_caret_range: anchor data");
    struct yetty_yrich_paragraph_ptr_result focus_res = yetty_yrich_paragraph_from(focus_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, focus_res, "ydoc_set_caret_range: focus data");
    size_t anchor_index;
    size_t focus_index;
    if (!ydoc_alias_index_of(ydoc, anchor_obj, &anchor_index) ||
        !ydoc_alias_index_of(ydoc, focus_obj, &focus_index)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_set_caret_range: paragraph not aliased");
    }
    anchor = clamp_caret(anchor_res.value, anchor);
    focus = clamp_caret(focus_res.value, focus);

    /* The focus paragraph carries the caret; nobody else is editing. */
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        if (YETTY_IS_OK(paragraph_res)) {
            paragraph_res.value->editing = 0;
        } else {
            yetty_ycore_error_destroy(paragraph_res.error);
        }
    }
    focus_res.value->editing = 1;
    focus_res.value->cursor_pos = focus;

    ydoc->select_all = 0;
    ydoc->sel_span_active = 1;
    if (anchor_index < focus_index) {
        ydoc->sel_span_lo = anchor_index;
        ydoc->sel_span_lo_off = anchor;
        ydoc->sel_span_hi = focus_index;
        ydoc->sel_span_hi_off = focus;
    } else {
        ydoc->sel_span_lo = focus_index;
        ydoc->sel_span_lo_off = focus;
        ydoc->sel_span_hi = anchor_index;
        ydoc->sel_span_hi_off = anchor;
    }

    struct yetty_yrich_element_id_result anchor_id_res = yetty_yrich_element_id_value(anchor_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, anchor_id_res, "ydoc_set_caret_range: anchor id");
    struct yetty_yrich_element_id_result focus_id_res = yetty_yrich_element_id_value(focus_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, focus_id_res, "ydoc_set_caret_range: focus id");
    struct yetty_yrich_selection_ptr_result selection_res = yetty_yrich_document_selection(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, selection_res, "ydoc_set_caret_range: selection");
    if (selection_res.value) {
        yetty_yrich_selection_select_text_range(selection_res.value, anchor_id_res.value, anchor,
                                                focus_id_res.value, focus);
    }

    struct yetty_ycore_void_result relayout_res = ydoc_relayout(ydoc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc_set_caret_range: relayout");
    return yetty_yrich_document_mark_dirty(obj);
}

static struct yetty_ycore_void_result ydoc_end_editing(struct yetty_yclass_object *obj,
                                                       struct yetty_yrich_ydoc *ydoc)
{
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res = ydoc_active_paragraph_get(obj, ydoc, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc_end_editing: active paragraph");
    if (active_res.value) {
        active.paragraph->editing = 0;
        active.paragraph->sel_start = active.paragraph->sel_end = active.paragraph->cursor_pos;
    }
    ydoc->sel_word_drag = 0;
    if (ydoc->sel_span_active) {
        ydoc->sel_span_active = 0;
        struct yetty_ycore_void_result relayout_res = ydoc_relayout(ydoc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc_end_editing: span clear");
    }
    return yetty_yrich_document_clear_selection(obj);
}

/*---------------------------------------------------------------------------
 * Undoable text edits — TEXT_INSERT / TEXT_DELETE ops via op-commands.
 *-------------------------------------------------------------------------*/

static char *dup_text_range(const char *text, size_t start, size_t count)
{
    char *copy = malloc(count + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text + start, count);
    copy[count] = '\0';
    return copy;
}

static struct yetty_yrich_operation_ptr_result ydoc_make_insert_op(struct yetty_yclass_object *obj,
                                                                   yetty_yrich_element_id id,
                                                                   int32_t position,
                                                                   const char *text, size_t len)
{
    struct yetty_yrich_operation_ptr_result op_res =
        yetty_yrich_document_create_op(obj, YETTY_YRICH_OP_TEXT_INSERT);
    YETTY_RETURN_IF_ERR(yetty_yrich_operation_ptr, op_res, "ydoc insert op: create");
    char *copy = dup_text_range(text, 0, len);
    if (!copy) {
        yetty_yrich_operation_destroy(op_res.value);
        return YETTY_ERR(yetty_yrich_operation_ptr, "ydoc insert op: text dup failed");
    }
    op_res.value->u.text_insert.id = id;
    op_res.value->u.text_insert.position = position;
    op_res.value->u.text_insert.text = copy;
    return op_res;
}

static struct yetty_yrich_operation_ptr_result ydoc_make_delete_op(
    struct yetty_yclass_object *obj, const struct ydoc_active_paragraph *active, int32_t position,
    int32_t length)
{
    struct yetty_yrich_operation_ptr_result op_res =
        yetty_yrich_document_create_op(obj, YETTY_YRICH_OP_TEXT_DELETE);
    YETTY_RETURN_IF_ERR(yetty_yrich_operation_ptr, op_res, "ydoc delete op: create");
    char *deleted = dup_text_range(active->paragraph->text, (size_t)position, (size_t)length);
    if (!deleted) {
        yetty_yrich_operation_destroy(op_res.value);
        return YETTY_ERR(yetty_yrich_operation_ptr, "ydoc delete op: text dup failed");
    }
    op_res.value->u.text_delete.id = active->id;
    op_res.value->u.text_delete.position = position;
    op_res.value->u.text_delete.length = length;
    op_res.value->u.text_delete.deleted_text = deleted;
    return op_res;
}

/* Run one or two ops as a single undoable command (delete may be NULL). */
static struct yetty_ycore_void_result ydoc_execute_edit(struct yetty_yclass_object *obj,
                                                        struct yetty_yrich_operation *delete_op,
                                                        struct yetty_yrich_operation *insert_op)
{
    struct yetty_yrich_command_ptr_result command_res = yetty_yrich_op_command_create();
    if (YETTY_IS_ERR(command_res)) {
        yetty_yrich_operation_destroy(delete_op);
        yetty_yrich_operation_destroy(insert_op);
        return YETTY_ERR(yetty_ycore_void, "ydoc edit: command create", command_res);
    }
    if (delete_op) {
        struct yetty_ycore_void_result record_res =
            yetty_yrich_command_record_op(command_res.value, delete_op);
        if (YETTY_IS_ERR(record_res)) {
            yetty_yrich_command_destroy(command_res.value);
            yetty_yrich_operation_destroy(insert_op);
            return YETTY_ERR(yetty_ycore_void, "ydoc edit: record delete", record_res);
        }
    }
    if (insert_op) {
        struct yetty_ycore_void_result record_res =
            yetty_yrich_command_record_op(command_res.value, insert_op);
        if (YETTY_IS_ERR(record_res)) {
            yetty_yrich_command_destroy(command_res.value);
            return YETTY_ERR(yetty_ycore_void, "ydoc edit: record insert", record_res);
        }
    }
    return yetty_yrich_document_execute(obj, command_res.value);
}

static struct yetty_ycore_void_result ydoc_delete_selection_or_range(
    struct yetty_yclass_object *obj, const struct ydoc_active_paragraph *active, int32_t position,
    int32_t length)
{
    struct yetty_yrich_operation_ptr_result delete_res =
        ydoc_make_delete_op(obj, active, position, length);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, delete_res, "ydoc delete: op");
    return ydoc_execute_edit(obj, delete_res.value, NULL);
}

/*---------------------------------------------------------------------------
 * Structural edits — split on Enter, merge on Backspace/Delete at the
 * paragraph boundary. Applied directly (not undoable).
 *-------------------------------------------------------------------------*/

static struct yetty_ycore_void_result ydoc_alias_insert_at(struct yetty_yrich_ydoc *ydoc,
                                                           size_t index,
                                                           struct yetty_yclass_object *obj)
{
    struct yetty_ycore_void_result push_res = paragraph_list_push(ydoc, obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, push_res, "ydoc alias insert: grow");
    if (index + 1 < ydoc->paragraph_count) {
        memmove(&ydoc->paragraphs[index + 1], &ydoc->paragraphs[index],
                (ydoc->paragraph_count - 1 - index) * sizeof(*ydoc->paragraphs));
        ydoc->paragraphs[index] = obj;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ydoc_split_paragraph(struct yetty_yclass_object *obj,
                                                           struct yetty_yrich_ydoc *ydoc,
                                                           struct ydoc_active_paragraph *active)
{
    struct yetty_yrich_paragraph *paragraph = active->paragraph;
    int32_t caret = clamp_caret(paragraph, active->caret);
    size_t tail_len = paragraph->text_len - (size_t)caret;
    char *tail = NULL;
    if (tail_len > 0) {
        tail = dup_text_range(paragraph->text, (size_t)caret, tail_len);
        if (!tail) {
            return YETTY_ERR(yetty_ycore_void, "ydoc split: tail dup failed");
        }
    }

    struct yetty_yclass_object_ptr_result create_res = yetty_yrich_paragraph_create(NULL);
    if (YETTY_IS_ERR(create_res)) {
        free(tail);
        return YETTY_ERR(yetty_ycore_void, "ydoc split: paragraph create", create_res);
    }
    struct yetty_yclass_object *tail_obj = create_res.value;
    struct yetty_yrich_paragraph_ptr_result tail_res = yetty_yrich_paragraph_from(tail_obj);
    if (YETTY_IS_ERR(tail_res)) {
        free(tail);
        struct yetty_ycore_void_result destroy_res = yetty_yrich_element_destroy(tail_obj);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        return YETTY_ERR(yetty_ycore_void, "ydoc split: data_get", tail_res);
    }
    struct yetty_yrich_paragraph *tail_paragraph = tail_res.value;
    tail_paragraph->style = paragraph->style;
    tail_paragraph->line_height = paragraph->line_height;
    tail_paragraph->line_spacing = paragraph->line_spacing;
    tail_paragraph->indent = paragraph->indent;
    tail_paragraph->heading_level = paragraph->heading_level;
    tail_paragraph->bounds.w = paragraph->bounds.w;
    if (tail) {
        free(tail_paragraph->text);
        tail_paragraph->text = tail;
        tail_paragraph->text_len = tail_len;
    }

    struct yetty_yrich_element_id_result id_res = yetty_yrich_document_next_id(obj);
    if (YETTY_IS_OK(id_res)) {
        struct yetty_ycore_void_result set_id_res =
            yetty_yrich_element_set_id(tail_obj, id_res.value);
        if (YETTY_IS_ERR(set_id_res)) {
            yetty_ycore_error_destroy(set_id_res.error);
        }
    } else {
        yetty_ycore_error_destroy(id_res.error);
    }

    struct yetty_ycore_void_result add_res = yetty_yrich_document_add_element(obj, tail_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_res, "ydoc split: add_element");
    struct yetty_ycore_void_result alias_res =
        ydoc_alias_insert_at(ydoc, active->alias_index + 1, tail_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, alias_res, "ydoc split: alias insert");

    /* Truncate the head paragraph at the split point. */
    paragraph->text_len = (size_t)caret;
    if (paragraph->text) {
        paragraph->text[paragraph->text_len] = '\0';
    }
    paragraph->editing = 0;
    paragraph->cursor_pos = caret;
    paragraph->sel_start = paragraph->sel_end = caret;
    paragraph_recompute_height(paragraph);
    tail_paragraph->text_len = tail_len;
    paragraph_recompute_height(tail_paragraph);

    struct yetty_ycore_void_result relayout_res = ydoc_relayout(ydoc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc split: relayout");
    return ydoc_set_caret(obj, tail_obj, 0, 0);
}

/* Append paragraph `source_index`'s text to paragraph `target_index` and
 * remove the source. The caret lands at the join position. */
static struct yetty_ycore_void_result ydoc_merge_paragraphs(struct yetty_yclass_object *obj,
                                                            struct yetty_yrich_ydoc *ydoc,
                                                            size_t target_index,
                                                            size_t source_index)
{
    struct yetty_yclass_object *target_obj = ydoc->paragraphs[target_index];
    struct yetty_yclass_object *source_obj = ydoc->paragraphs[source_index];
    struct yetty_yrich_paragraph_ptr_result target_res = yetty_yrich_paragraph_from(target_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, target_res, "ydoc merge: target data");
    struct yetty_yrich_paragraph_ptr_result source_res = yetty_yrich_paragraph_from(source_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, source_res, "ydoc merge: source data");
    struct yetty_yrich_paragraph *target = target_res.value;
    struct yetty_yrich_paragraph *source = source_res.value;

    int32_t join_position = (int32_t)target->text_len;
    if (source->text_len > 0) {
        struct yetty_ycore_void_result insert_res =
            paragraph_text_insert_at(target, join_position, source->text, source->text_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, insert_res, "ydoc merge: append");
    }

    struct yetty_yrich_element_id_result source_id_res = yetty_yrich_element_id_value(source_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, source_id_res, "ydoc merge: source id");
    memmove(&ydoc->paragraphs[source_index], &ydoc->paragraphs[source_index + 1],
            (ydoc->paragraph_count - source_index - 1) * sizeof(*ydoc->paragraphs));
    ydoc->paragraph_count--;
    struct yetty_ycore_void_result remove_res =
        yetty_yrich_document_remove_element(obj, source_id_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, remove_res, "ydoc merge: remove source");

    struct yetty_ycore_void_result relayout_res = ydoc_relayout(ydoc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc merge: relayout");
    return ydoc_set_caret(obj, target_obj, join_position, join_position);
}

/* True if the paragraph at `index` is a horizontal-rule divider. */
static int ydoc_index_is_divider(struct yetty_yrich_ydoc *ydoc, size_t index)
{
    if (index >= ydoc->paragraph_count) {
        return 0;
    }
    struct yetty_yrich_paragraph_ptr_result res =
        yetty_yrich_paragraph_from(ydoc->paragraphs[index]);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
        return 0;
    }
    return ydoc_block_is_rule_like(res.value->block_kind);
}

/* Remove the paragraph at `index` entirely (used to delete a divider). The
 * caller relayouts and repositions the caret. Structural, direct. */
static struct yetty_ycore_void_result ydoc_remove_paragraph_index(struct yetty_yclass_object *obj,
                                                                  struct yetty_yrich_ydoc *ydoc,
                                                                  size_t index)
{
    if (index >= ydoc->paragraph_count) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrich_element_id_result id_res =
        yetty_yrich_element_id_value(ydoc->paragraphs[index]);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "ydoc remove para: id");
    memmove(&ydoc->paragraphs[index], &ydoc->paragraphs[index + 1],
            (ydoc->paragraph_count - index - 1) * sizeof(*ydoc->paragraphs));
    ydoc->paragraph_count--;
    return yetty_yrich_document_remove_element(obj, id_res.value);
}

/* Create a fresh paragraph carrying `text` (optional) and `block_kind`, give it
 * a document id, add it to the element tree, and splice its alias in at
 * `index`. The new object is returned via `out_new`. Structural, direct. */
static struct yetty_ycore_void_result ydoc_insert_paragraph_at(struct yetty_yclass_object *obj,
                                                               struct yetty_yrich_ydoc *ydoc,
                                                               size_t index, const char *text,
                                                               size_t text_len, uint32_t block_kind,
                                                               struct yetty_yclass_object **out_new)
{
    struct yetty_yclass_object_ptr_result create_res = yetty_yrich_paragraph_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, create_res, "ydoc insert para: create");
    struct yetty_yclass_object *new_obj = create_res.value;
    struct yetty_yrich_paragraph_ptr_result new_res = yetty_yrich_paragraph_from(new_obj);
    if (YETTY_IS_ERR(new_res)) {
        struct yetty_ycore_void_result destroy_res = yetty_yrich_element_destroy(new_obj);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        return YETTY_ERR(yetty_ycore_void, "ydoc insert para: data", new_res);
    }
    struct yetty_yrich_paragraph *new_paragraph = new_res.value;
    new_paragraph->block_kind = block_kind;
    if (text && text_len > 0) {
        char *copy = dup_text_range(text, 0, text_len);
        if (!copy) {
            struct yetty_ycore_void_result destroy_res = yetty_yrich_element_destroy(new_obj);
            if (YETTY_IS_ERR(destroy_res)) {
                yetty_ycore_error_destroy(destroy_res.error);
            }
            return YETTY_ERR(yetty_ycore_void, "ydoc insert para: text dup");
        }
        free(new_paragraph->text);
        new_paragraph->text = copy;
        new_paragraph->text_len = text_len;
    }
    struct yetty_yrich_element_id_result id_res = yetty_yrich_document_next_id(obj);
    if (YETTY_IS_OK(id_res)) {
        struct yetty_ycore_void_result set_id_res =
            yetty_yrich_element_set_id(new_obj, id_res.value);
        if (YETTY_IS_ERR(set_id_res)) {
            yetty_ycore_error_destroy(set_id_res.error);
        }
    } else {
        yetty_ycore_error_destroy(id_res.error);
    }
    struct yetty_ycore_void_result add_res = yetty_yrich_document_add_element(obj, new_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_res, "ydoc insert para: add_element");
    struct yetty_ycore_void_result alias_res = ydoc_alias_insert_at(ydoc, index, new_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, alias_res, "ydoc insert para: alias insert");
    paragraph_recompute_height(new_paragraph);
    if (out_new) {
        *out_new = new_obj;
    }
    return YETTY_OK_VOID();
}

/* Insert a rule-like block (horizontal rule or page break) at the caret and
 * leave the caret on a fresh text line below it. Structural, direct (not
 * undoable — matches split/merge until the invertible-op foundation lands). */
static struct yetty_ycore_void_result ydoc_insert_rule_like(struct yetty_yclass_object *obj,
                                                            uint32_t block_kind)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc insert rule: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res = ydoc_active_paragraph_get(obj, ydoc, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc insert rule: active");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrich_paragraph *paragraph = active.paragraph;
    size_t index = active.alias_index;
    struct yetty_yclass_object *caret_target = NULL;

    if (paragraph->text_len == 0 && paragraph->block_kind == YDOC_BLOCK_TEXT) {
        /* On an empty line: that line becomes the rule; add a fresh line below
         * for the caret to land on. */
        paragraph->block_kind = block_kind;
        paragraph_recompute_height(paragraph);
        struct yetty_ycore_void_result trailing_res =
            ydoc_insert_paragraph_at(obj, ydoc, index + 1, NULL, 0, YDOC_BLOCK_TEXT, &caret_target);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, trailing_res, "ydoc insert rule: trailing line");
    } else {
        /* Split at the caret: [head][rule][tail], caret to the tail. */
        int32_t caret = clamp_caret(paragraph, active.caret);
        size_t tail_len = paragraph->text_len - (size_t)caret;
        struct yetty_ycore_void_result divider_res =
            ydoc_insert_paragraph_at(obj, ydoc, index + 1, NULL, 0, block_kind, NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, divider_res, "ydoc insert rule: divider");
        struct yetty_ycore_void_result tail_res =
            ydoc_insert_paragraph_at(obj, ydoc, index + 2, paragraph->text + caret, tail_len,
                                     YDOC_BLOCK_TEXT, &caret_target);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tail_res, "ydoc insert hr: tail line");
        if (caret_target) {
            struct yetty_yrich_paragraph_ptr_result tail_paragraph_res =
                yetty_yrich_paragraph_from(caret_target);
            if (YETTY_IS_OK(tail_paragraph_res)) {
                tail_paragraph_res.value->style = paragraph->style;
                tail_paragraph_res.value->line_spacing = paragraph->line_spacing;
                tail_paragraph_res.value->indent = paragraph->indent;
            }
        }
        paragraph->text_len = (size_t)caret;
        if (paragraph->text) {
            paragraph->text[paragraph->text_len] = '\0';
        }
        paragraph_recompute_height(paragraph);
    }

    struct yetty_ycore_void_result relayout_res = ydoc_relayout(ydoc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc insert hr: relayout");
    if (caret_target) {
        return ydoc_set_caret(obj, caret_target, 0, 0);
    }
    return yetty_yrich_document_mark_dirty(obj);
}

/* Insert a horizontal-rule divider at the caret. Exposed; Insert > Horizontal
 * rule dispatches here. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_insert_horizontal_rule(
    struct yetty_yclass_object *obj)
{
    return ydoc_insert_rule_like(obj, YDOC_BLOCK_DIVIDER);
}

/* Insert a page-break marker at the caret. Exposed; Insert > Page break
 * dispatches here. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_insert_page_break(struct yetty_yclass_object *obj)
{
    return ydoc_insert_rule_like(obj, YDOC_BLOCK_PAGE_BREAK);
}

/* Insert a rows x cols table after the caret, leaving the caret on a fresh
 * text line below it. Cells start empty; click a cell to edit it. Structural,
 * direct (not undoable). Exposed; Insert > Table dispatches here. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_insert_table(struct yetty_yclass_object *obj,
                                                             uint32_t rows, uint32_t cols)
{
    if (rows == 0 || cols == 0 || rows > 50 || cols > 20) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_insert_table: bad dimensions");
    }
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_insert_table: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;

    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res = ydoc_active_paragraph_get(obj, ydoc, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc_insert_table: active");
    size_t insert_index = active_res.value ? active.alias_index + 1 : ydoc->paragraph_count;

    struct yetty_yclass_object *table_obj = NULL;
    struct yetty_ycore_void_result table_res =
        ydoc_insert_paragraph_at(obj, ydoc, insert_index, NULL, 0, YDOC_BLOCK_TABLE, &table_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, table_res, "ydoc_insert_table: create");
    struct yetty_yrich_paragraph_ptr_result table_paragraph_res =
        yetty_yrich_paragraph_from(table_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, table_paragraph_res, "ydoc_insert_table: data");
    struct yetty_yrich_paragraph *table = table_paragraph_res.value;
    table->table_rows = rows;
    table->table_cols = cols;
    table->table_active_cell = -1;
    table->table_cells = calloc((size_t)rows * cols, sizeof(*table->table_cells));
    if (!table->table_cells) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_insert_table: cells alloc");
    }
    paragraph_recompute_height(table);

    struct yetty_yclass_object *caret_target = NULL;
    struct yetty_ycore_void_result trailing_res = ydoc_insert_paragraph_at(
        obj, ydoc, insert_index + 1, NULL, 0, YDOC_BLOCK_TEXT, &caret_target);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, trailing_res, "ydoc_insert_table: trailing line");

    struct yetty_ycore_void_result relayout_res = ydoc_relayout(ydoc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc_insert_table: relayout");
    if (caret_target) {
        return ydoc_set_caret(obj, caret_target, 0, 0);
    }
    return yetty_yrich_document_mark_dirty(obj);
}

/* Insert a table of contents at the caret: a "Contents" heading followed by
 * one indented entry per heading in the document (a static snapshot, like a
 * word processor's non-updating TOC). Structural, direct. Exposed. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_insert_toc(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_insert_toc: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;

    /* Collect (text, level) for every heading, in document order. */
    struct toc_entry {
        char *text;
        uint32_t level;
    };
    struct toc_entry *entries = NULL;
    size_t entry_count = 0;
    size_t entry_capacity = 0;
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        if (YETTY_IS_ERR(paragraph_res)) {
            yetty_ycore_error_destroy(paragraph_res.error);
            continue;
        }
        struct yetty_yrich_paragraph *paragraph = paragraph_res.value;
        if (paragraph->block_kind != YDOC_BLOCK_TEXT || paragraph->heading_level == 0 ||
            paragraph->heading_level > 6 || paragraph->text_len == 0) {
            continue; /* Title/Subtitle (7/8) are not outline entries. */
        }
        if (entry_count == entry_capacity) {
            size_t new_capacity = entry_capacity ? entry_capacity * 2 : 8;
            struct toc_entry *grown = realloc(entries, new_capacity * sizeof(*grown));
            if (!grown) {
                for (size_t e = 0; e < entry_count; e++) {
                    free(entries[e].text);
                }
                free(entries);
                return YETTY_ERR(yetty_ycore_void, "ydoc_insert_toc: entries grow");
            }
            entries = grown;
            entry_capacity = new_capacity;
        }
        entries[entry_count].text = dup_text_range(paragraph->text, 0, paragraph->text_len);
        if (!entries[entry_count].text) {
            for (size_t e = 0; e < entry_count; e++) {
                free(entries[e].text);
            }
            free(entries);
            return YETTY_ERR(yetty_ycore_void, "ydoc_insert_toc: text dup");
        }
        entries[entry_count].level = paragraph->heading_level;
        entry_count++;
    }
    if (entry_count == 0) {
        free(entries);
        return YETTY_OK_VOID();
    }

    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res = ydoc_active_paragraph_get(obj, ydoc, &active);
    if (YETTY_IS_ERR(active_res)) {
        for (size_t e = 0; e < entry_count; e++) {
            free(entries[e].text);
        }
        free(entries);
        return YETTY_ERR(yetty_ycore_void, "ydoc_insert_toc: active", active_res);
    }
    size_t insert_index = active_res.value ? active.alias_index + 1 : 0;

    struct yetty_ycore_void_result status = YETTY_OK_VOID();
    struct yetty_yclass_object *title_obj = NULL;
    status = ydoc_insert_paragraph_at(obj, ydoc, insert_index, "Contents", 8, YDOC_BLOCK_TEXT,
                                      &title_obj);
    if (!YETTY_IS_ERR(status) && title_obj) {
        struct yetty_yrich_paragraph_ptr_result title_res = yetty_yrich_paragraph_from(title_obj);
        if (YETTY_IS_OK(title_res)) {
            title_res.value->heading_level = 2;
            title_res.value->style.font_size = 20.0f;
            title_res.value->style.format |= YETTY_YRICH_FMT_BOLD;
            paragraph_recompute_height(title_res.value);
        }
    }
    for (size_t e = 0; !YETTY_IS_ERR(status) && e < entry_count; e++) {
        struct yetty_yclass_object *entry_obj = NULL;
        status = ydoc_insert_paragraph_at(obj, ydoc, insert_index + 1 + e, entries[e].text,
                                          strlen(entries[e].text), YDOC_BLOCK_TEXT, &entry_obj);
        if (!YETTY_IS_ERR(status) && entry_obj) {
            struct yetty_yrich_paragraph_ptr_result entry_res =
                yetty_yrich_paragraph_from(entry_obj);
            if (YETTY_IS_OK(entry_res)) {
                entry_res.value->indent = (float)(entries[e].level - 1) * (YDOC_INDENT_STEP * 0.5f);
            }
        }
    }
    for (size_t e = 0; e < entry_count; e++) {
        free(entries[e].text);
    }
    free(entries);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, status, "ydoc_insert_toc: insert");

    struct yetty_ycore_void_result relayout_res = ydoc_relayout(ydoc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc_insert_toc: relayout");
    return yetty_yrich_document_mark_dirty(obj);
}

/* Append UTF-8 `text` to the active cell of a table paragraph. */
static struct yetty_ycore_void_result table_cell_append(struct yetty_yrich_paragraph *table,
                                                        const char *text, size_t len)
{
    if (table->table_active_cell < 0 ||
        (uint32_t)table->table_active_cell >= table->table_rows * table->table_cols ||
        !table->table_cells) {
        return YETTY_OK_VOID();
    }
    char **slot = &table->table_cells[table->table_active_cell];
    size_t old_len = *slot ? strlen(*slot) : 0;
    char *grown = realloc(*slot, old_len + len + 1);
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "table_cell_append: grow");
    }
    memcpy(grown + old_len, text, len);
    grown[old_len + len] = '\0';
    *slot = grown;
    return YETTY_OK_VOID();
}

/* Remove the last UTF-8 codepoint from the active table cell. */
static void table_cell_backspace(struct yetty_yrich_paragraph *table)
{
    if (table->table_active_cell < 0 ||
        (uint32_t)table->table_active_cell >= table->table_rows * table->table_cols ||
        !table->table_cells) {
        return;
    }
    char *cell = table->table_cells[table->table_active_cell];
    if (!cell || cell[0] == '\0') {
        return;
    }
    size_t len = strlen(cell);
    size_t cut = len - 1;
    while (cut > 0 && (((unsigned char)cell[cut]) & 0xC0) == 0x80) {
        cut--;
    }
    cell[cut] = '\0';
}

/* The cell (row*cols+col) at document-space (x,y) inside a table paragraph, or
 * -1 if outside its grid. */
static int32_t table_cell_at_point(const struct yetty_yrich_paragraph *table, float x, float y)
{
    if (table->table_rows == 0 || table->table_cols == 0) {
        return -1;
    }
    float cell_w = table->bounds.w / (float)table->table_cols;
    float rel_x = x - table->bounds.x;
    float rel_y = y - table->bounds.y;
    if (rel_x < 0.0f || rel_y < 0.0f || rel_x >= table->bounds.w) {
        return -1;
    }
    uint32_t col = (uint32_t)(rel_x / cell_w);
    uint32_t row = (uint32_t)(rel_y / YDOC_TABLE_CELL_HEIGHT);
    if (col >= table->table_cols || row >= table->table_rows) {
        return -1;
    }
    return (int32_t)(row * table->table_cols + col);
}

enum ydoc_table_op {
    YDOC_TABLE_INSERT_ROW = 0,
    YDOC_TABLE_INSERT_COL = 1,
    YDOC_TABLE_DELETE_ROW = 2,
    YDOC_TABLE_DELETE_COL = 3,
};

/* Structurally edit the grid of `table` around its active cell (insert/delete a
 * row or column). Cell strings are moved by pointer, not copied. */
static struct yetty_ycore_void_result table_grid_edit(struct yetty_yrich_paragraph *table,
                                                      enum ydoc_table_op op)
{
    if (table->block_kind != YDOC_BLOCK_TABLE || table->table_rows == 0 || table->table_cols == 0 ||
        !table->table_cells) {
        return YETTY_OK_VOID();
    }
    uint32_t rows = table->table_rows;
    uint32_t cols = table->table_cols;
    char **old = table->table_cells;
    uint32_t active = table->table_active_cell >= 0 ? (uint32_t)table->table_active_cell : 0;
    uint32_t active_row = active / cols;
    uint32_t active_col = active % cols;

    uint32_t new_rows = rows;
    uint32_t new_cols = cols;
    switch (op) {
    case YDOC_TABLE_INSERT_ROW:
        new_rows = rows + 1;
        break;
    case YDOC_TABLE_INSERT_COL:
        new_cols = cols + 1;
        break;
    case YDOC_TABLE_DELETE_ROW:
        if (rows <= 1) {
            return YETTY_OK_VOID();
        }
        new_rows = rows - 1;
        break;
    case YDOC_TABLE_DELETE_COL:
        if (cols <= 1) {
            return YETTY_OK_VOID();
        }
        new_cols = cols - 1;
        break;
    }
    if (new_rows > 50 || new_cols > 20) {
        return YETTY_OK_VOID();
    }

    char **grid = calloc((size_t)new_rows * new_cols, sizeof(*grid));
    if (!grid) {
        return YETTY_ERR(yetty_ycore_void, "table_grid_edit: alloc");
    }
    for (uint32_t r = 0; r < rows; r++) {
        if (op == YDOC_TABLE_DELETE_ROW && r == active_row) {
            for (uint32_t c = 0; c < cols; c++) {
                free(old[r * cols + c]);
            }
            continue;
        }
        uint32_t dst_row = r;
        if (op == YDOC_TABLE_INSERT_ROW && r > active_row) {
            dst_row = r + 1;
        } else if (op == YDOC_TABLE_DELETE_ROW && r > active_row) {
            dst_row = r - 1;
        }
        uint32_t dst_col = 0;
        for (uint32_t c = 0; c < cols; c++) {
            if (op == YDOC_TABLE_DELETE_COL && c == active_col) {
                free(old[r * cols + c]);
                continue;
            }
            grid[dst_row * new_cols + dst_col] = old[r * cols + c];
            dst_col++;
            if (op == YDOC_TABLE_INSERT_COL && c == active_col) {
                dst_col++; /* leave the inserted column blank; next cell shifts right */
            }
        }
    }
    free(old);
    table->table_cells = grid;
    table->table_rows = new_rows;
    table->table_cols = new_cols;
    uint32_t cell_count = new_rows * new_cols;
    if (table->table_active_cell >= 0 && (uint32_t)table->table_active_cell >= cell_count) {
        table->table_active_cell = cell_count > 0 ? (int32_t)(cell_count - 1) : -1;
    }
    paragraph_recompute_height(table);
    return YETTY_OK_VOID();
}

/* Insert/delete a row or column around the active cell of the active table.
 * `op` is enum ydoc_table_op. Direct (not undoable). Exposed; the Insert menu
 * dispatches here. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_table_edit(struct yetty_yclass_object *obj,
                                                           uint32_t op)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_table_edit: data_get");
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc_table_edit: active");
    if (!active_res.value || active.paragraph->block_kind != YDOC_BLOCK_TABLE) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result edit_res =
        table_grid_edit(active.paragraph, (enum ydoc_table_op)op);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, edit_res, "ydoc_table_edit: grid");
    struct yetty_ycore_void_result relayout_res = ydoc_relayout(data_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc_table_edit: relayout");
    return yetty_yrich_document_mark_dirty(obj);
}

/* Delete the covered content of a cross-paragraph selection: the tail of the
 * first covered paragraph, every paragraph strictly in between, the head of
 * the last, then merge the two boundary paragraphs. Applied directly (not
 * undoable — the structural removals have no inverse op yet, same as
 * split/merge). The caret lands at the join. */
static struct yetty_ycore_void_result ydoc_delete_span(struct yetty_yclass_object *obj,
                                                       struct yetty_yrich_ydoc *ydoc)
{
    if (!ydoc->sel_span_active) {
        return YETTY_OK_VOID();
    }
    size_t span_lo = ydoc->sel_span_lo;
    size_t span_hi = ydoc->sel_span_hi;
    int32_t lo_off = ydoc->sel_span_lo_off;
    int32_t hi_off = ydoc->sel_span_hi_off;
    if (span_hi >= ydoc->paragraph_count || span_lo >= span_hi) {
        ydoc->sel_span_active = 0;
        return YETTY_OK_VOID();
    }
    for (size_t i = span_lo; i <= span_hi; i++) {
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, paragraph_res, "ydoc delete span: paragraph");
        struct yetty_yrich_paragraph *paragraph = paragraph_res.value;
        int32_t from = i == span_lo ? lo_off : 0;
        int32_t to = i == span_hi ? hi_off : (int32_t)paragraph->text_len;
        if (to > from) {
            struct yetty_ycore_void_result delete_res =
                paragraph_text_delete_range(paragraph, from, to - from);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, delete_res, "ydoc delete span: range");
        }
    }
    ydoc->sel_span_active = 0;
    /* Stitch the remaining boundary paragraphs together; the middles are
     * empty now, so every merge joins at the surviving head's end. */
    for (size_t remaining = span_hi - span_lo; remaining > 0; remaining--) {
        struct yetty_ycore_void_result merge_res =
            ydoc_merge_paragraphs(obj, ydoc, span_lo, span_lo + 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, merge_res, "ydoc delete span: merge");
    }
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Input overrides.
 *-------------------------------------------------------------------------*/

/* Formatting slot impls — defined below, used by the keyboard shortcuts. */
static struct yetty_ycore_void_result ydoc_toggle_format_impl(struct yetty_yclass_object *obj,
                                                              uint32_t format_flag);
static struct yetty_ycore_void_result ydoc_change_font_size_impl(struct yetty_yclass_object *obj,
                                                                 float delta);
struct yetty_ycore_void_result yetty_yrich_ydoc_change_list_level(struct yetty_yclass_object *obj,
                                                                  int32_t direction);

/* Word boundaries — alnum/underscore words, everything else is a gap. */
static int is_word_char(char character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_' ||
           (unsigned char)character >= 0x80;
}

static int32_t word_jump_left(const struct yetty_yrich_paragraph *paragraph, int32_t caret)
{
    int32_t pos = caret;
    while (pos > 0 && !is_word_char(paragraph->text[pos - 1])) {
        pos--;
    }
    while (pos > 0 && is_word_char(paragraph->text[pos - 1])) {
        pos--;
    }
    return pos;
}

static int32_t word_jump_right(const struct yetty_yrich_paragraph *paragraph, int32_t caret)
{
    int32_t pos = caret;
    int32_t len = (int32_t)paragraph->text_len;
    while (pos < len && !is_word_char(paragraph->text[pos])) {
        pos++;
    }
    while (pos < len && is_word_char(paragraph->text[pos])) {
        pos++;
    }
    return pos;
}

/* Byte extent [lo,hi) of the token at `pos` — the word if `pos` is inside or
 * immediately after one (double-click on a word selects the whole word), else
 * the run of whitespace/punctuation. Always non-empty for a non-empty
 * paragraph, so a double-click anywhere yields a draggable anchor. */
static void paragraph_word_extent(const struct yetty_yrich_paragraph *paragraph, int32_t pos,
                                  int32_t *out_lo, int32_t *out_hi)
{
    int32_t len = (int32_t)paragraph->text_len;
    if (pos < 0) {
        pos = 0;
    }
    if (pos > len) {
        pos = len;
    }
    int32_t lo = pos;
    int32_t hi = pos;
    if (pos < len && is_word_char(paragraph->text[pos])) {
        while (lo > 0 && is_word_char(paragraph->text[lo - 1])) {
            lo--;
        }
        while (hi < len && is_word_char(paragraph->text[hi])) {
            hi++;
        }
    } else if (pos > 0 && is_word_char(paragraph->text[pos - 1])) {
        while (lo > 0 && is_word_char(paragraph->text[lo - 1])) {
            lo--;
        }
    } else {
        while (lo > 0 && !is_word_char(paragraph->text[lo - 1])) {
            lo--;
        }
        while (hi < len && !is_word_char(paragraph->text[hi])) {
            hi++;
        }
    }
    *out_lo = lo;
    *out_hi = hi;
}

YETTY_ANNOTATE("override@yrich:ydoc:document_on_mouse_down")
static struct yetty_ycore_void_result ydoc_on_mouse_down(struct yetty_yclass_object *obj, float x,
                                                         float y, uint32_t button, uint32_t mods)
{
    if (button != YETTY_YRICH_MOUSE_LEFT) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_on_mouse_down: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;

    /* A fresh press begins a new gesture: any armed word-granularity drag
     * from a previous double-click ends here. (The double-click's own press
     * clears it too, then the double-click handler re-arms it afterward.) */
    ydoc->sel_word_drag = 0;

    /* Checklist gutter — a click on the checkbox marker (left of the text
     * box) toggles the item instead of placing the caret in the text. */
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        struct yetty_yrich_paragraph_ptr_result para_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        if (YETTY_IS_ERR(para_res)) {
            yetty_ycore_error_destroy(para_res.error);
            continue;
        }
        struct yetty_yrich_paragraph *paragraph = para_res.value;
        if (paragraph->list_kind != YDOC_LIST_CHECK) {
            continue;
        }
        if (y < paragraph->bounds.y || y >= paragraph->bounds.y + paragraph->bounds.h ||
            x < paragraph->bounds.x - YDOC_LIST_INDENT || x >= paragraph->bounds.x) {
            continue;
        }
        struct yetty_ycore_void_result caret_res = ydoc_set_caret(obj, ydoc->paragraphs[i], 0, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, caret_res, "ydoc_on_mouse_down: checkbox caret");
        return yetty_yrich_ydoc_toggle_checked(obj);
    }

    struct yetty_yclass_object_ptr_result hit_res = yetty_yrich_document_element_at(obj, x, y);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hit_res, "ydoc_on_mouse_down: element_at");
    struct yetty_yclass_object *hit_obj = hit_res.value;
    if (!hit_obj) {
        return ydoc_end_editing(obj, ydoc);
    }
    struct yetty_yrich_paragraph_ptr_result paragraph_res = yetty_yrich_paragraph_from(hit_obj);
    if (YETTY_IS_ERR(paragraph_res)) {
        /* Not a paragraph (an inline image) — element selection. */
        yetty_ycore_error_destroy(paragraph_res.error);
        struct yetty_yrich_element_id_result id_res = yetty_yrich_element_id_value(hit_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "ydoc_on_mouse_down: element id");
        struct yetty_yrich_selection_ptr_result selection_res = yetty_yrich_document_selection(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, selection_res, "ydoc_on_mouse_down: selection");
        if (selection_res.value) {
            struct yetty_ycore_void_result select_res =
                yetty_yrich_selection_select_element(selection_res.value, id_res.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, select_res, "ydoc_on_mouse_down: select");
        }
        return yetty_yrich_document_mark_dirty(obj);
    }

    /* A table click selects the cell under the pointer for editing. */
    if (paragraph_res.value->block_kind == YDOC_BLOCK_TABLE) {
        int32_t cell = table_cell_at_point(paragraph_res.value, x, y);
        paragraph_res.value->table_active_cell = cell;
        struct yetty_ycore_void_result caret_res = ydoc_set_caret(obj, hit_obj, 0, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, caret_res, "ydoc_on_mouse_down: table caret");
        return yetty_yrich_document_mark_dirty(obj);
    }

    /* A rule / page break is not a caret target: redirect the click to the
     * nearest text paragraph (the one below, else above). */
    if (ydoc_block_is_rule_like(paragraph_res.value->block_kind)) {
        size_t hit_index = 0;
        if (ydoc_alias_index_of(ydoc, hit_obj, &hit_index)) {
            struct yetty_yclass_object *redirect = NULL;
            for (size_t j = hit_index + 1; j < ydoc->paragraph_count; j++) {
                struct yetty_yrich_paragraph_ptr_result candidate =
                    yetty_yrich_paragraph_from(ydoc->paragraphs[j]);
                if (YETTY_IS_OK(candidate) && candidate.value->block_kind == YDOC_BLOCK_TEXT) {
                    redirect = ydoc->paragraphs[j];
                    break;
                }
            }
            for (size_t j = hit_index; redirect == NULL && j > 0; j--) {
                struct yetty_yrich_paragraph_ptr_result candidate =
                    yetty_yrich_paragraph_from(ydoc->paragraphs[j - 1]);
                if (YETTY_IS_OK(candidate) && candidate.value->block_kind == YDOC_BLOCK_TEXT) {
                    redirect = ydoc->paragraphs[j - 1];
                    break;
                }
            }
            if (redirect) {
                return ydoc_set_caret(obj, redirect, 0, 0);
            }
        }
        return ydoc_end_editing(obj, ydoc);
    }

    int32_t caret = paragraph_caret_from_point(paragraph_res.value, x, y);
    if (mods & YETTY_YRICH_MOD_SHIFT) {
        /* Extend from the existing selection anchor — possibly across
         * paragraphs. */
        struct yetty_yrich_selection_ptr_result selection_res = yetty_yrich_document_selection(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, selection_res, "ydoc_on_mouse_down: selection");
        struct yetty_yrich_selection *selection = selection_res.value;
        if (selection && selection->kind == YETTY_YRICH_SEL_TEXT) {
            struct yetty_yclass_object_ptr_result anchor_obj_res =
                yetty_yrich_document_find(obj, selection->u.text.element_id);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, anchor_obj_res,
                                "ydoc_on_mouse_down: anchor find");
            if (anchor_obj_res.value) {
                return ydoc_set_caret_range(obj, anchor_obj_res.value, selection->u.text.start,
                                            hit_obj, caret);
            }
        }
    }
    return ydoc_set_caret(obj, hit_obj, caret, caret);
}

/* Paragraph whose vertical band contains document-space y: the last
 * paragraph starting at or above y, clamped to the first one. NULL only for
 * an empty document. */
static struct yetty_yclass_object *ydoc_paragraph_at_y(struct yetty_yrich_ydoc *ydoc, float y)
{
    struct yetty_yclass_object *best = NULL;
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        if (YETTY_IS_ERR(paragraph_res)) {
            yetty_ycore_error_destroy(paragraph_res.error);
            continue;
        }
        if (!best || y >= paragraph_res.value->bounds.y) {
            best = ydoc->paragraphs[i];
        }
        if (y < paragraph_res.value->bounds.y) {
            break;
        }
    }
    return best;
}

YETTY_ANNOTATE("override@yrich:ydoc:document_on_mouse_drag")
static struct yetty_ycore_void_result ydoc_on_mouse_drag(struct yetty_yclass_object *obj, float x,
                                                         float y, uint32_t button, uint32_t mods)
{
    (void)button;
    (void)mods;
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_on_mouse_drag: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;
    struct yetty_yrich_selection_ptr_result selection_res = yetty_yrich_document_selection(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, selection_res, "ydoc_on_mouse_drag: selection");
    struct yetty_yrich_selection *selection = selection_res.value;
    if (!selection || selection->kind != YETTY_YRICH_SEL_TEXT) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object *hit_obj = ydoc_paragraph_at_y(ydoc, y);
    if (!hit_obj) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrich_paragraph_ptr_result hit_res = yetty_yrich_paragraph_from(hit_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hit_res, "ydoc_on_mouse_drag: hit data");
    int32_t caret = paragraph_caret_from_point(hit_res.value, x, y);

    /* Word-granularity drag (armed by a double-click): keep the whole anchor
     * word selected and snap the focus end to the far edge of the word under
     * the pointer, so the selection grows and shrinks word by word. */
    if (ydoc->sel_word_drag && ydoc->sel_word_anchor_index < ydoc->paragraph_count) {
        struct yetty_yclass_object *anchor_obj = ydoc->paragraphs[ydoc->sel_word_anchor_index];
        size_t focus_index = 0;
        if (!ydoc_alias_index_of(ydoc, hit_obj, &focus_index)) {
            return YETTY_OK_VOID();
        }
        int32_t focus_lo = 0;
        int32_t focus_hi = 0;
        paragraph_word_extent(hit_res.value, caret, &focus_lo, &focus_hi);
        /* Orient so the anchor word stays wholly covered: dragging forward
         * (focus at/after the anchor) keeps anchor_lo fixed and extends to the
         * focus word's end; dragging backward keeps anchor_hi fixed and
         * extends to the focus word's start. */
        int forward =
            focus_index > ydoc->sel_word_anchor_index ||
            (focus_index == ydoc->sel_word_anchor_index && focus_hi >= ydoc->sel_word_anchor_hi);
        int32_t anchor_point = forward ? ydoc->sel_word_anchor_lo : ydoc->sel_word_anchor_hi;
        int32_t focus_point = forward ? focus_hi : focus_lo;
        return ydoc_set_caret_range(obj, anchor_obj, anchor_point, hit_obj, focus_point);
    }

    /* Character-granularity drag: extend from the selection ANCHOR to the
     * paragraph under the pointer — which may be a different paragraph. */
    struct yetty_yclass_object_ptr_result anchor_obj_res =
        yetty_yrich_document_find(obj, selection->u.text.element_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, anchor_obj_res, "ydoc_on_mouse_drag: anchor find");
    if (!anchor_obj_res.value) {
        return YETTY_OK_VOID();
    }
    return ydoc_set_caret_range(obj, anchor_obj_res.value, selection->u.text.start, hit_obj, caret);
}

/* Move the caret one visual line up or down, hopping to the adjacent
 * paragraph at the document edge. */
static struct yetty_ycore_void_result ydoc_caret_vertical(struct yetty_yclass_object *obj,
                                                          struct yetty_yrich_ydoc *ydoc,
                                                          struct ydoc_active_paragraph *active,
                                                          int direction, uint32_t mods)
{
    struct caret_place place;
    paragraph_caret_place(active->paragraph, active->caret, &place);
    size_t line_count = paragraph_line_count(active->paragraph);
    struct yetty_yclass_object *target_obj = active->paragraph_obj;
    int32_t target_caret;

    if (direction < 0 && place.line_index == 0) {
        if (active->alias_index == 0) {
            target_caret = 0;
        } else {
            target_obj = ydoc->paragraphs[active->alias_index - 1];
            struct yetty_yrich_paragraph_ptr_result previous_res =
                yetty_yrich_paragraph_from(target_obj);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, previous_res, "ydoc caret up: data");
            size_t previous_lines = paragraph_line_count(previous_res.value);
            target_caret = paragraph_caret_at(previous_res.value, previous_lines - 1, place.column);
        }
    } else if (direction > 0 && place.line_index + 1 >= line_count) {
        if (active->alias_index + 1 >= ydoc->paragraph_count) {
            target_caret = (int32_t)active->paragraph->text_len;
        } else {
            target_obj = ydoc->paragraphs[active->alias_index + 1];
            struct yetty_yrich_paragraph_ptr_result next_res =
                yetty_yrich_paragraph_from(target_obj);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, next_res, "ydoc caret down: data");
            target_caret = paragraph_caret_at(next_res.value, 0, place.column);
        }
    } else {
        size_t target_line = direction < 0 ? place.line_index - 1 : place.line_index + 1;
        target_caret = paragraph_caret_at(active->paragraph, target_line, place.column);
    }

    int32_t anchor = (mods & YETTY_YRICH_MOD_SHIFT) && target_obj == active->paragraph_obj
                         ? active->anchor
                         : target_caret;
    return ydoc_set_caret(obj, target_obj, anchor, target_caret);
}

YETTY_ANNOTATE("override@yrich:ydoc:document_on_key_down")
static struct yetty_ycore_void_result ydoc_on_key_down(struct yetty_yclass_object *obj,
                                                       uint32_t key, uint32_t mods)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_on_key_down: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;

    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result have_active_res = ydoc_active_paragraph_get(obj, ydoc, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, have_active_res, "ydoc_on_key_down: active paragraph");
    int have_active = have_active_res.value;

    if (mods & YETTY_YRICH_MOD_CTRL) {
        switch (key) {
        case YETTY_YRICH_KEY_Z:
            if (mods & YETTY_YRICH_MOD_SHIFT) {
                return yetty_yrich_document_redo(obj);
            }
            return yetty_yrich_document_undo(obj);
        case YETTY_YRICH_KEY_Y:
            return yetty_yrich_document_redo(obj);
        case YETTY_YRICH_KEY_A: {
            /* Select the whole document (all paragraphs), not just the current
             * one. Projected onto each paragraph's wash at relayout. */
            data_res.value->select_all = 1;
            struct yetty_ycore_void_result relayout_res = ydoc_relayout(data_res.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc: select-all relayout");
            return yetty_yrich_document_mark_dirty(obj);
        }
        case YETTY_YRICH_KEY_B:
            return ydoc_toggle_format_impl(obj, YETTY_YRICH_FMT_BOLD);
        case YETTY_YRICH_KEY_I:
            return ydoc_toggle_format_impl(obj, YETTY_YRICH_FMT_ITALIC);
        case YETTY_YRICH_KEY_U:
            return ydoc_toggle_format_impl(obj, YETTY_YRICH_FMT_UNDERLINE);
        case YETTY_YRICH_KEY_LEFT:
        case YETTY_YRICH_KEY_RIGHT: {
            if (!have_active) {
                return YETTY_OK_VOID();
            }
            int32_t target = key == YETTY_YRICH_KEY_LEFT
                                 ? word_jump_left(active.paragraph, active.caret)
                                 : word_jump_right(active.paragraph, active.caret);
            int32_t anchor = (mods & YETTY_YRICH_MOD_SHIFT) ? active.anchor : target;
            return ydoc_set_caret(obj, active.paragraph_obj, anchor, target);
        }
        default:
            return YETTY_OK_VOID();
        }
    }

    if (!have_active) {
        return YETTY_OK_VOID();
    }

    /* Table cell editing: Backspace deletes in the active cell, Tab advances to
     * the next cell (wrapping), and the arrow keys move between cells. Other
     * keys do nothing special here. */
    if (active.paragraph->block_kind == YDOC_BLOCK_TABLE) {
        struct yetty_yrich_paragraph *table = active.paragraph;
        uint32_t cols = table->table_cols;
        uint32_t cell_count = table->table_rows * cols;
        if (key == YETTY_YRICH_KEY_BACKSPACE) {
            table_cell_backspace(table);
            return yetty_yrich_document_mark_dirty(obj);
        }
        if (cell_count > 0 && (key == YETTY_YRICH_KEY_TAB || key == YETTY_YRICH_KEY_LEFT ||
                               key == YETTY_YRICH_KEY_RIGHT || key == YETTY_YRICH_KEY_UP ||
                               key == YETTY_YRICH_KEY_DOWN)) {
            int32_t current = table->table_active_cell >= 0 ? table->table_active_cell : 0;
            int32_t next = current;
            switch (key) {
            case YETTY_YRICH_KEY_TAB:
            case YETTY_YRICH_KEY_RIGHT:
                next = current + 1;
                if (next < 0 || (uint32_t)next >= cell_count) {
                    next = 0;
                }
                break;
            case YETTY_YRICH_KEY_LEFT:
                next = current - 1;
                if (next < 0) {
                    next = (int32_t)cell_count - 1;
                }
                break;
            case YETTY_YRICH_KEY_DOWN:
                next = current + (int32_t)cols;
                if ((uint32_t)next >= cell_count) {
                    next = current % (int32_t)cols; /* wrap to top of the column */
                }
                break;
            case YETTY_YRICH_KEY_UP:
                next = current - (int32_t)cols;
                if (next < 0) {
                    next = (int32_t)cell_count - (int32_t)cols + (current % (int32_t)cols);
                }
                break;
            default:
                break;
            }
            table->table_active_cell = next;
            return yetty_yrich_document_mark_dirty(obj);
        }
        return YETTY_OK_VOID();
    }

    int32_t selection_lo = active.anchor < active.caret ? active.anchor : active.caret;
    int32_t selection_hi = active.anchor < active.caret ? active.caret : active.anchor;

    /* A cross-paragraph selection is replaced by editing keys, exactly like a
     * single-paragraph one: Backspace/Delete remove it, Enter replaces it
     * with a paragraph break, and the caret lands at the collapse point. */
    if (ydoc->sel_span_active &&
        (key == YETTY_YRICH_KEY_BACKSPACE || key == YETTY_YRICH_KEY_DELETE ||
         key == YETTY_YRICH_KEY_ENTER || key == YETTY_YRICH_KEY_TAB)) {
        struct yetty_ycore_void_result delete_res = ydoc_delete_span(obj, ydoc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, delete_res, "ydoc key: delete span");
        if (key == YETTY_YRICH_KEY_BACKSPACE || key == YETTY_YRICH_KEY_DELETE) {
            return YETTY_OK_VOID();
        }
        struct yetty_ycore_int_result active_res = ydoc_active_paragraph_get(obj, ydoc, &active);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc key: span active");
        if (!active_res.value) {
            return YETTY_OK_VOID();
        }
        selection_lo = selection_hi = active.caret;
    }

    switch (key) {
    case YETTY_YRICH_KEY_LEFT: {
        int32_t target = active.caret - 1;
        if (!(mods & YETTY_YRICH_MOD_SHIFT) && selection_lo != selection_hi) {
            target = selection_lo;
        }
        int32_t anchor = (mods & YETTY_YRICH_MOD_SHIFT) ? active.anchor : target;
        return ydoc_set_caret(obj, active.paragraph_obj, anchor, target);
    }
    case YETTY_YRICH_KEY_RIGHT: {
        int32_t target = active.caret + 1;
        if (!(mods & YETTY_YRICH_MOD_SHIFT) && selection_lo != selection_hi) {
            target = selection_hi;
        }
        int32_t anchor = (mods & YETTY_YRICH_MOD_SHIFT) ? active.anchor : target;
        return ydoc_set_caret(obj, active.paragraph_obj, anchor, target);
    }
    case YETTY_YRICH_KEY_UP:
        return ydoc_caret_vertical(obj, ydoc, &active, -1, mods);
    case YETTY_YRICH_KEY_DOWN:
        return ydoc_caret_vertical(obj, ydoc, &active, 1, mods);
    case YETTY_YRICH_KEY_PAGEUP:
    case YETTY_YRICH_KEY_PAGEDOWN: {
        /* Move the caret a page (a fixed number of visual lines) up or down by
         * stepping the vertical-motion primitive; it re-reads the active
         * paragraph each hop, so it crosses paragraph boundaries correctly. */
        int direction = key == YETTY_YRICH_KEY_PAGEUP ? -1 : 1;
        for (int step = 0; step < YDOC_PAGE_STEP_LINES; step++) {
            struct yetty_ycore_void_result move_res =
                ydoc_caret_vertical(obj, ydoc, &active, direction, mods);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, move_res, "ydoc page: vertical");
            struct yetty_ycore_int_result refresh = ydoc_active_paragraph_get(obj, ydoc, &active);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, refresh, "ydoc page: refresh");
            if (!refresh.value) {
                break;
            }
        }
        return YETTY_OK_VOID();
    }
    case YETTY_YRICH_KEY_HOME: {
        struct caret_place place;
        paragraph_caret_place(active.paragraph, active.caret, &place);
        int32_t target = paragraph_caret_at(active.paragraph, place.line_index, 0);
        int32_t anchor = (mods & YETTY_YRICH_MOD_SHIFT) ? active.anchor : target;
        return ydoc_set_caret(obj, active.paragraph_obj, anchor, target);
    }
    case YETTY_YRICH_KEY_END: {
        struct caret_place place;
        paragraph_caret_place(active.paragraph, active.caret, &place);
        int32_t target = paragraph_caret_at(active.paragraph, place.line_index, (size_t)INT32_MAX);
        int32_t anchor = (mods & YETTY_YRICH_MOD_SHIFT) ? active.anchor : target;
        return ydoc_set_caret(obj, active.paragraph_obj, anchor, target);
    }
    case YETTY_YRICH_KEY_BACKSPACE:
        if (selection_lo != selection_hi) {
            return ydoc_delete_selection_or_range(obj, &active, selection_lo,
                                                  selection_hi - selection_lo);
        }
        if (active.caret > 0) {
            return ydoc_delete_selection_or_range(obj, &active, active.caret - 1, 1);
        }
        if (active.alias_index > 0) {
            /* At the very start of a line: Backspace deletes a rule above it
             * outright, otherwise merges with the previous paragraph. */
            if (ydoc_index_is_divider(ydoc, active.alias_index - 1)) {
                struct yetty_ycore_void_result remove_res =
                    ydoc_remove_paragraph_index(obj, ydoc, active.alias_index - 1);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, remove_res, "ydoc backspace: remove rule");
                struct yetty_ycore_void_result relayout_res = ydoc_relayout(ydoc);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc backspace: relayout");
                return yetty_yrich_document_mark_dirty(obj);
            }
            return ydoc_merge_paragraphs(obj, ydoc, active.alias_index - 1, active.alias_index);
        }
        return YETTY_OK_VOID();
    case YETTY_YRICH_KEY_DELETE:
        if (selection_lo != selection_hi) {
            return ydoc_delete_selection_or_range(obj, &active, selection_lo,
                                                  selection_hi - selection_lo);
        }
        if ((size_t)active.caret < active.paragraph->text_len) {
            return ydoc_delete_selection_or_range(obj, &active, active.caret, 1);
        }
        if (active.alias_index + 1 < ydoc->paragraph_count) {
            /* At the very end of a line: Delete removes a rule below it
             * outright, otherwise merges with the next paragraph. */
            if (ydoc_index_is_divider(ydoc, active.alias_index + 1)) {
                struct yetty_ycore_void_result remove_res =
                    ydoc_remove_paragraph_index(obj, ydoc, active.alias_index + 1);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, remove_res, "ydoc delete: remove rule");
                struct yetty_ycore_void_result relayout_res = ydoc_relayout(ydoc);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc delete: relayout");
                return yetty_yrich_document_mark_dirty(obj);
            }
            return ydoc_merge_paragraphs(obj, ydoc, active.alias_index, active.alias_index + 1);
        }
        return YETTY_OK_VOID();
    case YETTY_YRICH_KEY_ENTER:
        if (selection_lo != selection_hi) {
            struct yetty_ycore_void_result delete_res = ydoc_delete_selection_or_range(
                obj, &active, selection_lo, selection_hi - selection_lo);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, delete_res, "ydoc enter: collapse selection");
            struct yetty_ycore_int_result active_res =
                ydoc_active_paragraph_get(obj, ydoc, &active);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc enter: active paragraph");
            if (!active_res.value) {
                return YETTY_OK_VOID();
            }
        }
        return ydoc_split_paragraph(obj, ydoc, &active);
    case YETTY_YRICH_KEY_TAB: {
        /* On a list item, Tab/Shift+Tab nests the item deeper/shallower (like a
         * word processor); elsewhere Tab inserts two spaces. */
        if (active.paragraph->list_kind != YDOC_LIST_NONE) {
            return yetty_yrich_ydoc_change_list_level(obj, (mods & YETTY_YRICH_MOD_SHIFT) ? -1 : 1);
        }
        if (mods & YETTY_YRICH_MOD_SHIFT) {
            return YETTY_OK_VOID();
        }
        struct yetty_yrich_operation_ptr_result insert_res =
            ydoc_make_insert_op(obj, active.id, active.caret, "  ", 2);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, insert_res, "ydoc tab: op");
        return ydoc_execute_edit(obj, NULL, insert_res.value);
    }
    default:
        return YETTY_OK_VOID();
    }
}

YETTY_ANNOTATE("override@yrich:ydoc:document_on_text_input")
static struct yetty_ycore_void_result ydoc_on_text_input(struct yetty_yclass_object *obj,
                                                         struct yetty_ycore_buffer text)
{
    if (!text.data || text.size == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_on_text_input: data_get");
    /* Typing replaces a cross-paragraph selection: remove it first, then the
     * insert lands at the collapsed caret. */
    if (data_res.value->sel_span_active) {
        struct yetty_ycore_void_result delete_res = ydoc_delete_span(obj, data_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, delete_res, "ydoc text input: delete span");
    }
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    /* A rule / page break holds no text; typing while the caret rests on one
     * (reachable by arrowing onto it) is ignored rather than corrupting it. */
    if (ydoc_block_is_rule_like(active.paragraph->block_kind)) {
        return YETTY_OK_VOID();
    }
    /* Typing into a table appends to the active cell (not undoable). */
    if (active.paragraph->block_kind == YDOC_BLOCK_TABLE) {
        struct yetty_ycore_void_result append_res =
            table_cell_append(active.paragraph, (const char *)text.data, text.size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "ydoc text input: table cell");
        return yetty_yrich_document_mark_dirty(obj);
    }
    int32_t selection_lo = active.anchor < active.caret ? active.anchor : active.caret;
    int32_t selection_hi = active.anchor < active.caret ? active.caret : active.anchor;

    struct yetty_yrich_operation *delete_op = NULL;
    if (selection_lo != selection_hi) {
        struct yetty_yrich_operation_ptr_result delete_res =
            ydoc_make_delete_op(obj, &active, selection_lo, selection_hi - selection_lo);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, delete_res, "ydoc text input: delete op");
        delete_op = delete_res.value;
    }
    struct yetty_yrich_operation_ptr_result insert_res = ydoc_make_insert_op(
        obj, active.id, selection_lo != selection_hi ? selection_lo : active.caret,
        (const char *)text.data, text.size);
    if (YETTY_IS_ERR(insert_res)) {
        yetty_yrich_operation_destroy(delete_op);
        return YETTY_ERR(yetty_ycore_void, "ydoc text input: insert op", insert_res);
    }
    return ydoc_execute_edit(obj, delete_op, insert_res.value);
}

YETTY_ANNOTATE("override@yrich:ydoc:document_on_mouse_double_click")
static struct yetty_ycore_void_result ydoc_on_mouse_double_click(struct yetty_yclass_object *obj,
                                                                 float x, float y, uint32_t button,
                                                                 uint32_t mods)
{
    (void)mods;
    if (button != YETTY_YRICH_MOUSE_LEFT) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object_ptr_result hit_res = yetty_yrich_document_element_at(obj, x, y);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hit_res, "ydoc_on_mouse_double_click: element_at");
    struct yetty_yclass_object *hit_obj = hit_res.value;
    if (!hit_obj) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrich_paragraph_ptr_result paragraph_res = yetty_yrich_paragraph_from(hit_obj);
    if (YETTY_IS_ERR(paragraph_res)) {
        yetty_ycore_error_destroy(paragraph_res.error);
        return YETTY_OK_VOID();
    }
    struct yetty_yrich_paragraph *paragraph = paragraph_res.value;
    int32_t caret = paragraph_caret_from_point(paragraph, x, y);
    int32_t word_start = 0;
    int32_t word_end = 0;
    paragraph_word_extent(paragraph, caret, &word_start, &word_end);

    /* Arm word-granularity dragging: the whole clicked word is the fixed
     * anchor, and a subsequent drag snaps the focus end to word boundaries. */
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_on_mouse_double_click: data_get");
    struct yetty_ycore_void_result select_res = ydoc_set_caret(obj, hit_obj, word_start, word_end);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, select_res, "ydoc_on_mouse_double_click: select word");
    size_t anchor_index = 0;
    if (ydoc_alias_index_of(data_res.value, hit_obj, &anchor_index)) {
        data_res.value->sel_word_drag = 1;
        data_res.value->sel_word_anchor_index = anchor_index;
        data_res.value->sel_word_anchor_lo = word_start;
        data_res.value->sel_word_anchor_hi = word_end;
    }
    return YETTY_OK_VOID();
}

/* Selected text as a fresh heap string (caller frees). NULL when the
 * selection is empty or not a text selection. */
YETTY_ANNOTATE("expose")
/* Concatenate every paragraph's text, joined by newlines (document-wide
 * selection copy). Returns a malloc'd string, or NULL on allocation failure. */
static char *ydoc_all_text(struct yetty_yrich_ydoc *ydoc)
{
    size_t total = 0;
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        if (YETTY_IS_ERR(paragraph_res)) {
            return NULL;
        }
        total += paragraph_res.value->text_len;
        if (i + 1 < ydoc->paragraph_count) {
            total += 1; /* newline separator */
        }
    }
    char *out = malloc(total + 1);
    if (!out) {
        return NULL;
    }
    size_t offset = 0;
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        if (YETTY_IS_ERR(paragraph_res)) {
            free(out);
            return NULL;
        }
        memcpy(out + offset, paragraph_res.value->text, paragraph_res.value->text_len);
        offset += paragraph_res.value->text_len;
        if (i + 1 < ydoc->paragraph_count) {
            out[offset++] = '\n';
        }
    }
    out[offset] = '\0';
    return out;
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_char_ptr_result yetty_yrich_ydoc_selection_text(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, data_res, "ydoc_selection_text: data_get");
    if (data_res.value->select_all) {
        return YETTY_OK(yetty_ycore_char_ptr, ydoc_all_text(data_res.value));
    }
    if (data_res.value->sel_span_active) {
        /* Concatenate the covered slice of every spanned paragraph,
         * newline-separated — what lands on the clipboard. */
        struct yetty_yrich_ydoc *ydoc = data_res.value;
        size_t total = 0;
        for (size_t i = ydoc->sel_span_lo; i <= ydoc->sel_span_hi; i++) {
            struct yetty_yrich_paragraph_ptr_result paragraph_res =
                yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
            if (YETTY_IS_ERR(paragraph_res)) {
                yetty_ycore_error_destroy(paragraph_res.error);
                continue;
            }
            size_t from = i == ydoc->sel_span_lo ? (size_t)ydoc->sel_span_lo_off : 0;
            size_t to = i == ydoc->sel_span_hi ? (size_t)ydoc->sel_span_hi_off
                                               : paragraph_res.value->text_len;
            total += (to > from ? to - from : 0) + 1; /* slice + '\n' (or NUL) */
        }
        char *joined = malloc(total > 0 ? total : 1);
        if (!joined) {
            return YETTY_ERR(yetty_ycore_char_ptr, "ydoc_selection_text: join alloc failed");
        }
        size_t at = 0;
        for (size_t i = ydoc->sel_span_lo; i <= ydoc->sel_span_hi; i++) {
            struct yetty_yrich_paragraph_ptr_result paragraph_res =
                yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
            if (YETTY_IS_ERR(paragraph_res)) {
                yetty_ycore_error_destroy(paragraph_res.error);
                continue;
            }
            size_t from = i == ydoc->sel_span_lo ? (size_t)ydoc->sel_span_lo_off : 0;
            size_t to = i == ydoc->sel_span_hi ? (size_t)ydoc->sel_span_hi_off
                                               : paragraph_res.value->text_len;
            if (i > ydoc->sel_span_lo) {
                joined[at++] = '\n';
            }
            if (to > from) {
                memcpy(joined + at, paragraph_res.value->text + from, to - from);
                at += to - from;
            }
        }
        joined[at] = '\0';
        return YETTY_OK(yetty_ycore_char_ptr, joined);
    }
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, active_res, "ydoc_selection_text: active paragraph");
    if (!active_res.value) {
        return YETTY_OK(yetty_ycore_char_ptr, NULL);
    }
    int32_t selection_lo = active.anchor < active.caret ? active.anchor : active.caret;
    int32_t selection_hi = active.anchor < active.caret ? active.caret : active.anchor;
    if (selection_lo >= selection_hi) {
        return YETTY_OK(yetty_ycore_char_ptr, NULL);
    }
    return YETTY_OK(yetty_ycore_char_ptr,
                    dup_text_range(active.paragraph->text, (size_t)selection_lo,
                                   (size_t)(selection_hi - selection_lo)));
}

/* Document statistics: codepoints, codepoints excluding whitespace,
 * whitespace-delimited words, and text paragraphs (dividers excluded). Any out
 * pointer may be NULL. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_word_count(struct yetty_yclass_object *obj,
                                                           uint32_t *out_words, uint32_t *out_chars,
                                                           uint32_t *out_chars_no_spaces,
                                                           uint32_t *out_paragraphs)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_word_count: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;
    uint32_t words = 0;
    uint32_t chars = 0;
    uint32_t chars_no_spaces = 0;
    uint32_t paragraphs = 0;
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        if (YETTY_IS_ERR(paragraph_res)) {
            yetty_ycore_error_destroy(paragraph_res.error);
            continue;
        }
        struct yetty_yrich_paragraph *paragraph = paragraph_res.value;
        if (paragraph->block_kind == YDOC_BLOCK_DIVIDER) {
            continue;
        }
        paragraphs++;
        int in_word = 0;
        for (size_t byte = 0; byte < paragraph->text_len; byte++) {
            unsigned char code = (unsigned char)paragraph->text[byte];
            if ((code & 0xC0) != 0x80) {
                chars++; /* count UTF-8 lead bytes = codepoints */
                char plain = paragraph->text[byte];
                if (plain != ' ' && plain != '\t' && plain != '\n' && plain != '\r') {
                    chars_no_spaces++;
                }
            }
            if (is_word_char(paragraph->text[byte])) {
                if (!in_word) {
                    words++;
                    in_word = 1;
                }
            } else {
                in_word = 0;
            }
        }
    }
    if (out_words) {
        *out_words = words;
    }
    if (out_chars) {
        *out_chars = chars;
    }
    if (out_chars_no_spaces) {
        *out_chars_no_spaces = chars_no_spaces;
    }
    if (out_paragraphs) {
        *out_paragraphs = paragraphs;
    }
    return YETTY_OK_VOID();
}

/* ASCII case-insensitive equality of `a` and `b` over `len` bytes. */
static int ydoc_ci_equal(const char *a, const char *b, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
    }
    return 1;
}

/* Select the next occurrence of `query` at or after the current selection end,
 * wrapping once to the top (case-insensitive). Returns 1 if found. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_yrich_ydoc_find_next(struct yetty_yclass_object *obj,
                                                         const char *query)
{
    if (!query || query[0] == '\0') {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ydoc_find_next: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;
    if (ydoc->paragraph_count == 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    size_t query_len = strlen(query);
    size_t start_para = 0;
    size_t start_off = 0;
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res = ydoc_active_paragraph_get(obj, ydoc, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, active_res, "ydoc_find_next: active");
    if (active_res.value) {
        start_para = active.alias_index;
        int32_t selection_hi = active.anchor > active.caret ? active.anchor : active.caret;
        start_off = selection_hi > 0 ? (size_t)selection_hi : 0;
    }
    for (size_t step = 0; step <= ydoc->paragraph_count; step++) {
        size_t index = (start_para + step) % ydoc->paragraph_count;
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[index]);
        if (YETTY_IS_ERR(paragraph_res)) {
            yetty_ycore_error_destroy(paragraph_res.error);
            continue;
        }
        struct yetty_yrich_paragraph *paragraph = paragraph_res.value;
        if (paragraph->block_kind == YDOC_BLOCK_DIVIDER || paragraph->text_len < query_len) {
            continue;
        }
        size_t from = step == 0 ? start_off : 0;
        for (size_t at = from; at + query_len <= paragraph->text_len; at++) {
            if (ydoc_ci_equal(paragraph->text + at, query, query_len)) {
                struct yetty_ycore_void_result select_res = ydoc_set_caret(
                    obj, ydoc->paragraphs[index], (int32_t)at, (int32_t)(at + query_len));
                YETTY_RETURN_IF_ERR(yetty_ycore_int, select_res, "ydoc_find_next: select");
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

/* Select the previous occurrence of `query` before the current selection start,
 * wrapping once to the bottom (case-insensitive). Returns 1 if found. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_yrich_ydoc_find_prev(struct yetty_yclass_object *obj,
                                                         const char *query)
{
    if (!query || query[0] == '\0') {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ydoc_find_prev: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;
    if (ydoc->paragraph_count == 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    size_t query_len = strlen(query);
    size_t start_para = ydoc->paragraph_count - 1;
    /* Search strictly before the selection start; SIZE_MAX = whole paragraph. */
    size_t start_limit = (size_t)-1;
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res = ydoc_active_paragraph_get(obj, ydoc, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, active_res, "ydoc_find_prev: active");
    if (active_res.value) {
        start_para = active.alias_index;
        int32_t selection_lo = active.anchor < active.caret ? active.anchor : active.caret;
        start_limit = selection_lo > 0 ? (size_t)selection_lo : 0;
    }
    for (size_t step = 0; step <= ydoc->paragraph_count; step++) {
        size_t index = (start_para + ydoc->paragraph_count - step) % ydoc->paragraph_count;
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[index]);
        if (YETTY_IS_ERR(paragraph_res)) {
            yetty_ycore_error_destroy(paragraph_res.error);
            continue;
        }
        struct yetty_yrich_paragraph *paragraph = paragraph_res.value;
        if (paragraph->block_kind == YDOC_BLOCK_DIVIDER || paragraph->text_len < query_len) {
            continue;
        }
        /* On the start paragraph the match must end at or before start_limit. */
        size_t scan_limit = step == 0 ? start_limit : paragraph->text_len;
        if (scan_limit > paragraph->text_len) {
            scan_limit = paragraph->text_len;
        }
        int found = 0;
        size_t best = 0;
        for (size_t at = 0; at + query_len <= scan_limit; at++) {
            if (ydoc_ci_equal(paragraph->text + at, query, query_len)) {
                best = at;
                found = 1;
            }
        }
        if (found) {
            struct yetty_ycore_void_result select_res = ydoc_set_caret(
                obj, ydoc->paragraphs[index], (int32_t)best, (int32_t)(best + query_len));
            YETTY_RETURN_IF_ERR(yetty_ycore_int, select_res, "ydoc_find_prev: select");
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

/* Replace every occurrence of `query` with `replacement` across all text
 * paragraphs as ONE undoable command. Returns the number replaced. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_yrich_ydoc_replace_all(struct yetty_yclass_object *obj,
                                                           const char *query,
                                                           const char *replacement)
{
    if (!query || query[0] == '\0') {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    if (!replacement) {
        replacement = "";
    }
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ydoc_replace_all: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;
    size_t query_len = strlen(query);
    size_t replacement_len = strlen(replacement);

    struct yetty_yrich_command_ptr_result command_res = yetty_yrich_op_command_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_int, command_res, "ydoc_replace_all: command create");
    struct yetty_yrich_command *command = command_res.value;
    int replaced = 0;
    struct yetty_ycore_int_result status = YETTY_OK(yetty_ycore_int, 0);

    for (size_t i = 0; i < ydoc->paragraph_count && !YETTY_IS_ERR(status); i++) {
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        if (YETTY_IS_ERR(paragraph_res)) {
            yetty_ycore_error_destroy(paragraph_res.error);
            continue;
        }
        struct yetty_yrich_paragraph *paragraph = paragraph_res.value;
        if (paragraph->block_kind == YDOC_BLOCK_DIVIDER || paragraph->text_len < query_len) {
            continue;
        }
        struct yetty_yrich_element_id_result id_res =
            yetty_yrich_element_id_value(ydoc->paragraphs[i]);
        if (YETTY_IS_ERR(id_res)) {
            yetty_ycore_error_destroy(id_res.error);
            continue;
        }
        struct ydoc_active_paragraph target = {
            .paragraph_obj = ydoc->paragraphs[i], .paragraph = paragraph, .id = id_res.value};
        /* Collect non-overlapping match offsets (ascending). */
        size_t capacity = 8;
        size_t match_count = 0;
        size_t *matches = malloc(capacity * sizeof(*matches));
        if (!matches) {
            status = YETTY_ERR(yetty_ycore_int, "ydoc_replace_all: matches alloc");
            break;
        }
        for (size_t at = 0; at + query_len <= paragraph->text_len;) {
            if (ydoc_ci_equal(paragraph->text + at, query, query_len)) {
                if (match_count == capacity) {
                    capacity *= 2;
                    size_t *grown = realloc(matches, capacity * sizeof(*matches));
                    if (!grown) {
                        status = YETTY_ERR(yetty_ycore_int, "ydoc_replace_all: matches grow");
                        break;
                    }
                    matches = grown;
                }
                matches[match_count++] = at;
                at += query_len;
            } else {
                at++;
            }
        }
        /* Record delete+insert per match, right-to-left so earlier offsets stay
         * valid as the command applies in recorded order. */
        for (size_t m = match_count; m > 0 && !YETTY_IS_ERR(status); m--) {
            size_t at = matches[m - 1];
            struct yetty_yrich_operation_ptr_result delete_res =
                ydoc_make_delete_op(obj, &target, (int32_t)at, (int32_t)query_len);
            if (YETTY_IS_ERR(delete_res)) {
                status = YETTY_ERR(yetty_ycore_int, "ydoc_replace_all: delete op", delete_res);
                break;
            }
            struct yetty_ycore_void_result record_del =
                yetty_yrich_command_record_op(command, delete_res.value);
            if (YETTY_IS_ERR(record_del)) {
                yetty_yrich_operation_destroy(delete_res.value);
                status = YETTY_ERR(yetty_ycore_int, "ydoc_replace_all: record delete", record_del);
                break;
            }
            if (replacement_len > 0) {
                struct yetty_yrich_operation_ptr_result insert_res =
                    ydoc_make_insert_op(obj, target.id, (int32_t)at, replacement, replacement_len);
                if (YETTY_IS_ERR(insert_res)) {
                    status = YETTY_ERR(yetty_ycore_int, "ydoc_replace_all: insert op", insert_res);
                    break;
                }
                struct yetty_ycore_void_result record_ins =
                    yetty_yrich_command_record_op(command, insert_res.value);
                if (YETTY_IS_ERR(record_ins)) {
                    yetty_yrich_operation_destroy(insert_res.value);
                    status =
                        YETTY_ERR(yetty_ycore_int, "ydoc_replace_all: record insert", record_ins);
                    break;
                }
            }
            replaced++;
        }
        free(matches);
    }

    if (YETTY_IS_ERR(status) || replaced == 0) {
        yetty_yrich_command_destroy(command);
        return status;
    }
    struct yetty_ycore_void_result execute_res = yetty_yrich_document_execute(obj, command);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, execute_res, "ydoc_replace_all: execute");
    return YETTY_OK(yetty_ycore_int, replaced);
}

/*---------------------------------------------------------------------------
 * Operation application — this is what execute / undo / redo run.
 *-------------------------------------------------------------------------*/

static struct yetty_ycore_void_result ydoc_super_apply_op(struct yetty_yclass_object *obj,
                                                          struct yetty_yrich_operation *op,
                                                          int local_flag)
{
    struct yetty_yclass_method_slot_result slot_res = yetty_yclass_method_slot_get(
        "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_apply_op);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_res, "ydoc_super_apply_op: slot");
    struct yetty_yclass_ptr_result parent_res =
        yetty_yclass_parent(yetty_yrich_ydoc_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, parent_res, "ydoc_super_apply_op: parent");
    if (!parent_res.value) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_impl_t_result impl_res =
        yetty_yclass_dispatch_lookup(parent_res.value, slot_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, impl_res, "ydoc_super_apply_op: dispatch");
    if (!impl_res.value) {
        return YETTY_OK_VOID();
    }
    return ((yetty_yrich_document_apply_op_fn)impl_res.value)(obj, op, local_flag);
}

YETTY_ANNOTATE("override@yrich:ydoc:document_apply_op")
static struct yetty_ycore_void_result ydoc_apply_op(struct yetty_yclass_object *obj,
                                                    struct yetty_yrich_operation *op,
                                                    int local_flag)
{
    if (!op) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_apply_op: NULL op");
    }
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_apply_op: data_get");

    switch (op->type) {
    case YETTY_YRICH_OP_TEXT_INSERT: {
        struct yetty_yclass_object_ptr_result target_res =
            yetty_yrich_document_find(obj, op->u.text_insert.id);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, target_res, "ydoc_apply_op: insert find");
        struct yetty_yclass_object *target_obj = target_res.value;
        if (!target_obj) {
            return YETTY_ERR(yetty_ycore_void, "ydoc_apply_op: unknown insert target");
        }
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(target_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, paragraph_res, "ydoc_apply_op: insert data");
        const char *text = op->u.text_insert.text ? op->u.text_insert.text : "";
        size_t text_len = strlen(text);
        struct yetty_ycore_void_result insert_res = paragraph_text_insert_at(
            paragraph_res.value, op->u.text_insert.position, text, text_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, insert_res, "ydoc_apply_op: insert");
        struct yetty_ycore_void_result relayout_res = ydoc_relayout(data_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc_apply_op: insert relayout");
        struct yetty_ycore_void_result caret_res =
            ydoc_set_caret(obj, target_obj, op->u.text_insert.position + (int32_t)text_len,
                           op->u.text_insert.position + (int32_t)text_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, caret_res, "ydoc_apply_op: insert caret");
        break;
    }
    case YETTY_YRICH_OP_TEXT_DELETE: {
        struct yetty_yclass_object_ptr_result target_res =
            yetty_yrich_document_find(obj, op->u.text_delete.id);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, target_res, "ydoc_apply_op: delete find");
        struct yetty_yclass_object *target_obj = target_res.value;
        if (!target_obj) {
            return YETTY_ERR(yetty_ycore_void, "ydoc_apply_op: unknown delete target");
        }
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(target_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, paragraph_res, "ydoc_apply_op: delete data");
        struct yetty_ycore_void_result delete_res = paragraph_text_delete_range(
            paragraph_res.value, op->u.text_delete.position, op->u.text_delete.length);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, delete_res, "ydoc_apply_op: delete");
        struct yetty_ycore_void_result relayout_res = ydoc_relayout(data_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc_apply_op: delete relayout");
        struct yetty_ycore_void_result caret_res =
            ydoc_set_caret(obj, target_obj, op->u.text_delete.position, op->u.text_delete.position);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, caret_res, "ydoc_apply_op: delete caret");
        break;
    }
    default:
        break;
    }
    return ydoc_super_apply_op(obj, op, local_flag);
}

/*---------------------------------------------------------------------------
 * Render override — re-layout (wrap widths / stacking) before painting.
 *-------------------------------------------------------------------------*/

YETTY_ANNOTATE("override@yrich:ydoc:document_render")
static struct yetty_ycore_void_result ydoc_render(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_render: data_get");
    struct yetty_ycore_void_result relayout_res = ydoc_relayout(data_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc_render: relayout");
    return yetty_yrich_super_void(obj, yetty_yrich_ydoc_class_get().value,
                                  (yetty_yclass_method_id_t)yetty_yrich_document_render);
}

/*---------------------------------------------------------------------------
 * Undoable formatting — snapshot command.
 *
 * Formatting (bold/italic/underline/strike, colour, alignment, heading,
 * font-size) mutates a paragraph's runs + base style in ways the fine-grained
 * op model can't invert precisely (the smart-toggle is not self-inverse over a
 * mixed range). So each formatting action captures the paragraph's full
 * formatting state before and after, and undo/redo restore those snapshots
 * exactly. Structural identity is by element id, so undo survives reordering.
 *-------------------------------------------------------------------------*/

struct ydoc_format_snapshot {
    struct yetty_yrich_text_style style;
    float line_height;
    float line_spacing;
    float indent;
    float space_before;
    float space_after;
    uint32_t heading_level;
    uint32_t halign;
    uint32_t list_kind;
    int list_checked;
    uint32_t list_level;
    uint32_t block_kind;
    struct yetty_yrich_text_run *runs; /* deep copy; owned */
    size_t run_count;
};

/* Deep-copy the formatting-relevant state of `paragraph` into `snapshot`.
 * Returns 0 on allocation failure. */
static int ydoc_format_snapshot_capture(struct ydoc_format_snapshot *snapshot,
                                        const struct yetty_yrich_paragraph *paragraph)
{
    snapshot->style = paragraph->style;
    snapshot->line_height = paragraph->line_height;
    snapshot->line_spacing = paragraph->line_spacing;
    snapshot->indent = paragraph->indent;
    snapshot->space_before = paragraph->space_before;
    snapshot->space_after = paragraph->space_after;
    snapshot->heading_level = paragraph->heading_level;
    snapshot->halign = paragraph->halign;
    snapshot->list_kind = paragraph->list_kind;
    snapshot->list_checked = paragraph->list_checked;
    snapshot->list_level = paragraph->list_level;
    snapshot->block_kind = paragraph->block_kind;
    snapshot->run_count = paragraph->run_count;
    snapshot->runs = NULL;
    if (paragraph->run_count > 0) {
        snapshot->runs = malloc(paragraph->run_count * sizeof(*snapshot->runs));
        if (!snapshot->runs) {
            return 0;
        }
        memcpy(snapshot->runs, paragraph->runs, paragraph->run_count * sizeof(*snapshot->runs));
    }
    return 1;
}

static struct yetty_ycore_void_result ydoc_format_snapshot_restore(
    const struct ydoc_format_snapshot *snapshot, struct yetty_yrich_paragraph *paragraph)
{
    struct yetty_yrich_text_run *new_runs = NULL;
    if (snapshot->run_count > 0) {
        new_runs = malloc(snapshot->run_count * sizeof(*new_runs));
        if (!new_runs) {
            return YETTY_ERR(yetty_ycore_void, "format restore: runs alloc failed");
        }
        memcpy(new_runs, snapshot->runs, snapshot->run_count * sizeof(*new_runs));
    }
    free(paragraph->runs);
    paragraph->runs = new_runs;
    paragraph->run_count = snapshot->run_count;
    paragraph->run_capacity = snapshot->run_count;
    paragraph->style = snapshot->style;
    paragraph->line_height = snapshot->line_height;
    paragraph->line_spacing = snapshot->line_spacing;
    paragraph->indent = snapshot->indent;
    paragraph->space_before = snapshot->space_before;
    paragraph->space_after = snapshot->space_after;
    paragraph->heading_level = snapshot->heading_level;
    paragraph->halign = snapshot->halign;
    paragraph->list_kind = snapshot->list_kind;
    paragraph->list_checked = snapshot->list_checked;
    paragraph->list_level = snapshot->list_level;
    paragraph->block_kind = snapshot->block_kind;
    return YETTY_OK_VOID();
}

/* The undoable command. `base` first so a base pointer == the container. */
struct ydoc_format_entry {
    yetty_yrich_element_id id;
    struct ydoc_format_snapshot before;
    struct ydoc_format_snapshot after;
};

struct ydoc_format_command {
    struct yetty_yrich_command base;
    struct ydoc_format_entry *entries; /* owned */
    size_t count;
};

/* Restore one paragraph's snapshot by id (no relayout). */
static struct yetty_ycore_void_result ydoc_format_restore_by_id(
    struct yetty_yclass_object *doc_obj, yetty_yrich_element_id id,
    const struct ydoc_format_snapshot *snapshot)
{
    struct yetty_yclass_object_ptr_result target_res = yetty_yrich_document_find(doc_obj, id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, target_res, "format apply: find");
    if (!target_res.value) {
        return YETTY_ERR(yetty_ycore_void, "format apply: paragraph gone");
    }
    struct yetty_yrich_paragraph_ptr_result paragraph_res =
        yetty_yrich_paragraph_from(target_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, paragraph_res, "format apply: paragraph data");
    return ydoc_format_snapshot_restore(snapshot, paragraph_res.value);
}

/* Restore every entry (before or after), then relayout + mark dirty once. */
static struct yetty_ycore_void_result ydoc_format_command_apply(struct yetty_yclass_object *doc_obj,
                                                                struct ydoc_format_command *cmd,
                                                                int use_after)
{
    for (size_t i = 0; i < cmd->count; i++) {
        const struct ydoc_format_snapshot *snapshot =
            use_after ? &cmd->entries[i].after : &cmd->entries[i].before;
        struct yetty_ycore_void_result restore_res =
            ydoc_format_restore_by_id(doc_obj, cmd->entries[i].id, snapshot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, restore_res, "format apply: restore entry");
    }
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(doc_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "format apply: ydoc data");
    struct yetty_ycore_void_result relayout_res = ydoc_relayout(data_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "format apply: relayout");
    return yetty_yrich_document_mark_dirty(doc_obj);
}

static struct yetty_ycore_void_result ydoc_format_command_execute(
    struct yetty_yrich_command *self, struct yetty_yclass_object *doc_obj)
{
    return ydoc_format_command_apply(doc_obj, (struct ydoc_format_command *)self, 1);
}

static struct yetty_ycore_void_result ydoc_format_command_undo(struct yetty_yrich_command *self,
                                                               struct yetty_yclass_object *doc_obj)
{
    return ydoc_format_command_apply(doc_obj, (struct ydoc_format_command *)self, 0);
}

static struct yetty_ycore_void_result ydoc_format_command_redo(struct yetty_yrich_command *self,
                                                               struct yetty_yclass_object *doc_obj)
{
    return ydoc_format_command_apply(doc_obj, (struct ydoc_format_command *)self, 1);
}

static void ydoc_format_command_destroy(struct yetty_yrich_command *self)
{
    struct ydoc_format_command *cmd = (struct ydoc_format_command *)self;
    for (size_t i = 0; i < cmd->count; i++) {
        free(cmd->entries[i].before.runs);
        free(cmd->entries[i].after.runs);
    }
    free(cmd->entries);
    free(cmd);
}

static const struct yetty_yrich_command_ops *ydoc_format_command_ops(void)
{
    static const struct yetty_yrich_command_ops ops = {
        .destroy = ydoc_format_command_destroy,
        .execute = ydoc_format_command_execute,
        .undo = ydoc_format_command_undo,
        .redo = ydoc_format_command_redo,
    };
    return &ops;
}

/* Take ownership of the entries array (and each snapshot's runs) and run the
 * change as one undo step covering every touched paragraph. */
static struct yetty_ycore_void_result ydoc_push_format_entries(struct yetty_yclass_object *obj,
                                                               struct ydoc_format_entry *entries,
                                                               size_t count)
{
    struct ydoc_format_command *cmd = calloc(1, sizeof(*cmd));
    if (!cmd) {
        for (size_t i = 0; i < count; i++) {
            free(entries[i].before.runs);
            free(entries[i].after.runs);
        }
        free(entries);
        return YETTY_ERR(yetty_ycore_void, "format command: alloc failed");
    }
    cmd->base.ops = ydoc_format_command_ops();
    cmd->entries = entries;
    cmd->count = count;
    return yetty_yrich_document_execute(obj, &cmd->base);
}

/* Single-paragraph: capture the active paragraph's post-change state and push a
 * one-entry command. Takes ownership of `before`. */
static struct yetty_ycore_void_result ydoc_format_commit(struct yetty_yclass_object *obj,
                                                         const struct ydoc_active_paragraph *active,
                                                         struct ydoc_format_snapshot *before)
{
    struct ydoc_format_entry *entries = calloc(1, sizeof(*entries));
    if (!entries) {
        free(before->runs);
        return YETTY_ERR(yetty_ycore_void, "format commit: entries alloc failed");
    }
    struct ydoc_format_snapshot after;
    if (!ydoc_format_snapshot_capture(&after, active->paragraph)) {
        free(before->runs);
        free(entries);
        return YETTY_ERR(yetty_ycore_void, "format commit: snapshot after failed");
    }
    entries[0].id = active->id;
    entries[0].before = *before;
    entries[0].after = after;
    return ydoc_push_format_entries(obj, entries, 1);
}

/* Whole-paragraph mutation kinds applied to every paragraph under select-all. */
enum ydoc_fmt_kind {
    YDOC_FMT_TOGGLE,
    YDOC_FMT_COLOR,
    YDOC_FMT_HIGHLIGHT,
    YDOC_FMT_CLEAR,
    YDOC_FMT_ALIGN,
    YDOC_FMT_HEADING,
    YDOC_FMT_FONT_DELTA,
    YDOC_FMT_SET_FONTSIZE,
    YDOC_FMT_LINE_SPACING,
    YDOC_FMT_INDENT_DELTA,
    YDOC_FMT_SPACE_BEFORE,
    YDOC_FMT_SPACE_AFTER,
    YDOC_FMT_LIST_LEVEL_DELTA,
};

static struct yetty_ycore_void_result ydoc_apply_fmt_whole(struct yetty_yrich_paragraph *paragraph,
                                                           enum ydoc_fmt_kind kind, uint32_t uarg,
                                                           float farg)
{
    switch (kind) {
    case YDOC_FMT_TOGGLE:
        if (paragraph->text_len > 0) {
            return paragraph_apply_format_range(paragraph, 0, paragraph->text_len, uarg);
        }
        paragraph->style.format ^= uarg;
        return YETTY_OK_VOID();
    case YDOC_FMT_COLOR:
        if (paragraph->text_len > 0) {
            return paragraph_apply_color_range(paragraph, 0, paragraph->text_len, uarg);
        }
        paragraph->style.color = uarg;
        return YETTY_OK_VOID();
    case YDOC_FMT_HIGHLIGHT:
        if (paragraph->text_len > 0) {
            return paragraph_apply_bgcolor_range(paragraph, 0, paragraph->text_len, uarg);
        }
        paragraph->style.bg_color = uarg;
        return YETTY_OK_VOID();
    case YDOC_FMT_CLEAR:
        if (paragraph->text_len > 0) {
            return paragraph_clear_format_range(paragraph, 0, paragraph->text_len);
        }
        paragraph->style.format = YETTY_YRICH_FMT_NONE;
        paragraph->style.color = YETTY_YRICH_COLOR_BLACK;
        paragraph->style.bg_color = YETTY_YRICH_COLOR_TRANSPARENT;
        return YETTY_OK_VOID();
    case YDOC_FMT_ALIGN:
        paragraph->halign = uarg;
        return YETTY_OK_VOID();
    case YDOC_FMT_HEADING: {
        /* H1..H6 (and Title=7 / Subtitle=8) sizes; 0 = normal body. */
        static const float sizes[] = {14.0f, 30.0f, 24.0f, 19.0f, 16.0f,
                                      14.0f, 13.0f, 36.0f, 18.0f};
        uint32_t level = uarg <= 8 ? uarg : 6;
        float font_size = sizes[level];
        paragraph->heading_level = uarg;
        paragraph->style.font_size = font_size;
        paragraph->line_height = font_size * paragraph->line_spacing;
        /* Subtitle (8) is muted and not bold; other headings are bold. */
        if (level == 8) {
            paragraph->style.format &= ~(uint32_t)YETTY_YRICH_FMT_BOLD;
        } else if (level > 0) {
            paragraph->style.format |= YETTY_YRICH_FMT_BOLD;
        } else {
            paragraph->style.format &= ~(uint32_t)YETTY_YRICH_FMT_BOLD;
        }
        return YETTY_OK_VOID();
    }
    case YDOC_FMT_FONT_DELTA: {
        float font_size = clamp_font_size(paragraph->style.font_size + farg);
        paragraph->style.font_size = font_size;
        paragraph->line_height = font_size * paragraph->line_spacing;
        return YETTY_OK_VOID();
    }
    case YDOC_FMT_SET_FONTSIZE: {
        float font_size = clamp_font_size(farg);
        paragraph->style.font_size = font_size;
        paragraph->line_height = font_size * paragraph->line_spacing;
        return YETTY_OK_VOID();
    }
    case YDOC_FMT_LINE_SPACING:
        paragraph->line_spacing = farg;
        paragraph->line_height = paragraph->style.font_size * paragraph->line_spacing;
        return YETTY_OK_VOID();
    case YDOC_FMT_INDENT_DELTA: {
        float indent = paragraph->indent + farg;
        if (indent < 0.0f) {
            indent = 0.0f;
        }
        paragraph->indent = indent;
        return YETTY_OK_VOID();
    }
    case YDOC_FMT_SPACE_BEFORE:
        paragraph->space_before = farg < 0.0f ? 0.0f : farg;
        return YETTY_OK_VOID();
    case YDOC_FMT_SPACE_AFTER:
        paragraph->space_after = farg < 0.0f ? 0.0f : farg;
        return YETTY_OK_VOID();
    case YDOC_FMT_LIST_LEVEL_DELTA: {
        /* Only list items nest; clamp to [0, 7]. */
        if (paragraph->list_kind == YDOC_LIST_NONE) {
            return YETTY_OK_VOID();
        }
        int32_t level = (int32_t)paragraph->list_level + (int32_t)uarg - 1; /* uarg encodes +1/-1 */
        if (level < 0) {
            level = 0;
        }
        if (level > 7) {
            level = 7;
        }
        paragraph->list_level = (uint32_t)level;
        return YETTY_OK_VOID();
    }
    }
    return YETTY_OK_VOID();
}

/* Document-wide (select-all): apply a whole-paragraph mutation to EVERY
 * paragraph as one undoable command. */
static struct yetty_ycore_void_result ydoc_format_all(struct yetty_yclass_object *obj,
                                                      struct yetty_yrich_ydoc *ydoc,
                                                      enum ydoc_fmt_kind kind, uint32_t uarg,
                                                      float farg)
{
    if (ydoc->paragraph_count == 0) {
        return YETTY_OK_VOID();
    }
    struct ydoc_format_entry *entries = calloc(ydoc->paragraph_count, sizeof(*entries));
    if (!entries) {
        return YETTY_ERR(yetty_ycore_void, "format all: entries alloc failed");
    }
    size_t built = 0;
    struct yetty_ycore_void_result status = YETTY_OK_VOID();
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        if (YETTY_IS_ERR(paragraph_res)) {
            status = YETTY_ERR(yetty_ycore_void, "format all: paragraph", paragraph_res);
            break;
        }
        struct yetty_yrich_element_id_result id_res =
            yetty_yrich_element_id_value(ydoc->paragraphs[i]);
        if (YETTY_IS_ERR(id_res)) {
            status = YETTY_ERR(yetty_ycore_void, "format all: element id", id_res);
            break;
        }
        struct yetty_yrich_paragraph *paragraph = paragraph_res.value;
        if (!ydoc_format_snapshot_capture(&entries[built].before, paragraph)) {
            status = YETTY_ERR(yetty_ycore_void, "format all: snapshot before");
            break;
        }
        struct yetty_ycore_void_result apply_res =
            ydoc_apply_fmt_whole(paragraph, kind, uarg, farg);
        if (YETTY_IS_ERR(apply_res)) {
            free(entries[built].before.runs);
            status = YETTY_ERR(yetty_ycore_void, "format all: apply", apply_res);
            break;
        }
        if (!ydoc_format_snapshot_capture(&entries[built].after, paragraph)) {
            free(entries[built].before.runs);
            status = YETTY_ERR(yetty_ycore_void, "format all: snapshot after");
            break;
        }
        entries[built].id = id_res.value;
        built++;
    }
    if (YETTY_IS_ERR(status)) {
        for (size_t i = 0; i < built; i++) {
            free(entries[i].before.runs);
            free(entries[i].after.runs);
        }
        free(entries);
        return status;
    }
    return ydoc_push_format_entries(obj, entries, built);
}

/* Character-level formatting applied to a byte range of one paragraph;
 * paragraph-level attributes (alignment, heading, spacing, lists…) fall back
 * to the whole-paragraph application — that is how a word processor treats
 * them for a partially covered paragraph. */
static struct yetty_ycore_void_result ydoc_apply_fmt_range(struct yetty_yrich_paragraph *paragraph,
                                                           size_t lo, size_t hi,
                                                           enum ydoc_fmt_kind kind, uint32_t uarg,
                                                           float farg)
{
    if (hi > paragraph->text_len) {
        hi = paragraph->text_len;
    }
    if (lo >= hi) {
        return ydoc_apply_fmt_whole(paragraph, kind, uarg, farg);
    }
    switch (kind) {
    case YDOC_FMT_TOGGLE:
        return paragraph_apply_format_range(paragraph, lo, hi, uarg);
    case YDOC_FMT_COLOR:
        return paragraph_apply_color_range(paragraph, lo, hi, uarg);
    case YDOC_FMT_HIGHLIGHT:
        return paragraph_apply_bgcolor_range(paragraph, lo, hi, uarg);
    case YDOC_FMT_CLEAR:
        return paragraph_clear_format_range(paragraph, lo, hi);
    case YDOC_FMT_FONT_DELTA:
        return paragraph_apply_fontsize_range(paragraph, lo, hi, farg, 0.0f);
    case YDOC_FMT_SET_FONTSIZE:
        return paragraph_apply_fontsize_range(paragraph, lo, hi, 0.0f, farg);
    default:
        return ydoc_apply_fmt_whole(paragraph, kind, uarg, farg);
    }
}

/* Cross-paragraph selection: apply a formatting action to the covered range
 * of every spanned paragraph as ONE undoable command. */
static struct yetty_ycore_void_result ydoc_format_span(struct yetty_yclass_object *obj,
                                                       struct yetty_yrich_ydoc *ydoc,
                                                       enum ydoc_fmt_kind kind, uint32_t uarg,
                                                       float farg)
{
    if (!ydoc->sel_span_active || ydoc->sel_span_hi >= ydoc->paragraph_count) {
        return YETTY_OK_VOID();
    }
    size_t count = ydoc->sel_span_hi - ydoc->sel_span_lo + 1;
    struct ydoc_format_entry *entries = calloc(count, sizeof(*entries));
    if (!entries) {
        return YETTY_ERR(yetty_ycore_void, "format span: entries alloc failed");
    }
    size_t built = 0;
    struct yetty_ycore_void_result status = YETTY_OK_VOID();
    for (size_t i = ydoc->sel_span_lo; i <= ydoc->sel_span_hi; i++) {
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        if (YETTY_IS_ERR(paragraph_res)) {
            status = YETTY_ERR(yetty_ycore_void, "format span: paragraph", paragraph_res);
            break;
        }
        struct yetty_yrich_element_id_result id_res =
            yetty_yrich_element_id_value(ydoc->paragraphs[i]);
        if (YETTY_IS_ERR(id_res)) {
            status = YETTY_ERR(yetty_ycore_void, "format span: element id", id_res);
            break;
        }
        struct yetty_yrich_paragraph *paragraph = paragraph_res.value;
        if (!ydoc_format_snapshot_capture(&entries[built].before, paragraph)) {
            status = YETTY_ERR(yetty_ycore_void, "format span: snapshot before");
            break;
        }
        size_t from = i == ydoc->sel_span_lo ? (size_t)ydoc->sel_span_lo_off : 0;
        size_t to = i == ydoc->sel_span_hi ? (size_t)ydoc->sel_span_hi_off : paragraph->text_len;
        struct yetty_ycore_void_result apply_res =
            ydoc_apply_fmt_range(paragraph, from, to, kind, uarg, farg);
        if (YETTY_IS_ERR(apply_res)) {
            free(entries[built].before.runs);
            status = YETTY_ERR(yetty_ycore_void, "format span: apply", apply_res);
            break;
        }
        if (!ydoc_format_snapshot_capture(&entries[built].after, paragraph)) {
            free(entries[built].before.runs);
            status = YETTY_ERR(yetty_ycore_void, "format span: snapshot after");
            break;
        }
        entries[built].id = id_res.value;
        built++;
    }
    if (YETTY_IS_ERR(status)) {
        for (size_t i = 0; i < built; i++) {
            free(entries[i].before.runs);
            free(entries[i].after.runs);
        }
        free(entries);
        return status;
    }
    return ydoc_push_format_entries(obj, entries, built);
}

/*---------------------------------------------------------------------------
 * Formatting slots — wire-marshallable (scalars only).
 *-------------------------------------------------------------------------*/

YETTY_ANNOTATE("virtual@yrich:ydoc:ydoc_toggle_format")
static struct yetty_ycore_void_result ydoc_toggle_format_impl(struct yetty_yclass_object *obj,
                                                              uint32_t format_flag)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_toggle_format: data_get");
    if (data_res.value->select_all) {
        return ydoc_format_all(obj, data_res.value, YDOC_FMT_TOGGLE, format_flag, 0.0f);
    }
    if (data_res.value->sel_span_active) {
        return ydoc_format_span(obj, data_res.value, YDOC_FMT_TOGGLE, format_flag, 0.0f);
    }
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, active.paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_toggle_format: snapshot before");
    }
    int32_t selection_lo = active.anchor < active.caret ? active.anchor : active.caret;
    int32_t selection_hi = active.anchor < active.caret ? active.caret : active.anchor;
    struct yetty_ycore_void_result apply_res;
    if (selection_lo != selection_hi) {
        /* Selection-scoped: toggle just the selected range. */
        apply_res = paragraph_apply_format_range(active.paragraph, (size_t)selection_lo,
                                                 (size_t)selection_hi, format_flag);
    } else if (active.paragraph->text_len > 0) {
        /* No selection: toggle the whole paragraph. */
        apply_res = paragraph_apply_format_range(active.paragraph, 0, active.paragraph->text_len,
                                                 format_flag);
    } else {
        active.paragraph->style.format ^= format_flag;
        apply_res = YETTY_OK_VOID();
    }
    if (YETTY_IS_ERR(apply_res)) {
        free(before.runs);
        return YETTY_ERR(yetty_ycore_void, "ydoc_toggle_format: apply", apply_res);
    }
    return ydoc_format_commit(obj, &active, &before);
}

/* Text colour — selection-scoped like toggle_format, absolute (no toggle). */
YETTY_ANNOTATE("virtual@yrich:ydoc:ydoc_set_text_color")
static struct yetty_ycore_void_result ydoc_set_text_color_impl(struct yetty_yclass_object *obj,
                                                               uint32_t color)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_text_color: data_get");
    if (data_res.value->select_all) {
        return ydoc_format_all(obj, data_res.value, YDOC_FMT_COLOR, color, 0.0f);
    }
    if (data_res.value->sel_span_active) {
        return ydoc_format_span(obj, data_res.value, YDOC_FMT_COLOR, color, 0.0f);
    }
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, active.paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_set_text_color: snapshot before");
    }
    int32_t selection_lo = active.anchor < active.caret ? active.anchor : active.caret;
    int32_t selection_hi = active.anchor < active.caret ? active.caret : active.anchor;
    struct yetty_ycore_void_result apply_res;
    if (selection_lo != selection_hi) {
        apply_res = paragraph_apply_color_range(active.paragraph, (size_t)selection_lo,
                                                (size_t)selection_hi, color);
    } else if (active.paragraph->text_len > 0) {
        apply_res =
            paragraph_apply_color_range(active.paragraph, 0, active.paragraph->text_len, color);
    } else {
        active.paragraph->style.color = color;
        apply_res = YETTY_OK_VOID();
    }
    if (YETTY_IS_ERR(apply_res)) {
        free(before.runs);
        return YETTY_ERR(yetty_ycore_void, "ydoc_set_text_color: apply", apply_res);
    }
    return ydoc_format_commit(obj, &active, &before);
}

/* Paragraph alignment (enum yetty_yrich_halign). */
YETTY_ANNOTATE("virtual@yrich:ydoc:ydoc_set_alignment")
static struct yetty_ycore_void_result ydoc_set_alignment_impl(struct yetty_yclass_object *obj,
                                                              uint32_t halign)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_alignment: data_get");
    if (data_res.value->select_all) {
        return ydoc_format_all(obj, data_res.value, YDOC_FMT_ALIGN, halign, 0.0f);
    }
    if (data_res.value->sel_span_active) {
        return ydoc_format_span(obj, data_res.value, YDOC_FMT_ALIGN, halign, 0.0f);
    }
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, active.paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_set_alignment: snapshot before");
    }
    active.paragraph->halign = halign;
    return ydoc_format_commit(obj, &active, &before);
}

/* Line-spacing multiplier for the active paragraph (document-wide under
 * select-all): 1.0 single, 1.5, 2.0 double. Recomputes line_height and is
 * fully undoable. */
YETTY_ANNOTATE("virtual@yrich:ydoc:ydoc_set_line_spacing")
static struct yetty_ycore_void_result ydoc_set_line_spacing_impl(struct yetty_yclass_object *obj,
                                                                 float spacing)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_line_spacing: data_get");
    if (data_res.value->select_all) {
        return ydoc_format_all(obj, data_res.value, YDOC_FMT_LINE_SPACING, 0, spacing);
    }
    if (data_res.value->sel_span_active) {
        return ydoc_format_span(obj, data_res.value, YDOC_FMT_LINE_SPACING, 0, spacing);
    }
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, active.paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_set_line_spacing: snapshot before");
    }
    active.paragraph->line_spacing = spacing;
    active.paragraph->line_height = active.paragraph->style.font_size * spacing;
    return ydoc_format_commit(obj, &active, &before);
}

/* Shift the active paragraph's left indent by one step (positive = increase,
 * negative = decrease; clamped at 0). Document-wide under select-all; undoable. */
YETTY_ANNOTATE("virtual@yrich:ydoc:ydoc_adjust_indent")
static struct yetty_ycore_void_result ydoc_adjust_indent_impl(struct yetty_yclass_object *obj,
                                                              int32_t direction)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_adjust_indent: data_get");
    float delta = (direction >= 0 ? 1.0f : -1.0f) * YDOC_INDENT_STEP;
    if (data_res.value->select_all) {
        return ydoc_format_all(obj, data_res.value, YDOC_FMT_INDENT_DELTA, 0, delta);
    }
    if (data_res.value->sel_span_active) {
        return ydoc_format_span(obj, data_res.value, YDOC_FMT_INDENT_DELTA, 0, delta);
    }
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, active.paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_adjust_indent: snapshot before");
    }
    float indent = active.paragraph->indent + delta;
    if (indent < 0.0f) {
        indent = 0.0f;
    }
    active.paragraph->indent = indent;
    return ydoc_format_commit(obj, &active, &before);
}

/* Apply a whole-paragraph formatting kind to the active paragraph (or every
 * paragraph under select-all / the spanned range under a cross-paragraph
 * selection), as one undoable command. Shared by the simple per-paragraph
 * setters below. */
static struct yetty_ycore_void_result ydoc_apply_paragraph_fmt(struct yetty_yclass_object *obj,
                                                               enum ydoc_fmt_kind kind,
                                                               uint32_t uarg, float farg)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc paragraph fmt: data_get");
    if (data_res.value->select_all) {
        return ydoc_format_all(obj, data_res.value, kind, uarg, farg);
    }
    if (data_res.value->sel_span_active) {
        return ydoc_format_span(obj, data_res.value, kind, uarg, farg);
    }
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc paragraph fmt: active");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, active.paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc paragraph fmt: snapshot before");
    }
    struct yetty_ycore_void_result apply_res =
        ydoc_apply_fmt_whole(active.paragraph, kind, uarg, farg);
    if (YETTY_IS_ERR(apply_res)) {
        free(before.runs);
        return YETTY_ERR(yetty_ycore_void, "ydoc paragraph fmt: apply", apply_res);
    }
    return ydoc_format_commit(obj, &active, &before);
}

/* Extra vertical gap above the paragraph, in px (0 removes it). Undoable. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_set_space_before(struct yetty_yclass_object *obj,
                                                                 float px)
{
    return ydoc_apply_paragraph_fmt(obj, YDOC_FMT_SPACE_BEFORE, 0, px);
}

/* Extra vertical gap below the paragraph, in px (0 removes it). Undoable. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_set_space_after(struct yetty_yclass_object *obj,
                                                                float px)
{
    return ydoc_apply_paragraph_fmt(obj, YDOC_FMT_SPACE_AFTER, 0, px);
}

/* Nest a list item one level deeper (direction > 0) or shallower (<= 0);
 * no-op on non-list paragraphs. Undoable. Wired to Tab / Shift+Tab. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_change_list_level(struct yetty_yclass_object *obj,
                                                                  int32_t direction)
{
    return ydoc_apply_paragraph_fmt(obj, YDOC_FMT_LIST_LEVEL_DELTA, direction > 0 ? 2u : 0u, 0.0f);
}

/* Capture the character style at the caret into the paint-format clipboard. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_copy_format(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_copy_format: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res = ydoc_active_paragraph_get(obj, ydoc, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc_copy_format: active");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrich_paragraph *paragraph = active.paragraph;
    if (paragraph->text_len == 0) {
        ydoc->paint_style = paragraph->style;
    } else {
        size_t pos = active.caret > 0 ? (size_t)(active.caret - 1) : 0;
        if (pos >= paragraph->text_len) {
            pos = paragraph->text_len - 1;
        }
        struct char_attrs attrs = paragraph_attrs_at(paragraph, pos);
        ydoc->paint_style.format = attrs.format;
        ydoc->paint_style.color = attrs.color;
        ydoc->paint_style.bg_color = attrs.bg_color;
        ydoc->paint_style.font_size = attrs.font_size;
        ydoc->paint_style.font_id = paragraph->style.font_id;
    }
    ydoc->paint_has = 1;
    return YETTY_OK_VOID();
}

/* Apply the paint-format clipboard to the selection (or the whole active
 * paragraph when the selection is collapsed). Undoable. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_paint_format(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_paint_format: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;
    if (!ydoc->paint_has) {
        return YETTY_OK_VOID();
    }
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res = ydoc_active_paragraph_get(obj, ydoc, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc_paint_format: active");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrich_paragraph *paragraph = active.paragraph;
    if (paragraph->text_len == 0) {
        return YETTY_OK_VOID();
    }
    int32_t lo = active.anchor < active.caret ? active.anchor : active.caret;
    int32_t hi = active.anchor < active.caret ? active.caret : active.anchor;
    if (lo == hi) {
        lo = 0;
        hi = (int32_t)paragraph->text_len;
    }
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_paint_format: snapshot before");
    }
    struct yetty_ycore_void_result apply_res =
        paragraph_apply_style_range(paragraph, (size_t)lo, (size_t)hi, &ydoc->paint_style);
    if (YETTY_IS_ERR(apply_res)) {
        free(before.runs);
        return YETTY_ERR(yetty_ycore_void, "ydoc_paint_format: apply", apply_res);
    }
    return ydoc_format_commit(obj, &active, &before);
}

/* link_id covering `pos` in `paragraph` (0 = none); probes `pos` then pos-1 so
 * a caret sitting just after a link still resolves it. */
static uint32_t paragraph_link_at(const struct yetty_yrich_paragraph *paragraph, int32_t pos)
{
    if (paragraph->text_len == 0) {
        return 0;
    }
    int32_t len = (int32_t)paragraph->text_len;
    if (pos >= len) {
        pos = len - 1;
    }
    if (pos < 0) {
        pos = 0;
    }
    uint32_t link_id = paragraph_attrs_at(paragraph, (size_t)pos).link_id;
    if (link_id == 0 && pos > 0) {
        link_id = paragraph_attrs_at(paragraph, (size_t)(pos - 1)).link_id;
    }
    return link_id;
}

/* Contiguous byte span carrying `link_id` that contains `pos`. */
static void paragraph_link_extent(const struct yetty_yrich_paragraph *paragraph, int32_t pos,
                                  uint32_t link_id, int32_t *out_lo, int32_t *out_hi)
{
    int32_t len = (int32_t)paragraph->text_len;
    if (pos >= len) {
        pos = len > 0 ? len - 1 : 0;
    }
    int32_t lo = pos;
    int32_t hi = pos;
    while (lo > 0 && paragraph_attrs_at(paragraph, (size_t)(lo - 1)).link_id == link_id) {
        lo--;
    }
    while (hi < len && paragraph_attrs_at(paragraph, (size_t)hi).link_id == link_id) {
        hi++;
    }
    *out_lo = lo;
    *out_hi = hi;
}

/* Make the current selection (or the word at the caret when collapsed) a
 * hyperlink to `url`. The URL is interned in the doc link table. Undoable. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_set_link(struct yetty_yclass_object *obj,
                                                         const char *url)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_link: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;
    if (!url || url[0] == '\0') {
        return YETTY_OK_VOID();
    }
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res = ydoc_active_paragraph_get(obj, ydoc, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc_set_link: active");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrich_paragraph *paragraph = active.paragraph;
    if (paragraph->text_len == 0 || ydoc_block_is_rule_like(paragraph->block_kind)) {
        return YETTY_OK_VOID();
    }
    int32_t lo = active.anchor < active.caret ? active.anchor : active.caret;
    int32_t hi = active.anchor < active.caret ? active.caret : active.anchor;
    if (lo == hi) {
        paragraph_word_extent(paragraph, active.caret, &lo, &hi);
    }
    if (lo >= hi) {
        return YETTY_OK_VOID();
    }
    uint32_t link_id = ydoc_link_intern(ydoc, url, strlen(url));
    if (link_id == 0) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_set_link: intern failed");
    }
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_set_link: snapshot before");
    }
    struct yetty_ycore_void_result apply_res =
        paragraph_apply_link_range(paragraph, (size_t)lo, (size_t)hi, link_id);
    if (YETTY_IS_ERR(apply_res)) {
        free(before.runs);
        return YETTY_ERR(yetty_ycore_void, "ydoc_set_link: apply", apply_res);
    }
    return ydoc_format_commit(obj, &active, &before);
}

/* Remove the hyperlink covering the selection, or the whole link span at the
 * caret when collapsed. No-op when there is no link. Undoable. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_remove_link(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_remove_link: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res = ydoc_active_paragraph_get(obj, ydoc, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc_remove_link: active");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrich_paragraph *paragraph = active.paragraph;
    if (paragraph->text_len == 0) {
        return YETTY_OK_VOID();
    }
    int32_t lo = active.anchor < active.caret ? active.anchor : active.caret;
    int32_t hi = active.anchor < active.caret ? active.caret : active.anchor;
    if (lo == hi) {
        uint32_t link_id = paragraph_link_at(paragraph, active.caret);
        if (link_id == 0) {
            return YETTY_OK_VOID();
        }
        paragraph_link_extent(paragraph, active.caret, link_id, &lo, &hi);
    }
    if (lo >= hi) {
        return YETTY_OK_VOID();
    }
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_remove_link: snapshot before");
    }
    struct yetty_ycore_void_result apply_res =
        paragraph_apply_link_range(paragraph, (size_t)lo, (size_t)hi, 0);
    if (YETTY_IS_ERR(apply_res)) {
        free(before.runs);
        return YETTY_ERR(yetty_ycore_void, "ydoc_remove_link: apply", apply_res);
    }
    return ydoc_format_commit(obj, &active, &before);
}

/* URL of the hyperlink at the caret, or NULL when there is none. The pointer is
 * owned by the document link table (valid until the link is changed/removed or
 * the document is destroyed) — callers must not free it. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_const_char_ptr_result yetty_yrich_ydoc_link_at_caret(
    struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, data_res, "ydoc_link_at_caret: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res = ydoc_active_paragraph_get(obj, ydoc, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, active_res, "ydoc_link_at_caret: active");
    if (!active_res.value) {
        return YETTY_OK(yetty_ycore_const_char_ptr, NULL);
    }
    uint32_t link_id = paragraph_link_at(active.paragraph, active.caret);
    return YETTY_OK(yetty_ycore_const_char_ptr, ydoc_link_url(ydoc, link_id));
}

/* URL for `link_id` in this document's link table (NULL if 0 or unknown).
 * Serializer side — resolves the id a run carries back to its URL. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_const_char_ptr_result yetty_yrich_ydoc_link_url(struct yetty_yclass_object *obj,
                                                                   uint32_t link_id)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, data_res, "ydoc_link_url: data_get");
    return YETTY_OK(yetty_ycore_const_char_ptr, ydoc_link_url(data_res.value, link_id));
}

/* Loader helper: intern `url` and stamp it as the hyperlink over [start, end) of
 * `paragraph_obj` (which must belong to this document). Not undoable — used only
 * while rebuilding a document from serialized form. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_apply_run_link(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *paragraph_obj, int32_t start,
    int32_t end, const char *url)
{
    struct yetty_yrich_ydoc_ptr_result doc_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "ydoc_apply_run_link: data_get");
    struct yetty_yrich_paragraph_ptr_result para_res = yetty_yrich_paragraph_from(paragraph_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, para_res, "ydoc_apply_run_link: paragraph");
    if (!url || url[0] == '\0' || start < 0 || end <= start) {
        return YETTY_OK_VOID();
    }
    uint32_t link_id = ydoc_link_intern(doc_res.value, url, strlen(url));
    if (link_id == 0) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_apply_run_link: intern failed");
    }
    return paragraph_apply_link_range(para_res.value, (size_t)start, (size_t)end, link_id);
}

/* Name (or clear, with NULL/empty) the bookmark on the caret-holding paragraph.
 * Direct, not undoable — matching the other structural markers. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_set_bookmark(struct yetty_yclass_object *obj,
                                                             const char *name)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_bookmark: data_get");
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc_set_bookmark: active");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    char *copy = NULL;
    if (name && name[0] != '\0') {
        size_t len = strlen(name);
        copy = malloc(len + 1);
        if (!copy) {
            return YETTY_ERR(yetty_ycore_void, "ydoc_set_bookmark: alloc");
        }
        memcpy(copy, name, len + 1);
    }
    free(active.paragraph->bookmark);
    active.paragraph->bookmark = copy;
    return YETTY_OK_VOID();
}

/* Highlight (background) color for the selection — 0 clears it. Applies to the
 * selected range, else the whole active paragraph; document-wide under
 * select-all. Fully undoable via the format snapshot. */
YETTY_ANNOTATE("virtual@yrich:ydoc:ydoc_set_highlight")
static struct yetty_ycore_void_result ydoc_set_highlight_impl(struct yetty_yclass_object *obj,
                                                              uint32_t bg_color)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_highlight: data_get");
    if (data_res.value->select_all) {
        return ydoc_format_all(obj, data_res.value, YDOC_FMT_HIGHLIGHT, bg_color, 0.0f);
    }
    if (data_res.value->sel_span_active) {
        return ydoc_format_span(obj, data_res.value, YDOC_FMT_HIGHLIGHT, bg_color, 0.0f);
    }
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    int32_t selection_lo = active.anchor < active.caret ? active.anchor : active.caret;
    int32_t selection_hi = active.anchor < active.caret ? active.caret : active.anchor;
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, active.paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_set_highlight: snapshot before");
    }
    struct yetty_ycore_void_result apply_res;
    if (selection_lo != selection_hi) {
        apply_res = paragraph_apply_bgcolor_range(active.paragraph, (size_t)selection_lo,
                                                  (size_t)selection_hi, bg_color);
    } else if (active.paragraph->text_len > 0) {
        apply_res = paragraph_apply_bgcolor_range(active.paragraph, 0, active.paragraph->text_len,
                                                  bg_color);
    } else {
        active.paragraph->style.bg_color = bg_color;
        apply_res = YETTY_OK_VOID();
    }
    if (YETTY_IS_ERR(apply_res)) {
        free(before.runs);
        return YETTY_ERR(yetty_ycore_void, "ydoc_set_highlight: apply", apply_res);
    }
    return ydoc_format_commit(obj, &active, &before);
}

/* Clear character formatting (bold/italic/underline/strike, text color, and
 * highlight) from the selection — the whole active paragraph if the caret is
 * collapsed, document-wide under select-all. Fully undoable. */
YETTY_ANNOTATE("virtual@yrich:ydoc:ydoc_clear_format")
static struct yetty_ycore_void_result ydoc_clear_format_impl(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_clear_format: data_get");
    if (data_res.value->select_all) {
        return ydoc_format_all(obj, data_res.value, YDOC_FMT_CLEAR, 0, 0.0f);
    }
    if (data_res.value->sel_span_active) {
        return ydoc_format_span(obj, data_res.value, YDOC_FMT_CLEAR, 0, 0.0f);
    }
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    int32_t selection_lo = active.anchor < active.caret ? active.anchor : active.caret;
    int32_t selection_hi = active.anchor < active.caret ? active.caret : active.anchor;
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, active.paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_clear_format: snapshot before");
    }
    struct yetty_ycore_void_result apply_res;
    if (selection_lo != selection_hi) {
        apply_res = paragraph_clear_format_range(active.paragraph, (size_t)selection_lo,
                                                 (size_t)selection_hi);
    } else if (active.paragraph->text_len > 0) {
        apply_res = paragraph_clear_format_range(active.paragraph, 0, active.paragraph->text_len);
    } else {
        active.paragraph->style.format = YETTY_YRICH_FMT_NONE;
        active.paragraph->style.color = YETTY_YRICH_COLOR_BLACK;
        active.paragraph->style.bg_color = YETTY_YRICH_COLOR_TRANSPARENT;
        apply_res = YETTY_OK_VOID();
    }
    if (YETTY_IS_ERR(apply_res)) {
        free(before.runs);
        return YETTY_ERR(yetty_ycore_void, "ydoc_clear_format: apply", apply_res);
    }
    return ydoc_format_commit(obj, &active, &before);
}

/* Heading levels — 0 = normal text, 1..3 = headings (size + bold base). */
YETTY_ANNOTATE("virtual@yrich:ydoc:ydoc_set_heading")
static struct yetty_ycore_void_result ydoc_set_heading_impl(struct yetty_yclass_object *obj,
                                                            uint32_t level)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_heading: data_get");
    if (data_res.value->select_all) {
        return ydoc_format_all(obj, data_res.value, YDOC_FMT_HEADING, level, 0.0f);
    }
    if (data_res.value->sel_span_active) {
        return ydoc_format_span(obj, data_res.value, YDOC_FMT_HEADING, level, 0.0f);
    }
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, active.paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_set_heading: snapshot before");
    }
    float font_size;
    switch (level) {
    case 1:
        font_size = 26.0f;
        break;
    case 2:
        font_size = 20.0f;
        break;
    case 3:
        font_size = 16.0f;
        break;
    default:
        font_size = 14.0f;
        break;
    }
    active.paragraph->heading_level = level;
    active.paragraph->style.font_size = font_size;
    active.paragraph->line_height = font_size * active.paragraph->line_spacing;
    if (level > 0) {
        active.paragraph->style.format |= YETTY_YRICH_FMT_BOLD;
    } else {
        active.paragraph->style.format &= ~(uint32_t)YETTY_YRICH_FMT_BOLD;
    }
    return ydoc_format_commit(obj, &active, &before);
}

YETTY_ANNOTATE("virtual@yrich:ydoc:ydoc_change_font_size")
static struct yetty_ycore_void_result ydoc_change_font_size_impl(struct yetty_yclass_object *obj,
                                                                 float delta)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_change_font_size: data_get");
    if (data_res.value->select_all) {
        return ydoc_format_all(obj, data_res.value, YDOC_FMT_FONT_DELTA, 0, delta);
    }
    if (data_res.value->sel_span_active) {
        return ydoc_format_span(obj, data_res.value, YDOC_FMT_FONT_DELTA, 0, delta);
    }
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    int32_t selection_lo = active.anchor < active.caret ? active.anchor : active.caret;
    int32_t selection_hi = active.anchor < active.caret ? active.caret : active.anchor;
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, active.paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_change_font_size: snapshot before");
    }
    struct yetty_ycore_void_result apply_res;
    if (selection_lo != selection_hi) {
        /* Resize just the selected run(s) — mixed sizes within the paragraph. */
        apply_res = paragraph_apply_fontsize_range(active.paragraph, (size_t)selection_lo,
                                                   (size_t)selection_hi, delta, 0.0f);
    } else {
        /* No selection: resize the whole paragraph's base style. */
        float font_size = active.paragraph->style.font_size + delta;
        if (font_size < 6.0f) {
            font_size = 6.0f;
        }
        if (font_size > 96.0f) {
            font_size = 96.0f;
        }
        active.paragraph->style.font_size = font_size;
        apply_res = YETTY_OK_VOID();
    }
    if (YETTY_IS_ERR(apply_res)) {
        free(before.runs);
        return YETTY_ERR(yetty_ycore_void, "ydoc_change_font_size: apply", apply_res);
    }
    return ydoc_format_commit(obj, &active, &before);
}

/* Set an absolute font size on the selection (per-run), the whole active
 * paragraph if collapsed, or document-wide under select-all. Undoable. */
YETTY_ANNOTATE("virtual@yrich:ydoc:ydoc_set_font_size")
static struct yetty_ycore_void_result ydoc_set_font_size_impl(struct yetty_yclass_object *obj,
                                                              float size)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_font_size: data_get");
    if (data_res.value->select_all) {
        return ydoc_format_all(obj, data_res.value, YDOC_FMT_SET_FONTSIZE, 0, size);
    }
    if (data_res.value->sel_span_active) {
        return ydoc_format_span(obj, data_res.value, YDOC_FMT_SET_FONTSIZE, 0, size);
    }
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    int32_t selection_lo = active.anchor < active.caret ? active.anchor : active.caret;
    int32_t selection_hi = active.anchor < active.caret ? active.caret : active.anchor;
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, active.paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_set_font_size: snapshot before");
    }
    struct yetty_ycore_void_result apply_res;
    if (selection_lo != selection_hi) {
        apply_res = paragraph_apply_fontsize_range(active.paragraph, (size_t)selection_lo,
                                                   (size_t)selection_hi, 0.0f, size);
    } else {
        active.paragraph->style.font_size = clamp_font_size(size);
        apply_res = YETTY_OK_VOID();
    }
    if (YETTY_IS_ERR(apply_res)) {
        free(before.runs);
        return YETTY_ERR(yetty_ycore_void, "ydoc_set_font_size: apply", apply_res);
    }
    return ydoc_format_commit(obj, &active, &before);
}

/* Toggle the active paragraph's list kind (1=bullet, 2=numbered, 3=checklist);
 * re-applying the same kind clears it. Undoable. Exported; dispatched from
 * the command layer. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_set_list(struct yetty_yclass_object *obj,
                                                         uint32_t kind)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_list: data_get");
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc_set_list: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, active.paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_set_list: snapshot before");
    }
    active.paragraph->list_kind = (active.paragraph->list_kind == kind) ? YDOC_LIST_NONE : kind;
    if (active.paragraph->list_kind != YDOC_LIST_CHECK) {
        /* The checkbox state belongs to the checklist kind — leaving it would
         * resurrect a stale check when the paragraph becomes a checklist
         * again later. Undo restores it from the snapshot. */
        active.paragraph->list_checked = 0;
    }
    return ydoc_format_commit(obj, &active, &before);
}

/* Toggle the active checklist paragraph's checkbox. A no-op (no undo entry)
 * when the active paragraph is not a checklist item. Undoable. Exported;
 * dispatched from the command layer and the gutter click. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_toggle_checked(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_toggle_checked: data_get");
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc_toggle_checked: active paragraph");
    if (!active_res.value || active.paragraph->list_kind != YDOC_LIST_CHECK) {
        return YETTY_OK_VOID();
    }
    struct ydoc_format_snapshot before;
    if (!ydoc_format_snapshot_capture(&before, active.paragraph)) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_toggle_checked: snapshot before");
    }
    active.paragraph->list_checked = !active.paragraph->list_checked;
    return ydoc_format_commit(obj, &active, &before);
}

/*---------------------------------------------------------------------------
 * Exposed editor support API.
 *-------------------------------------------------------------------------*/

/* Place the caret (a collapsed text selection) at byte `position` of the
 * paragraph at `paragraph_index`, clamped to its text. The programmatic
 * counterpart of a mouse click — used by tests, scripting, and bindings. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_place_caret(struct yetty_yclass_object *obj,
                                                            int32_t paragraph_index,
                                                            int32_t position)
{
    struct yetty_yclass_object_ptr_result para_res =
        yetty_yrich_ydoc_paragraph_at(obj, paragraph_index);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, para_res, "ydoc_place_caret: paragraph_at");
    if (!para_res.value) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_place_caret: no such paragraph");
    }
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(para_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_place_caret: data_get");
    if (position < 0) {
        position = 0;
    }
    if ((size_t)position > data_res.value->text_len) {
        position = (int32_t)data_res.value->text_len;
    }
    return ydoc_set_caret(obj, para_res.value, position, position);
}

/* Place the caret at the start of the paragraph carrying bookmark `name`.
 * Returns 1 when a match was found and navigated, 0 when no bookmark matches. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_yrich_ydoc_goto_bookmark(struct yetty_yclass_object *obj,
                                                             const char *name)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ydoc_goto_bookmark: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;
    if (!name || name[0] == '\0') {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        struct yetty_yrich_paragraph_ptr_result para_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        if (YETTY_IS_ERR(para_res)) {
            yetty_ycore_error_destroy(para_res.error);
            continue;
        }
        if (para_res.value->bookmark && strcmp(para_res.value->bookmark, name) == 0) {
            struct yetty_ycore_void_result caret_res =
                yetty_yrich_ydoc_place_caret(obj, (int32_t)i, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, caret_res, "ydoc_goto_bookmark: place_caret");
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

/* Select from (anchor_paragraph, anchor_offset) to (focus_paragraph,
 * focus_offset) — the programmatic counterpart of a cross-paragraph drag.
 * Offsets are clamped; equal paragraphs give a single-paragraph selection. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_select_range(struct yetty_yclass_object *obj,
                                                             int32_t anchor_paragraph,
                                                             int32_t anchor_offset,
                                                             int32_t focus_paragraph,
                                                             int32_t focus_offset)
{
    struct yetty_yclass_object_ptr_result anchor_res =
        yetty_yrich_ydoc_paragraph_at(obj, anchor_paragraph);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, anchor_res, "ydoc_select_range: anchor paragraph");
    struct yetty_yclass_object_ptr_result focus_res =
        yetty_yrich_ydoc_paragraph_at(obj, focus_paragraph);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, focus_res, "ydoc_select_range: focus paragraph");
    if (!anchor_res.value || !focus_res.value) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_select_range: no such paragraph");
    }
    return ydoc_set_caret_range(obj, anchor_res.value, anchor_offset, focus_res.value,
                                focus_offset);
}

/* Drop every element and start over with one empty paragraph (File > New).
 * The undo history is cleared — its ops reference dead elements. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_clear(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_clear: data_get");
    struct yetty_yrich_ydoc *ydoc = data_res.value;

    struct yetty_ycore_void_result end_res = ydoc_end_editing(obj, ydoc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, end_res, "ydoc_clear: end editing");

    while (ydoc->paragraph_count > 0) {
        struct yetty_yclass_object *paragraph_obj = ydoc->paragraphs[ydoc->paragraph_count - 1];
        struct yetty_yrich_element_id_result id_res = yetty_yrich_element_id_value(paragraph_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "ydoc_clear: paragraph id");
        ydoc->paragraph_count--;
        struct yetty_ycore_void_result remove_res =
            yetty_yrich_document_remove_element(obj, id_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remove_res, "ydoc_clear: remove paragraph");
    }
    while (ydoc->image_count > 0) {
        struct yetty_yclass_object *image_obj = ydoc->images[ydoc->image_count - 1];
        struct yetty_yrich_element_id_result id_res = yetty_yrich_element_id_value(image_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "ydoc_clear: image id");
        ydoc->image_count--;
        struct yetty_ycore_void_result remove_res =
            yetty_yrich_document_remove_element(obj, id_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remove_res, "ydoc_clear: remove image");
    }
    struct yetty_ycore_void_result history_res = yetty_yrich_document_clear_history(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, history_res, "ydoc_clear: clear history");

    struct yetty_yclass_object_ptr_result paragraph_res =
        yetty_yrich_ydoc_add_paragraph(obj, "", 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, paragraph_res, "ydoc_clear: seed paragraph");
    return ydoc_set_caret(obj, paragraph_res.value, 0, 0);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_ydoc_set_source_path(struct yetty_yclass_object *obj,
                                                                const char *path)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_source_path: data_get");
    char *copy = NULL;
    if (path) {
        size_t path_len = strlen(path);
        copy = malloc(path_len + 1);
        if (!copy) {
            return YETTY_ERR(yetty_ycore_void, "ydoc_set_source_path: alloc failed");
        }
        memcpy(copy, path, path_len + 1);
    }
    free(data_res.value->source_path);
    data_res.value->source_path = copy;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_const_char_ptr_result yetty_yrich_ydoc_source_path(
    struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, data_res, "ydoc_source_path: data_get");
    return YETTY_OK(yetty_ycore_const_char_ptr, data_res.value->source_path);
}

/*---------------------------------------------------------------------------
 * Read accessors for the YAML writer and other consumers — the data
 * slices are private to this TU.
 *-------------------------------------------------------------------------*/

YETTY_ANNOTATE("expose")
struct yetty_ycore_float_result yetty_yrich_ydoc_page_width(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "ydoc_page_width: data_get");
    return YETTY_OK(yetty_ycore_float, data_res.value->page_width);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_float_result yetty_yrich_ydoc_margin(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "ydoc_margin: data_get");
    return YETTY_OK(yetty_ycore_float, data_res.value->margin);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_size_result yetty_yrich_ydoc_paragraph_count(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, data_res, "ydoc_paragraph_count: data_get");
    return YETTY_OK(yetty_ycore_size, data_res.value->paragraph_count);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_const_char_ptr_result yetty_yrich_paragraph_text(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, data_res, "paragraph_text: data_get");
    return YETTY_OK(yetty_ycore_const_char_ptr, data_res.value->text ? data_res.value->text : "");
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_size_result yetty_yrich_paragraph_text_len(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, data_res, "paragraph_text_len: data_get");
    return YETTY_OK(yetty_ycore_size, data_res.value->text_len);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_float_result yetty_yrich_paragraph_font_size(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "paragraph_font_size: data_get");
    return YETTY_OK(yetty_ycore_float, data_res.value->style.font_size);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_float_result yetty_yrich_paragraph_line_spacing(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "paragraph_line_spacing: data_get");
    return YETTY_OK(yetty_ycore_float, data_res.value->line_spacing);
}

/* Loader-side setter: set the multiplier and recompute line_height. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_line_spacing(
    struct yetty_yclass_object *obj, float spacing)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_line_spacing: data_get");
    data_res.value->line_spacing = spacing;
    data_res.value->line_height = data_res.value->style.font_size * spacing;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_float_result yetty_yrich_paragraph_indent(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "paragraph_indent: data_get");
    return YETTY_OK(yetty_ycore_float, data_res.value->indent);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_yrich_paragraph_heading_level(
    struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, data_res, "paragraph_heading_level: data_get");
    return YETTY_OK(yetty_ycore_uint32, data_res.value->heading_level);
}

/* Loader-side: set the semantic level only. The visual (font size, bold) is
 * serialized independently and already restored from fontSize/format. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_heading_level(
    struct yetty_yclass_object *obj, uint32_t level)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_heading_level: data_get");
    data_res.value->heading_level = level;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_indent(struct yetty_yclass_object *obj,
                                                                float indent)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_indent: data_get");
    data_res.value->indent = indent < 0.0f ? 0.0f : indent;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_yrich_paragraph_color(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, data_res, "paragraph_color: data_get");
    return YETTY_OK(yetty_ycore_uint32, data_res.value->style.color);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_yrich_paragraph_format(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, data_res, "paragraph_format: data_get");
    return YETTY_OK(yetty_ycore_uint32, data_res.value->style.format);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_yrich_paragraph_alignment(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, data_res, "paragraph_alignment: data_get");
    return YETTY_OK(yetty_ycore_uint32, data_res.value->halign);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_alignment(struct yetty_yclass_object *obj,
                                                                   uint32_t halign)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_alignment: data_get");
    data_res.value->halign = halign;
    return YETTY_OK_VOID();
}

/* List kind (0 none, 1 bullet, 2 numbered, 3 checklist) + the checklist
 * checkbox state — loader/saver side; editing goes through ydoc_set_list
 * and ydoc_toggle_checked. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_yrich_paragraph_list_kind(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, data_res, "paragraph_list_kind: data_get");
    return YETTY_OK(yetty_ycore_uint32, data_res.value->list_kind);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_list_kind(struct yetty_yclass_object *obj,
                                                                   uint32_t list_kind)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_list_kind: data_get");
    data_res.value->list_kind = list_kind;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_yrich_paragraph_list_checked(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, data_res, "paragraph_list_checked: data_get");
    return YETTY_OK(yetty_ycore_uint32, (uint32_t)data_res.value->list_checked);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_list_checked(
    struct yetty_yclass_object *obj, uint32_t checked)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_list_checked: data_get");
    data_res.value->list_checked = checked ? 1 : 0;
    return YETTY_OK_VOID();
}

/* Block kind (0 text, 1 horizontal-rule divider) — loader/saver side. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_yrich_paragraph_block_kind(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, data_res, "paragraph_block_kind: data_get");
    return YETTY_OK(yetty_ycore_uint32, data_res.value->block_kind);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_block_kind(struct yetty_yclass_object *obj,
                                                                    uint32_t block_kind)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_block_kind: data_get");
    data_res.value->block_kind = block_kind;
    paragraph_recompute_height(data_res.value);
    return YETTY_OK_VOID();
}

/* Table dimensions/cells — loader/saver side; editing is via the ydoc. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_table_size(struct yetty_yclass_object *obj,
                                                                uint32_t *out_rows,
                                                                uint32_t *out_cols)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_table_size: data_get");
    if (out_rows) {
        *out_rows = data_res.value->table_rows;
    }
    if (out_cols) {
        *out_cols = data_res.value->table_cols;
    }
    return YETTY_OK_VOID();
}

/* Allocate the cell grid (clears any prior one). block_kind is set to table. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_table(struct yetty_yclass_object *obj,
                                                               uint32_t rows, uint32_t cols)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_table: data_get");
    struct yetty_yrich_paragraph *paragraph = data_res.value;
    if (rows == 0 || cols == 0 || rows > 50 || cols > 20) {
        return YETTY_ERR(yetty_ycore_void, "paragraph_set_table: bad dimensions");
    }
    if (paragraph->table_cells) {
        uint32_t old_cells = paragraph->table_rows * paragraph->table_cols;
        for (uint32_t i = 0; i < old_cells; i++) {
            free(paragraph->table_cells[i]);
        }
        free(paragraph->table_cells);
    }
    paragraph->table_cells = calloc((size_t)rows * cols, sizeof(*paragraph->table_cells));
    if (!paragraph->table_cells) {
        paragraph->table_rows = 0;
        paragraph->table_cols = 0;
        return YETTY_ERR(yetty_ycore_void, "paragraph_set_table: cells alloc");
    }
    paragraph->table_rows = rows;
    paragraph->table_cols = cols;
    paragraph->table_active_cell = -1;
    paragraph->block_kind = YDOC_BLOCK_TABLE;
    paragraph_recompute_height(paragraph);
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_const_char_ptr_result yetty_yrich_paragraph_table_cell(
    struct yetty_yclass_object *obj, uint32_t row, uint32_t col)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, data_res, "paragraph_table_cell: data_get");
    struct yetty_yrich_paragraph *paragraph = data_res.value;
    if (!paragraph->table_cells || row >= paragraph->table_rows || col >= paragraph->table_cols) {
        return YETTY_OK(yetty_ycore_const_char_ptr, NULL);
    }
    return YETTY_OK(yetty_ycore_const_char_ptr,
                    paragraph->table_cells[row * paragraph->table_cols + col]);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_table_cell(struct yetty_yclass_object *obj,
                                                                    uint32_t row, uint32_t col,
                                                                    const char *text)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_table_cell: data_get");
    struct yetty_yrich_paragraph *paragraph = data_res.value;
    if (!paragraph->table_cells || row >= paragraph->table_rows || col >= paragraph->table_cols) {
        return YETTY_ERR(yetty_ycore_void, "paragraph_set_table_cell: out of range");
    }
    char **slot = &paragraph->table_cells[row * paragraph->table_cols + col];
    free(*slot);
    *slot = NULL;
    if (text && text[0] != '\0') {
        size_t len = strlen(text);
        char *copy = malloc(len + 1);
        if (!copy) {
            return YETTY_ERR(yetty_ycore_void, "paragraph_set_table_cell: alloc");
        }
        memcpy(copy, text, len + 1);
        *slot = copy;
    }
    return YETTY_OK_VOID();
}

/* Paragraph spacing (px above/below) + list nesting level — loader/saver side. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_float_result yetty_yrich_paragraph_space_before(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "paragraph_space_before: data_get");
    return YETTY_OK(yetty_ycore_float, data_res.value->space_before);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_space_before(
    struct yetty_yclass_object *obj, float px)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_space_before: data_get");
    data_res.value->space_before = px < 0.0f ? 0.0f : px;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_float_result yetty_yrich_paragraph_space_after(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "paragraph_space_after: data_get");
    return YETTY_OK(yetty_ycore_float, data_res.value->space_after);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_space_after(
    struct yetty_yclass_object *obj, float px)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_space_after: data_get");
    data_res.value->space_after = px < 0.0f ? 0.0f : px;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_yrich_paragraph_list_level(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, data_res, "paragraph_list_level: data_get");
    return YETTY_OK(yetty_ycore_uint32, data_res.value->list_level);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_list_level(struct yetty_yclass_object *obj,
                                                                    uint32_t level)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_list_level: data_get");
    data_res.value->list_level = level > 7 ? 7 : level;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_size_result yetty_yrich_paragraph_run_count(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, data_res, "paragraph_run_count: data_get");
    return YETTY_OK(yetty_ycore_size, data_res.value->run_count);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_run_get(
    struct yetty_yclass_object *obj, size_t index, int32_t *out_start, int32_t *out_end,
    uint32_t *out_format, uint32_t *out_color, uint32_t *out_bg_color, float *out_font_size)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_run_get: data_get");
    struct yetty_yrich_paragraph *paragraph = data_res.value;
    if (index >= paragraph->run_count || !out_start || !out_end || !out_format || !out_color ||
        !out_bg_color || !out_font_size) {
        return YETTY_ERR(yetty_ycore_void, "paragraph_run_get: bad index / NULL out");
    }
    const struct yetty_yrich_text_run *run = &paragraph->runs[index];
    *out_start = run->start;
    *out_end = run->end;
    *out_format = run->style.format;
    *out_color = run->style.color;
    *out_bg_color = run->style.bg_color;
    *out_font_size = run->style.font_size;
    return YETTY_OK_VOID();
}

/* Hyperlink id of run `index` (0 = no link); resolves to a URL via the owning
 * ydoc's link table. Serializer side. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_yrich_paragraph_run_link_id(struct yetty_yclass_object *obj,
                                                                   size_t index)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, data_res, "paragraph_run_link_id: data_get");
    struct yetty_yrich_paragraph *paragraph = data_res.value;
    if (index >= paragraph->run_count) {
        return YETTY_ERR(yetty_ycore_uint32, "paragraph_run_link_id: bad index");
    }
    return YETTY_OK(yetty_ycore_uint32, paragraph->runs[index].link_id);
}

/* Name the bookmark anchored at this paragraph (NULL/empty clears it). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_set_bookmark(struct yetty_yclass_object *obj,
                                                                  const char *name)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_set_bookmark: data_get");
    struct yetty_yrich_paragraph *paragraph = data_res.value;
    char *copy = NULL;
    if (name && name[0] != '\0') {
        size_t len = strlen(name);
        copy = malloc(len + 1);
        if (!copy) {
            return YETTY_ERR(yetty_ycore_void, "paragraph_set_bookmark: alloc");
        }
        memcpy(copy, name, len + 1);
    }
    free(paragraph->bookmark);
    paragraph->bookmark = copy;
    return YETTY_OK_VOID();
}

/* Bookmark name anchoring this paragraph, or NULL when none. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_const_char_ptr_result yetty_yrich_paragraph_bookmark(
    struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, data_res, "paragraph_bookmark: data_get");
    return YETTY_OK(yetty_ycore_const_char_ptr, data_res.value->bookmark);
}

/* Append one styled run — loader-side; ranges must arrive sorted and
 * non-overlapping (the writer emits them that way). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_add_run(struct yetty_yclass_object *obj,
                                                             int32_t start, int32_t end,
                                                             uint32_t format, uint32_t color,
                                                             uint32_t bg_color, float font_size)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_add_run: data_get");
    struct yetty_yrich_paragraph *paragraph = data_res.value;
    if (paragraph->run_count == paragraph->run_capacity) {
        size_t new_cap = paragraph->run_capacity ? paragraph->run_capacity * 2 : 4;
        struct yetty_yrich_text_run *new_runs =
            realloc(paragraph->runs, new_cap * sizeof(*new_runs));
        if (!new_runs) {
            return YETTY_ERR(yetty_ycore_void, "paragraph_add_run: grow failed");
        }
        paragraph->runs = new_runs;
        paragraph->run_capacity = new_cap;
    }
    struct yetty_yrich_text_run *run = &paragraph->runs[paragraph->run_count++];
    run->start = start;
    run->end = end;
    run->style = paragraph->style;
    run->style.format = format;
    run->style.color = color;
    run->style.bg_color = bg_color;
    run->style.font_size = font_size > 0.0f ? font_size : paragraph->style.font_size;
    run->link_id = 0;
    return YETTY_OK_VOID();
}

#include "ydoc.gen.c"
