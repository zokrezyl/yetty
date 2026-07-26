/*
 * yrich formatting under document-wide selection (select-all).
 *
 * Pins the fix for the bug where Ctrl+B after Ctrl+A only bolded the active
 * paragraph: with select-all set, a formatting command must apply to EVERY
 * paragraph (whole paragraph, not just its last line), as one undoable step.
 */

#include <yetty/api/yrich/document.h>
#include <yetty/api/yrich/ydoc.h>
#include <yetty/yrich/yrich-types.h>

#include "ytest.h"

#include <string.h>

/* Exported from ydoc.c (no generated header entry). */
struct yetty_ycore_void_result yetty_yrich_ydoc_select_all(struct yetty_yclass_object *obj);

static struct yetty_yclass_object *make_doc(void)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    if (YETTY_IS_ERR(doc_res)) {
        yetty_ycore_error_destroy(doc_res.error);
        return NULL;
    }
    struct yetty_yclass_object *doc = doc_res.value;
    const char *paras[] = {"First paragraph with several words",
                           "Second paragraph, also multiple words", "Third and final paragraph"};
    for (size_t i = 0; i < sizeof(paras) / sizeof(paras[0]); i++) {
        struct yetty_yclass_object_ptr_result para =
            yetty_yrich_ydoc_add_paragraph(doc, paras[i], strlen(paras[i]));
        if (YETTY_IS_ERR(para)) {
            yetty_ycore_error_destroy(para.error);
            struct yetty_ycore_void_result d = yetty_yrich_document_destroy(doc);
            if (YETTY_IS_ERR(d)) {
                yetty_ycore_error_destroy(d.error);
            }
            return NULL;
        }
    }
    return doc;
}

/* -1 on error, else 0/1 for whether paragraph `index` is effectively bold —
 * either in its base style or via a style run (whole-paragraph bold is stored
 * as a run [0,len] when the base was not bold). */
static int para_bold(struct yetty_yclass_object *doc, int32_t index)
{
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_paragraph_at(doc, index);
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        return -1;
    }
    struct yetty_ycore_uint32_result fmt = yetty_yrich_paragraph_format(para.value);
    if (YETTY_IS_ERR(fmt)) {
        yetty_ycore_error_destroy(fmt.error);
        return -1;
    }
    if (fmt.value & YETTY_YRICH_FMT_BOLD) {
        return 1;
    }
    struct yetty_ycore_size_result run_count = yetty_yrich_paragraph_run_count(para.value);
    if (YETTY_IS_ERR(run_count)) {
        yetty_ycore_error_destroy(run_count.error);
        return -1;
    }
    for (size_t i = 0; i < run_count.value; i++) {
        int32_t start = 0;
        int32_t end = 0;
        uint32_t run_format = 0;
        uint32_t run_color = 0;
        uint32_t run_bg = 0;
        float run_fs = 0.0f;
        struct yetty_ycore_void_result run = yetty_yrich_paragraph_run_get(
            para.value, i, &start, &end, &run_format, &run_color, &run_bg, &run_fs);
        if (YETTY_IS_ERR(run)) {
            yetty_ycore_error_destroy(run.error);
            return -1;
        }
        if (run_format & YETTY_YRICH_FMT_BOLD) {
            return 1;
        }
    }
    return 0;
}

/* -1 on error, else 0/1 for whether paragraph `index` carries a highlight
 * (any run with a non-transparent background). */
static int para_highlight(struct yetty_yclass_object *doc, int32_t index)
{
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_paragraph_at(doc, index);
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        return -1;
    }
    struct yetty_ycore_size_result run_count = yetty_yrich_paragraph_run_count(para.value);
    if (YETTY_IS_ERR(run_count)) {
        yetty_ycore_error_destroy(run_count.error);
        return -1;
    }
    for (size_t i = 0; i < run_count.value; i++) {
        int32_t start = 0;
        int32_t end = 0;
        uint32_t run_format = 0;
        uint32_t run_color = 0;
        uint32_t run_bg = 0;
        float run_fs = 0.0f;
        struct yetty_ycore_void_result run = yetty_yrich_paragraph_run_get(
            para.value, i, &start, &end, &run_format, &run_color, &run_bg, &run_fs);
        if (YETTY_IS_ERR(run)) {
            yetty_ycore_error_destroy(run.error);
            return -1;
        }
        if (run_bg != YETTY_YRICH_COLOR_TRANSPARENT) {
            return 1;
        }
    }
    return 0;
}

/* True if every paragraph's highlight state equals `want`. */
static int all_highlight(struct yetty_yclass_object *doc, int want)
{
    struct yetty_ycore_size_result count = yetty_yrich_ydoc_paragraph_count(doc);
    if (YETTY_IS_ERR(count)) {
        yetty_ycore_error_destroy(count.error);
        return 0;
    }
    for (size_t i = 0; i < count.value; i++) {
        if (para_highlight(doc, (int32_t)i) != want) {
            return 0;
        }
    }
    return count.value > 0;
}

/* True if every paragraph's bold state equals `want`. */
static int all_bold(struct yetty_yclass_object *doc, int want)
{
    struct yetty_ycore_size_result count = yetty_yrich_ydoc_paragraph_count(doc);
    if (YETTY_IS_ERR(count)) {
        yetty_ycore_error_destroy(count.error);
        return 0;
    }
    if (count.value == 0) {
        return 0;
    }
    for (size_t i = 0; i < count.value; i++) {
        if (para_bold(doc, (int32_t)i) != want) {
            return 0;
        }
    }
    return 1;
}

