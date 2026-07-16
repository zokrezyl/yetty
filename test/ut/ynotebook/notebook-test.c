/*
 * ynotebook document-model contract test.
 *
 * Loads a small nbformat 4.5 notebook, walks notebook -> cell -> output ->
 * mime_bundle through the generated yclass API, then checks:
 *   - typed field access (nbformat, cell types, source, outputs, bundle);
 *   - unknown top-level fields and unknown metadata survive load/serialize;
 *   - cell source mutation round-trips;
 *   - save_file / load_file round-trips.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <yyjson.h>

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

static const char *INPUT =
    "{"
    "  \"nbformat\": 4,"
    "  \"nbformat_minor\": 5,"
    "  \"metadata\": {\"kernelspec\": {\"name\": \"python3\"}, \"custom_unknown\": 42},"
    "  \"cells\": ["
    "    {\"cell_type\": \"markdown\", \"id\": \"c1\", \"metadata\": {},"
    "     \"source\": [\"# Title\\n\", \"body\"]},"
    "    {\"cell_type\": \"code\", \"id\": \"c2\", \"execution_count\": 3,"
    "     \"metadata\": {\"tags\": [\"x\"]}, \"source\": \"print('hi')\","
    "     \"outputs\": ["
    "       {\"output_type\": \"stream\", \"name\": \"stdout\", \"text\": [\"hi\\n\"]},"
    "       {\"output_type\": \"execute_result\", \"execution_count\": 3,"
    "        \"data\": {\"text/plain\": \"42\", \"application/json\": {\"k\": 7}},"
    "        \"metadata\": {}}"
    "     ]}"
    "  ],"
    "  \"extra_top_level\": \"keep me\""
    "}";

/* does emitted text contain a top-level string field with the given value? */
static int emitted_has(const char *text, const char *key, const char *value)
{
    yyjson_doc *doc = yyjson_read(text, strlen(text), 0);
    if (!doc)
        return 0;
    yyjson_val *field = yyjson_obj_get(yyjson_doc_get_root(doc), key);
    int ok = field && yyjson_is_str(field) && strcmp(yyjson_get_str(field), value) == 0;
    yyjson_doc_free(doc);
    return ok;
}

