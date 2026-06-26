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
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <stdlib.h>
#include <string.h>

#define YDOC_DEFAULT_PAGE_WIDTH 600.0f
#define YDOC_DEFAULT_MARGIN 20.0f
#define YDOC_DEFAULT_LINE_HEIGHT 20.0f

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

    float line_height;
    uint32_t halign; /* enum yetty_yrich_halign */

    int editing;
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

static float paragraph_char_width(const struct yetty_yrich_paragraph *paragraph)
{
    return paragraph->style.font_size * 0.6f;
}

static size_t paragraph_max_cols(const struct yetty_yrich_paragraph *paragraph)
{
    float char_width = paragraph_char_width(paragraph);
    if (char_width <= 0.0f || paragraph->bounds.w <= char_width) {
        return 1;
    }
    return (size_t)(paragraph->bounds.w / char_width);
}

/* Compute the line starting at `start` — greedy break at the last space
 * that fits, hard break when a word exceeds the width, explicit '\n'
 * always ends a line. */
static void paragraph_line_from(const struct yetty_yrich_paragraph *paragraph, size_t start,
                                struct paragraph_line *out_line)
{
    size_t max_cols = paragraph_max_cols(paragraph);
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
        if (pos - start >= max_cols) {
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

/* Horizontal offset of a line inside the paragraph box per alignment. */
static float paragraph_line_x_offset(const struct yetty_yrich_paragraph *paragraph, size_t line_len)
{
    float text_width = (float)line_len * paragraph_char_width(paragraph);
    float slack = paragraph->bounds.w - text_width;
    if (slack <= 0.0f) {
        return 0.0f;
    }
    switch (paragraph->halign) {
    case YETTY_YRICH_HALIGN_CENTER:
        return slack * 0.5f;
    case YETTY_YRICH_HALIGN_RIGHT:
        return slack;
    default:
        return 0.0f;
    }
}

/*---------------------------------------------------------------------------
 * Attribute engine — per-character format/color resolved from the base
 * style plus the run list. Mutations decompress to a per-char array,
 * edit it, and recompress to runs; O(text_len), trivial at typing rates.
 *-------------------------------------------------------------------------*/

struct char_attrs {
    uint32_t format;
    uint32_t color;
};

static int char_attrs_equal(struct char_attrs a, struct char_attrs b)
{
    return a.format == b.format && a.color == b.color;
}

static struct char_attrs paragraph_base_attrs(const struct yetty_yrich_paragraph *paragraph)
{
    struct char_attrs attrs = {paragraph->style.format, paragraph->style.color};
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
        i = span_end;
    }
    paragraph->run_count = run_index;
    return YETTY_OK_VOID();
}

/* Toggle `format_flag` across [lo, hi): if every char already carries the
 * flag, clear it; otherwise set it (the Google-docs toggle rule). */
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
    float line_offset = paragraph_line_x_offset(paragraph, line.end - line.start);
    float char_width = paragraph_char_width(paragraph);
    float relative_x = x - paragraph->bounds.x - line_offset;
    long column = char_width > 0.0f ? (long)((relative_x + char_width * 0.5f) / char_width) : 0;
    if (column < 0) {
        column = 0;
    }
    return paragraph_caret_at(paragraph, (size_t)line_index, (size_t)column);
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
    float char_width = paragraph_char_width(paragraph);
    float line_top = paragraph->bounds.y + (float)line_index * paragraph->line_height;
    float baseline = line_top + paragraph->style.font_size;
    size_t line_len = line->end - line->start;
    float line_left = paragraph->bounds.x + paragraph_line_x_offset(paragraph, line_len);