static void test_select_all_bold_bolds_every_paragraph(struct ytest *test)
{
    struct yetty_yclass_object *doc = make_doc();
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        return;
    }

    /* Nothing bold to start. */
    YTEST_CHECK(test, all_bold(doc, 0));

    /* Select all, then Ctrl+B. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_select_all(doc)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_toggle_format(doc, YETTY_YRICH_FMT_BOLD)));

    /* EVERY paragraph is now bold — not just the last one (the bug). */
    YTEST_CHECK(test, all_bold(doc, 1));

    /* A single undo reverts the whole-document change. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_undo(doc)));
    YTEST_CHECK(test, all_bold(doc, 0));

    /* Redo re-applies to all. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_redo(doc)));
    YTEST_CHECK(test, all_bold(doc, 1));

    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

static void test_select_all_font_size_all_paragraphs(struct ytest *test)
{
    struct yetty_yclass_object *doc = make_doc();
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        return;
    }
    /* Heading-1 over the whole document sets every paragraph's font size. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_select_all(doc)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_set_heading(doc, 1)));

    struct yetty_ycore_size_result count = yetty_yrich_ydoc_paragraph_count(doc);
    YTEST_CHECK(test, !YETTY_IS_ERR(count));
    if (!YETTY_IS_ERR(count)) {
        for (size_t i = 0; i < count.value; i++) {
            struct yetty_yclass_object_ptr_result para =
                yetty_yrich_ydoc_paragraph_at(doc, (int32_t)i);
            YTEST_CHECK(test, !YETTY_IS_ERR(para));
            if (!YETTY_IS_ERR(para)) {
                struct yetty_ycore_float_result fs = yetty_yrich_paragraph_font_size(para.value);
                YTEST_CHECK(test, !YETTY_IS_ERR(fs));
                if (!YETTY_IS_ERR(fs)) {
                    YTEST_CHECK_NEAR(test, fs.value, 30.0f, 0.01f); /* H1 size */
                }
            }
        }
    }

    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

static void test_select_all_highlight_and_clear(struct ytest *test)
{
    struct yetty_yclass_object *doc = make_doc();
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        return;
    }
    /* Nothing highlighted or bold to start. */
    YTEST_CHECK(test, all_highlight(doc, 0));

    /* Select all, bold + highlight the whole document. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_select_all(doc)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_toggle_format(doc, YETTY_YRICH_FMT_BOLD)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_select_all(doc)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_set_highlight(
                          doc, YETTY_YRICH_RGBA(255, 235, 130, 255))));
    YTEST_CHECK(test, all_bold(doc, 1));
    YTEST_CHECK(test, all_highlight(doc, 1));

    /* Clear formatting document-wide removes bold AND highlight. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_select_all(doc)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_clear_format(doc)));
    YTEST_CHECK(test, all_bold(doc, 0));
    YTEST_CHECK(test, all_highlight(doc, 0));

    /* Undo restores the cleared formatting (highlight + bold back). */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_undo(doc)));
    YTEST_CHECK(test, all_highlight(doc, 1));
    YTEST_CHECK(test, all_bold(doc, 1));

    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

static void test_select_all_line_spacing(struct ytest *test)
{
    struct yetty_yclass_object *doc = make_doc();
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        return;
    }
    /* Document-wide double spacing sets every paragraph's multiplier. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_select_all(doc)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_set_line_spacing(doc, 2.0f)));

    struct yetty_ycore_size_result count = yetty_yrich_ydoc_paragraph_count(doc);
    YTEST_CHECK(test, !YETTY_IS_ERR(count));
    if (!YETTY_IS_ERR(count)) {
        for (size_t i = 0; i < count.value; i++) {
            struct yetty_yclass_object_ptr_result para =
                yetty_yrich_ydoc_paragraph_at(doc, (int32_t)i);
            YTEST_CHECK(test, !YETTY_IS_ERR(para));
            if (!YETTY_IS_ERR(para)) {
                struct yetty_ycore_float_result ls = yetty_yrich_paragraph_line_spacing(para.value);
                YTEST_CHECK(test, !YETTY_IS_ERR(ls));
                if (!YETTY_IS_ERR(ls)) {
                    YTEST_CHECK_NEAR(test, ls.value, 2.0f, 0.01f);
                }
            }
        }
    }

    /* Undo reverts every paragraph to the default multiplier. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_undo(doc)));
    struct yetty_yclass_object_ptr_result para0 = yetty_yrich_ydoc_paragraph_at(doc, 0);
    YTEST_CHECK(test, !YETTY_IS_ERR(para0));
    if (!YETTY_IS_ERR(para0)) {
        struct yetty_ycore_float_result ls = yetty_yrich_paragraph_line_spacing(para0.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(ls));
        if (!YETTY_IS_ERR(ls)) {
            YTEST_CHECK_NEAR(test, ls.value, 1.4f, 0.01f); /* YDOC_DEFAULT_LINE_SPACING */
        }
    }

    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

