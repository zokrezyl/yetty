/*
 * yrich atomic-save contract (Phase-0A steps 5–6).
 *
 * Pins the temp-file -> fsync -> atomic-rename saver:
 *   - a document round-trips through save + load;
 *   - a save that fails partway (its temp file cannot be created) leaves the
 *     previously-saved destination byte-for-byte intact — never truncated.
 *
 * Uses the context-free construction path (yetty_yrich_ydoc_create(NULL)),
 * the same one tools/ydoc uses.
 */

#include <yetty/yrich/document.h>
#include <yetty/yrich/ydoc.h>
#include <yetty/yrich/yrich-export.h>
#include <yetty/yrich/yrich-types.h>
#include <yetty/yrich/yrich-yaml.h>

#include "ytest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static struct yetty_yclass_object *make_doc(const char *text)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    if (YETTY_IS_ERR(doc_res)) {
        yetty_ycore_error_destroy(doc_res.error);
        return NULL;
    }
    struct yetty_yclass_object *doc = doc_res.value;
    struct yetty_yclass_object_ptr_result para_res =
        yetty_yrich_ydoc_add_paragraph(doc, text, strlen(text));
    if (YETTY_IS_ERR(para_res)) {
        yetty_ycore_error_destroy(para_res.error);
        struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
        if (YETTY_IS_ERR(destroy)) {
            yetty_ycore_error_destroy(destroy.error);
        }
        return NULL;
    }
    return doc;
}

/* Discard a void result, freeing any error chain. */
static void destroy_maybe(struct yetty_ycore_void_result res)
{
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
}

static void destroy_doc(struct yetty_yclass_object *doc)
{
    if (!doc) {
        return;
    }
    struct yetty_ycore_void_result destroy = yetty_yrich_document_destroy(doc);
    if (YETTY_IS_ERR(destroy)) {
        yetty_ycore_error_destroy(destroy.error);
    }
}

/* True if the file at `path` loads and any paragraph equals `expected`. */
static bool file_has_paragraph(const char *path, const char *expected)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_load_yaml_file(path);
    if (YETTY_IS_ERR(doc_res)) {
        yetty_ycore_error_destroy(doc_res.error);
        return false;
    }
    struct yetty_yclass_object *doc = doc_res.value;
    bool found = false;
    struct yetty_ycore_size_result count = yetty_yrich_ydoc_paragraph_count(doc);
    if (YETTY_IS_ERR(count)) {
        yetty_ycore_error_destroy(count.error);
    } else {
        for (size_t i = 0; i < count.value && !found; i++) {
            struct yetty_yclass_object_ptr_result para =
                yetty_yrich_ydoc_paragraph_at(doc, (int32_t)i);
            if (YETTY_IS_ERR(para)) {
                yetty_ycore_error_destroy(para.error);
                continue;
            }
            struct yetty_ycore_const_char_ptr_result text = yetty_yrich_paragraph_text(para.value);
            if (YETTY_IS_ERR(text)) {
                yetty_ycore_error_destroy(text.error);
                continue;
            }
            if (text.value && strcmp(text.value, expected) == 0) {
                found = true;
            }
        }
    }
    destroy_doc(doc);
    return found;
}

static void test_save_roundtrip(struct ytest *test)
{
    char path[] = "/tmp/yrich-save-rt-XXXXXX";
    int fd = mkstemp(path);
    YTEST_CHECK(test, fd >= 0);
    if (fd < 0) {
        return;
    }
    close(fd); /* the atomic save replaces this file */

    struct yetty_yclass_object *doc = make_doc("Hello atomic save");
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        unlink(path);
        return;
    }

    struct yetty_ycore_void_result save = yetty_yrich_ydoc_save_yaml_file(doc, path);
    YTEST_CHECK(test, !YETTY_IS_ERR(save));
    if (YETTY_IS_ERR(save)) {
        yetty_ycore_error_destroy(save.error);
    }

    YTEST_CHECK(test, file_has_paragraph(path, "Hello atomic save"));

    destroy_doc(doc);
    unlink(path);
}

static void test_save_failure_preserves_original(struct ytest *test)
{
    char path[] = "/tmp/yrich-save-atomic-XXXXXX";
    int fd = mkstemp(path);
    YTEST_CHECK(test, fd >= 0);
    if (fd < 0) {
        return;
    }
    close(fd);

    /* 1. Save a good original. */
    struct yetty_yclass_object *original = make_doc("ORIGINAL");
    YTEST_CHECK(test, original != NULL);
    if (!original) {
        unlink(path);
        return;
    }
    struct yetty_ycore_void_result save1 = yetty_yrich_ydoc_save_yaml_file(original, path);
    YTEST_CHECK(test, !YETTY_IS_ERR(save1));
    if (YETTY_IS_ERR(save1)) {
        yetty_ycore_error_destroy(save1.error);
    }
    YTEST_CHECK(test, file_has_paragraph(path, "ORIGINAL"));

    /* 2. Occupy the saver's temp path (<path>.tmp) with a directory so its
     *    fopen(temp, "wb") cannot succeed — forcing a save failure that must
     *    leave the destination untouched. */
    char tmp_path[320];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    YTEST_CHECK(test, mkdir(tmp_path, 0777) == 0);

    /* 3. Saving a different document to the same path must fail. */
    struct yetty_yclass_object *replacement = make_doc("SHOULD NOT PERSIST");
    YTEST_CHECK(test, replacement != NULL);
    if (replacement) {
        struct yetty_ycore_void_result save2 = yetty_yrich_ydoc_save_yaml_file(replacement, path);
        YTEST_CHECK(test, YETTY_IS_ERR(save2)); /* temp open failed */
        if (YETTY_IS_ERR(save2)) {
            yetty_ycore_error_destroy(save2.error);
        }
    }

    /* 4. The destination must still be the intact original — never truncated,
     *    never partially overwritten. */
    YTEST_CHECK(test, file_has_paragraph(path, "ORIGINAL"));
    YTEST_CHECK(test, !file_has_paragraph(path, "SHOULD NOT PERSIST"));

    rmdir(tmp_path);
    unlink(path);
    destroy_doc(original);
    destroy_doc(replacement);
}