    /* Selection wash behind the text. */
    if (paragraph->editing && paragraph->sel_start != paragraph->sel_end) {
        int32_t selection_lo =
            paragraph->sel_start < paragraph->sel_end ? paragraph->sel_start : paragraph->sel_end;
        int32_t selection_hi =
            paragraph->sel_start < paragraph->sel_end ? paragraph->sel_end : paragraph->sel_start;
        size_t overlap_lo = (size_t)selection_lo > line->start ? (size_t)selection_lo : line->start;
        size_t overlap_hi = (size_t)selection_hi < line->end ? (size_t)selection_hi : line->end;
        if (overlap_lo < overlap_hi) {
            float x0 = line_left + (float)(overlap_lo - line->start) * char_width;
            float x1 = line_left + (float)(overlap_hi - line->start) * char_width;
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
        size_t segment_end = segment_start + 1;
        while (segment_end < line->end &&
               char_attrs_equal(paragraph_attrs_at(paragraph, segment_end), attrs)) {
            segment_end++;
        }
        size_t segment_len = segment_end - segment_start;
        float segment_x = line_left + (float)(segment_start - line->start) * char_width;
        float segment_end_x = segment_x + (float)segment_len * char_width;

        struct yetty_ycore_buffer text = {
            .data = (uint8_t *)(paragraph->text + segment_start),
            .size = segment_len,
            .capacity = segment_len,
        };
        struct yetty_ycore_void_result text_res = yetty_ydraw_drawable_list_add_text(
            pass->drawable_list, segment_x, baseline, &text, paragraph->style.font_size,
            attrs.color, pass->layer + 1, paragraph->style.font_id, 0.0f);
        if (YETTY_IS_ERR(text_res)) {
            pass->status = YETTY_ERR(yetty_ycore_void, "paragraph_render: add_text", text_res);
            return 1;
        }
        if (attrs.format & YETTY_YRICH_FMT_BOLD) {
            /* Poor man's bold — re-draw with a sub-pixel x offset. */
            struct yetty_ycore_void_result bold_res = yetty_ydraw_drawable_list_add_text(
                pass->drawable_list, segment_x + 0.6f, baseline, &text, paragraph->style.font_size,
                attrs.color, pass->layer + 1, paragraph->style.font_id, 0.0f);
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
        if (attrs.format & YETTY_YRICH_FMT_STRIKE) {
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
        float caret_x = paragraph->bounds.x + paragraph_line_x_offset(paragraph, place.line_len) +
                        (float)place.column * paragraph_char_width(paragraph);
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
        paragraph->line_height = font_size * 1.4f;
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
    /* The document base frees the elements and the object itself. */
    return yetty_yrich_super_void(obj, yetty_yrich_ydoc_class_get().value,
                                  (yetty_yclass_method_id_t)yetty_yrich_document_destroy);
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
    for (size_t i = 0; i < ydoc->paragraph_count; i++) {
        struct yetty_yrich_paragraph_ptr_result paragraph_res =
            yetty_yrich_paragraph_from(ydoc->paragraphs[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, paragraph_res, "ydoc_relayout: paragraph");
        struct yetty_yrich_paragraph *paragraph = paragraph_res.value;
        paragraph->bounds.x = ydoc->margin;
        paragraph->bounds.w = content_width;
        paragraph->bounds.y = y;
        paragraph_recompute_height(paragraph);
        y += paragraph->bounds.h;
    }
    return YETTY_OK_VOID();
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
    struct yetty_yclass_object_ptr_result paragraph_obj_res =
        yetty_yrich_document_find(obj, selection->u.text.element_id);
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
    out_active->id = selection->u.text.element_id;
    out_active->anchor = selection->u.text.start;
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

/*---------------------------------------------------------------------------
 * Input overrides.
 *-------------------------------------------------------------------------*/

/* Formatting slot impls — defined below, used by the keyboard shortcuts. */
static struct yetty_ycore_void_result ydoc_toggle_format_impl(struct yetty_yclass_object *obj,
                                                              uint32_t format_flag);
static struct yetty_ycore_void_result ydoc_change_font_size_impl(struct yetty_yclass_object *obj,
                                                                 float delta);

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

    int32_t caret = paragraph_caret_from_point(paragraph_res.value, x, y);
    int32_t anchor = caret;
    if (mods & YETTY_YRICH_MOD_SHIFT) {
        struct ydoc_active_paragraph active;
        struct yetty_ycore_int_result active_res = ydoc_active_paragraph_get(obj, ydoc, &active);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc_on_mouse_down: active paragraph");
        if (active_res.value && active.paragraph_obj == hit_obj) {
            anchor = active.anchor;
        }
    }
    return ydoc_set_caret(obj, hit_obj, anchor, caret);
}

YETTY_ANNOTATE("override@yrich:ydoc:document_on_mouse_drag")
static struct yetty_ycore_void_result ydoc_on_mouse_drag(struct yetty_yclass_object *obj, float x,
                                                         float y, uint32_t button, uint32_t mods)
{
    (void)button;
    (void)mods;
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_on_mouse_drag: data_get");
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc_on_mouse_drag: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    int32_t caret = paragraph_caret_from_point(active.paragraph, x, y);
    return ydoc_set_caret(obj, active.paragraph_obj, active.anchor, caret);
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
        case YETTY_YRICH_KEY_A:
            if (have_active) {
                return ydoc_set_caret(obj, active.paragraph_obj, 0,
                                      (int32_t)active.paragraph->text_len);
            }
            return YETTY_OK_VOID();
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
    int32_t selection_lo = active.anchor < active.caret ? active.anchor : active.caret;
    int32_t selection_hi = active.anchor < active.caret ? active.caret : active.anchor;

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
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
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
    struct yetty_yclass_object *hit_obj = yetty_yrich_document_element_at(obj, x, y);
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
    int32_t word_start = word_jump_left(paragraph, caret);
    int32_t word_end = word_jump_right(paragraph, word_start);
    if (word_end < caret) {
        /* Clicked a gap between words — keep the caret only. */
        word_start = word_end = caret;
    }
    return ydoc_set_caret(obj, hit_obj, word_start, word_end);
}

/* Selected text as a fresh heap string (caller frees). NULL when the
 * selection is empty or not a text selection. */
YETTY_ANNOTATE("expose")
char *yetty_yrich_ydoc_selection_text(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return NULL;
    }
    struct ydoc_active_paragraph active;
    if (!ydoc_active_paragraph_get(obj, data_res.value, &active)) {
        return NULL;
    }
    int32_t selection_lo = active.anchor < active.caret ? active.anchor : active.caret;
    int32_t selection_hi = active.anchor < active.caret ? active.caret : active.anchor;
    if (selection_lo >= selection_hi) {
        return NULL;
    }
    return dup_text_range(active.paragraph->text, (size_t)selection_lo,
                          (size_t)(selection_hi - selection_lo));
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
 * Formatting slots — wire-marshallable (scalars only).
 *-------------------------------------------------------------------------*/

YETTY_ANNOTATE("virtual@yrich:ydoc:ydoc_toggle_format")
static struct yetty_ycore_void_result ydoc_toggle_format_impl(struct yetty_yclass_object *obj,
                                                              uint32_t format_flag)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_toggle_format: data_get");
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
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
    YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "ydoc_toggle_format: apply");
    return yetty_yrich_document_mark_dirty(obj);
}

/* Text colour — selection-scoped like toggle_format, absolute (no toggle). */
YETTY_ANNOTATE("virtual@yrich:ydoc:ydoc_set_text_color")
static struct yetty_ycore_void_result ydoc_set_text_color_impl(struct yetty_yclass_object *obj,
                                                               uint32_t color)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_text_color: data_get");
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
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
    YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "ydoc_set_text_color: apply");
    return yetty_yrich_document_mark_dirty(obj);
}

/* Paragraph alignment (enum yetty_yrich_halign). */
YETTY_ANNOTATE("virtual@yrich:ydoc:ydoc_set_alignment")
static struct yetty_ycore_void_result ydoc_set_alignment_impl(struct yetty_yclass_object *obj,
                                                              uint32_t halign)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_alignment: data_get");
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    active.paragraph->halign = halign;
    return yetty_yrich_document_mark_dirty(obj);
}

