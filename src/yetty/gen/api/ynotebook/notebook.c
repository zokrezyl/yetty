/* GENERATED — do not edit. */
#include <yetty/api/ynotebook/notebook.h>

#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h> /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h> /* malloc/free for buffer marshalling */
#include <string.h> /* memcpy/strlen */

struct yetty_yclass_object_ptr_result;
struct yetty_ycore_char_ptr_result;
struct yetty_ycore_const_char_ptr_result;
struct yetty_ycore_int_result;
struct yetty_ycore_size_result;
struct yetty_ycore_void_result;
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_type(
    struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_stream_name(
    struct yetty_yclass_object *obj);
struct yetty_ycore_char_ptr_result yetty_ynotebook_output_text(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ynotebook_output_execution_count(
    struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_error_name(
    struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_error_value(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ynotebook_output_bundle(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ynotebook_output_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_cell_type(struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_cell_id(struct yetty_yclass_object *obj);
struct yetty_ycore_char_ptr_result yetty_ynotebook_cell_source(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ynotebook_cell_set_source(struct yetty_yclass_object *obj,
                                                               const char *text);
struct yetty_ycore_int_result yetty_ynotebook_cell_execution_count(struct yetty_yclass_object *obj);
struct yetty_ycore_size_result yetty_ynotebook_cell_output_count(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ynotebook_cell_output_at(
    struct yetty_yclass_object *obj, size_t index);
struct yetty_ycore_char_ptr_result yetty_ynotebook_cell_metadata_json(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ynotebook_cell_apply_message(struct yetty_yclass_object *obj,
                                                                  const char *msg_type,
                                                                  const char *content_json);
struct yetty_ycore_void_result yetty_ynotebook_cell_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ynotebook_notebook_load_text(struct yetty_yclass_object *obj,
                                                                  const char *json);
struct yetty_ycore_void_result yetty_ynotebook_notebook_load_file(struct yetty_yclass_object *obj,
                                                                  const char *path);
struct yetty_ycore_char_ptr_result yetty_ynotebook_notebook_to_text(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ynotebook_notebook_save_file(struct yetty_yclass_object *obj,
                                                                  const char *path);
struct yetty_ycore_int_result yetty_ynotebook_notebook_nbformat(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ynotebook_notebook_nbformat_minor(
    struct yetty_yclass_object *obj);
struct yetty_ycore_size_result yetty_ynotebook_notebook_cell_count(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ynotebook_notebook_cell_at(
    struct yetty_yclass_object *obj, size_t index);
struct yetty_ycore_char_ptr_result yetty_ynotebook_notebook_metadata_json(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ynotebook_notebook_destroy(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_output_type_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_output_stream_name_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_output_text_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ynotebook_output_execution_count_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_output_error_name_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_output_error_value_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_yclass_object_ptr_result (*yetty_ynotebook_output_bundle_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_output_destroy_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_cell_type_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_cell_id_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_cell_source_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_cell_set_source_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ynotebook_cell_execution_count_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_size_result (*yetty_ynotebook_cell_output_count_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_yclass_object_ptr_result (*yetty_ynotebook_cell_output_at_fn)(
    struct yetty_yclass_object *, size_t);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_cell_metadata_json_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_cell_apply_message_fn)(
    struct yetty_yclass_object *, const char *, const char *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_cell_destroy_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_notebook_load_text_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_notebook_load_file_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_notebook_to_text_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_notebook_save_file_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ynotebook_notebook_nbformat_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ynotebook_notebook_nbformat_minor_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_size_result (*yetty_ynotebook_notebook_cell_count_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_yclass_object_ptr_result (*yetty_ynotebook_notebook_cell_at_fn)(
    struct yetty_yclass_object *, size_t);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_notebook_metadata_json_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_notebook_destroy_fn)(
    struct yetty_yclass_object *);

struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_type(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_type);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_ynotebook_output_type: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_output_type: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_ynotebook_output_type: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_ynotebook_output_type: dispatch_lookup failed");
    return ((yetty_ynotebook_output_type_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_stream_name(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_stream_name);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_ynotebook_output_stream_name: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr,
                         "yetty_ynotebook_output_stream_name: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_ynotebook_output_stream_name: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_ynotebook_output_stream_name: dispatch_lookup failed");
    return ((yetty_ynotebook_output_stream_name_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_char_ptr_result yetty_ynotebook_output_text(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_text);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_char_ptr,
                             "yetty_ynotebook_output_text: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_output_text: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r,
                        "yetty_ynotebook_output_text: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r,
                        "yetty_ynotebook_output_text: dispatch_lookup failed");
    return ((yetty_ynotebook_output_text_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_int_result yetty_ynotebook_output_execution_count(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_execution_count);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ynotebook_output_execution_count: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ynotebook_output_execution_count: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                        "yetty_ynotebook_output_execution_count: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                        "yetty_ynotebook_output_execution_count: dispatch_lookup failed");
    return ((yetty_ynotebook_output_execution_count_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_error_name(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_error_name);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_ynotebook_output_error_name: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr,
                         "yetty_ynotebook_output_error_name: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_ynotebook_output_error_name: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_ynotebook_output_error_name: dispatch_lookup failed");
    return ((yetty_ynotebook_output_error_name_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_error_value(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_error_value);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_ynotebook_output_error_value: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr,
                         "yetty_ynotebook_output_error_value: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_ynotebook_output_error_value: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_ynotebook_output_error_value: dispatch_lookup failed");
    return ((yetty_ynotebook_output_error_value_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_output_bundle(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_bundle);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ynotebook_output_bundle: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ynotebook_output_bundle: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_class_r,
                        "yetty_ynotebook_output_bundle: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, dispatch_impl_r,
                        "yetty_ynotebook_output_bundle: dispatch_lookup failed");
    return ((yetty_ynotebook_output_bundle_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ynotebook_output_destroy(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_destroy);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ynotebook_output_destroy: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_output_destroy: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ynotebook_output_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ynotebook_output_destroy: dispatch_lookup failed");
    return ((yetty_ynotebook_output_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_ynotebook_cell_type(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_type);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_ynotebook_cell_type: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_cell_type: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_ynotebook_cell_type: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_ynotebook_cell_type: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_type_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_ynotebook_cell_id(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_id);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_ynotebook_cell_id: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_cell_id: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_ynotebook_cell_id: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_ynotebook_cell_id: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_id_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_char_ptr_result yetty_ynotebook_cell_source(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_source);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_char_ptr,
                             "yetty_ynotebook_cell_source: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_cell_source: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r,
                        "yetty_ynotebook_cell_source: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r,
                        "yetty_ynotebook_cell_source: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_source_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ynotebook_cell_set_source(struct yetty_yclass_object *obj,
                                                               const char *text)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_set_source);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ynotebook_cell_set_source: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_cell_set_source: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ynotebook_cell_set_source: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ynotebook_cell_set_source: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_set_source_fn)dispatch_impl_r.value)(obj, text);
}

struct yetty_ycore_int_result yetty_ynotebook_cell_execution_count(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_execution_count);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ynotebook_cell_execution_count: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ynotebook_cell_execution_count: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                        "yetty_ynotebook_cell_execution_count: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                        "yetty_ynotebook_cell_execution_count: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_execution_count_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_size_result yetty_ynotebook_cell_output_count(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_output_count);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_size,
                             "yetty_ynotebook_cell_output_count: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_size, "yetty_ynotebook_cell_output_count: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, object_class_r,
                        "yetty_ynotebook_cell_output_count: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, dispatch_impl_r,
                        "yetty_ynotebook_cell_output_count: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_output_count_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_cell_output_at(
    struct yetty_yclass_object *obj, size_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_output_at);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ynotebook_cell_output_at: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ynotebook_cell_output_at: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_class_r,
                        "yetty_ynotebook_cell_output_at: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, dispatch_impl_r,
                        "yetty_ynotebook_cell_output_at: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_output_at_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ycore_char_ptr_result yetty_ynotebook_cell_metadata_json(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_metadata_json);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_char_ptr,
                             "yetty_ynotebook_cell_metadata_json: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_cell_metadata_json: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r,
                        "yetty_ynotebook_cell_metadata_json: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r,
                        "yetty_ynotebook_cell_metadata_json: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_metadata_json_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ynotebook_cell_apply_message(struct yetty_yclass_object *obj,
                                                                  const char *msg_type,
                                                                  const char *content_json)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_apply_message);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ynotebook_cell_apply_message: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_cell_apply_message: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ynotebook_cell_apply_message: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ynotebook_cell_apply_message: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_apply_message_fn)dispatch_impl_r.value)(obj, msg_type,
                                                                          content_json);
}

struct yetty_ycore_void_result yetty_ynotebook_cell_destroy(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_destroy);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ynotebook_cell_destroy: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_cell_destroy: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ynotebook_cell_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ynotebook_cell_destroy: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ynotebook_notebook_load_text(struct yetty_yclass_object *obj,
                                                                  const char *json)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_load_text);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ynotebook_notebook_load_text: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_notebook_load_text: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ynotebook_notebook_load_text: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ynotebook_notebook_load_text: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_load_text_fn)dispatch_impl_r.value)(obj, json);
}

struct yetty_ycore_void_result yetty_ynotebook_notebook_load_file(struct yetty_yclass_object *obj,
                                                                  const char *path)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_load_file);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ynotebook_notebook_load_file: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_notebook_load_file: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ynotebook_notebook_load_file: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ynotebook_notebook_load_file: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_load_file_fn)dispatch_impl_r.value)(obj, path);
}

struct yetty_ycore_char_ptr_result yetty_ynotebook_notebook_to_text(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_to_text);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_char_ptr,
                             "yetty_ynotebook_notebook_to_text: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_notebook_to_text: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r,
                        "yetty_ynotebook_notebook_to_text: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r,
                        "yetty_ynotebook_notebook_to_text: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_to_text_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ynotebook_notebook_save_file(struct yetty_yclass_object *obj,
                                                                  const char *path)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_save_file);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ynotebook_notebook_save_file: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_notebook_save_file: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ynotebook_notebook_save_file: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ynotebook_notebook_save_file: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_save_file_fn)dispatch_impl_r.value)(obj, path);
}

struct yetty_ycore_int_result yetty_ynotebook_notebook_nbformat(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_nbformat);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ynotebook_notebook_nbformat: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ynotebook_notebook_nbformat: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                        "yetty_ynotebook_notebook_nbformat: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                        "yetty_ynotebook_notebook_nbformat: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_nbformat_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_int_result yetty_ynotebook_notebook_nbformat_minor(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_nbformat_minor);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ynotebook_notebook_nbformat_minor: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ynotebook_notebook_nbformat_minor: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                        "yetty_ynotebook_notebook_nbformat_minor: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                        "yetty_ynotebook_notebook_nbformat_minor: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_nbformat_minor_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_size_result yetty_ynotebook_notebook_cell_count(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_cell_count);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_size,
                             "yetty_ynotebook_notebook_cell_count: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_size, "yetty_ynotebook_notebook_cell_count: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, object_class_r,
                        "yetty_ynotebook_notebook_cell_count: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, dispatch_impl_r,
                        "yetty_ynotebook_notebook_cell_count: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_cell_count_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_notebook_cell_at(
    struct yetty_yclass_object *obj, size_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_cell_at);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ynotebook_notebook_cell_at: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ynotebook_notebook_cell_at: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_class_r,
                        "yetty_ynotebook_notebook_cell_at: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, dispatch_impl_r,
                        "yetty_ynotebook_notebook_cell_at: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_cell_at_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ycore_char_ptr_result yetty_ynotebook_notebook_metadata_json(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_metadata_json);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_char_ptr,
                             "yetty_ynotebook_notebook_metadata_json: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_char_ptr,
                         "yetty_ynotebook_notebook_metadata_json: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r,
                        "yetty_ynotebook_notebook_metadata_json: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r,
                        "yetty_ynotebook_notebook_metadata_json: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_metadata_json_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ynotebook_notebook_destroy(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_destroy);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ynotebook_notebook_destroy: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_notebook_destroy: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ynotebook_notebook_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ynotebook_notebook_destroy: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_destroy_fn)dispatch_impl_r.value)(obj);
}
