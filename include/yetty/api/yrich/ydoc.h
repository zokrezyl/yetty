/* GENERATED — do not edit. */
/* Object API for regular class(es) `paragraph, inline_image, ydoc` (implementation module: yrich).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YRICH_YDOC_H
#define YETTY_YCLASSGEN_API_YRICH_YDOC_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrich/yrich-types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yrich_ydoc;

struct yetty_yclass_ptr_result yetty_yrich_paragraph_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_inline_image_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_ydoc_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yrich_paragraph;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YRICH_PARAGRAPH_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YRICH_PARAGRAPH_PTR_RESULT
struct yetty_yrich_paragraph_ptr_result {
    int ok;
    union {
        struct yetty_yrich_paragraph *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yrich_paragraph_ptr_result yetty_yrich_paragraph_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_paragraph_to(struct yetty_yrich_paragraph *data);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yrich_inline_image;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YRICH_INLINE_IMAGE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YRICH_INLINE_IMAGE_PTR_RESULT
struct yetty_yrich_inline_image_ptr_result {
    int ok;
    union {
        struct yetty_yrich_inline_image *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yrich_inline_image_ptr_result yetty_yrich_inline_image_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_inline_image_to(
    struct yetty_yrich_inline_image *data);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yrich_ydoc;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YRICH_YDOC_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YRICH_YDOC_PTR_RESULT
struct yetty_yrich_ydoc_ptr_result {
    int ok;
    union {
        struct yetty_yrich_ydoc *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yrich_ydoc_ptr_result yetty_yrich_ydoc_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_to(struct yetty_yrich_ydoc *data);

struct yetty_ycore_void_result yetty_yrich_ydoc_toggle_format(struct yetty_yclass_object *obj,
                                                              uint32_t format_flag);
/* Text colour — selection-scoped like toggle_format, absolute (no toggle). */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_text_color(struct yetty_yclass_object *obj,
                                                               uint32_t color);
/* Paragraph alignment (enum yetty_yrich_halign). */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_alignment(struct yetty_yclass_object *obj,
                                                              uint32_t halign);
/* Line-spacing multiplier for the active paragraph (document-wide under
 * select-all): 1.0 single, 1.5, 2.0 double. Recomputes line_height and is
 * fully undoable. */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_line_spacing(struct yetty_yclass_object *obj,
                                                                 float spacing);
/* Shift the active paragraph's left indent by one step (positive = increase,
 * negative = decrease; clamped at 0). Document-wide under select-all; undoable. */
struct yetty_ycore_void_result yetty_yrich_ydoc_adjust_indent(struct yetty_yclass_object *obj,
                                                              int32_t direction);
/* Highlight (background) color for the selection — 0 clears it. Applies to the
 * selected range, else the whole active paragraph; document-wide under
 * select-all. Fully undoable via the format snapshot. */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_highlight(struct yetty_yclass_object *obj,
                                                              uint32_t bg_color);
/* Clear character formatting (bold/italic/underline/strike, text color, and
 * highlight) from the selection — the whole active paragraph if the caret is
 * collapsed, document-wide under select-all. Fully undoable. */
struct yetty_ycore_void_result yetty_yrich_ydoc_clear_format(struct yetty_yclass_object *obj);
/* Heading levels — 0 = normal text, 1..3 = headings (size + bold base). */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_heading(struct yetty_yclass_object *obj,
                                                            uint32_t level);
struct yetty_ycore_void_result yetty_yrich_ydoc_change_font_size(struct yetty_yclass_object *obj,
                                                                 float delta);
/* Set an absolute font size on the selection (per-run), the whole active
 * paragraph if collapsed, or document-wide under select-all. Undoable. */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_font_size(struct yetty_yclass_object *obj,
                                                              float size);

struct yetty_yclass_object_ptr_result yetty_yrich_paragraph_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_inline_image_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yrich_paragraph_set_text(struct yetty_yclass_object *obj,
                                                              const char *text, size_t len);
/* Style setters — the data slice is private to this TU; the yaml loader and
 * future bindings reach the style through these. */