static void test_select_all_heading_level(struct ytest *test)
{
    struct yetty_yclass_object *doc = make_doc();
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        return;
    }
    /* Heading 2 over the whole document records the semantic level on each. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_select_all(doc)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_set_heading(doc, 2)));

    struct yetty_yclass_object_ptr_result para0 = yetty_yrich_ydoc_paragraph_at(doc, 0);
    YTEST_CHECK(test, !YETTY_IS_ERR(para0));
    if (!YETTY_IS_ERR(para0)) {
        struct yetty_ycore_uint32_result hl = yetty_yrich_paragraph_heading_level(para0.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(hl));
        if (!YETTY_IS_ERR(hl)) {
            YTEST_CHECK_EQ_INT(test, hl.value, 2);
        }
    }

    /* Undo reverts to body text (level 0). */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_undo(doc)));
    struct yetty_yclass_object_ptr_result para0b = yetty_yrich_ydoc_paragraph_at(doc, 0);
    YTEST_CHECK(test, !YETTY_IS_ERR(para0b));
    if (!YETTY_IS_ERR(para0b)) {
        struct yetty_ycore_uint32_result hl = yetty_yrich_paragraph_heading_level(para0b.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(hl));
        if (!YETTY_IS_ERR(hl)) {
            YTEST_CHECK_EQ_INT(test, hl.value, 0);
        }
    }

    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

/* -1 on error, else the paragraph's halign. */
static int para_alignment(struct yetty_yclass_object *doc, int32_t index)
{
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_paragraph_at(doc, index);
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        return -1;
    }
    struct yetty_ycore_uint32_result align = yetty_yrich_paragraph_alignment(para.value);
    if (YETTY_IS_ERR(align)) {
        yetty_ycore_error_destroy(align.error);
        return -1;
    }
    return (int)align.value;
}

static void test_select_all_justify_alignment(struct ytest *test)
{
    struct yetty_yclass_object *doc = make_doc();
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        return;
    }

    YTEST_CHECK(test, para_alignment(doc, 0) == YETTY_YRICH_HALIGN_LEFT);

    /* Justify the whole document as one undoable step. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_select_all(doc)));
    YTEST_CHECK(test,
                !YETTY_IS_ERR(yetty_yrich_ydoc_set_alignment(doc, YETTY_YRICH_HALIGN_JUSTIFY)));
    YTEST_CHECK(test, para_alignment(doc, 0) == YETTY_YRICH_HALIGN_JUSTIFY);
    YTEST_CHECK(test, para_alignment(doc, 1) == YETTY_YRICH_HALIGN_JUSTIFY);
    YTEST_CHECK(test, para_alignment(doc, 2) == YETTY_YRICH_HALIGN_JUSTIFY);

    /* Undo restores, redo re-applies. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_undo(doc)));
    YTEST_CHECK(test, para_alignment(doc, 0) == YETTY_YRICH_HALIGN_LEFT);
    YTEST_CHECK(test, para_alignment(doc, 2) == YETTY_YRICH_HALIGN_LEFT);
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_redo(doc)));
    YTEST_CHECK(test, para_alignment(doc, 2) == YETTY_YRICH_HALIGN_JUSTIFY);

    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

/* -1 on error, else the paragraph's list state packed as kind * 10 + checked. */
static int para_list_state(struct yetty_yclass_object *doc, int32_t index)
{
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_paragraph_at(doc, index);
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        return -1;
    }
    struct yetty_ycore_uint32_result kind = yetty_yrich_paragraph_list_kind(para.value);
    if (YETTY_IS_ERR(kind)) {
        yetty_ycore_error_destroy(kind.error);
        return -1;
    }
    struct yetty_ycore_uint32_result checked = yetty_yrich_paragraph_list_checked(para.value);
    if (YETTY_IS_ERR(checked)) {
        yetty_ycore_error_destroy(checked.error);
        return -1;
    }
    return (int)(kind.value * 10 + checked.value);
}

static void test_checklist_toggle_and_undo(struct ytest *test)
{
    struct yetty_yclass_object *doc = make_doc();
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        return;
    }

    /* Put the caret into the last paragraph — list commands act on the
     * caret-holding paragraph. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_place_caret(doc, 2, 0)));
    YTEST_CHECK(test, para_list_state(doc, 2) == 0);

    /* Turn it into a checklist item, then check it off. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_set_list(doc, 3)));
    YTEST_CHECK(test, para_list_state(doc, 2) == 30);
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_toggle_checked(doc)));
    YTEST_CHECK(test, para_list_state(doc, 2) == 31);

    /* Undo unchecks; another undo drops the checklist kind entirely. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_undo(doc)));
    YTEST_CHECK(test, para_list_state(doc, 2) == 30);
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_undo(doc)));
    YTEST_CHECK(test, para_list_state(doc, 2) == 0);

    /* Redo brings both back. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_redo(doc)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_redo(doc)));
    YTEST_CHECK(test, para_list_state(doc, 2) == 31);

    /* Toggling on a non-checklist paragraph is a no-op with no undo entry:
     * re-applying kind 3 clears the list, and a toggle then does nothing. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_set_list(doc, 3)));
    YTEST_CHECK(test, para_list_state(doc, 2) == 0);
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_toggle_checked(doc)));
    YTEST_CHECK(test, para_list_state(doc, 2) == 0);

    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

/* Cross-paragraph selection: copy joins the covered slices, formatting is one
 * undoable command over every covered range, and Backspace collapses the span
 * merging the boundary paragraphs. */
