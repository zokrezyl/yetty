/* GENERATED — do not edit. */
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h>  /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h>  /* calloc/free for proxy + buffer marshalling */
#include <string.h>  /* memcpy/strcmp/strlen */

struct yetty_yclass_object_ptr_result;
struct yetty_ycore_char_ptr_result;
struct yetty_ycore_const_char_ptr_result;
struct yetty_ycore_int_result;
struct yetty_ycore_size_result;
struct yetty_ycore_void_result;
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_type(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_stream_name(struct yetty_yclass_object * obj);
struct yetty_ycore_char_ptr_result yetty_ynotebook_output_text(struct yetty_yclass_object * obj);
struct yetty_ycore_int_result yetty_ynotebook_output_execution_count(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_error_name(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_error_value(struct yetty_yclass_object * obj);
struct yetty_yclass_object_ptr_result yetty_ynotebook_output_bundle(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ynotebook_output_destroy(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_cell_type(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_cell_id(struct yetty_yclass_object * obj);
struct yetty_ycore_char_ptr_result yetty_ynotebook_cell_source(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ynotebook_cell_set_source(struct yetty_yclass_object * obj, const char * text);
struct yetty_ycore_int_result yetty_ynotebook_cell_execution_count(struct yetty_yclass_object * obj);
struct yetty_ycore_size_result yetty_ynotebook_cell_output_count(struct yetty_yclass_object * obj);
struct yetty_yclass_object_ptr_result yetty_ynotebook_cell_output_at(struct yetty_yclass_object * obj, size_t index);
struct yetty_ycore_char_ptr_result yetty_ynotebook_cell_metadata_json(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ynotebook_cell_apply_message(struct yetty_yclass_object * obj, const char * msg_type, const char * content_json);
struct yetty_ycore_void_result yetty_ynotebook_cell_destroy(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ynotebook_notebook_load_text(struct yetty_yclass_object * obj, const char * json);
struct yetty_ycore_void_result yetty_ynotebook_notebook_load_file(struct yetty_yclass_object * obj, const char * path);
struct yetty_ycore_char_ptr_result yetty_ynotebook_notebook_to_text(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ynotebook_notebook_save_file(struct yetty_yclass_object * obj, const char * path);
struct yetty_ycore_int_result yetty_ynotebook_notebook_nbformat(struct yetty_yclass_object * obj);
struct yetty_ycore_int_result yetty_ynotebook_notebook_nbformat_minor(struct yetty_yclass_object * obj);
struct yetty_ycore_size_result yetty_ynotebook_notebook_cell_count(struct yetty_yclass_object * obj);
struct yetty_yclass_object_ptr_result yetty_ynotebook_notebook_cell_at(struct yetty_yclass_object * obj, size_t index);
struct yetty_ycore_char_ptr_result yetty_ynotebook_notebook_metadata_json(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ynotebook_notebook_destroy(struct yetty_yclass_object * obj);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_output_type_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_output_stream_name_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_output_text_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ynotebook_output_execution_count_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_output_error_name_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_output_error_value_fn)(struct yetty_yclass_object *);
typedef struct yetty_yclass_object_ptr_result (*yetty_ynotebook_output_bundle_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_output_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_cell_type_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_cell_id_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_cell_source_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_cell_set_source_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ynotebook_cell_execution_count_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_size_result (*yetty_ynotebook_cell_output_count_fn)(struct yetty_yclass_object *);
typedef struct yetty_yclass_object_ptr_result (*yetty_ynotebook_cell_output_at_fn)(struct yetty_yclass_object *, size_t);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_cell_metadata_json_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_cell_apply_message_fn)(struct yetty_yclass_object *, const char *, const char *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_cell_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_notebook_load_text_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_notebook_load_file_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_notebook_to_text_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_notebook_save_file_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ynotebook_notebook_nbformat_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ynotebook_notebook_nbformat_minor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_size_result (*yetty_ynotebook_notebook_cell_count_fn)(struct yetty_yclass_object *);
typedef struct yetty_yclass_object_ptr_result (*yetty_ynotebook_notebook_cell_at_fn)(struct yetty_yclass_object *, size_t);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_notebook_metadata_json_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_notebook_destroy_fn)(struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_type_fn yetty_ynotebook_output_yetty_ynotebook_output_type_check = output_type;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_stream_name_fn yetty_ynotebook_output_yetty_ynotebook_output_stream_name_check = output_stream_name;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_text_fn yetty_ynotebook_output_yetty_ynotebook_output_text_check = output_text;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_execution_count_fn yetty_ynotebook_output_yetty_ynotebook_output_execution_count_check = output_execution_count;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_error_name_fn yetty_ynotebook_output_yetty_ynotebook_output_error_name_check = output_error_name;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_error_value_fn yetty_ynotebook_output_yetty_ynotebook_output_error_value_check = output_error_value;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_bundle_fn yetty_ynotebook_output_yetty_ynotebook_output_bundle_check = output_bundle;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_output_destroy_fn yetty_ynotebook_output_yetty_ynotebook_output_destroy_check = output_destroy;

struct yetty_yclass_ptr_result yetty_ynotebook_output_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ynotebook_output");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ynotebook_output",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ynotebook_output),
        .data_align = _Alignof(struct yetty_ynotebook_output),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ynotebook", "output_type", (yetty_yclass_method_id_t)yetty_ynotebook_output_type, (yetty_yclass_impl_t)output_type},
        {"yetty_ynotebook", "output_stream_name", (yetty_yclass_method_id_t)yetty_ynotebook_output_stream_name, (yetty_yclass_impl_t)output_stream_name},
        {"yetty_ynotebook", "output_text", (yetty_yclass_method_id_t)yetty_ynotebook_output_text, (yetty_yclass_impl_t)output_text},
        {"yetty_ynotebook", "output_execution_count", (yetty_yclass_method_id_t)yetty_ynotebook_output_execution_count, (yetty_yclass_impl_t)output_execution_count},
        {"yetty_ynotebook", "output_error_name", (yetty_yclass_method_id_t)yetty_ynotebook_output_error_name, (yetty_yclass_impl_t)output_error_name},
        {"yetty_ynotebook", "output_error_value", (yetty_yclass_method_id_t)yetty_ynotebook_output_error_value, (yetty_yclass_impl_t)output_error_value},
        {"yetty_ynotebook", "output_bundle", (yetty_yclass_method_id_t)yetty_ynotebook_output_bundle, (yetty_yclass_impl_t)output_bundle},
        {"yetty_ynotebook", "output_destroy", (yetty_yclass_method_id_t)yetty_ynotebook_output_destroy, (yetty_yclass_impl_t)output_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ynotebook_output_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ynotebook_output_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ynotebook_output_ptr_result yetty_ynotebook_output_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_output_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_ynotebook_output_ptr, "yetty_ynotebook_output_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_ynotebook_output_ptr, "yetty_ynotebook_output_from: object_data", slice_r);
    return YETTY_OK(yetty_ynotebook_output_ptr, (struct yetty_ynotebook_output *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_output_to(struct yetty_ynotebook_output *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_output_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ynotebook_output_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ynotebook_output_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_type_fn yetty_ynotebook_cell_yetty_ynotebook_cell_type_check = cell_type;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_id_fn yetty_ynotebook_cell_yetty_ynotebook_cell_id_check = cell_id;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_source_fn yetty_ynotebook_cell_yetty_ynotebook_cell_source_check = cell_source;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_set_source_fn yetty_ynotebook_cell_yetty_ynotebook_cell_set_source_check = cell_set_source;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_execution_count_fn yetty_ynotebook_cell_yetty_ynotebook_cell_execution_count_check = cell_execution_count;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_output_count_fn yetty_ynotebook_cell_yetty_ynotebook_cell_output_count_check = cell_output_count;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_output_at_fn yetty_ynotebook_cell_yetty_ynotebook_cell_output_at_check = cell_output_at;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_metadata_json_fn yetty_ynotebook_cell_yetty_ynotebook_cell_metadata_json_check = cell_metadata_json;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_apply_message_fn yetty_ynotebook_cell_yetty_ynotebook_cell_apply_message_check = cell_apply_message;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_cell_destroy_fn yetty_ynotebook_cell_yetty_ynotebook_cell_destroy_check = cell_destroy;

struct yetty_yclass_ptr_result yetty_ynotebook_cell_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ynotebook_cell");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ynotebook_cell",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ynotebook_cell),
        .data_align = _Alignof(struct yetty_ynotebook_cell),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ynotebook", "cell_type", (yetty_yclass_method_id_t)yetty_ynotebook_cell_type, (yetty_yclass_impl_t)cell_type},
        {"yetty_ynotebook", "cell_id", (yetty_yclass_method_id_t)yetty_ynotebook_cell_id, (yetty_yclass_impl_t)cell_id},
        {"yetty_ynotebook", "cell_source", (yetty_yclass_method_id_t)yetty_ynotebook_cell_source, (yetty_yclass_impl_t)cell_source},
        {"yetty_ynotebook", "cell_set_source", (yetty_yclass_method_id_t)yetty_ynotebook_cell_set_source, (yetty_yclass_impl_t)cell_set_source},
        {"yetty_ynotebook", "cell_execution_count", (yetty_yclass_method_id_t)yetty_ynotebook_cell_execution_count, (yetty_yclass_impl_t)cell_execution_count},
        {"yetty_ynotebook", "cell_output_count", (yetty_yclass_method_id_t)yetty_ynotebook_cell_output_count, (yetty_yclass_impl_t)cell_output_count},
        {"yetty_ynotebook", "cell_output_at", (yetty_yclass_method_id_t)yetty_ynotebook_cell_output_at, (yetty_yclass_impl_t)cell_output_at},
        {"yetty_ynotebook", "cell_metadata_json", (yetty_yclass_method_id_t)yetty_ynotebook_cell_metadata_json, (yetty_yclass_impl_t)cell_metadata_json},
        {"yetty_ynotebook", "cell_apply_message", (yetty_yclass_method_id_t)yetty_ynotebook_cell_apply_message, (yetty_yclass_impl_t)cell_apply_message},
        {"yetty_ynotebook", "cell_destroy", (yetty_yclass_method_id_t)yetty_ynotebook_cell_destroy, (yetty_yclass_impl_t)cell_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ynotebook_cell_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ynotebook_cell_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ynotebook_cell_ptr_result yetty_ynotebook_cell_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_cell_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_ynotebook_cell_ptr, "yetty_ynotebook_cell_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_ynotebook_cell_ptr, "yetty_ynotebook_cell_from: object_data", slice_r);
    return YETTY_OK(yetty_ynotebook_cell_ptr, (struct yetty_ynotebook_cell *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_cell_to(struct yetty_ynotebook_cell *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_cell_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ynotebook_cell_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ynotebook_cell_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_load_text_fn yetty_ynotebook_notebook_yetty_ynotebook_notebook_load_text_check = notebook_load_text;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_load_file_fn yetty_ynotebook_notebook_yetty_ynotebook_notebook_load_file_check = notebook_load_file;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_to_text_fn yetty_ynotebook_notebook_yetty_ynotebook_notebook_to_text_check = notebook_to_text;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_save_file_fn yetty_ynotebook_notebook_yetty_ynotebook_notebook_save_file_check = notebook_save_file;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_nbformat_fn yetty_ynotebook_notebook_yetty_ynotebook_notebook_nbformat_check = notebook_nbformat;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_nbformat_minor_fn yetty_ynotebook_notebook_yetty_ynotebook_notebook_nbformat_minor_check = notebook_nbformat_minor;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_cell_count_fn yetty_ynotebook_notebook_yetty_ynotebook_notebook_cell_count_check = notebook_cell_count;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_cell_at_fn yetty_ynotebook_notebook_yetty_ynotebook_notebook_cell_at_check = notebook_cell_at;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_metadata_json_fn yetty_ynotebook_notebook_yetty_ynotebook_notebook_metadata_json_check = notebook_metadata_json;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_notebook_destroy_fn yetty_ynotebook_notebook_yetty_ynotebook_notebook_destroy_check = notebook_destroy;

struct yetty_yclass_ptr_result yetty_ynotebook_notebook_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ynotebook_notebook");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ynotebook_notebook",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ynotebook_notebook),
        .data_align = _Alignof(struct yetty_ynotebook_notebook),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ynotebook", "notebook_load_text", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_load_text, (yetty_yclass_impl_t)notebook_load_text},
        {"yetty_ynotebook", "notebook_load_file", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_load_file, (yetty_yclass_impl_t)notebook_load_file},
        {"yetty_ynotebook", "notebook_to_text", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_to_text, (yetty_yclass_impl_t)notebook_to_text},
        {"yetty_ynotebook", "notebook_save_file", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_save_file, (yetty_yclass_impl_t)notebook_save_file},
        {"yetty_ynotebook", "notebook_nbformat", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_nbformat, (yetty_yclass_impl_t)notebook_nbformat},
        {"yetty_ynotebook", "notebook_nbformat_minor", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_nbformat_minor, (yetty_yclass_impl_t)notebook_nbformat_minor},
        {"yetty_ynotebook", "notebook_cell_count", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_cell_count, (yetty_yclass_impl_t)notebook_cell_count},
        {"yetty_ynotebook", "notebook_cell_at", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_cell_at, (yetty_yclass_impl_t)notebook_cell_at},
        {"yetty_ynotebook", "notebook_metadata_json", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_metadata_json, (yetty_yclass_impl_t)notebook_metadata_json},
        {"yetty_ynotebook", "notebook_destroy", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_destroy, (yetty_yclass_impl_t)notebook_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ynotebook_notebook_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ynotebook_notebook_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ynotebook_notebook_ptr_result yetty_ynotebook_notebook_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_notebook_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_ynotebook_notebook_ptr, "yetty_ynotebook_notebook_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_ynotebook_notebook_ptr, "yetty_ynotebook_notebook_from: object_data", slice_r);
    return YETTY_OK(yetty_ynotebook_notebook_ptr, (struct yetty_ynotebook_notebook *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_notebook_to(struct yetty_ynotebook_notebook *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_notebook_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ynotebook_notebook_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ynotebook_notebook_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}


struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_type(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_type);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_output_type: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_output_type: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_ynotebook_output_type: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_ynotebook_output_type: dispatch_lookup failed");
    return ((yetty_ynotebook_output_type_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_stream_name(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_stream_name);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_output_stream_name: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_output_stream_name: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_ynotebook_output_stream_name: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_ynotebook_output_stream_name: dispatch_lookup failed");
    return ((yetty_ynotebook_output_stream_name_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_char_ptr_result yetty_ynotebook_output_text(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_text);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_output_text: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_output_text: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r, "yetty_ynotebook_output_text: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r, "yetty_ynotebook_output_text: dispatch_lookup failed");
    return ((yetty_ynotebook_output_text_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_int_result yetty_ynotebook_output_execution_count(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_execution_count);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ynotebook_output_execution_count: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ynotebook_output_execution_count: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ynotebook_output_execution_count: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ynotebook_output_execution_count: dispatch_lookup failed");
    return ((yetty_ynotebook_output_execution_count_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_error_name(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_error_name);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_output_error_name: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_output_error_name: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_ynotebook_output_error_name: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_ynotebook_output_error_name: dispatch_lookup failed");
    return ((yetty_ynotebook_output_error_name_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_error_value(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_error_value);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_output_error_value: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_output_error_value: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_ynotebook_output_error_value: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_ynotebook_output_error_value: dispatch_lookup failed");
    return ((yetty_ynotebook_output_error_value_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_output_bundle(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_bundle);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ynotebook_output_bundle: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ynotebook_output_bundle: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_class_r, "yetty_ynotebook_output_bundle: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, dispatch_impl_r, "yetty_ynotebook_output_bundle: dispatch_lookup failed");
    return ((yetty_ynotebook_output_bundle_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ynotebook_output_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_output_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_output_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_output_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ynotebook_output_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ynotebook_output_destroy: dispatch_lookup failed");
    return ((yetty_ynotebook_output_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_ynotebook_cell_type(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_type);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_cell_type: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_cell_type: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_ynotebook_cell_type: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_ynotebook_cell_type: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_type_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_ynotebook_cell_id(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_id);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_cell_id: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_cell_id: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_ynotebook_cell_id: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_ynotebook_cell_id: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_id_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_char_ptr_result yetty_ynotebook_cell_source(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_source);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_cell_source: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_cell_source: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r, "yetty_ynotebook_cell_source: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r, "yetty_ynotebook_cell_source: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_source_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ynotebook_cell_set_source(struct yetty_yclass_object * obj, const char * text)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_set_source);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_cell_set_source: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_cell_set_source: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ynotebook_cell_set_source: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ynotebook_cell_set_source: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_set_source_fn)dispatch_impl_r.value)(obj, text);
}

struct yetty_ycore_int_result yetty_ynotebook_cell_execution_count(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_execution_count);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ynotebook_cell_execution_count: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ynotebook_cell_execution_count: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ynotebook_cell_execution_count: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ynotebook_cell_execution_count: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_execution_count_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_size_result yetty_ynotebook_cell_output_count(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_output_count);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_size, "yetty_ynotebook_cell_output_count: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_size, "yetty_ynotebook_cell_output_count: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, object_class_r, "yetty_ynotebook_cell_output_count: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, dispatch_impl_r, "yetty_ynotebook_cell_output_count: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_output_count_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_cell_output_at(struct yetty_yclass_object * obj, size_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_output_at);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ynotebook_cell_output_at: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ynotebook_cell_output_at: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_class_r, "yetty_ynotebook_cell_output_at: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, dispatch_impl_r, "yetty_ynotebook_cell_output_at: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_output_at_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ycore_char_ptr_result yetty_ynotebook_cell_metadata_json(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_metadata_json);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_cell_metadata_json: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_cell_metadata_json: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r, "yetty_ynotebook_cell_metadata_json: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r, "yetty_ynotebook_cell_metadata_json: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_metadata_json_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ynotebook_cell_apply_message(struct yetty_yclass_object * obj, const char * msg_type, const char * content_json)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_apply_message);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_cell_apply_message: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_cell_apply_message: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ynotebook_cell_apply_message: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ynotebook_cell_apply_message: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_apply_message_fn)dispatch_impl_r.value)(obj, msg_type, content_json);
}

struct yetty_ycore_void_result yetty_ynotebook_cell_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_cell_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_cell_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_cell_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ynotebook_cell_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ynotebook_cell_destroy: dispatch_lookup failed");
    return ((yetty_ynotebook_cell_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ynotebook_notebook_load_text(struct yetty_yclass_object * obj, const char * json)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_load_text);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_notebook_load_text: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_notebook_load_text: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ynotebook_notebook_load_text: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ynotebook_notebook_load_text: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_load_text_fn)dispatch_impl_r.value)(obj, json);
}

struct yetty_ycore_void_result yetty_ynotebook_notebook_load_file(struct yetty_yclass_object * obj, const char * path)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_load_file);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_notebook_load_file: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_notebook_load_file: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ynotebook_notebook_load_file: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ynotebook_notebook_load_file: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_load_file_fn)dispatch_impl_r.value)(obj, path);
}

struct yetty_ycore_char_ptr_result yetty_ynotebook_notebook_to_text(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_to_text);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_notebook_to_text: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_notebook_to_text: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r, "yetty_ynotebook_notebook_to_text: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r, "yetty_ynotebook_notebook_to_text: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_to_text_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ynotebook_notebook_save_file(struct yetty_yclass_object * obj, const char * path)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_save_file);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_notebook_save_file: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_notebook_save_file: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ynotebook_notebook_save_file: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ynotebook_notebook_save_file: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_save_file_fn)dispatch_impl_r.value)(obj, path);
}

struct yetty_ycore_int_result yetty_ynotebook_notebook_nbformat(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_nbformat);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ynotebook_notebook_nbformat: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ynotebook_notebook_nbformat: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ynotebook_notebook_nbformat: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ynotebook_notebook_nbformat: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_nbformat_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_int_result yetty_ynotebook_notebook_nbformat_minor(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_nbformat_minor);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ynotebook_notebook_nbformat_minor: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ynotebook_notebook_nbformat_minor: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ynotebook_notebook_nbformat_minor: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ynotebook_notebook_nbformat_minor: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_nbformat_minor_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_size_result yetty_ynotebook_notebook_cell_count(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_cell_count);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_size, "yetty_ynotebook_notebook_cell_count: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_size, "yetty_ynotebook_notebook_cell_count: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, object_class_r, "yetty_ynotebook_notebook_cell_count: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, dispatch_impl_r, "yetty_ynotebook_notebook_cell_count: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_cell_count_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_notebook_cell_at(struct yetty_yclass_object * obj, size_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_cell_at);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ynotebook_notebook_cell_at: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ynotebook_notebook_cell_at: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_class_r, "yetty_ynotebook_notebook_cell_at: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, dispatch_impl_r, "yetty_ynotebook_notebook_cell_at: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_cell_at_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ycore_char_ptr_result yetty_ynotebook_notebook_metadata_json(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_metadata_json);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_notebook_metadata_json: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_notebook_metadata_json: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r, "yetty_ynotebook_notebook_metadata_json: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r, "yetty_ynotebook_notebook_metadata_json: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_metadata_json_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ynotebook_notebook_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_notebook_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_notebook_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_notebook_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ynotebook_notebook_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ynotebook_notebook_destroy: dispatch_lookup failed");
    return ((yetty_ynotebook_notebook_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_output_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ynotebook_output_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ynotebook_output");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ynotebook_output_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_output_create: class accessor failed", class_accessor_r);
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r =
            yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) return alloc_r;
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ynotebook_output");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_ynotebook_output_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_ynotebook_output";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_output_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_output_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ynotebook_output_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_cell_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ynotebook_cell_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ynotebook_cell");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ynotebook_cell_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_cell_create: class accessor failed", class_accessor_r);
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r =
            yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) return alloc_r;
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ynotebook_cell");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_ynotebook_cell_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_ynotebook_cell";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_cell_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_cell_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ynotebook_cell_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_notebook_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ynotebook_notebook_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ynotebook_notebook");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ynotebook_notebook_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_notebook_create: class accessor failed", class_accessor_r);
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r =
            yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) return alloc_r;
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ynotebook_notebook");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_ynotebook_notebook_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_ynotebook_notebook";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_notebook_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_notebook_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ynotebook_notebook_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