struct yetty_ycore_void_result yetty_yrich_paragraph_set_font_size(struct yetty_yclass_object *obj,
                                                                   float font_size);
struct yetty_ycore_void_result yetty_yrich_paragraph_set_color(struct yetty_yclass_object *obj,
                                                               uint32_t color);
struct yetty_ycore_void_result yetty_yrich_paragraph_set_format(struct yetty_yclass_object *obj,
                                                                uint32_t format);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_page_width(struct yetty_yclass_object *obj,
                                                               float width);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_margin(struct yetty_yclass_object *obj,
                                                           float margin);
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_add_paragraph(
    struct yetty_yclass_object *obj, const char *text, size_t text_len);
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_paragraph_at(struct yetty_yclass_object *obj,
                                                                    int32_t index);
/* Set the inline image's source (a file path decoded at render time). NULL/empty
 * clears it. */
struct yetty_ycore_void_result yetty_yrich_inline_image_set_source(struct yetty_yclass_object *obj,
                                                                   const char *source);
/* The inline image's source path, or NULL when unset. */
struct yetty_ycore_const_char_ptr_result yetty_yrich_inline_image_source(
    struct yetty_yclass_object *obj);
/* Set the image's document-space bounds (position + display size). */
struct yetty_ycore_void_result yetty_yrich_inline_image_set_bounds(struct yetty_yclass_object *obj,
                                                                   float x, float y, float width,
                                                                   float height);
/* Read the image's document-space bounds. Any out pointer may be NULL. */
struct yetty_ycore_void_result yetty_yrich_inline_image_bounds(struct yetty_yclass_object *obj,
                                                               float *out_x, float *out_y,
                                                               float *out_width, float *out_height);
/* Number of inline images in the document (serializer / enumeration side). */
struct yetty_ycore_size_result yetty_yrich_ydoc_image_count(struct yetty_yclass_object *obj);
/* The inline image at `index`, or NULL when out of range. */
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_image_at(struct yetty_yclass_object *obj,
                                                                int32_t index);
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_insert_image(struct yetty_yclass_object *obj,
                                                                    int32_t paragraph_index,
                                                                    float width, float height);
/* Declare which styled faces the render ygrid has registered, as a bitmask
 * (bit 0 = bold at font slot 1, bit 1 = italic at slot 2, bit 2 = bold-italic at
 * slot 3). The host calls this after wiring the styled fonts so ydoc renders
 * bold/italic runs with the real face instead of the synthetic-bold fallback. */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_styled_font_mask(
    struct yetty_yclass_object *obj, uint32_t mask);
/* Toggle the space-middot / paragraph-pilcrow marks (a view preference, not
 * document content). Exposed; wired to Format > Show nonprinting characters. */
struct yetty_ycore_void_result yetty_yrich_ydoc_toggle_nonprinting(struct yetty_yclass_object *obj);
/* Insert a horizontal-rule divider at the caret. Exposed; Insert > Horizontal
 * rule dispatches here. */
struct yetty_ycore_void_result yetty_yrich_ydoc_insert_horizontal_rule(
    struct yetty_yclass_object *obj);
/* Insert a page-break marker at the caret. Exposed; Insert > Page break
 * dispatches here. */
struct yetty_ycore_void_result yetty_yrich_ydoc_insert_page_break(struct yetty_yclass_object *obj);
/* Insert a rows x cols table after the caret, leaving the caret on a fresh
 * text line below it. Cells start empty; click a cell to edit it. Structural,
 * direct (not undoable). Exposed; Insert > Table dispatches here. */
struct yetty_ycore_void_result yetty_yrich_ydoc_insert_table(struct yetty_yclass_object *obj,
                                                             uint32_t rows, uint32_t cols);
/* Insert a table of contents at the caret: a "Contents" heading followed by
 * one indented entry per heading in the document (a static snapshot, like a
 * word processor's non-updating TOC). Structural, direct. Exposed. */
