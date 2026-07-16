/*
 * ynotebook execution-routing contract test.
 *
 * Drives cell_apply_message with simulated kernel output messages and checks the
 * nbformat output rules: same-name stream records coalesce, rich outputs append,
 * clear_output(wait=false) clears immediately, and clear_output(wait=true)
 * defers until the next output arrives.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <yetty/ynotebook/notebook.h>
#include <yetty/ynotebook/mime-bundle.h>

static int failures;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s\n", (msg));                                                  \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const char *NOTEBOOK =
    "{\"nbformat\":4,\"nbformat_minor\":5,\"metadata\":{},\"cells\":["
    "{\"cell_type\":\"code\",\"id\":\"c1\",\"execution_count\":null,\"metadata\":{},"
    "\"source\":\"x\",\"outputs\":[]}]}";

static size_t out_count(struct yetty_yclass_object *cell)
{
    struct yetty_ycore_size_result r = yetty_ynotebook_cell_output_count(cell);
    return YETTY_IS_OK(r) ? r.value : (size_t)-1;
}

int main(void)
{
    CHECK(YETTY_IS_OK(yetty_ynotebook_register()), "module register");

    struct yetty_yclass_ctx ctx = {0};
    struct yetty_yclass_object_ptr_result nb_r = yetty_ynotebook_notebook_create(&ctx);
    if (YETTY_IS_ERR(nb_r))
        return 1;
    struct yetty_yclass_object *notebook = nb_r.value;
    CHECK(YETTY_IS_OK(yetty_ynotebook_notebook_load_text(notebook, NOTEBOOK)), "load notebook");

    struct yetty_yclass_object_ptr_result cell_r = yetty_ynotebook_notebook_cell_at(notebook, 0);
    struct yetty_yclass_object *cell = cell_r.value;
    CHECK(out_count(cell) == 0, "cell starts with no outputs");

    /* two stdout streams coalesce into one output */
    yetty_ynotebook_cell_apply_message(cell, "stream", "{\"name\":\"stdout\",\"text\":\"Hello \"}");
    yetty_ynotebook_cell_apply_message(cell, "stream", "{\"name\":\"stdout\",\"text\":\"world\\n\"}");
    CHECK(out_count(cell) == 1, "streams coalesced to one output");
    {
        struct yetty_yclass_object_ptr_result o0 = yetty_ynotebook_cell_output_at(cell, 0);
        struct yetty_ycore_const_char_ptr_result t = yetty_ynotebook_output_type(o0.value);
        CHECK(YETTY_IS_OK(t) && strcmp(t.value, "stream") == 0, "output 0 is stream");
        struct yetty_ycore_char_ptr_result txt = yetty_ynotebook_output_text(o0.value);
        CHECK(YETTY_IS_OK(txt) && strcmp(txt.value, "Hello world\n") == 0, "stream text coalesced");
        if (YETTY_IS_OK(txt))
            free(txt.value);
    }

    /* a rich execute_result appends a second output with a mime bundle */
    yetty_ynotebook_cell_apply_message(
        cell, "execute_result",
        "{\"data\":{\"text/plain\":\"42\"},\"metadata\":{},\"execution_count\":1}");
    CHECK(out_count(cell) == 2, "execute_result appended");
    {
        struct yetty_yclass_object_ptr_result o1 = yetty_ynotebook_cell_output_at(cell, 1);
        struct yetty_ycore_const_char_ptr_result t = yetty_ynotebook_output_type(o1.value);
        CHECK(YETTY_IS_OK(t) && strcmp(t.value, "execute_result") == 0, "output 1 execute_result");
        struct yetty_yclass_object_ptr_result b = yetty_ynotebook_output_bundle(o1.value);
        CHECK(YETTY_IS_OK(b), "output 1 has a mime bundle");
        if (YETTY_IS_OK(b)) {
            struct yetty_ycore_size_result bc = yetty_ynotebook_mime_bundle_count(b.value);
            CHECK(YETTY_IS_OK(bc) && bc.value == 1, "bundle has one representation");
        }
    }

    /* clear_output(wait=false) clears immediately */
    yetty_ynotebook_cell_apply_message(cell, "clear_output", "{\"wait\":false}");
    CHECK(out_count(cell) == 0, "clear_output(wait=false) cleared immediately");

    /* clear_output(wait=true) defers until the next output */
    yetty_ynotebook_cell_apply_message(cell, "stream", "{\"name\":\"stdout\",\"text\":\"again\\n\"}");
    CHECK(out_count(cell) == 1, "one output before deferred clear");
    yetty_ynotebook_cell_apply_message(cell, "clear_output", "{\"wait\":true}");
    CHECK(out_count(cell) == 1, "deferred clear does not clear yet");
    yetty_ynotebook_cell_apply_message(cell, "stream", "{\"name\":\"stdout\",\"text\":\"fresh\\n\"}");
    CHECK(out_count(cell) == 1, "deferred clear fired then appended");
    {
        struct yetty_yclass_object_ptr_result o0 = yetty_ynotebook_cell_output_at(cell, 0);
        struct yetty_ycore_char_ptr_result txt = yetty_ynotebook_output_text(o0.value);
        CHECK(YETTY_IS_OK(txt) && strcmp(txt.value, "fresh\n") == 0,
              "post-clear stream is not coalesced with the cleared one");
        if (YETTY_IS_OK(txt))
            free(txt.value);
    }

    /* mutation persists through serialization */
    struct yetty_ycore_char_ptr_result text_r = yetty_ynotebook_notebook_to_text(notebook);
    CHECK(YETTY_IS_OK(text_r), "serialize after routing");
    if (YETTY_IS_OK(text_r))
        free(text_r.value);

    yetty_ynotebook_notebook_destroy(notebook);

    if (failures == 0) {
        printf("ok - ynotebook execution routing\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
}