static void test_cross_paragraph_selection(struct ytest *test)
{
    struct yetty_yclass_object *doc = make_doc();
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        return;
    }

    /* From "paragraph…" in P0 (offset 6) to just after "Third" in P2. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_select_range(doc, 0, 6, 2, 5)));

    struct yetty_ycore_char_ptr_result copy = yetty_yrich_ydoc_selection_text(doc);
    YTEST_CHECK(test, !YETTY_IS_ERR(copy) && copy.value != NULL);
    if (!YETTY_IS_ERR(copy) && copy.value) {
        YTEST_CHECK_STR_EQ(
            test, copy.value,
            "paragraph with several words\nSecond paragraph, also multiple words\nThird");
        free(copy.value);
    } else if (YETTY_IS_ERR(copy)) {
        yetty_ycore_error_destroy(copy.error);
    }

    /* Bold over the span — every covered paragraph, ONE undo step. */
    YTEST_CHECK(test, all_bold(doc, 0));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_toggle_format(doc, YETTY_YRICH_FMT_BOLD)));
    YTEST_CHECK(test, para_bold(doc, 0) == 1);
    YTEST_CHECK(test, para_bold(doc, 1) == 1);
    YTEST_CHECK(test, para_bold(doc, 2) == 1);
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_undo(doc)));
    YTEST_CHECK(test, all_bold(doc, 0));

    /* Backspace over the span deletes the covered text and stitches the
     * boundary paragraphs together. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_select_range(doc, 0, 6, 2, 5)));
    YTEST_CHECK(test,
                !YETTY_IS_ERR(yetty_yrich_document_on_key_down(doc, YETTY_YRICH_KEY_BACKSPACE, 0)));
    struct yetty_ycore_size_result count = yetty_yrich_ydoc_paragraph_count(doc);
    YTEST_CHECK(test, !YETTY_IS_ERR(count) && count.value == 1);
    if (YETTY_IS_ERR(count)) {
        yetty_ycore_error_destroy(count.error);
    }
    struct yetty_yclass_object_ptr_result merged = yetty_yrich_ydoc_paragraph_at(doc, 0);
    YTEST_CHECK(test, !YETTY_IS_ERR(merged));
    if (!YETTY_IS_ERR(merged)) {
        struct yetty_ycore_const_char_ptr_result text = yetty_yrich_paragraph_text(merged.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(text) && text.value != NULL);
        if (!YETTY_IS_ERR(text) && text.value) {
            YTEST_CHECK_STR_EQ(test, text.value, "First  and final paragraph");
        } else if (YETTY_IS_ERR(text)) {
            yetty_ycore_error_destroy(text.error);
        }
    } else {
        yetty_ycore_error_destroy(merged.error);
    }

    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

/* Fetch the current selection text (heap; caller frees). NULL on error. */
static char *selection_text(struct yetty_yclass_object *doc)
{
    struct yetty_ycore_char_ptr_result res = yetty_yrich_ydoc_selection_text(doc);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
        return NULL;
    }
    return res.value;
}

/* Double-click selects the whole word; then dragging (still held) extends the
 * selection word by word — forward, backward, and across a paragraph — exactly
 * as a desktop editor does. Layout uses the deterministic fallback metric
 * (font_size 14 x 0.6 = 8.4 px/char, margin 20), so click coordinates are
 * exact. */
static void test_double_click_word_and_drag(struct ytest *test)
{
    struct yetty_yclass_object *doc = make_doc();
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        return;
    }
    const float ch = 14.0f * 0.6f; /* fallback char advance */
    const float margin = 20.0f;
    const float y0 = 30.0f; /* centre of paragraph 0 (rows 20..40) */
    const float y1 = 50.0f; /* centre of paragraph 1 (rows 40..60) */
#define COL_X(col) (margin + (float)(col) * ch)

    /* Double-click inside "paragraph" (column 9) selects the whole word. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_on_mouse_double_click(
                          doc, COL_X(9), y0, YETTY_YRICH_MOUSE_LEFT, 0)));
    char *sel = selection_text(doc);
    YTEST_CHECK(test, sel != NULL && strcmp(sel, "paragraph") == 0);
    free(sel);

    /* Drag forward into "several" (column 24) — grows to whole words. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_on_mouse_drag(doc, COL_X(24), y0,
                                                                       YETTY_YRICH_MOUSE_LEFT, 0)));
    sel = selection_text(doc);
    YTEST_CHECK(test, sel != NULL && strcmp(sel, "paragraph with several") == 0);
    free(sel);

    /* Drag backward past the anchor into "First" (column 2) — the anchor word
     * stays covered and the selection flips to the words before it. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_on_mouse_drag(doc, COL_X(2), y0,
                                                                       YETTY_YRICH_MOUSE_LEFT, 0)));
    sel = selection_text(doc);
    YTEST_CHECK(test, sel != NULL && strcmp(sel, "First paragraph") == 0);
    free(sel);

    /* Drag down into paragraph 1's "Second" (column 3) — word-granular and
     * cross-paragraph at once. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_on_mouse_drag(doc, COL_X(3), y1,
                                                                       YETTY_YRICH_MOUSE_LEFT, 0)));
    sel = selection_text(doc);
    YTEST_CHECK(test, sel != NULL && strcmp(sel, "paragraph with several words\nSecond") == 0);
    free(sel);

    /* A fresh single click ends word-drag mode: a following ordinary drag is
     * character-granular again. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_on_mouse_down(doc, COL_X(6), y0,
                                                                       YETTY_YRICH_MOUSE_LEFT, 0)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_on_mouse_drag(doc, COL_X(9), y0,
                                                                       YETTY_YRICH_MOUSE_LEFT, 0)));
    sel = selection_text(doc);
    YTEST_CHECK(test, sel != NULL && strcmp(sel, "par") == 0); /* cols 6..9, not a whole word */
    free(sel);

