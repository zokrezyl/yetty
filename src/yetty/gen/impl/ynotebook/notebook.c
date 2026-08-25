/* GENERATED — do not edit. */
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h> /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h> /* calloc/free for proxy + buffer marshalling */
#include <string.h> /* memcpy/strcmp/strlen */

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

YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_type_fn
    yetty_ynotebook_output_yetty_ynotebook_output_type_output_type_check = output_type;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_stream_name_fn
    yetty_ynotebook_output_yetty_ynotebook_output_stream_name_output_stream_name_check =
        output_stream_name;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_text_fn
    yetty_ynotebook_output_yetty_ynotebook_output_text_output_text_check = output_text;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_execution_count_fn
    yetty_ynotebook_output_yetty_ynotebook_output_execution_count_output_execution_count_check =
        output_execution_count;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_error_name_fn
    yetty_ynotebook_output_yetty_ynotebook_output_error_name_output_error_name_check =
        output_error_name;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_error_value_fn
    yetty_ynotebook_output_yetty_ynotebook_output_error_value_output_error_value_check =
        output_error_value;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_bundle_fn
    yetty_ynotebook_output_yetty_ynotebook_output_bundle_output_bundle_check = output_bundle;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_destroy_fn
    yetty_ynotebook_output_yetty_ynotebook_output_destroy_output_destroy_check = output_destroy;