int main(void)
{
    struct yetty_ycore_void_result reg_r = yetty_ynotebook_register();
    CHECK(YETTY_IS_OK(reg_r), "module register");

    struct yetty_yclass_ctx ctx = {0};
    struct yetty_yclass_object_ptr_result nb_r = yetty_ynotebook_notebook_create(&ctx);
    CHECK(YETTY_IS_OK(nb_r), "notebook create");
    if (YETTY_IS_ERR(nb_r))
        return 1;
    struct yetty_yclass_object *notebook = nb_r.value;

    struct yetty_ycore_void_result load_r = yetty_ynotebook_notebook_load_text(notebook, INPUT);
    CHECK(YETTY_IS_OK(load_r), "notebook load_text");

    /* ---- document-level fields ---- */
    struct yetty_ycore_int_result fmt_r = yetty_ynotebook_notebook_nbformat(notebook);
    CHECK(YETTY_IS_OK(fmt_r) && fmt_r.value == 4, "nbformat == 4");
    struct yetty_ycore_int_result minor_r = yetty_ynotebook_notebook_nbformat_minor(notebook);
    CHECK(YETTY_IS_OK(minor_r) && minor_r.value == 5, "nbformat_minor == 5");
    struct yetty_ycore_size_result cells_r = yetty_ynotebook_notebook_cell_count(notebook);
    CHECK(YETTY_IS_OK(cells_r) && cells_r.value == 2, "two cells");

    /* ---- cell 0: markdown ---- */
    struct yetty_yclass_object_ptr_result cell0_r = yetty_ynotebook_notebook_cell_at(notebook, 0);
    CHECK(YETTY_IS_OK(cell0_r), "cell 0 fetch");
    struct yetty_yclass_object *cell0 = cell0_r.value;
    struct yetty_ycore_const_char_ptr_result c0type = yetty_ynotebook_cell_type(cell0);
    CHECK(YETTY_IS_OK(c0type) && strcmp(c0type.value, "markdown") == 0, "cell 0 is markdown");
    struct yetty_ycore_char_ptr_result c0src = yetty_ynotebook_cell_source(cell0);
    CHECK(YETTY_IS_OK(c0src) && strcmp(c0src.value, "# Title\nbody") == 0, "cell 0 source joined");
    if (YETTY_IS_OK(c0src))
        free(c0src.value);

    /* ---- cell 1: code with outputs ---- */
    struct yetty_yclass_object_ptr_result cell1_r = yetty_ynotebook_notebook_cell_at(notebook, 1);
    struct yetty_yclass_object *cell1 = cell1_r.value;
    struct yetty_ycore_int_result c1ec = yetty_ynotebook_cell_execution_count(cell1);
    CHECK(YETTY_IS_OK(c1ec) && c1ec.value == 3, "cell 1 execution_count == 3");
    struct yetty_ycore_size_result c1out = yetty_ynotebook_cell_output_count(cell1);
    CHECK(YETTY_IS_OK(c1out) && c1out.value == 2, "cell 1 has two outputs");

    /* output 0: stream */
    struct yetty_yclass_object_ptr_result out0_r = yetty_ynotebook_cell_output_at(cell1, 0);
    struct yetty_yclass_object *out0 = out0_r.value;
    struct yetty_ycore_const_char_ptr_result o0type = yetty_ynotebook_output_type(out0);
    CHECK(YETTY_IS_OK(o0type) && strcmp(o0type.value, "stream") == 0, "output 0 is stream");
    struct yetty_ycore_const_char_ptr_result o0name = yetty_ynotebook_output_stream_name(out0);
    CHECK(YETTY_IS_OK(o0name) && strcmp(o0name.value, "stdout") == 0, "output 0 stream stdout");
    struct yetty_ycore_char_ptr_result o0text = yetty_ynotebook_output_text(out0);
    CHECK(YETTY_IS_OK(o0text) && strcmp(o0text.value, "hi\n") == 0, "output 0 stream text");
    if (YETTY_IS_OK(o0text))
        free(o0text.value);

    /* output 1: execute_result with a mime bundle */
    struct yetty_yclass_object_ptr_result out1_r = yetty_ynotebook_cell_output_at(cell1, 1);
    struct yetty_yclass_object *out1 = out1_r.value;
    struct yetty_ycore_const_char_ptr_result o1type = yetty_ynotebook_output_type(out1);
    CHECK(YETTY_IS_OK(o1type) && strcmp(o1type.value, "execute_result") == 0,
          "output 1 is execute_result");
    struct yetty_yclass_object_ptr_result bundle_r = yetty_ynotebook_output_bundle(out1);
    CHECK(YETTY_IS_OK(bundle_r), "output 1 exposes a mime bundle");
    if (YETTY_IS_OK(bundle_r)) {
        struct yetty_yclass_object *bundle = bundle_r.value;
        struct yetty_ycore_size_result bcount = yetty_ynotebook_mime_bundle_count(bundle);
        CHECK(YETTY_IS_OK(bcount) && bcount.value == 2, "bundle has two representations");
        /* find application/json and confirm it is a structured json payload */
        int found_json = 0;
        for (size_t i = 0; i < (YETTY_IS_OK(bcount) ? bcount.value : 0); i++) {
            struct yetty_ycore_const_char_ptr_result mime =
                yetty_ynotebook_mime_bundle_mime_at(bundle, i);
            if (YETTY_IS_OK(mime) && strcmp(mime.value, "application/json") == 0) {
                struct yetty_ycore_const_char_ptr_result kind =
                    yetty_ynotebook_mime_bundle_kind_at(bundle, i);
                found_json = YETTY_IS_OK(kind) && strcmp(kind.value, "json") == 0;
            }
        }
        CHECK(found_json, "bundle application/json is a structured json payload");
    }

    /* ---- serialize: unknown fields + metadata must survive ---- */
    struct yetty_ycore_char_ptr_result text_r = yetty_ynotebook_notebook_to_text(notebook);
    CHECK(YETTY_IS_OK(text_r), "notebook to_text");
    if (YETTY_IS_OK(text_r)) {
        CHECK(emitted_has(text_r.value, "extra_top_level", "keep me"),
              "unknown top-level field preserved");
        /* metadata.custom_unknown survives via the retained tree */
        yyjson_doc *doc = yyjson_read(text_r.value, strlen(text_r.value), 0);
        if (doc) {
            yyjson_val *meta = yyjson_obj_get(yyjson_doc_get_root(doc), "metadata");
            yyjson_val *custom = meta ? yyjson_obj_get(meta, "custom_unknown") : NULL;
            CHECK(custom && yyjson_get_int(custom) == 42, "unknown metadata field preserved");
            yyjson_doc_free(doc);
        }
        free(text_r.value);
    }

    /* ---- mutation: set_source round-trips ---- */
    struct yetty_ycore_void_result set_r = yetty_ynotebook_cell_set_source(cell0, "changed source");
    CHECK(YETTY_IS_OK(set_r), "cell set_source");
    struct yetty_ycore_char_ptr_result c0src2 = yetty_ynotebook_cell_source(cell0);
    CHECK(YETTY_IS_OK(c0src2) && strcmp(c0src2.value, "changed source") == 0,
          "cell source updated");
    if (YETTY_IS_OK(c0src2))
        free(c0src2.value);

    /* ---- save_file / load_file round-trip ---- */
    const char *path = "ynotebook-save-roundtrip.ipynb";
    struct yetty_ycore_void_result save_r = yetty_ynotebook_notebook_save_file(notebook, path);
    CHECK(YETTY_IS_OK(save_r), "notebook save_file");
    if (YETTY_IS_OK(save_r)) {
        struct yetty_yclass_object_ptr_result nb2_r = yetty_ynotebook_notebook_create(&ctx);
        struct yetty_yclass_object *notebook2 = nb2_r.value;
        struct yetty_ycore_void_result load2_r =
            yetty_ynotebook_notebook_load_file(notebook2, path);
        CHECK(YETTY_IS_OK(load2_r), "notebook load_file");
        struct yetty_ycore_size_result cells2_r =
            yetty_ynotebook_notebook_cell_count(notebook2);
        CHECK(YETTY_IS_OK(cells2_r) && cells2_r.value == 2, "reloaded notebook has two cells");
        yetty_ynotebook_notebook_destroy(notebook2);
        remove(path);
    }

    yetty_ynotebook_notebook_destroy(notebook);

    if (failures == 0) {
        printf("ok - ynotebook document model\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
}