#undef COL_X
    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

/* -1 on error, else the block kind (0 text, 1 divider) of paragraph `index`. */
static int para_block_kind(struct yetty_yclass_object *doc, int32_t index)
{
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_paragraph_at(doc, index);
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        return -1;
    }
    struct yetty_ycore_uint32_result kind = yetty_yrich_paragraph_block_kind(para.value);
    if (YETTY_IS_ERR(kind)) {
        yetty_ycore_error_destroy(kind.error);
        return -1;
    }
    return (int)kind.value;
}

/* Insert > Horizontal rule adds a divider block between a split of the current
 * paragraph, leaving the caret on a fresh line below it; Backspace at the start
 * of that line removes the rule outright. */
static void test_horizontal_rule_insert_and_delete(struct ytest *test)
{
    struct yetty_yclass_object *doc = make_doc();
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        return;
    }
    /* Caret at end of paragraph 0, then Insert > Horizontal rule. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_place_caret(doc, 0, 1000)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_insert_horizontal_rule(doc)));

    struct yetty_ycore_size_result count = yetty_yrich_ydoc_paragraph_count(doc);
    YTEST_CHECK(test, !YETTY_IS_ERR(count) && count.value == 5); /* was 3: +divider +tail */
    if (YETTY_IS_ERR(count)) {
        yetty_ycore_error_destroy(count.error);
    }
    YTEST_CHECK(test, para_block_kind(doc, 0) == 0); /* head text */
    YTEST_CHECK(test, para_block_kind(doc, 1) == 1); /* the rule */
    YTEST_CHECK(test, para_block_kind(doc, 2) == 0); /* fresh tail */

    /* The caret is on the tail line (index 2, offset 0). Backspace removes the
     * rule above it rather than merging text. */
    YTEST_CHECK(test,
                !YETTY_IS_ERR(yetty_yrich_document_on_key_down(doc, YETTY_YRICH_KEY_BACKSPACE, 0)));
    count = yetty_yrich_ydoc_paragraph_count(doc);
    YTEST_CHECK(test, !YETTY_IS_ERR(count) && count.value == 4);
    if (YETTY_IS_ERR(count)) {
        yetty_ycore_error_destroy(count.error);
    }
    YTEST_CHECK(test, para_block_kind(doc, 0) == 0);
    YTEST_CHECK(test, para_block_kind(doc, 1) == 0); /* no divider remains */

    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

/* -1 on error, else the list nesting level of paragraph `index`. */
static int para_list_level(struct yetty_yclass_object *doc, int32_t index)
{
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_paragraph_at(doc, index);
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        return -1;
    }
    struct yetty_ycore_uint32_result level = yetty_yrich_paragraph_list_level(para.value);
    if (YETTY_IS_ERR(level)) {
        yetty_ycore_error_destroy(level.error);
        return -1;
    }
    return (int)level.value;
}

/* -1.0 on error, else space_before of paragraph `index`. */
static float para_space_before(struct yetty_yclass_object *doc, int32_t index)
{
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_paragraph_at(doc, index);
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        return -1.0f;
    }
    struct yetty_ycore_float_result px = yetty_yrich_paragraph_space_before(para.value);
    if (YETTY_IS_ERR(px)) {
        yetty_ycore_error_destroy(px.error);
        return -1.0f;
    }
    return px.value;
}