struct yetty_yclass_ptr_result yetty_ynotebook_output_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ynotebook_output");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ynotebook_output",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ynotebook_output),
        .data_align = _Alignof(struct yetty_ynotebook_output),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ynotebook", "output_type", (yetty_yclass_method_id_t)yetty_ynotebook_output_type,
         (yetty_yclass_impl_t)output_type},
        {"yetty_ynotebook", "output_stream_name",
         (yetty_yclass_method_id_t)yetty_ynotebook_output_stream_name,
         (yetty_yclass_impl_t)output_stream_name},
        {"yetty_ynotebook", "output_text", (yetty_yclass_method_id_t)yetty_ynotebook_output_text,
         (yetty_yclass_impl_t)output_text},
        {"yetty_ynotebook", "output_execution_count",
         (yetty_yclass_method_id_t)yetty_ynotebook_output_execution_count,
         (yetty_yclass_impl_t)output_execution_count},
        {"yetty_ynotebook", "output_error_name",
         (yetty_yclass_method_id_t)yetty_ynotebook_output_error_name,
         (yetty_yclass_impl_t)output_error_name},
        {"yetty_ynotebook", "output_error_value",
         (yetty_yclass_method_id_t)yetty_ynotebook_output_error_value,
         (yetty_yclass_impl_t)output_error_value},
        {"yetty_ynotebook", "output_bundle",
         (yetty_yclass_method_id_t)yetty_ynotebook_output_bundle,
         (yetty_yclass_impl_t)output_bundle},
        {"yetty_ynotebook", "output_destroy",
         (yetty_yclass_method_id_t)yetty_ynotebook_output_destroy,
         (yetty_yclass_impl_t)output_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ynotebook_output_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ynotebook_output_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ynotebook_output_ptr_result yetty_ynotebook_output_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_output_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ynotebook_output_ptr, "yetty_ynotebook_output_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ynotebook_output_ptr, "yetty_ynotebook_output_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ynotebook_output_ptr, (struct yetty_ynotebook_output *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_output_to(struct yetty_ynotebook_output *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_output_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ynotebook_output_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_ynotebook_output_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_type_fn yetty_ynotebook_cell_yetty_ynotebook_cell_type_cell_type_check =
    cell_type;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_id_fn yetty_ynotebook_cell_yetty_ynotebook_cell_id_cell_id_check =
    cell_id;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_source_fn
    yetty_ynotebook_cell_yetty_ynotebook_cell_source_cell_source_check = cell_source;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_set_source_fn
    yetty_ynotebook_cell_yetty_ynotebook_cell_set_source_cell_set_source_check = cell_set_source;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_execution_count_fn
    yetty_ynotebook_cell_yetty_ynotebook_cell_execution_count_cell_execution_count_check =
        cell_execution_count;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_output_count_fn
    yetty_ynotebook_cell_yetty_ynotebook_cell_output_count_cell_output_count_check =
        cell_output_count;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_output_at_fn
    yetty_ynotebook_cell_yetty_ynotebook_cell_output_at_cell_output_at_check = cell_output_at;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_metadata_json_fn
    yetty_ynotebook_cell_yetty_ynotebook_cell_metadata_json_cell_metadata_json_check =
        cell_metadata_json;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_apply_message_fn
    yetty_ynotebook_cell_yetty_ynotebook_cell_apply_message_cell_apply_message_check =
        cell_apply_message;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_destroy_fn
    yetty_ynotebook_cell_yetty_ynotebook_cell_destroy_cell_destroy_check = cell_destroy;

struct yetty_yclass_ptr_result yetty_ynotebook_cell_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ynotebook_cell");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ynotebook_cell",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ynotebook_cell),
        .data_align = _Alignof(struct yetty_ynotebook_cell),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ynotebook", "cell_type", (yetty_yclass_method_id_t)yetty_ynotebook_cell_type,
         (yetty_yclass_impl_t)cell_type},
        {"yetty_ynotebook", "cell_id", (yetty_yclass_method_id_t)yetty_ynotebook_cell_id,
         (yetty_yclass_impl_t)cell_id},
        {"yetty_ynotebook", "cell_source", (yetty_yclass_method_id_t)yetty_ynotebook_cell_source,
         (yetty_yclass_impl_t)cell_source},
        {"yetty_ynotebook", "cell_set_source",
         (yetty_yclass_method_id_t)yetty_ynotebook_cell_set_source,
         (yetty_yclass_impl_t)cell_set_source},
        {"yetty_ynotebook", "cell_execution_count",
         (yetty_yclass_method_id_t)yetty_ynotebook_cell_execution_count,
         (yetty_yclass_impl_t)cell_execution_count},
        {"yetty_ynotebook", "cell_output_count",
         (yetty_yclass_method_id_t)yetty_ynotebook_cell_output_count,
         (yetty_yclass_impl_t)cell_output_count},
        {"yetty_ynotebook", "cell_output_at",
         (yetty_yclass_method_id_t)yetty_ynotebook_cell_output_at,
         (yetty_yclass_impl_t)cell_output_at},
        {"yetty_ynotebook", "cell_metadata_json",
         (yetty_yclass_method_id_t)yetty_ynotebook_cell_metadata_json,
         (yetty_yclass_impl_t)cell_metadata_json},
        {"yetty_ynotebook", "cell_apply_message",
         (yetty_yclass_method_id_t)yetty_ynotebook_cell_apply_message,
         (yetty_yclass_impl_t)cell_apply_message},
        {"yetty_ynotebook", "cell_destroy", (yetty_yclass_method_id_t)yetty_ynotebook_cell_destroy,
         (yetty_yclass_impl_t)cell_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ynotebook_cell_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ynotebook_cell_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ynotebook_cell_ptr_result yetty_ynotebook_cell_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_cell_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ynotebook_cell_ptr, "yetty_ynotebook_cell_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ynotebook_cell_ptr, "yetty_ynotebook_cell_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ynotebook_cell_ptr, (struct yetty_ynotebook_cell *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_cell_to(struct yetty_ynotebook_cell *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_cell_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ynotebook_cell_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ynotebook_cell_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_load_text_fn
    yetty_ynotebook_notebook_yetty_ynotebook_notebook_load_text_notebook_load_text_check =
        notebook_load_text;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_load_file_fn
    yetty_ynotebook_notebook_yetty_ynotebook_notebook_load_file_notebook_load_file_check =
        notebook_load_file;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_to_text_fn
    yetty_ynotebook_notebook_yetty_ynotebook_notebook_to_text_notebook_to_text_check =
        notebook_to_text;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_save_file_fn
    yetty_ynotebook_notebook_yetty_ynotebook_notebook_save_file_notebook_save_file_check =
        notebook_save_file;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_nbformat_fn
    yetty_ynotebook_notebook_yetty_ynotebook_notebook_nbformat_notebook_nbformat_check =
        notebook_nbformat;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_nbformat_minor_fn
    yetty_ynotebook_notebook_yetty_ynotebook_notebook_nbformat_minor_notebook_nbformat_minor_check =
        notebook_nbformat_minor;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_cell_count_fn
    yetty_ynotebook_notebook_yetty_ynotebook_notebook_cell_count_notebook_cell_count_check =
        notebook_cell_count;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_cell_at_fn
    yetty_ynotebook_notebook_yetty_ynotebook_notebook_cell_at_notebook_cell_at_check =
        notebook_cell_at;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_metadata_json_fn
    yetty_ynotebook_notebook_yetty_ynotebook_notebook_metadata_json_notebook_metadata_json_check =
        notebook_metadata_json;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_destroy_fn
    yetty_ynotebook_notebook_yetty_ynotebook_notebook_destroy_notebook_destroy_check =
        notebook_destroy;

struct yetty_yclass_ptr_result yetty_ynotebook_notebook_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ynotebook_notebook");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ynotebook_notebook",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ynotebook_notebook),
        .data_align = _Alignof(struct yetty_ynotebook_notebook),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ynotebook", "notebook_load_text",
         (yetty_yclass_method_id_t)yetty_ynotebook_notebook_load_text,
         (yetty_yclass_impl_t)notebook_load_text},
        {"yetty_ynotebook", "notebook_load_file",
         (yetty_yclass_method_id_t)yetty_ynotebook_notebook_load_file,
         (yetty_yclass_impl_t)notebook_load_file},
        {"yetty_ynotebook", "notebook_to_text",
         (yetty_yclass_method_id_t)yetty_ynotebook_notebook_to_text,
         (yetty_yclass_impl_t)notebook_to_text},
        {"yetty_ynotebook", "notebook_save_file",
         (yetty_yclass_method_id_t)yetty_ynotebook_notebook_save_file,
         (yetty_yclass_impl_t)notebook_save_file},
        {"yetty_ynotebook", "notebook_nbformat",
         (yetty_yclass_method_id_t)yetty_ynotebook_notebook_nbformat,
         (yetty_yclass_impl_t)notebook_nbformat},
        {"yetty_ynotebook", "notebook_nbformat_minor",
         (yetty_yclass_method_id_t)yetty_ynotebook_notebook_nbformat_minor,
         (yetty_yclass_impl_t)notebook_nbformat_minor},
        {"yetty_ynotebook", "notebook_cell_count",
         (yetty_yclass_method_id_t)yetty_ynotebook_notebook_cell_count,
         (yetty_yclass_impl_t)notebook_cell_count},
        {"yetty_ynotebook", "notebook_cell_at",
         (yetty_yclass_method_id_t)yetty_ynotebook_notebook_cell_at,
         (yetty_yclass_impl_t)notebook_cell_at},
        {"yetty_ynotebook", "notebook_metadata_json",
         (yetty_yclass_method_id_t)yetty_ynotebook_notebook_metadata_json,
         (yetty_yclass_impl_t)notebook_metadata_json},
        {"yetty_ynotebook", "notebook_destroy",
         (yetty_yclass_method_id_t)yetty_ynotebook_notebook_destroy,
         (yetty_yclass_impl_t)notebook_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ynotebook_notebook_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ynotebook_notebook_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ynotebook_notebook_ptr_result yetty_ynotebook_notebook_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_notebook_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ynotebook_notebook_ptr,
                         "yetty_ynotebook_notebook_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ynotebook_notebook_ptr, "yetty_ynotebook_notebook_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ynotebook_notebook_ptr, (struct yetty_ynotebook_notebook *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_notebook_to(
    struct yetty_ynotebook_notebook *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_notebook_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ynotebook_notebook_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_ynotebook_notebook_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_output_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ynotebook_output_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ynotebook_output");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ynotebook_output_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ynotebook_output_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_output_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_cell_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ynotebook_cell_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ynotebook_cell");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ynotebook_cell_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ynotebook_cell_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_cell_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_notebook_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ynotebook_notebook_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ynotebook_notebook");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ynotebook_notebook_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ynotebook_notebook_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_notebook_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_ynotebook_output_class_get(void);
struct yetty_yclass_ptr_result yetty_ynotebook_cell_class_get(void);
struct yetty_yclass_ptr_result yetty_ynotebook_notebook_class_get(void);
struct yetty_ycore_void_result yetty_ynotebook_notebook_register(void);

/* ---- ynotebook_notebook: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ynotebook_notebook_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ynotebook_output") == 0) {
        return yetty_ynotebook_output_class_get();
    }
    if (strcmp(name, "yetty_ynotebook_cell") == 0) {
        return yetty_ynotebook_cell_class_get();
    }
    if (strcmp(name, "yetty_ynotebook_notebook") == 0) {
        return yetty_ynotebook_notebook_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ynotebook_notebook: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ynotebook_notebook_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ynotebook_notebook_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ynotebook_notebook_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
