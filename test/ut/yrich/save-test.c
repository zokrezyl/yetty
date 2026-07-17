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

int main(void)
{
    struct ytest test = ytest_begin("yrich_save_atomic");
    YTEST_RUN(&test, test_save_roundtrip);
    YTEST_RUN(&test, test_save_failure_preserves_original);
    return ytest_end(&test);
}