/* Justified alignment and checklist state survive save + reload. */
static void test_save_roundtrip_paragraph_attrs(struct ytest *test)
{
    char path[] = "/tmp/yrich-save-attrs-XXXXXX";
    int fd = mkstemp(path);
    YTEST_CHECK(test, fd >= 0);
    if (fd < 0) {
        return;
    }
    close(fd);

    struct yetty_yclass_object *doc = make_doc("Checked item with justified text");
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        unlink(path);
        return;
    }
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_paragraph_at(doc, 0);
    YTEST_CHECK(test, !YETTY_IS_ERR(para));
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        destroy_doc(doc);
        unlink(path);
        return;
    }
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_paragraph_set_alignment(
                          para.value, YETTY_YRICH_HALIGN_JUSTIFY)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_paragraph_set_list_kind(para.value, 3)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_paragraph_set_list_checked(para.value, 1)));
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_paragraph_set_block_kind(para.value, 1)));

    struct yetty_ycore_void_result save = yetty_yrich_ydoc_save_yaml_file(doc, path);
    YTEST_CHECK(test, !YETTY_IS_ERR(save));
    if (YETTY_IS_ERR(save)) {
        yetty_ycore_error_destroy(save.error);
    }
    destroy_doc(doc);

    struct yetty_yclass_object_ptr_result loaded_res = yetty_yrich_ydoc_load_yaml_file(path);
    YTEST_CHECK(test, !YETTY_IS_ERR(loaded_res));
    if (YETTY_IS_ERR(loaded_res)) {
        yetty_ycore_error_destroy(loaded_res.error);
        unlink(path);
        return;
    }
    struct yetty_yclass_object *loaded = loaded_res.value;
    struct yetty_yclass_object_ptr_result loaded_para = yetty_yrich_ydoc_paragraph_at(loaded, 0);
    YTEST_CHECK(test, !YETTY_IS_ERR(loaded_para));
    if (!YETTY_IS_ERR(loaded_para)) {
        struct yetty_ycore_uint32_result align = yetty_yrich_paragraph_alignment(loaded_para.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(align) && align.value == YETTY_YRICH_HALIGN_JUSTIFY);
        if (YETTY_IS_ERR(align)) {
            yetty_ycore_error_destroy(align.error);
        }
        struct yetty_ycore_uint32_result kind = yetty_yrich_paragraph_list_kind(loaded_para.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(kind) && kind.value == 3);
        if (YETTY_IS_ERR(kind)) {
            yetty_ycore_error_destroy(kind.error);
        }
        struct yetty_ycore_uint32_result checked =
            yetty_yrich_paragraph_list_checked(loaded_para.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(checked) && checked.value == 1);
        if (YETTY_IS_ERR(checked)) {
            yetty_ycore_error_destroy(checked.error);
        }
        struct yetty_ycore_uint32_result block =
            yetty_yrich_paragraph_block_kind(loaded_para.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(block) && block.value == 1);
        if (YETTY_IS_ERR(block)) {
            yetty_ycore_error_destroy(block.error);
        }
    } else {
        yetty_ycore_error_destroy(loaded_para.error);
    }
    destroy_doc(loaded);
    unlink(path);
}

/* Read a uint32 paragraph attribute via one of the accessors. */
typedef struct yetty_ycore_uint32_result (*u32_getter)(struct yetty_yclass_object *);
static uint32_t para_u32(struct yetty_yclass_object *doc, int32_t index, u32_getter get)
{
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_paragraph_at(doc, index);
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        return 0xFFFFFFFFu;
    }
    struct yetty_ycore_uint32_result res = get(para.value);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
        return 0xFFFFFFFFu;
    }
    return res.value;
}

/* A document with headings, nested lists, a checklist, a rule, and an inline
 * bold run survives Markdown export -> import with its structure intact. */