/* Paragraph spacing (undoable) and multilevel list nesting via Tab depth. */
static void test_paragraph_spacing_and_list_nesting(struct ytest *test)
{
    struct yetty_yclass_object *doc = make_doc();
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        return;
    }

    /* Space before: set on paragraph 0, undo restores it. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_place_caret(doc, 0, 0)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_set_space_before(doc, 12.0f)));
    YTEST_CHECK_NEAR(test, para_space_before(doc, 0), 12.0f, 0.01f);
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_undo(doc)));
    YTEST_CHECK_NEAR(test, para_space_before(doc, 0), 0.0f, 0.01f);

    /* List nesting: make paragraph 1 a bullet, then Tab deeper / shallower. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_place_caret(doc, 1, 0)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_set_list(doc, 1)));
    YTEST_CHECK(test, para_list_level(doc, 1) == 0);
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_change_list_level(doc, 1)));
    YTEST_CHECK(test, para_list_level(doc, 1) == 1);
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_change_list_level(doc, 1)));
    YTEST_CHECK(test, para_list_level(doc, 1) == 2);
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_change_list_level(doc, -1)));
    YTEST_CHECK(test, para_list_level(doc, 1) == 1);
    /* Undo the outdent → back to level 2. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_undo(doc)));
    YTEST_CHECK(test, para_list_level(doc, 1) == 2);

    /* Nesting is a no-op on a non-list paragraph. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_place_caret(doc, 2, 0)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_change_list_level(doc, 1)));
    YTEST_CHECK(test, para_list_level(doc, 2) == 0);

    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

/* Word count, find-next selection, and undoable replace-all. */
static void test_word_count_find_replace(struct ytest *test)
{
    struct yetty_yclass_object *doc = make_doc();
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        return;
    }

    /* Statistics over the three demo paragraphs. */
    uint32_t words = 0;
    uint32_t chars = 0;
    uint32_t chars_no_spaces = 0;
    uint32_t paragraphs = 0;
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_word_count(doc, &words, &chars,
                                                                &chars_no_spaces, &paragraphs)));
    YTEST_CHECK_EQ_INT(test, (int)words, 14);
    YTEST_CHECK_EQ_INT(test, (int)chars, 96);
    YTEST_CHECK_EQ_INT(test, (int)paragraphs, 3);
    /* Characters excluding whitespace must be positive and not exceed the total. */
    YTEST_CHECK(test, chars_no_spaces > 0 && chars_no_spaces <= chars);

    /* Find next selects "paragraph" — present in all three paragraphs. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_place_caret(doc, 0, 0)));
    for (int occurrence = 0; occurrence < 3; occurrence++) {
        struct yetty_ycore_int_result found = yetty_yrich_ydoc_find_next(doc, "paragraph");
        YTEST_CHECK(test, !YETTY_IS_ERR(found) && found.value == 1);
        if (YETTY_IS_ERR(found)) {
            yetty_ycore_error_destroy(found.error);
        }
        char *sel = selection_text(doc);
        YTEST_CHECK(test, sel != NULL && strcmp(sel, "paragraph") == 0);
        free(sel);
    }

    /* Case-insensitive: an uppercased query still matches. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_place_caret(doc, 0, 0)));
    {
        struct yetty_ycore_int_result ci = yetty_yrich_ydoc_find_next(doc, "PARAGRAPH");
        YTEST_CHECK(test, !YETTY_IS_ERR(ci) && ci.value == 1);
        if (YETTY_IS_ERR(ci)) {
            yetty_ycore_error_destroy(ci.error);
        }
        char *sel = selection_text(doc);
        YTEST_CHECK(test, sel != NULL && strcmp(sel, "paragraph") == 0);
        free(sel);
    }

    /* Find previous from the end wraps and selects a match too. */
    {
        struct yetty_ycore_int_result prev = yetty_yrich_ydoc_find_prev(doc, "paragraph");
        YTEST_CHECK(test, !YETTY_IS_ERR(prev) && prev.value == 1);
        if (YETTY_IS_ERR(prev)) {
            yetty_ycore_error_destroy(prev.error);
        }
        char *sel = selection_text(doc);
        YTEST_CHECK(test, sel != NULL && strcmp(sel, "paragraph") == 0);
        free(sel);
    }

    /* Replace all "paragraph" -> "PARA": one per paragraph = 3, undoable. */
    struct yetty_ycore_int_result replaced = yetty_yrich_ydoc_replace_all(doc, "paragraph", "PARA");
    YTEST_CHECK(test, !YETTY_IS_ERR(replaced) && replaced.value == 3);
    if (YETTY_IS_ERR(replaced)) {
        yetty_ycore_error_destroy(replaced.error);
    }
    struct yetty_yclass_object_ptr_result para0 = yetty_yrich_ydoc_paragraph_at(doc, 0);
    YTEST_CHECK(test, !YETTY_IS_ERR(para0));
    if (!YETTY_IS_ERR(para0)) {
        struct yetty_ycore_const_char_ptr_result text = yetty_yrich_paragraph_text(para0.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(text) && text.value != NULL &&
                              strcmp(text.value, "First PARA with several words") == 0);
        if (YETTY_IS_ERR(text)) {
            yetty_ycore_error_destroy(text.error);
        }
    } else {
        yetty_ycore_error_destroy(para0.error);
    }

    /* One undo reverts every replacement. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_undo(doc)));
    para0 = yetty_yrich_ydoc_paragraph_at(doc, 0);
    if (!YETTY_IS_ERR(para0)) {
        struct yetty_ycore_const_char_ptr_result text = yetty_yrich_paragraph_text(para0.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(text) && text.value != NULL &&
                              strcmp(text.value, "First paragraph with several words") == 0);
        if (YETTY_IS_ERR(text)) {
            yetty_ycore_error_destroy(text.error);
        }
    } else {
        yetty_ycore_error_destroy(para0.error);
    }

    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

/* Discard a void result, freeing any error chain. */
static void destroy_maybe_void(struct yetty_ycore_void_result res)
{
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
}

/* True if table cell (row,col) of paragraph `index` equals `expected` (NULL =
 * empty cell). */
static int table_cell_is(struct yetty_yclass_object *doc, int32_t index, uint32_t row, uint32_t col,
                         const char *expected)
{
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_paragraph_at(doc, index);
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        return 0;
    }
    struct yetty_ycore_const_char_ptr_result cell =
        yetty_yrich_paragraph_table_cell(para.value, row, col);
    if (YETTY_IS_ERR(cell)) {
        yetty_ycore_error_destroy(cell.error);
        return 0;
    }
    if (!expected) {
        return cell.value == NULL || cell.value[0] == '\0';
    }
    return cell.value != NULL && strcmp(cell.value, expected) == 0;
}

/* Read a table paragraph's dimensions as rows*100+cols, -1 on error. */
static int table_dims(struct yetty_yclass_object *doc, int32_t index)
{
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_paragraph_at(doc, index);
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        return -1;
    }
    uint32_t rows = 0;
    uint32_t cols = 0;
    struct yetty_ycore_void_result res = yetty_yrich_paragraph_table_size(para.value, &rows, &cols);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
        return -1;
    }
    return (int)(rows * 100 + cols);
}