/* Heading levels — 0 = normal text, 1..3 = headings (size + bold base). */
YETTY_ANNOTATE("virtual@yrich:ydoc:ydoc_set_heading")
static struct yetty_ycore_void_result ydoc_set_heading_impl(struct yetty_yclass_object *obj,
                                                            uint32_t level)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_set_heading: data_get");
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
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
    active.paragraph->style.font_size = font_size;
    active.paragraph->line_height = font_size * 1.4f;
    if (level > 0) {
        active.paragraph->style.format |= YETTY_YRICH_FMT_BOLD;
    } else {
        active.paragraph->style.format &= ~(uint32_t)YETTY_YRICH_FMT_BOLD;
    }
    struct yetty_ycore_void_result relayout_res = ydoc_relayout(data_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc_set_heading: relayout");
    return yetty_yrich_document_mark_dirty(obj);
}

YETTY_ANNOTATE("virtual@yrich:ydoc:ydoc_change_font_size")
static struct yetty_ycore_void_result ydoc_change_font_size_impl(struct yetty_yclass_object *obj,
                                                                 float delta)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ydoc_change_font_size: data_get");
    struct ydoc_active_paragraph active;
    struct yetty_ycore_int_result active_res =
        ydoc_active_paragraph_get(obj, data_res.value, &active);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, active_res, "ydoc: active paragraph");
    if (!active_res.value) {
        return YETTY_OK_VOID();
    }
    float font_size = active.paragraph->style.font_size + delta;
    if (font_size < 6.0f) {
        font_size = 6.0f;
    }
    if (font_size > 96.0f) {
        font_size = 96.0f;
    }
    active.paragraph->style.font_size = font_size;
    active.paragraph->line_height = font_size * 1.4f;
    struct yetty_ycore_void_result relayout_res = ydoc_relayout(data_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, relayout_res, "ydoc_change_font_size: relayout");
    return yetty_yrich_document_mark_dirty(obj);
}