static void test_markdown_roundtrip(struct ytest *test)
{
    char path[] = "/tmp/yrich-md-XXXXXX";
    int fd = mkstemp(path);
    YTEST_CHECK(test, fd >= 0);
    if (fd < 0) {
        return;
    }
    close(fd);

    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    YTEST_CHECK(test, !YETTY_IS_ERR(doc_res));
    if (YETTY_IS_ERR(doc_res)) {
        yetty_ycore_error_destroy(doc_res.error);
        unlink(path);
        return;
    }
    struct yetty_yclass_object *doc = doc_res.value;
    struct yetty_yclass_object_ptr_result heading = yetty_yrich_ydoc_add_paragraph(doc, "Title", 5);
    if (!YETTY_IS_ERR(heading)) {
        destroy_maybe(yetty_yrich_paragraph_set_heading_level(heading.value, 2));
    } else {
        yetty_ycore_error_destroy(heading.error);
    }
    /* Paragraph with a bold run over "bold" (chars 5..9 of "Some bold text"). */
    struct yetty_yclass_object_ptr_result body =
        yetty_yrich_ydoc_add_paragraph(doc, "Some bold text", 14);
    if (!YETTY_IS_ERR(body)) {
        destroy_maybe(yetty_yrich_paragraph_add_run(body.value, 5, 9, YETTY_YRICH_FMT_BOLD,
                                                    YETTY_YRICH_COLOR_BLACK,
                                                    YETTY_YRICH_COLOR_TRANSPARENT, 14.0f));
    } else {
        yetty_ycore_error_destroy(body.error);
    }
    struct yetty_yclass_object_ptr_result item = yetty_yrich_ydoc_add_paragraph(doc, "Nested", 6);
    if (!YETTY_IS_ERR(item)) {
        destroy_maybe(yetty_yrich_paragraph_set_list_kind(item.value, 1));
        destroy_maybe(yetty_yrich_paragraph_set_list_level(item.value, 2));
    } else {
        yetty_ycore_error_destroy(item.error);
    }
    struct yetty_yclass_object_ptr_result check = yetty_yrich_ydoc_add_paragraph(doc, "Done", 4);
    if (!YETTY_IS_ERR(check)) {
        destroy_maybe(yetty_yrich_paragraph_set_list_kind(check.value, 3));
        destroy_maybe(yetty_yrich_paragraph_set_list_checked(check.value, 1));
    } else {
        yetty_ycore_error_destroy(check.error);
    }
    struct yetty_yclass_object_ptr_result rule = yetty_yrich_ydoc_add_paragraph(doc, "", 0);
    if (!YETTY_IS_ERR(rule)) {
        destroy_maybe(yetty_yrich_paragraph_set_block_kind(rule.value, 1));
    } else {
        yetty_ycore_error_destroy(rule.error);
    }

    struct yetty_ycore_void_result exp = yetty_yrich_ydoc_export_markdown_file(doc, path);
    YTEST_CHECK(test, !YETTY_IS_ERR(exp));
    if (YETTY_IS_ERR(exp)) {
        yetty_ycore_error_destroy(exp.error);
    }
    destroy_doc(doc);

    struct yetty_yclass_object_ptr_result loaded_res = yetty_yrich_ydoc_import_markdown_file(path);
    YTEST_CHECK(test, !YETTY_IS_ERR(loaded_res));
    if (YETTY_IS_ERR(loaded_res)) {
        yetty_ycore_error_destroy(loaded_res.error);
        unlink(path);
        return;
    }
    struct yetty_yclass_object *loaded = loaded_res.value;
    struct yetty_ycore_size_result count = yetty_yrich_ydoc_paragraph_count(loaded);
    YTEST_CHECK(test, !YETTY_IS_ERR(count) && count.value == 5);
    if (YETTY_IS_ERR(count)) {
        yetty_ycore_error_destroy(count.error);
    }
    YTEST_CHECK(test, para_u32(loaded, 0, yetty_yrich_paragraph_heading_level) == 2);
    YTEST_CHECK(test, para_u32(loaded, 2, yetty_yrich_paragraph_list_kind) == 1);
    YTEST_CHECK(test, para_u32(loaded, 2, yetty_yrich_paragraph_list_level) == 2);
    YTEST_CHECK(test, para_u32(loaded, 3, yetty_yrich_paragraph_list_kind) == 3);
    YTEST_CHECK(test, para_u32(loaded, 3, yetty_yrich_paragraph_list_checked) == 1);
    YTEST_CHECK(test, para_u32(loaded, 4, yetty_yrich_paragraph_block_kind) == 1);
    /* The bold run survived. */
    struct yetty_yclass_object_ptr_result body_res = yetty_yrich_ydoc_paragraph_at(loaded, 1);
    YTEST_CHECK(test, !YETTY_IS_ERR(body_res));
    if (!YETTY_IS_ERR(body_res)) {
        struct yetty_ycore_size_result runs = yetty_yrich_paragraph_run_count(body_res.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(runs) && runs.value >= 1);
        if (YETTY_IS_ERR(runs)) {
            yetty_ycore_error_destroy(runs.error);
        }
        struct yetty_ycore_const_char_ptr_result text = yetty_yrich_paragraph_text(body_res.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(text) && text.value != NULL &&
                              strcmp(text.value, "Some bold text") == 0);
        if (YETTY_IS_ERR(text)) {
            yetty_ycore_error_destroy(text.error);
        }
    } else {
        yetty_ycore_error_destroy(body_res.error);
    }
    destroy_doc(loaded);
    unlink(path);
}