/* Table row/column insert and delete around the clicked cell. */
static void test_table_row_col_edits(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    YTEST_CHECK(test, !YETTY_IS_ERR(doc_res));
    if (YETTY_IS_ERR(doc_res)) {
        yetty_ycore_error_destroy(doc_res.error);
        return;
    }
    struct yetty_yclass_object *doc = doc_res.value;
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_add_paragraph(doc, "", 0);
    YTEST_CHECK(test, !YETTY_IS_ERR(para));
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        return;
    }
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_paragraph_set_table(para.value, 2, 2)));
    destroy_maybe_void(yetty_yrich_paragraph_set_table_cell(para.value, 0, 0, "A"));
    destroy_maybe_void(yetty_yrich_paragraph_set_table_cell(para.value, 0, 1, "B"));
    destroy_maybe_void(yetty_yrich_paragraph_set_table_cell(para.value, 1, 0, "C"));
    destroy_maybe_void(yetty_yrich_paragraph_set_table_cell(para.value, 1, 1, "D"));

    /* Click cell (0,0) — margin 20, so a point just inside the grid. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_on_mouse_down(doc, 25.0f, 25.0f, 0, 0)));
    YTEST_CHECK(test, table_dims(doc, 0) == 202);
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_table_edit(doc, 1))); /* insert col after 0 */
    YTEST_CHECK(test, table_dims(doc, 0) == 203);
    /* A/B stay put with a blank between: row0 = A | _ | B. */
    YTEST_CHECK(test, table_cell_is(doc, 0, 0, 0, "A"));
    YTEST_CHECK(test, table_cell_is(doc, 0, 0, 1, NULL));
    YTEST_CHECK(test, table_cell_is(doc, 0, 0, 2, "B"));
    YTEST_CHECK(test, table_cell_is(doc, 0, 1, 2, "D"));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_table_edit(doc, 3))); /* delete col 0 */
    YTEST_CHECK(test, table_dims(doc, 0) == 202);
    /* Column A removed: row0 = _ | B. */
    YTEST_CHECK(test, table_cell_is(doc, 0, 0, 0, NULL));
    YTEST_CHECK(test, table_cell_is(doc, 0, 0, 1, "B"));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_table_edit(doc, 0))); /* insert row */
    YTEST_CHECK(test, table_dims(doc, 0) == 302);
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_table_edit(doc, 2))); /* delete row */
    YTEST_CHECK(test, table_dims(doc, 0) == 202);

    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

/* 1 if the byte at `offset` of paragraph `index` is bold (covering run, else
 * base), -1 on error. */
static int char_is_bold(struct yetty_yclass_object *doc, int32_t index, int32_t offset)
{
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_paragraph_at(doc, index);
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        return -1;
    }
    struct yetty_ycore_size_result run_count = yetty_yrich_paragraph_run_count(para.value);
    if (YETTY_IS_ERR(run_count)) {
        yetty_ycore_error_destroy(run_count.error);
        return -1;
    }
    for (size_t i = 0; i < run_count.value; i++) {
        int32_t start = 0, end = 0;
        uint32_t fmt = 0, color = 0, bg = 0;
        float fs = 0.0f;
        struct yetty_ycore_void_result run =
            yetty_yrich_paragraph_run_get(para.value, i, &start, &end, &fmt, &color, &bg, &fs);
        if (YETTY_IS_ERR(run)) {
            yetty_ycore_error_destroy(run.error);
            return -1;
        }
        if (offset >= start && offset < end) {
            return (fmt & YETTY_YRICH_FMT_BOLD) ? 1 : 0;
        }
    }
    struct yetty_ycore_uint32_result base = yetty_yrich_paragraph_format(para.value);
    if (YETTY_IS_ERR(base)) {
        yetty_ycore_error_destroy(base.error);
        return -1;
    }
    return (base.value & YETTY_YRICH_FMT_BOLD) ? 1 : 0;
}

/* Paint format: copy a run's style, apply it to another selection; undoable. */
static void test_paint_format(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    YTEST_CHECK(test, !YETTY_IS_ERR(doc_res));
    if (YETTY_IS_ERR(doc_res)) {
        yetty_ycore_error_destroy(doc_res.error);
        return;
    }
    struct yetty_yclass_object *doc = doc_res.value;
    struct yetty_yclass_object_ptr_result para =
        yetty_yrich_ydoc_add_paragraph(doc, "Hello World", 11);
    YTEST_CHECK(test, !YETTY_IS_ERR(para));
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        return;
    }

    /* Bold "Hello" [0,5). */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_select_range(doc, 0, 0, 0, 5)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_toggle_format(doc, YETTY_YRICH_FMT_BOLD)));
    YTEST_CHECK(test, char_is_bold(doc, 0, 2) == 1); /* Hello bold */
    YTEST_CHECK(test, char_is_bold(doc, 0, 8) == 0); /* World not */

    /* Copy the style from inside "Hello", paint it onto "World" [6,11). */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_place_caret(doc, 0, 3)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_copy_format(doc)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_select_range(doc, 0, 6, 0, 11)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_paint_format(doc)));
    YTEST_CHECK(test, char_is_bold(doc, 0, 8) == 1); /* World now bold */

    /* Undo reverts the paint. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_undo(doc)));
    YTEST_CHECK(test, char_is_bold(doc, 0, 8) == 0);

    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

static int para_text_is(struct yetty_yclass_object *doc, int32_t index, const char *expected);

/* PageDown moves the caret down about a page of single-line paragraphs. */
static void test_page_down_caret(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    YTEST_CHECK(test, !YETTY_IS_ERR(doc_res));
    if (YETTY_IS_ERR(doc_res)) {
        yetty_ycore_error_destroy(doc_res.error);
        return;
    }
    struct yetty_yclass_object *doc = doc_res.value;
    for (int i = 0; i < 15; i++) {
        struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_add_paragraph(doc, "line", 4);
        if (YETTY_IS_ERR(para)) {
            yetty_ycore_error_destroy(para.error);
        }
    }
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_place_caret(doc, 0, 0)));
    YTEST_CHECK(test,
                !YETTY_IS_ERR(yetty_yrich_document_on_key_down(doc, YETTY_YRICH_KEY_PAGEDOWN, 0)));
    /* Type a marker at the caret; it should land 10 single-line paragraphs down. */
    struct yetty_ycore_buffer marker = {.data = (uint8_t *)"X", .size = 1, .capacity = 1};
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_document_on_text_input(doc, marker)));
    YTEST_CHECK(test, para_text_is(doc, 10, "Xline"));

    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

