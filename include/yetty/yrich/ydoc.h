#ifndef YETTY_YRICH_YDOC_H
#define YETTY_YRICH_YDOC_H

/*
 * ydoc — paragraph-flow rich text document.
 *
 * The C port covers paragraphs (with rich-text runs) plus a flat list of
 * inline images. Tables, comments, and version history from the C++ POC are
 * deferred — they map cleanly into the existing element list and can be
 * added later without touching the document base.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-element.h>
#include <yetty/yrich/yrich-types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * TextRun — a contiguous span of text with uniform style.
 *===========================================================================*/

struct yetty_yrich_text_run {
    int32_t start;
    int32_t end;
    struct yetty_yrich_text_style style;
};

/*=============================================================================
 * Paragraph — block of editable text with rich runs.
 *===========================================================================*/

struct yetty_yrich_paragraph {
    struct yetty_yrich_element base;

    struct yetty_yrich_rect bounds;

    char *text; /* owned, NUL-terminated */
    size_t text_len;
    struct yetty_yrich_text_style style; /* default style */

    struct yetty_yrich_text_run *runs;
    size_t run_count;
    size_t run_capacity;

    float line_height;

    bool editing;
    int32_t cursor_pos;
    int32_t sel_start;
    int32_t sel_end;
};

YETTY_YRESULT_DECLARE(yetty_yrich_paragraph_ptr, struct yetty_yrich_paragraph *);

struct yetty_yrich_paragraph_ptr_result yetty_yrich_paragraph_create(yetty_yrich_element_id id,
                                                                     float x, float y, float width);

void yetty_yrich_paragraph_set_text(struct yetty_yrich_paragraph *p, const char *text, size_t len);

/*=============================================================================
 * InlineImage — image element placed inside the doc flow.
 *===========================================================================*/

struct yetty_yrich_inline_image {
    struct yetty_yrich_element base;

    struct yetty_yrich_rect bounds;
    char *source;   /* owned base64/path, may be NULL */
    char *alt_text; /* owned, may be NULL */
    char *caption;  /* owned, may be NULL */
    uint32_t align; /* enum yetty_yrich_halign */
};

YETTY_YRESULT_DECLARE(yetty_yrich_inline_image_ptr, struct yetty_yrich_inline_image *);

struct yetty_yrich_inline_image_ptr_result yetty_yrich_inline_image_create(
    yetty_yrich_element_id id, float x, float y, float w, float h);

/*=============================================================================
 * Document
 *===========================================================================*/

struct yetty_yrich_doc_cursor {
    int32_t paragraph_index;
    int32_t char_index;
};

struct yetty_yrich_ydoc {
    struct yetty_yrich_document base;

    float page_width;
    float margin;

    /* Paragraph order — these alias the element list, never owning. */
    struct yetty_yrich_paragraph **paragraphs;
    size_t paragraph_count;
    size_t paragraph_capacity;

    /* Inline images — also alias. */
    struct yetty_yrich_inline_image **images;
    size_t image_count;
    size_t image_capacity;

    struct yetty_yrich_doc_cursor cursor;
    struct yetty_yrich_doc_cursor selection_anchor;
    bool has_selection;
};

YETTY_YRESULT_DECLARE(yetty_yrich_ydoc_ptr, struct yetty_yrich_ydoc *);

struct yetty_yrich_ydoc_ptr_result yetty_yrich_ydoc_create(void);

void yetty_yrich_ydoc_set_page_width(struct yetty_yrich_ydoc *d, float w);
void yetty_yrich_ydoc_set_margin(struct yetty_yrich_ydoc *d, float m);

struct yetty_yrich_paragraph_ptr_result yetty_yrich_ydoc_add_paragraph(struct yetty_yrich_ydoc *d,
                                                                       const char *text,
                                                                       size_t text_len);

struct yetty_yrich_paragraph *yetty_yrich_ydoc_paragraph_at(const struct yetty_yrich_ydoc *d,
                                                            int32_t index);

struct yetty_yrich_inline_image_ptr_result yetty_yrich_ydoc_insert_image(struct yetty_yrich_ydoc *d,
                                                                         int32_t paragraph_index,
                                                                         float width, float height);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YDOC_H */