/* A table paragraph (dimensions + cell text) survives native save/reload. */
static void test_table_roundtrip(struct ytest *test)
{
    char path[] = "/tmp/yrich-table-XXXXXX";
    int fd = mkstemp(path);
    YTEST_CHECK(test, fd >= 0);
    if (fd < 0) {
        return;
    }
    close(fd);

    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    YTEST_CHECK(test, !YETTY_IS_ERR(doc_res));
    if (YETTY_IS_ERR(doc_res)) {
        yetty_ycore_error_destroy(doc_res.error);
        unlink(path);
        return;
    }
    struct yetty_yclass_object *doc = doc_res.value;
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_add_paragraph(doc, "", 0);
    YTEST_CHECK(test, !YETTY_IS_ERR(para));
    if (YETTY_IS_ERR(para)) {
        yetty_ycore_error_destroy(para.error);
        destroy_doc(doc);
        unlink(path);
        return;
    }
    destroy_maybe(yetty_yrich_paragraph_set_table(para.value, 2, 2));
    destroy_maybe(yetty_yrich_paragraph_set_table_cell(para.value, 0, 0, "A1"));
    destroy_maybe(yetty_yrich_paragraph_set_table_cell(para.value, 0, 1, "B1"));
    destroy_maybe(yetty_yrich_paragraph_set_table_cell(para.value, 1, 0, "A2"));
    destroy_maybe(yetty_yrich_paragraph_set_table_cell(para.value, 1, 1, "B2"));

    struct yetty_ycore_void_result save = yetty_yrich_ydoc_save_yaml_file(doc, path);
    YTEST_CHECK(test, !YETTY_IS_ERR(save));
    if (YETTY_IS_ERR(save)) {
        yetty_ycore_error_destroy(save.error);
    }
    destroy_doc(doc);

    struct yetty_yclass_object_ptr_result loaded_res = yetty_yrich_ydoc_load_yaml_file(path);
    YTEST_CHECK(test, !YETTY_IS_ERR(loaded_res));
    if (YETTY_IS_ERR(loaded_res)) {
        yetty_ycore_error_destroy(loaded_res.error);
        unlink(path);
        return;
    }
    struct yetty_yclass_object *loaded = loaded_res.value;
    struct yetty_yclass_object_ptr_result lpara = yetty_yrich_ydoc_paragraph_at(loaded, 0);
    YTEST_CHECK(test, !YETTY_IS_ERR(lpara));
    if (!YETTY_IS_ERR(lpara)) {
        struct yetty_ycore_uint32_result block = yetty_yrich_paragraph_block_kind(lpara.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(block) && block.value == 2);
        if (YETTY_IS_ERR(block)) {
            yetty_ycore_error_destroy(block.error);
        }
        uint32_t rows = 0;
        uint32_t cols = 0;
        destroy_maybe(yetty_yrich_paragraph_table_size(lpara.value, &rows, &cols));
        YTEST_CHECK(test, rows == 2 && cols == 2);
        struct yetty_ycore_const_char_ptr_result c00 =
            yetty_yrich_paragraph_table_cell(lpara.value, 0, 0);
        struct yetty_ycore_const_char_ptr_result c11 =
            yetty_yrich_paragraph_table_cell(lpara.value, 1, 1);
        YTEST_CHECK(test, !YETTY_IS_ERR(c00) && c00.value && strcmp(c00.value, "A1") == 0);
        YTEST_CHECK(test, !YETTY_IS_ERR(c11) && c11.value && strcmp(c11.value, "B2") == 0);
        if (YETTY_IS_ERR(c00)) {
            yetty_ycore_error_destroy(c00.error);
        }
        if (YETTY_IS_ERR(c11)) {
            yetty_ycore_error_destroy(c11.error);
        }
    } else {
        yetty_ycore_error_destroy(lpara.error);
    }
    destroy_doc(loaded);
    unlink(path);
}

/* Saved files carry a schema version; a newer-than-supported version is
 * rejected, and a versionless legacy file still loads. */
static void test_schema_version(struct ytest *test)
{
    char path[] = "/tmp/yrich-ver-XXXXXX";
    int fd = mkstemp(path);
    YTEST_CHECK(test, fd >= 0);
    if (fd < 0) {
        return;
    }
    close(fd);

    struct yetty_yclass_object *doc = make_doc("Versioned");
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        unlink(path);
        return;
    }
    struct yetty_ycore_void_result save = yetty_yrich_ydoc_save_yaml_file(doc, path);
    YTEST_CHECK(test, !YETTY_IS_ERR(save));
    if (YETTY_IS_ERR(save)) {
        yetty_ycore_error_destroy(save.error);
    }
    destroy_doc(doc);

    /* The file records version 1. */
    FILE *file = fopen(path, "rb");
    YTEST_CHECK(test, file != NULL);
    if (file) {
        char buffer[4096];
        size_t read = fread(buffer, 1, sizeof(buffer) - 1, file);
        buffer[read] = '\0';
        fclose(file);
        YTEST_CHECK(test, strstr(buffer, "version") != NULL);
    }
    unlink(path);

    /* A legacy file with no version key still loads. */
    const char *legacy = "document:\n  pageWidth: 600\n  margin: 20\n  paragraphs:\n"
                         "    - text: \"Legacy\"\n";
    struct yetty_yclass_object_ptr_result legacy_res =
        yetty_yrich_ydoc_load_yaml(legacy, strlen(legacy));
    YTEST_CHECK(test, !YETTY_IS_ERR(legacy_res));
    if (!YETTY_IS_ERR(legacy_res)) {
        destroy_doc(legacy_res.value);
    } else {
        yetty_ycore_error_destroy(legacy_res.error);
    }

    /* A version from the future is rejected. */
    const char *future = "document:\n  version: 999\n  paragraphs:\n    - text: \"X\"\n";
    struct yetty_yclass_object_ptr_result future_res =
        yetty_yrich_ydoc_load_yaml(future, strlen(future));
    YTEST_CHECK(test, YETTY_IS_ERR(future_res));
    if (YETTY_IS_ERR(future_res)) {
        yetty_ycore_error_destroy(future_res.error);
    } else {
        destroy_doc(future_res.value);
    }
}

/* HTML export -> import preserves heading level, a horizontal rule, and an
 * inline bold run; entities in text survive escaping. */