struct yetty_ycore_void_result yetty_yrich_ydoc_insert_toc(struct yetty_yclass_object *obj);
/* Insert/delete a row or column around the active cell of the active table.
 * `op` is enum ydoc_table_op. Direct (not undoable). Exposed; the Insert menu
 * dispatches here. */
struct yetty_ycore_void_result yetty_yrich_ydoc_table_edit(struct yetty_yclass_object *obj,
                                                           uint32_t op);
/* Selected text as a fresh heap string (caller frees). NULL when the
 * selection is empty or not a text selection. */
char *ydoc_all_text(struct yetty_yrich_ydoc *ydoc);
struct yetty_ycore_char_ptr_result yetty_yrich_ydoc_selection_text(struct yetty_yclass_object *obj);
/* Document statistics: codepoints, codepoints excluding whitespace,
 * whitespace-delimited words, and text paragraphs (dividers excluded). Any out
 * pointer may be NULL. */
struct yetty_ycore_void_result yetty_yrich_ydoc_word_count(struct yetty_yclass_object *obj,
                                                           uint32_t *out_words, uint32_t *out_chars,
                                                           uint32_t *out_chars_no_spaces,
                                                           uint32_t *out_paragraphs);
/* Select the next occurrence of `query` at or after the current selection end,
 * wrapping once to the top (case-insensitive). Returns 1 if found. */
struct yetty_ycore_int_result yetty_yrich_ydoc_find_next(struct yetty_yclass_object *obj,
                                                         const char *query);
/* Select the previous occurrence of `query` before the current selection start,
 * wrapping once to the bottom (case-insensitive). Returns 1 if found. */
struct yetty_ycore_int_result yetty_yrich_ydoc_find_prev(struct yetty_yclass_object *obj,
                                                         const char *query);
/* Replace every occurrence of `query` with `replacement` across all text
 * paragraphs as ONE undoable command. Returns the number replaced. */
struct yetty_ycore_int_result yetty_yrich_ydoc_replace_all(struct yetty_yclass_object *obj,
                                                           const char *query,
                                                           const char *replacement);
/* Extra vertical gap above the paragraph, in px (0 removes it). Undoable. */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_space_before(struct yetty_yclass_object *obj,
                                                                 float px);
/* Extra vertical gap below the paragraph, in px (0 removes it). Undoable. */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_space_after(struct yetty_yclass_object *obj,
                                                                float px);
/* Nest a list item one level deeper (direction > 0) or shallower (<= 0);
 * no-op on non-list paragraphs. Undoable. Wired to Tab / Shift+Tab. */
struct yetty_ycore_void_result yetty_yrich_ydoc_change_list_level(struct yetty_yclass_object *obj,
                                                                  int32_t direction);
/* Capture the character style at the caret into the paint-format clipboard. */
struct yetty_ycore_void_result yetty_yrich_ydoc_copy_format(struct yetty_yclass_object *obj);
/* Apply the paint-format clipboard to the selection (or the whole active
 * paragraph when the selection is collapsed). Undoable. */
struct yetty_ycore_void_result yetty_yrich_ydoc_paint_format(struct yetty_yclass_object *obj);
/* Make the current selection (or the word at the caret when collapsed) a
 * hyperlink to `url`. The URL is interned in the doc link table. Undoable. */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_link(struct yetty_yclass_object *obj,
                                                         const char *url);
/* Remove the hyperlink covering the selection, or the whole link span at the
 * caret when collapsed. No-op when there is no link. Undoable. */
struct yetty_ycore_void_result yetty_yrich_ydoc_remove_link(struct yetty_yclass_object *obj);
/* URL of the hyperlink at the caret, or NULL when there is none. The pointer is
 * owned by the document link table (valid until the link is changed/removed or
 * the document is destroyed) — callers must not free it. */
struct yetty_ycore_const_char_ptr_result yetty_yrich_ydoc_link_at_caret(
    struct yetty_yclass_object *obj);
/* URL for `link_id` in this document's link table (NULL if 0 or unknown).
 * Serializer side — resolves the id a run carries back to its URL. */
struct yetty_ycore_const_char_ptr_result yetty_yrich_ydoc_link_url(struct yetty_yclass_object *obj,
                                                                   uint32_t link_id);
