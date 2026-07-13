#ifndef YETTY_YMSOFFICE_MODEL_H
#define YETTY_YMSOFFICE_MODEL_H

/*
 * model.h - neutral in-memory model for Microsoft Office documents.
 *
 * The parsers (docx.c / xlsx.c / pptx.c) fill this model; consumers map it
 * onto a rendering target. The model deliberately knows nothing about ydraw
 * or yrich so both can consume it: the ycat handler renders it to a ydraw
 * buffer today, and a yrich converter can build editable ydoc / spreadsheet
 * / slides objects from the same data later.
 *
 * Fidelity notes: the model keeps document *structure* (paragraphs, styled
 * runs, tables, lists, sheets, slides, shapes) and the character-level
 * styling a terminal renderer can show. It does not model page geometry,
 * sections, footnotes, themes, or embedded media bytes (images surface as
 * named placeholders with their declared size).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

enum yetty_ymsoffice_kind {
    YETTY_YMSOFFICE_KIND_UNKNOWN = 0,
    YETTY_YMSOFFICE_KIND_WORD,   /* docx — WordprocessingML */
    YETTY_YMSOFFICE_KIND_SHEET,  /* xlsx — SpreadsheetML */
    YETTY_YMSOFFICE_KIND_SLIDES, /* pptx — PresentationML */
};

enum yetty_ymsoffice_align {
    YETTY_YMSOFFICE_ALIGN_LEFT = 0,
    YETTY_YMSOFFICE_ALIGN_CENTER,
    YETTY_YMSOFFICE_ALIGN_RIGHT,
    YETTY_YMSOFFICE_ALIGN_JUSTIFY,
};

/*=============================================================================
 * Shared text model (word + slides + table cells)
 *===========================================================================*/

struct yetty_ymsoffice_text_run {
    char *text; /* owned UTF-8; may contain '\n' for explicit breaks */
    size_t text_len;
    bool bold;
    bool italic;
    bool underline;
    bool strike;
    bool hyperlink;
    float font_size_pt; /* 0 = inherit the document default */
    uint32_t color_rgb; /* 0xRRGGBB, valid when has_color */
    bool has_color;
    uint32_t highlight_rgb; /* 0xRRGGBB, valid when has_highlight */
    bool has_highlight;
};

struct yetty_ymsoffice_paragraph {
    struct yetty_ymsoffice_text_run *runs;
    size_t run_count;
    size_t run_cap;
    int heading_level; /* 0 = body text, 1..6 = heading */
    enum yetty_ymsoffice_align align;
    int list_level;    /* -1 = not a list item, 0-based indent level */
    bool list_ordered; /* numbered vs bullet, valid when list_level >= 0 */
};

/*=============================================================================
 * Word document (docx)
 *===========================================================================*/

struct yetty_ymsoffice_table_cell {
    struct yetty_ymsoffice_paragraph *paragraphs;
    size_t paragraph_count;
    size_t paragraph_cap;
    int col_span;         /* >= 1 */
    bool merged_continue; /* vertically-merged continuation cell */
};

struct yetty_ymsoffice_table_row {
    struct yetty_ymsoffice_table_cell *cells;
    size_t cell_count;
    size_t cell_cap;
};

struct yetty_ymsoffice_table {
    struct yetty_ymsoffice_table_row *rows;
    size_t row_count;
    size_t row_cap;
};

struct yetty_ymsoffice_image {
    char *name;     /* owned; drawing name/description, may be NULL */
    float width_pt; /* declared extent in points; 0 = unknown */
    float height_pt;
};

enum yetty_ymsoffice_block_kind {
    YETTY_YMSOFFICE_BLOCK_PARAGRAPH = 0,
    YETTY_YMSOFFICE_BLOCK_TABLE,
    YETTY_YMSOFFICE_BLOCK_IMAGE,
};

struct yetty_ymsoffice_block {
    enum yetty_ymsoffice_block_kind kind;
    union {
        struct yetty_ymsoffice_paragraph paragraph;
        struct yetty_ymsoffice_table table;
        struct yetty_ymsoffice_image image;
    };
};

struct yetty_ymsoffice_word_document {
    struct yetty_ymsoffice_block *blocks;
    size_t block_count;
    size_t block_cap;
};

/*=============================================================================
 * Spreadsheet (xlsx)
 *===========================================================================*/

struct yetty_ymsoffice_sheet_cell {
    uint32_t row; /* 0-based */
    uint32_t col; /* 0-based */
    char *text;   /* owned display text (shared strings resolved) */
    size_t text_len;
    char *formula; /* owned, NULL when the cell holds a plain value */
    bool is_number;
};

struct yetty_ymsoffice_sheet {
    char *name; /* owned */
    struct yetty_ymsoffice_sheet_cell *cells;
    size_t cell_count;
    size_t cell_cap;
    uint32_t max_row; /* highest populated 0-based index */
    uint32_t max_col;
};

struct yetty_ymsoffice_sheet_document {
    struct yetty_ymsoffice_sheet *sheets;
    size_t sheet_count;
    size_t sheet_cap;
};

/*=============================================================================
 * Presentation (pptx)
 *===========================================================================*/

enum yetty_ymsoffice_shape_kind {
    YETTY_YMSOFFICE_SHAPE_BOX = 0, /* rect / rounded rect / generic preset */
    YETTY_YMSOFFICE_SHAPE_ELLIPSE,
    YETTY_YMSOFFICE_SHAPE_LINE,
    YETTY_YMSOFFICE_SHAPE_PICTURE,
    YETTY_YMSOFFICE_SHAPE_FRAME, /* graphicFrame: embedded table/chart */
};

struct yetty_ymsoffice_shape {
    enum yetty_ymsoffice_shape_kind kind;
    char *name; /* owned, may be NULL */
    float x_pt; /* slide-relative frame, in points */
    float y_pt;
    float width_pt;
    float height_pt;
    bool has_frame; /* the xfrm offset/extent were present */
    uint32_t fill_rgb;
    bool has_fill;
    struct yetty_ymsoffice_paragraph *paragraphs; /* text body */
    size_t paragraph_count;
    size_t paragraph_cap;
};

struct yetty_ymsoffice_slide {
    struct yetty_ymsoffice_shape *shapes;
    size_t shape_count;
    size_t shape_cap;
};

struct yetty_ymsoffice_slides_document {
    struct yetty_ymsoffice_slide *slides;
    size_t slide_count;
    size_t slide_cap;
    float width_pt; /* slide size from presentation.xml */
    float height_pt;
};

/*=============================================================================
 * Top-level document
 *===========================================================================*/

struct yetty_ymsoffice_document {
    enum yetty_ymsoffice_kind kind;
    union {
        struct yetty_ymsoffice_word_document word;
        struct yetty_ymsoffice_sheet_document sheet;
        struct yetty_ymsoffice_slides_document slides;
    };
};

YETTY_YRESULT_DECLARE(yetty_ymsoffice_document_ptr, struct yetty_ymsoffice_document *);

struct yetty_ymsoffice_document_ptr_result yetty_ymsoffice_document_create(
    enum yetty_ymsoffice_kind kind);

void yetty_ymsoffice_document_destroy(struct yetty_ymsoffice_document *document);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMSOFFICE_MODEL_H */