static void test_html_roundtrip(struct ytest *test)
{
    char path[] = "/tmp/yrich-html-XXXXXX";
    int fd = mkstemp(path);
    YTEST_CHECK(test, fd >= 0);
    if (fd < 0) {
        return;
    }
    close(fd);

    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    YTEST_CHECK(test, !YETTY_IS_ERR(doc_res));
    if (YETTY_IS_ERR(doc_res)) {
        yetty_ycore_error_destroy(doc_res.error);
        unlink(path);
        return;
    }
    struct yetty_yclass_object *doc = doc_res.value;
    struct yetty_yclass_object_ptr_result heading = yetty_yrich_ydoc_add_paragraph(doc, "Title", 5);
    if (!YETTY_IS_ERR(heading)) {
        destroy_maybe(yetty_yrich_paragraph_set_heading_level(heading.value, 3));
    } else {
        yetty_ycore_error_destroy(heading.error);
    }
    struct yetty_yclass_object_ptr_result body =
        yetty_yrich_ydoc_add_paragraph(doc, "a < b and bold text", 19);
    if (!YETTY_IS_ERR(body)) {
        /* "bold" is chars 10..14. */
        destroy_maybe(yetty_yrich_paragraph_add_run(body.value, 10, 14, YETTY_YRICH_FMT_BOLD,
                                                    YETTY_YRICH_COLOR_BLACK,
                                                    YETTY_YRICH_COLOR_TRANSPARENT, 14.0f));
    } else {
        yetty_ycore_error_destroy(body.error);
    }
    struct yetty_yclass_object_ptr_result rule = yetty_yrich_ydoc_add_paragraph(doc, "", 0);
    if (!YETTY_IS_ERR(rule)) {
        destroy_maybe(yetty_yrich_paragraph_set_block_kind(rule.value, 1));
    } else {
        yetty_ycore_error_destroy(rule.error);
    }

    struct yetty_ycore_void_result exp = yetty_yrich_ydoc_export_html_file(doc, path);
    YTEST_CHECK(test, !YETTY_IS_ERR(exp));
    if (YETTY_IS_ERR(exp)) {
        yetty_ycore_error_destroy(exp.error);
    }
    destroy_doc(doc);

    struct yetty_yclass_object_ptr_result loaded_res = yetty_yrich_ydoc_import_html_file(path);
    YTEST_CHECK(test, !YETTY_IS_ERR(loaded_res));
    if (YETTY_IS_ERR(loaded_res)) {
        yetty_ycore_error_destroy(loaded_res.error);
        unlink(path);
        return;
    }
    struct yetty_yclass_object *loaded = loaded_res.value;
    struct yetty_ycore_size_result count = yetty_yrich_ydoc_paragraph_count(loaded);
    YTEST_CHECK(test, !YETTY_IS_ERR(count) && count.value == 3);
    if (YETTY_IS_ERR(count)) {
        yetty_ycore_error_destroy(count.error);
    }
    struct yetty_yclass_object_ptr_result h = yetty_yrich_ydoc_paragraph_at(loaded, 0);
    if (!YETTY_IS_ERR(h)) {
        struct yetty_ycore_uint32_result hl = yetty_yrich_paragraph_heading_level(h.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(hl) && hl.value == 3);
        if (YETTY_IS_ERR(hl)) {
            yetty_ycore_error_destroy(hl.error);
        }
    } else {
        yetty_ycore_error_destroy(h.error);
    }
    struct yetty_yclass_object_ptr_result b = yetty_yrich_ydoc_paragraph_at(loaded, 1);
    if (!YETTY_IS_ERR(b)) {
        struct yetty_ycore_const_char_ptr_result txt = yetty_yrich_paragraph_text(b.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(txt) && txt.value != NULL &&
                              strcmp(txt.value, "a < b and bold text") == 0);
        if (YETTY_IS_ERR(txt)) {
            yetty_ycore_error_destroy(txt.error);
        }
        struct yetty_ycore_size_result runs = yetty_yrich_paragraph_run_count(b.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(runs) && runs.value >= 1);
        if (YETTY_IS_ERR(runs)) {
            yetty_ycore_error_destroy(runs.error);
        }
    } else {
        yetty_ycore_error_destroy(b.error);
    }
    struct yetty_yclass_object_ptr_result r = yetty_yrich_ydoc_paragraph_at(loaded, 2);
    if (!YETTY_IS_ERR(r)) {
        struct yetty_ycore_uint32_result bk = yetty_yrich_paragraph_block_kind(r.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(bk) && bk.value == 1);
        if (YETTY_IS_ERR(bk)) {
            yetty_ycore_error_destroy(bk.error);
        }
    } else {
        yetty_ycore_error_destroy(r.error);
    }
    destroy_doc(loaded);
    unlink(path);
}

/* RTF export -> import preserves heading level, a horizontal rule, an inline
 * bold run, and RTF-metacharacter text (a literal brace). */