/* Insert > Page break creates a page-break block and a trailing text line. */
static void test_page_break(struct ytest *test)
{
    struct yetty_yclass_object *doc = make_doc();
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        return;
    }
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_place_caret(doc, 0, 1000)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_insert_page_break(doc)));
    /* 3 seed paragraphs + page-break block + trailing line = 5. */
    struct yetty_ycore_size_result count = yetty_yrich_ydoc_paragraph_count(doc);
    YTEST_CHECK(test, !YETTY_IS_ERR(count) && count.value == 5);
    if (YETTY_IS_ERR(count)) {
        yetty_ycore_error_destroy(count.error);
    }
    YTEST_CHECK(test, para_block_kind(doc, 1) == 3); /* the page break */
    YTEST_CHECK(test, para_block_kind(doc, 2) == 0); /* fresh trailing line */
    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

/* True if paragraph `index` has exactly `expected` text. */
static int para_text_is(struct yetty_yclass_object *doc, int32_t index, const char *expected)
{
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_paragraph_at(doc, index);
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        return 0;
    }
    struct yetty_ycore_const_char_ptr_result text = yetty_yrich_paragraph_text(para.value);
    if (YETTY_IS_ERR(text)) {
        yetty_ycore_error_destroy(text.error);
        return 0;
    }
    return text.value != NULL && strcmp(text.value, expected) == 0;
}

/* Table of contents built from the document's headings. */
static void test_table_of_contents(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    YTEST_CHECK(test, !YETTY_IS_ERR(doc_res));
    if (YETTY_IS_ERR(doc_res)) {
        yetty_ycore_error_destroy(doc_res.error);
        return;
    }
    struct yetty_yclass_object *doc = doc_res.value;
    struct {
        const char *text;
        uint32_t heading;
    } seed[] = {{"Intro", 1}, {"Details", 2}, {"Body text", 0}, {"Summary", 1}};
    for (size_t i = 0; i < sizeof(seed) / sizeof(seed[0]); i++) {
        struct yetty_yclass_object_ptr_result para =
            yetty_yrich_ydoc_add_paragraph(doc, seed[i].text, strlen(seed[i].text));
        YTEST_CHECK(test, !YETTY_IS_ERR(para));
        if (!YETTY_IS_ERR(para) && seed[i].heading > 0) {
            destroy_maybe_void(
                yetty_yrich_paragraph_set_heading_level(para.value, seed[i].heading));
        } else if (YETTY_IS_ERR(para)) {
            yetty_ycore_error_destroy(para.error);
        }
    }

    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_place_caret(doc, 0, 0)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_insert_toc(doc)));

    /* 4 seed paragraphs + "Contents" title + 3 heading entries = 8. */
    struct yetty_ycore_size_result count = yetty_yrich_ydoc_paragraph_count(doc);
    YTEST_CHECK(test, !YETTY_IS_ERR(count) && count.value == 8);
    if (YETTY_IS_ERR(count)) {
        yetty_ycore_error_destroy(count.error);
    }
    YTEST_CHECK(test, para_text_is(doc, 1, "Contents"));
    YTEST_CHECK(test, para_text_is(doc, 2, "Intro"));
    YTEST_CHECK(test, para_text_is(doc, 3, "Details"));
    YTEST_CHECK(test, para_text_is(doc, 4, "Summary"));

    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

int main(void)
{
    struct ytest test = ytest_begin("yrich_format");
    YTEST_RUN(&test, test_select_all_bold_bolds_every_paragraph);
    YTEST_RUN(&test, test_select_all_font_size_all_paragraphs);
    YTEST_RUN(&test, test_select_all_highlight_and_clear);
    YTEST_RUN(&test, test_select_all_line_spacing);
    YTEST_RUN(&test, test_select_all_heading_level);
    YTEST_RUN(&test, test_select_all_justify_alignment);
    YTEST_RUN(&test, test_checklist_toggle_and_undo);
    YTEST_RUN(&test, test_cross_paragraph_selection);
    YTEST_RUN(&test, test_double_click_word_and_drag);
    YTEST_RUN(&test, test_horizontal_rule_insert_and_delete);
    YTEST_RUN(&test, test_paragraph_spacing_and_list_nesting);
    YTEST_RUN(&test, test_word_count_find_replace);
    YTEST_RUN(&test, test_table_row_col_edits);
    YTEST_RUN(&test, test_table_of_contents);
    YTEST_RUN(&test, test_page_break);
    YTEST_RUN(&test, test_paint_format);
    YTEST_RUN(&test, test_page_down_caret);
    return ytest_end(&test);
}
