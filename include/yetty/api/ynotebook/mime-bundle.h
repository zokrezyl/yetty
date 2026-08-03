/* GENERATED — do not edit. */
/* Object API for regular class(es) `mime_bundle` (implementation module: ynotebook).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YNOTEBOOK_MIME_BUNDLE_H
#define YETTY_YCLASSGEN_API_YNOTEBOOK_MIME_BUNDLE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ynotebook_mime_bundle_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ynotebook_mime_bundle;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YNOTEBOOK_MIME_BUNDLE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YNOTEBOOK_MIME_BUNDLE_PTR_RESULT
struct yetty_ynotebook_mime_bundle_ptr_result {
    int ok;
    union {
        struct yetty_ynotebook_mime_bundle *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ynotebook_mime_bundle_ptr_result yetty_ynotebook_mime_bundle_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ynotebook_mime_bundle_to(
    struct yetty_ynotebook_mime_bundle *data);

/* from_json_text: replace the bundle's contents from an nbformat `data` object
 * (JSON text). `metadata_json` is the matching `metadata` object as JSON text,
 * or NULL / "" for none. Each representation is classified as text, binary
 * (base64-decoded), or a retained JSON value. */
struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_from_json_text(
    struct yetty_yclass_object *obj, const char *data_json, const char *metadata_json);
/* to_json_text: serialize the bundle as an nbformat `data` object (JSON text).
 * Caller owns the returned string and frees it with free(). */
struct yetty_ycore_char_ptr_result yetty_ynotebook_mime_bundle_to_json_text(
    struct yetty_yclass_object *obj);
/* count: number of representations. */
struct yetty_ycore_size_result yetty_ynotebook_mime_bundle_count(struct yetty_yclass_object *obj);
/* mime_at: the MIME type string of representation `index` (borrowed). */
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_mime_bundle_mime_at(
    struct yetty_yclass_object *obj, size_t index);
/* kind_at: payload kind of representation `index` — "text", "binary", or
 * "json" (borrowed static string). */
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_mime_bundle_kind_at(
    struct yetty_yclass_object *obj, size_t index);
/* bytes_at: the text/binary payload of representation `index` (borrowed, points
 * into the bundle). Yields NULL/0 for a json representation. Local: it returns
 * a borrowed buffer through out-pointers. */
struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_bytes_at(struct yetty_yclass_object *obj,
                                                                    size_t index,
                                                                    const uint8_t **out_bytes,
                                                                    size_t *out_len);
/* json_at: representation `index` serialized as pretty JSON text (owned; caller
 * frees). Empty string for a non-json representation — bytes_at covers those.
 * Local: it allocates and returns owned text. */
struct yetty_ycore_char_ptr_result yetty_ynotebook_mime_bundle_json_at(
    struct yetty_yclass_object *obj, size_t index);
/* destroy: free the representations and the yclass allocation. */
struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_destroy(struct yetty_yclass_object *obj);

struct yetty_yclass_object_ptr_result yetty_ynotebook_mime_bundle_create(
    struct yetty_yclass_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