static void test_rtf_roundtrip(struct ytest *test)
{
    char path[] = "/tmp/yrich-rtf-XXXXXX";
    int fd = mkstemp(path);
    YTEST_CHECK(test, fd >= 0);
    if (fd < 0) {
        return;
    }
    close(fd);

    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    YTEST_CHECK(test, !YETTY_IS_ERR(doc_res));
    if (YETTY_IS_ERR(doc_res)) {
        yetty_ycore_error_destroy(doc_res.error);
        unlink(path);
        return;
    }
    struct yetty_yclass_object *doc = doc_res.value;
    struct yetty_yclass_object_ptr_result heading =
        yetty_yrich_ydoc_add_paragraph(doc, "Chapter", 7);
    if (!YETTY_IS_ERR(heading)) {
        destroy_maybe(yetty_yrich_paragraph_set_heading_level(heading.value, 2));
    } else {
        yetty_ycore_error_destroy(heading.error);
    }
    struct yetty_yclass_object_ptr_result body =
        yetty_yrich_ydoc_add_paragraph(doc, "a {brace} and bold text", 23);
    if (!YETTY_IS_ERR(body)) {
        /* "bold" is chars 14..18. */
        destroy_maybe(yetty_yrich_paragraph_add_run(body.value, 14, 18, YETTY_YRICH_FMT_BOLD,
                                                    YETTY_YRICH_COLOR_BLACK,
                                                    YETTY_YRICH_COLOR_TRANSPARENT, 14.0f));
    } else {
        yetty_ycore_error_destroy(body.error);
    }
    struct yetty_yclass_object_ptr_result rule = yetty_yrich_ydoc_add_paragraph(doc, "", 0);
    if (!YETTY_IS_ERR(rule)) {
        destroy_maybe(yetty_yrich_paragraph_set_block_kind(rule.value, 1));
    } else {
        yetty_ycore_error_destroy(rule.error);
    }

    struct yetty_ycore_void_result exp = yetty_yrich_ydoc_export_rtf_file(doc, path);
    YTEST_CHECK(test, !YETTY_IS_ERR(exp));
    if (YETTY_IS_ERR(exp)) {
        yetty_ycore_error_destroy(exp.error);
    }
    destroy_doc(doc);

    struct yetty_yclass_object_ptr_result loaded_res = yetty_yrich_ydoc_import_rtf_file(path);
    YTEST_CHECK(test, !YETTY_IS_ERR(loaded_res));
    if (YETTY_IS_ERR(loaded_res)) {
        yetty_ycore_error_destroy(loaded_res.error);
        unlink(path);
        return;
    }
    struct yetty_yclass_object *loaded = loaded_res.value;
    struct yetty_ycore_size_result count = yetty_yrich_ydoc_paragraph_count(loaded);
    YTEST_CHECK(test, !YETTY_IS_ERR(count) && count.value == 3);
    if (YETTY_IS_ERR(count)) {
        yetty_ycore_error_destroy(count.error);
    }
    struct yetty_yclass_object_ptr_result h = yetty_yrich_ydoc_paragraph_at(loaded, 0);
    if (!YETTY_IS_ERR(h)) {
        struct yetty_ycore_uint32_result hl = yetty_yrich_paragraph_heading_level(h.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(hl) && hl.value == 2);
        if (YETTY_IS_ERR(hl)) {
            yetty_ycore_error_destroy(hl.error);
        }
    } else {
        yetty_ycore_error_destroy(h.error);
    }
    struct yetty_yclass_object_ptr_result b = yetty_yrich_ydoc_paragraph_at(loaded, 1);
    if (!YETTY_IS_ERR(b)) {
        struct yetty_ycore_const_char_ptr_result txt = yetty_yrich_paragraph_text(b.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(txt) && txt.value != NULL &&
                              strcmp(txt.value, "a {brace} and bold text") == 0);
        if (YETTY_IS_ERR(txt)) {
            yetty_ycore_error_destroy(txt.error);
        }
        struct yetty_ycore_size_result runs = yetty_yrich_paragraph_run_count(b.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(runs) && runs.value >= 1);
        if (YETTY_IS_ERR(runs)) {
            yetty_ycore_error_destroy(runs.error);
        }
    } else {
        yetty_ycore_error_destroy(b.error);
    }
    struct yetty_yclass_object_ptr_result r = yetty_yrich_ydoc_paragraph_at(loaded, 2);
    if (!YETTY_IS_ERR(r)) {
        struct yetty_ycore_uint32_result bk = yetty_yrich_paragraph_block_kind(r.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(bk) && bk.value == 1);
        if (YETTY_IS_ERR(bk)) {
            yetty_ycore_error_destroy(bk.error);
        }
    } else {
        yetty_ycore_error_destroy(r.error);
    }
    destroy_doc(loaded);
    unlink(path);
}

/* Editable links: set a hyperlink over a selection, query it, and confirm it
 * survives a YAML save/reload with its URL intact. */
