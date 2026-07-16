/*
 * ynotebook:mime_bundle contract test.
 *
 * Drives the generated yclass API: register the module, create a bundle, load
 * an nbformat mimebundle (`data` + `metadata`) from JSON text, check each
 * payload is classified correctly, then serialize back and re-parse to prove
 * the JSON representation survives structured (not stringified) and the text /
 * binary representations round-trip byte-exactly.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <yyjson.h>

#include <yetty/ynotebook/mime-bundle.h>

static int failures;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s\n", (msg));                                                  \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* index of the representation whose mime type equals `mime`, or SIZE_MAX. */
static size_t index_of(struct yetty_yclass_object *bundle, size_t count, const char *mime)
{
    for (size_t i = 0; i < count; i++) {
        struct yetty_ycore_const_char_ptr_result mime_r =
            yetty_ynotebook_mime_bundle_mime_at(bundle, i);
        if (YETTY_IS_OK(mime_r) && strcmp(mime_r.value, mime) == 0)
            return i;
    }
    return (size_t)-1;
}

static const char *kind_of(struct yetty_yclass_object *bundle, size_t index)
{
    struct yetty_ycore_const_char_ptr_result kind_r =
        yetty_ynotebook_mime_bundle_kind_at(bundle, index);
    return YETTY_IS_OK(kind_r) ? kind_r.value : "<err>";
}

int main(void)
{
    struct yetty_ycore_void_result reg_r = yetty_ynotebook_register();
    CHECK(YETTY_IS_OK(reg_r), "module register");

    struct yetty_yclass_ctx ctx = {0};
    struct yetty_yclass_object_ptr_result obj_r = yetty_ynotebook_mime_bundle_create(&ctx);
    CHECK(YETTY_IS_OK(obj_r), "bundle create");
    if (YETTY_IS_ERR(obj_r))
        return 1;
    struct yetty_yclass_object *bundle = obj_r.value;

    /* text/plain: single string; text/html: list-of-lines; image/png: base64
     * ("AAAA" -> three 0x00 bytes); application/json: a structured object. */
    const char *data_json = "{"
                            "  \"text/plain\": \"hello\","
                            "  \"text/html\": [\"<b>\", \"hi\", \"</b>\"],"
                            "  \"image/png\": \"AAAA\","
                            "  \"application/json\": {\"a\": 1, \"b\": [true, null]}"
                            "}";
    const char *metadata_json = "{ \"image/png\": {\"width\": 4} }";

    struct yetty_ycore_void_result load_r =
        yetty_ynotebook_mime_bundle_from_json_text(bundle, data_json, metadata_json);
    CHECK(YETTY_IS_OK(load_r), "bundle load");

    struct yetty_ycore_size_result count_r = yetty_ynotebook_mime_bundle_count(bundle);
    CHECK(YETTY_IS_OK(count_r) && count_r.value == 4, "four representations");
    size_t count = YETTY_IS_OK(count_r) ? count_r.value : 0;

    size_t plain = index_of(bundle, count, "text/plain");
    CHECK(plain != (size_t)-1 && strcmp(kind_of(bundle, plain), "text") == 0, "text/plain is text");

    size_t html = index_of(bundle, count, "text/html");
    CHECK(html != (size_t)-1 && strcmp(kind_of(bundle, html), "text") == 0, "text/html is text");
    if (html != (size_t)-1) {
        const uint8_t *bytes = NULL;
        size_t len = 0;
        struct yetty_ycore_void_result bytes_r =
            yetty_ynotebook_mime_bundle_bytes_at(bundle, html, &bytes, &len);
        CHECK(YETTY_IS_OK(bytes_r) && len == 9 && bytes && memcmp(bytes, "<b>hi</b>", 9) == 0,
              "text/html list-of-lines joined");
    }

    size_t png = index_of(bundle, count, "image/png");
    CHECK(png != (size_t)-1 && strcmp(kind_of(bundle, png), "binary") == 0, "image/png is binary");
    if (png != (size_t)-1) {
        const uint8_t *bytes = NULL;
        size_t len = 0;
        yetty_ynotebook_mime_bundle_bytes_at(bundle, png, &bytes, &len);
        CHECK(len == 3, "image/png decoded to 3 bytes");
    }

    size_t json = index_of(bundle, count, "application/json");
    CHECK(json != (size_t)-1 && strcmp(kind_of(bundle, json), "json") == 0,
          "application/json is json");

    /* Serialize back and re-parse. */
    struct yetty_ycore_char_ptr_result text_r = yetty_ynotebook_mime_bundle_to_json_text(bundle);
    CHECK(YETTY_IS_OK(text_r), "serialize to json text");
    if (YETTY_IS_OK(text_r)) {
        char *emitted = text_r.value;
        yyjson_doc *reparsed = yyjson_read(emitted, strlen(emitted), 0);
        CHECK(reparsed != NULL, "reparse emitted");
        if (reparsed) {
            yyjson_val *root = yyjson_doc_get_root(reparsed);

            yyjson_val *rt_json = yyjson_obj_get(root, "application/json");
            CHECK(rt_json && yyjson_is_obj(rt_json),
                  "application/json still an object after round-trip");
            yyjson_val *field_a = rt_json ? yyjson_obj_get(rt_json, "a") : NULL;
            CHECK(field_a && yyjson_is_int(field_a) && yyjson_get_int(field_a) == 1,
                  "application/json field preserved");

            yyjson_val *rt_plain = yyjson_obj_get(root, "text/plain");
            CHECK(rt_plain && yyjson_is_str(rt_plain) &&
                      strcmp(yyjson_get_str(rt_plain), "hello") == 0,
                  "text/plain string preserved");

            yyjson_val *rt_png = yyjson_obj_get(root, "image/png");
            CHECK(rt_png && yyjson_is_str(rt_png) && strcmp(yyjson_get_str(rt_png), "AAAA") == 0,
                  "image/png base64 re-encoded exactly");

            yyjson_doc_free(reparsed);
        }
        free(emitted);
    }

    yetty_ynotebook_mime_bundle_destroy(bundle);

    if (failures == 0) {
        printf("ok - ynotebook mime-bundle round-trip\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
}