/* Loader helper: intern `url` and stamp it as the hyperlink over [start, end) of
 * `paragraph_obj` (which must belong to this document). Not undoable — used only
 * while rebuilding a document from serialized form. */
struct yetty_ycore_void_result yetty_yrich_ydoc_apply_run_link(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *paragraph_obj, int32_t start,
    int32_t end, const char *url);
/* Name (or clear, with NULL/empty) the bookmark on the caret-holding paragraph.
 * Direct, not undoable — matching the other structural markers. */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_bookmark(struct yetty_yclass_object *obj,
                                                             const char *name);
/* Toggle the active paragraph's list kind (1=bullet, 2=numbered, 3=checklist);
 * re-applying the same kind clears it. Undoable. Exported; dispatched from
 * the command layer. */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_list(struct yetty_yclass_object *obj,
                                                         uint32_t kind);
/* Toggle the active checklist paragraph's checkbox. A no-op (no undo entry)
 * when the active paragraph is not a checklist item. Undoable. Exported;
 * dispatched from the command layer and the gutter click. */
struct yetty_ycore_void_result yetty_yrich_ydoc_toggle_checked(struct yetty_yclass_object *obj);
/* Place the caret (a collapsed text selection) at byte `position` of the
 * paragraph at `paragraph_index`, clamped to its text. The programmatic
 * counterpart of a mouse click — used by tests, scripting, and bindings. */
struct yetty_ycore_void_result yetty_yrich_ydoc_place_caret(struct yetty_yclass_object *obj,
                                                            int32_t paragraph_index,
                                                            int32_t position);
/* Place the caret at the start of the paragraph carrying bookmark `name`.
 * Returns 1 when a match was found and navigated, 0 when no bookmark matches. */
struct yetty_ycore_int_result yetty_yrich_ydoc_goto_bookmark(struct yetty_yclass_object *obj,
                                                             const char *name);
/* Select from (anchor_paragraph, anchor_offset) to (focus_paragraph,
 * focus_offset) — the programmatic counterpart of a cross-paragraph drag.
 * Offsets are clamped; equal paragraphs give a single-paragraph selection. */
struct yetty_ycore_void_result yetty_yrich_ydoc_select_range(struct yetty_yclass_object *obj,
                                                             int32_t anchor_paragraph,
                                                             int32_t anchor_offset,
                                                             int32_t focus_paragraph,
                                                             int32_t focus_offset);
/* Drop every element and start over with one empty paragraph (File > New).
 * The undo history is cleared — its ops reference dead elements. */