static void test_link_roundtrip(struct ytest *test)
{
    char path[] = "/tmp/yrich-link-XXXXXX";
    int fd = mkstemp(path);
    YTEST_CHECK(test, fd >= 0);
    if (fd < 0) {
        return;
    }
    close(fd);

    struct yetty_yclass_object *doc = make_doc("Visit example dot com now");
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        unlink(path);
        return;
    }
    /* Select "example dot com" (chars 6..21) and link it. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_select_range(doc, 0, 6, 0, 21)));
    struct yetty_ycore_void_result set_res = yetty_yrich_ydoc_set_link(doc, "https://example.com");
    YTEST_CHECK(test, !YETTY_IS_ERR(set_res));
    if (YETTY_IS_ERR(set_res)) {
        yetty_ycore_error_destroy(set_res.error);
    }

    /* A run now carries a non-zero link id resolving to the URL. */
    struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_paragraph_at(doc, 0);
    YTEST_CHECK(test, !YETTY_IS_ERR(para));
    int found_link = 0;
    if (!YETTY_IS_ERR(para)) {
        struct yetty_ycore_size_result runs = yetty_yrich_paragraph_run_count(para.value);
        if (!YETTY_IS_ERR(runs)) {
            for (size_t i = 0; i < runs.value; i++) {
                struct yetty_ycore_uint32_result id =
                    yetty_yrich_paragraph_run_link_id(para.value, i);
                if (!YETTY_IS_ERR(id) && id.value != 0) {
                    struct yetty_ycore_const_char_ptr_result url =
                        yetty_yrich_ydoc_link_url(doc, id.value);
                    if (!YETTY_IS_ERR(url) && url.value &&
                        strcmp(url.value, "https://example.com") == 0) {
                        found_link = 1;
                    }
                    if (YETTY_IS_ERR(url)) {
                        yetty_ycore_error_destroy(url.error);
                    }
                }
                if (YETTY_IS_ERR(id)) {
                    yetty_ycore_error_destroy(id.error);
                }
            }
        } else {
            yetty_ycore_error_destroy(runs.error);
        }
    } else {
        yetty_ycore_error_destroy(para.error);
    }
    YTEST_CHECK(test, found_link);

    /* link_at_caret resolves the URL when the caret sits inside the link. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_place_caret(doc, 0, 10)));
    struct yetty_ycore_const_char_ptr_result at_caret = yetty_yrich_ydoc_link_at_caret(doc);
    YTEST_CHECK(test, !YETTY_IS_ERR(at_caret) && at_caret.value != NULL &&
                          strcmp(at_caret.value, "https://example.com") == 0);
    if (YETTY_IS_ERR(at_caret)) {
        yetty_ycore_error_destroy(at_caret.error);
    }

    struct yetty_ycore_void_result save = yetty_yrich_ydoc_save_yaml_file(doc, path);
    YTEST_CHECK(test, !YETTY_IS_ERR(save));
    if (YETTY_IS_ERR(save)) {
        yetty_ycore_error_destroy(save.error);
    }
    destroy_doc(doc);

    /* Reload: the link URL survives on some run. */
    struct yetty_yclass_object_ptr_result loaded_res = yetty_yrich_ydoc_load_yaml_file(path);
    YTEST_CHECK(test, !YETTY_IS_ERR(loaded_res));
    if (YETTY_IS_ERR(loaded_res)) {
        yetty_ycore_error_destroy(loaded_res.error);
        unlink(path);
        return;
    }
    struct yetty_yclass_object *loaded = loaded_res.value;
    struct yetty_yclass_object_ptr_result lpara = yetty_yrich_ydoc_paragraph_at(loaded, 0);
    int survived = 0;
    if (!YETTY_IS_ERR(lpara)) {
        struct yetty_ycore_size_result runs = yetty_yrich_paragraph_run_count(lpara.value);
        if (!YETTY_IS_ERR(runs)) {
            for (size_t i = 0; i < runs.value; i++) {
                struct yetty_ycore_uint32_result id =
                    yetty_yrich_paragraph_run_link_id(lpara.value, i);
                if (!YETTY_IS_ERR(id) && id.value != 0) {
                    struct yetty_ycore_const_char_ptr_result url =
                        yetty_yrich_ydoc_link_url(loaded, id.value);
                    if (!YETTY_IS_ERR(url) && url.value &&
                        strcmp(url.value, "https://example.com") == 0) {
                        survived = 1;
                    }
                    if (YETTY_IS_ERR(url)) {
                        yetty_ycore_error_destroy(url.error);
                    }
                }
                if (YETTY_IS_ERR(id)) {
                    yetty_ycore_error_destroy(id.error);
                }
            }
        } else {
            yetty_ycore_error_destroy(runs.error);
        }
    } else {
        yetty_ycore_error_destroy(lpara.error);
    }
    YTEST_CHECK(test, survived);

    /* Remove the link at the caret; the run's link id clears. */
    YTEST_CHECK(test, !YETTY_IS_ERR(yetty_yrich_ydoc_place_caret(loaded, 0, 10)));
    struct yetty_ycore_void_result rm = yetty_yrich_ydoc_remove_link(loaded);
    YTEST_CHECK(test, !YETTY_IS_ERR(rm));
    if (YETTY_IS_ERR(rm)) {
        yetty_ycore_error_destroy(rm.error);
    }
    struct yetty_ycore_const_char_ptr_result after = yetty_yrich_ydoc_link_at_caret(loaded);
    YTEST_CHECK(test, !YETTY_IS_ERR(after) && after.value == NULL);
    if (YETTY_IS_ERR(after)) {
        yetty_ycore_error_destroy(after.error);
    }
    destroy_doc(loaded);
    unlink(path);
}

/* Bookmarks: anchor a named bookmark on a paragraph, confirm it survives a
 * YAML save/reload, and that goto navigates to it (and misses cleanly). */
static void test_bookmark_roundtrip(struct ytest *test)
{
    char path[] = "/tmp/yrich-bm-XXXXXX";
    int fd = mkstemp(path);
    YTEST_CHECK(test, fd >= 0);
    if (fd < 0) {
        return;
    }
    close(fd);

    struct yetty_yclass_object_ptr_result doc_res = yetty_yrich_ydoc_create(NULL);
    YTEST_CHECK(test, !YETTY_IS_ERR(doc_res));
    if (YETTY_IS_ERR(doc_res)) {
        yetty_ycore_error_destroy(doc_res.error);
        unlink(path);
        return;
    }
    struct yetty_yclass_object *doc = doc_res.value;
    struct yetty_yclass_object_ptr_result p0 = yetty_yrich_ydoc_add_paragraph(doc, "Intro", 5);
    if (YETTY_IS_ERR(p0)) {
        yetty_ycore_error_destroy(p0.error);
    }
    struct yetty_yclass_object_ptr_result p1 =
        yetty_yrich_ydoc_add_paragraph(doc, "Target here", 11);
    if (!YETTY_IS_ERR(p1)) {
        destroy_maybe(yetty_yrich_paragraph_set_bookmark(p1.value, "sec2"));
    } else {
        yetty_ycore_error_destroy(p1.error);
    }

    struct yetty_ycore_void_result save = yetty_yrich_ydoc_save_yaml_file(doc, path);
    YTEST_CHECK(test, !YETTY_IS_ERR(save));
    if (YETTY_IS_ERR(save)) {
        yetty_ycore_error_destroy(save.error);
    }
    destroy_doc(doc);

    struct yetty_yclass_object_ptr_result loaded_res = yetty_yrich_ydoc_load_yaml_file(path);
    YTEST_CHECK(test, !YETTY_IS_ERR(loaded_res));
    if (YETTY_IS_ERR(loaded_res)) {
        yetty_ycore_error_destroy(loaded_res.error);
        unlink(path);
        return;
    }
    struct yetty_yclass_object *loaded = loaded_res.value;
    /* The bookmark string survived on the right paragraph. */
    struct yetty_yclass_object_ptr_result lp = yetty_yrich_ydoc_paragraph_at(loaded, 1);
    if (!YETTY_IS_ERR(lp)) {
        struct yetty_ycore_const_char_ptr_result bm = yetty_yrich_paragraph_bookmark(lp.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(bm) && bm.value != NULL && strcmp(bm.value, "sec2") == 0);
        if (YETTY_IS_ERR(bm)) {
            yetty_ycore_error_destroy(bm.error);
        }
    } else {
        yetty_ycore_error_destroy(lp.error);
    }
    /* goto finds it (returns 1) and misses a bogus name (returns 0). */
    struct yetty_ycore_int_result hit = yetty_yrich_ydoc_goto_bookmark(loaded, "sec2");
    YTEST_CHECK(test, !YETTY_IS_ERR(hit) && hit.value == 1);
    if (YETTY_IS_ERR(hit)) {
        yetty_ycore_error_destroy(hit.error);
    }
    struct yetty_ycore_int_result miss = yetty_yrich_ydoc_goto_bookmark(loaded, "nope");
    YTEST_CHECK(test, !YETTY_IS_ERR(miss) && miss.value == 0);
    if (YETTY_IS_ERR(miss)) {
        yetty_ycore_error_destroy(miss.error);
    }
    destroy_doc(loaded);
    unlink(path);
}