/*---------------------------------------------------------------------------
 * Exposed editor support API.
 *-------------------------------------------------------------------------*/

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
const char *yetty_yrich_ydoc_source_path(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_ydoc_ptr_result data_res = yetty_yrich_ydoc_from(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return NULL;
    }
    return data_res.value->source_path;
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
const char *yetty_yrich_paragraph_text(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return "";
    }
    return data_res.value->text ? data_res.value->text : "";
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

YETTY_ANNOTATE("expose")
struct yetty_ycore_size_result yetty_yrich_paragraph_run_count(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, data_res, "paragraph_run_count: data_get");
    return YETTY_OK(yetty_ycore_size, data_res.value->run_count);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_run_get(struct yetty_yclass_object *obj,
                                                             size_t index, int32_t *out_start,
                                                             int32_t *out_end, uint32_t *out_format,
                                                             uint32_t *out_color)
{
    struct yetty_yrich_paragraph_ptr_result data_res = yetty_yrich_paragraph_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "paragraph_run_get: data_get");
    struct yetty_yrich_paragraph *paragraph = data_res.value;
    if (index >= paragraph->run_count || !out_start || !out_end || !out_format || !out_color) {
        return YETTY_ERR(yetty_ycore_void, "paragraph_run_get: bad index / NULL out");
    }
    const struct yetty_yrich_text_run *run = &paragraph->runs[index];
    *out_start = run->start;
    *out_end = run->end;
    *out_format = run->style.format;
    *out_color = run->style.color;
    return YETTY_OK_VOID();
}

/* Append one styled run — loader-side; ranges must arrive sorted and
 * non-overlapping (the writer emits them that way). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yrich_paragraph_add_run(struct yetty_yclass_object *obj,
                                                             int32_t start, int32_t end,
                                                             uint32_t format, uint32_t color)
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
    return YETTY_OK_VOID();
}

#include "ydoc.gen.c"