struct yetty_ycore_void_result yetty_yrich_ydoc_clear(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_source_path(struct yetty_yclass_object *obj,
                                                                const char *path);
struct yetty_ycore_const_char_ptr_result yetty_yrich_ydoc_source_path(
    struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_ydoc_page_width(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_ydoc_margin(struct yetty_yclass_object *obj);
struct yetty_ycore_size_result yetty_yrich_ydoc_paragraph_count(struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_yrich_paragraph_text(
    struct yetty_yclass_object *obj);
struct yetty_ycore_size_result yetty_yrich_paragraph_text_len(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_paragraph_font_size(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_paragraph_line_spacing(struct yetty_yclass_object *obj);
/* Loader-side setter: set the multiplier and recompute line_height. */
struct yetty_ycore_void_result yetty_yrich_paragraph_set_line_spacing(
    struct yetty_yclass_object *obj, float spacing);
struct yetty_ycore_float_result yetty_yrich_paragraph_indent(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yrich_paragraph_heading_level(
    struct yetty_yclass_object *obj);
/* Loader-side: set the semantic level only. The visual (font size, bold) is
 * serialized independently and already restored from fontSize/format. */
struct yetty_ycore_void_result yetty_yrich_paragraph_set_heading_level(
    struct yetty_yclass_object *obj, uint32_t level);
struct yetty_ycore_void_result yetty_yrich_paragraph_set_indent(struct yetty_yclass_object *obj,
                                                                float indent);
struct yetty_ycore_uint32_result yetty_yrich_paragraph_color(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yrich_paragraph_format(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yrich_paragraph_alignment(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_paragraph_set_alignment(struct yetty_yclass_object *obj,
                                                                   uint32_t halign);
/* List kind (0 none, 1 bullet, 2 numbered, 3 checklist) + the checklist
 * checkbox state — loader/saver side; editing goes through ydoc_set_list
 * and ydoc_toggle_checked. */
struct yetty_ycore_uint32_result yetty_yrich_paragraph_list_kind(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_paragraph_set_list_kind(struct yetty_yclass_object *obj,
                                                                   uint32_t list_kind);
struct yetty_ycore_uint32_result yetty_yrich_paragraph_list_checked(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_paragraph_set_list_checked(
    struct yetty_yclass_object *obj, uint32_t checked);
/* Block kind (0 text, 1 horizontal-rule divider) — loader/saver side. */
struct yetty_ycore_uint32_result yetty_yrich_paragraph_block_kind(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_paragraph_set_block_kind(struct yetty_yclass_object *obj,
                                                                    uint32_t block_kind);
/* Table dimensions/cells — loader/saver side; editing is via the ydoc. */
struct yetty_ycore_void_result yetty_yrich_paragraph_table_size(struct yetty_yclass_object *obj,
                                                                uint32_t *out_rows,
                                                                uint32_t *out_cols);
/* Allocate the cell grid (clears any prior one). block_kind is set to table. */
struct yetty_ycore_void_result yetty_yrich_paragraph_set_table(struct yetty_yclass_object *obj,
                                                               uint32_t rows, uint32_t cols);
struct yetty_ycore_const_char_ptr_result yetty_yrich_paragraph_table_cell(
    struct yetty_yclass_object *obj, uint32_t row, uint32_t col);
struct yetty_ycore_void_result yetty_yrich_paragraph_set_table_cell(struct yetty_yclass_object *obj,
                                                                    uint32_t row, uint32_t col,
                                                                    const char *text);
/* Paragraph spacing (px above/below) + list nesting level — loader/saver side. */
struct yetty_ycore_float_result yetty_yrich_paragraph_space_before(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_paragraph_set_space_before(
    struct yetty_yclass_object *obj, float px);
struct yetty_ycore_float_result yetty_yrich_paragraph_space_after(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_paragraph_set_space_after(
    struct yetty_yclass_object *obj, float px);
struct yetty_ycore_uint32_result yetty_yrich_paragraph_list_level(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_paragraph_set_list_level(struct yetty_yclass_object *obj,
                                                                    uint32_t level);
struct yetty_ycore_size_result yetty_yrich_paragraph_run_count(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_paragraph_run_get(
    struct yetty_yclass_object *obj, size_t index, int32_t *out_start, int32_t *out_end,
    uint32_t *out_format, uint32_t *out_color, uint32_t *out_bg_color, float *out_font_size);
/* Hyperlink id of run `index` (0 = no link); resolves to a URL via the owning
 * ydoc's link table. Serializer side. */
struct yetty_ycore_uint32_result yetty_yrich_paragraph_run_link_id(struct yetty_yclass_object *obj,
                                                                   size_t index);
/* Name the bookmark anchored at this paragraph (NULL/empty clears it). */
struct yetty_ycore_void_result yetty_yrich_paragraph_set_bookmark(struct yetty_yclass_object *obj,
                                                                  const char *name);
/* Bookmark name anchoring this paragraph, or NULL when none. */
struct yetty_ycore_const_char_ptr_result yetty_yrich_paragraph_bookmark(
    struct yetty_yclass_object *obj);
/* Append one styled run — loader-side; ranges must arrive sorted and
 * non-overlapping (the writer emits them that way). */
struct yetty_ycore_void_result yetty_yrich_paragraph_add_run(struct yetty_yclass_object *obj,
                                                             int32_t start, int32_t end,
                                                             uint32_t format, uint32_t color,
                                                             uint32_t bg_color, float font_size);

#ifdef __cplusplus
}
#endif

#endif