/* Inline images: source path + document-space bounds survive save + reload. */
static void test_image_roundtrip(struct ytest *test)
{
    char path[] = "/tmp/yrich-img-XXXXXX";
    int fd = mkstemp(path);
    YTEST_CHECK(test, fd >= 0);
    if (fd < 0) {
        return;
    }
    close(fd);

    struct yetty_yclass_object *doc = make_doc("Doc with an image");
    YTEST_CHECK(test, doc != NULL);
    if (!doc) {
        unlink(path);
        return;
    }
    struct yetty_yclass_object_ptr_result img =
        yetty_yrich_ydoc_insert_image(doc, -1, 320.0f, 240.0f);
    YTEST_CHECK(test, !YETTY_IS_ERR(img));
    if (!YETTY_IS_ERR(img)) {
        destroy_maybe(yetty_yrich_inline_image_set_bounds(img.value, 40.0f, 60.0f, 320.0f, 240.0f));
        destroy_maybe(yetty_yrich_inline_image_set_source(img.value, "/tmp/some-image.png"));
    } else {
        yetty_ycore_error_destroy(img.error);
    }

    struct yetty_ycore_void_result save = yetty_yrich_ydoc_save_yaml_file(doc, path);
    YTEST_CHECK(test, !YETTY_IS_ERR(save));
    if (YETTY_IS_ERR(save)) {
        yetty_ycore_error_destroy(save.error);
    }
    destroy_doc(doc);

    struct yetty_yclass_object_ptr_result loaded_res = yetty_yrich_ydoc_load_yaml_file(path);
    YTEST_CHECK(test, !YETTY_IS_ERR(loaded_res));
    if (YETTY_IS_ERR(loaded_res)) {
        yetty_ycore_error_destroy(loaded_res.error);
        unlink(path);
        return;
    }
    struct yetty_yclass_object *loaded = loaded_res.value;
    struct yetty_ycore_size_result count = yetty_yrich_ydoc_image_count(loaded);
    YTEST_CHECK(test, !YETTY_IS_ERR(count) && count.value == 1);
    if (YETTY_IS_ERR(count)) {
        yetty_ycore_error_destroy(count.error);
    }
    struct yetty_yclass_object_ptr_result limg = yetty_yrich_ydoc_image_at(loaded, 0);
    if (!YETTY_IS_ERR(limg) && limg.value) {
        struct yetty_ycore_const_char_ptr_result src = yetty_yrich_inline_image_source(limg.value);
        YTEST_CHECK(test, !YETTY_IS_ERR(src) && src.value != NULL &&
                              strcmp(src.value, "/tmp/some-image.png") == 0);
        if (YETTY_IS_ERR(src)) {
            yetty_ycore_error_destroy(src.error);
        }
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        struct yetty_ycore_void_result b =
            yetty_yrich_inline_image_bounds(limg.value, &x, &y, &w, &h);
        YTEST_CHECK(test,
                    !YETTY_IS_ERR(b) && x == 40.0f && y == 60.0f && w == 320.0f && h == 240.0f);
        if (YETTY_IS_ERR(b)) {
            yetty_ycore_error_destroy(b.error);
        }
    } else {
        if (YETTY_IS_ERR(limg)) {
            yetty_ycore_error_destroy(limg.error);
        }
        YTEST_CHECK(test, 0);
    }
    destroy_doc(loaded);
    unlink(path);
}

int main(void)
{
    struct ytest test = ytest_begin("yrich_save_atomic");
    YTEST_RUN(&test, test_save_roundtrip);
    YTEST_RUN(&test, test_save_failure_preserves_original);
    YTEST_RUN(&test, test_save_roundtrip_paragraph_attrs);
    YTEST_RUN(&test, test_markdown_roundtrip);
    YTEST_RUN(&test, test_table_roundtrip);
    YTEST_RUN(&test, test_schema_version);
    YTEST_RUN(&test, test_html_roundtrip);
    YTEST_RUN(&test, test_rtf_roundtrip);
    YTEST_RUN(&test, test_link_roundtrip);
    YTEST_RUN(&test, test_bookmark_roundtrip);
    YTEST_RUN(&test, test_image_roundtrip);
    return ytest_end(&test);
}
