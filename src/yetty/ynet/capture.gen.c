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

struct yetty_ycore_const_char_ptr_result;
struct yetty_ycore_const_uint8_ptr_result;
struct yetty_ycore_float_result;
struct yetty_ycore_uint32_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;
struct yetty_ycore_void_result yetty_ynet_load_file(struct yetty_yclass_object * obj, const char * path);
struct yetty_ycore_uint32_result yetty_ynet_packet_count(struct yetty_yclass_object * obj);
struct yetty_ycore_float_result yetty_ynet_packet_time(struct yetty_yclass_object * obj, uint32_t index);
struct yetty_ycore_uint32_result yetty_ynet_packet_length(struct yetty_yclass_object * obj, uint32_t index);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_protocol(struct yetty_yclass_object * obj, uint32_t index);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_source(struct yetty_yclass_object * obj, uint32_t index);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_destination(struct yetty_yclass_object * obj, uint32_t index);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_info(struct yetty_yclass_object * obj, uint32_t index);
struct yetty_ycore_const_uint8_ptr_result yetty_ynet_packet_bytes(struct yetty_yclass_object * obj, uint32_t index);
struct yetty_ycore_uint32_result yetty_ynet_packet_caplen(struct yetty_yclass_object * obj, uint32_t index);
struct yetty_ycore_uint32_result yetty_ynet_flow_count(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_ynet_flow_summary(struct yetty_yclass_object * obj, uint32_t index);
struct yetty_ydraw_drawable_list_result yetty_ynet_render(struct yetty_yclass_object * obj, uint32_t width, uint32_t height);
struct yetty_ycore_void_result yetty_ynet_destroy(struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_ynet_load_file_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_uint32_result (*yetty_ynet_packet_count_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_ynet_packet_time_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_uint32_result (*yetty_ynet_packet_length_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynet_packet_protocol_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynet_packet_source_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynet_packet_destination_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynet_packet_info_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_const_uint8_ptr_result (*yetty_ynet_packet_bytes_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_uint32_result (*yetty_ynet_packet_caplen_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_uint32_result (*yetty_ynet_flow_count_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynet_flow_summary_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ynet_render_fn)(struct yetty_yclass_object *, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ynet_destroy_fn)(struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_ynet_load_file_fn yetty_ynet_capture_yetty_ynet_load_file_check = capture_load_file;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_count_fn yetty_ynet_capture_yetty_ynet_packet_count_check = capture_packet_count;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_time_fn yetty_ynet_capture_yetty_ynet_packet_time_check = capture_packet_time;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_length_fn yetty_ynet_capture_yetty_ynet_packet_length_check = capture_packet_length;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_protocol_fn yetty_ynet_capture_yetty_ynet_packet_protocol_check = capture_packet_protocol;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_source_fn yetty_ynet_capture_yetty_ynet_packet_source_check = capture_packet_source;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_destination_fn yetty_ynet_capture_yetty_ynet_packet_destination_check = capture_packet_destination;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_info_fn yetty_ynet_capture_yetty_ynet_packet_info_check = capture_packet_info;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_bytes_fn yetty_ynet_capture_yetty_ynet_packet_bytes_check = capture_packet_bytes;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_caplen_fn yetty_ynet_capture_yetty_ynet_packet_caplen_check = capture_packet_caplen;
YETTY_MAYBE_UNUSED
static yetty_ynet_flow_count_fn yetty_ynet_capture_yetty_ynet_flow_count_check = capture_flow_count;
YETTY_MAYBE_UNUSED
static yetty_ynet_flow_summary_fn yetty_ynet_capture_yetty_ynet_flow_summary_check = capture_flow_summary;
YETTY_MAYBE_UNUSED
static yetty_ynet_render_fn yetty_ynet_capture_yetty_ynet_render_check = capture_render;
YETTY_MAYBE_UNUSED
static yetty_ynet_destroy_fn yetty_ynet_capture_yetty_ynet_destroy_check = capture_destroy;

struct yetty_yclass_ptr_result yetty_ynet_capture_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ynet_capture");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ynet_capture",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ynet_capture),
        .data_align = _Alignof(struct yetty_ynet_capture),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ynet", "load_file", (yetty_yclass_method_id_t)yetty_ynet_load_file, (yetty_yclass_impl_t)capture_load_file},
        {"yetty_ynet", "packet_count", (yetty_yclass_method_id_t)yetty_ynet_packet_count, (yetty_yclass_impl_t)capture_packet_count},
        {"yetty_ynet", "packet_time", (yetty_yclass_method_id_t)yetty_ynet_packet_time, (yetty_yclass_impl_t)capture_packet_time},
        {"yetty_ynet", "packet_length", (yetty_yclass_method_id_t)yetty_ynet_packet_length, (yetty_yclass_impl_t)capture_packet_length},
        {"yetty_ynet", "packet_protocol", (yetty_yclass_method_id_t)yetty_ynet_packet_protocol, (yetty_yclass_impl_t)capture_packet_protocol},
        {"yetty_ynet", "packet_source", (yetty_yclass_method_id_t)yetty_ynet_packet_source, (yetty_yclass_impl_t)capture_packet_source},
        {"yetty_ynet", "packet_destination", (yetty_yclass_method_id_t)yetty_ynet_packet_destination, (yetty_yclass_impl_t)capture_packet_destination},
        {"yetty_ynet", "packet_info", (yetty_yclass_method_id_t)yetty_ynet_packet_info, (yetty_yclass_impl_t)capture_packet_info},
        {"yetty_ynet", "packet_bytes", (yetty_yclass_method_id_t)yetty_ynet_packet_bytes, (yetty_yclass_impl_t)capture_packet_bytes},
        {"yetty_ynet", "packet_caplen", (yetty_yclass_method_id_t)yetty_ynet_packet_caplen, (yetty_yclass_impl_t)capture_packet_caplen},
        {"yetty_ynet", "flow_count", (yetty_yclass_method_id_t)yetty_ynet_flow_count, (yetty_yclass_impl_t)capture_flow_count},
        {"yetty_ynet", "flow_summary", (yetty_yclass_method_id_t)yetty_ynet_flow_summary, (yetty_yclass_impl_t)capture_flow_summary},
        {"yetty_ynet", "render", (yetty_yclass_method_id_t)yetty_ynet_render, (yetty_yclass_impl_t)capture_render},
        {"yetty_ynet", "destroy", (yetty_yclass_method_id_t)yetty_ynet_destroy, (yetty_yclass_impl_t)capture_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ynet_capture_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ynet_capture_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ynet_capture_ptr_result yetty_ynet_capture_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ynet_capture_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_ynet_capture_ptr, "yetty_ynet_capture_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_ynet_capture_ptr, "yetty_ynet_capture_from: object_data", slice_r);
    return YETTY_OK(yetty_ynet_capture_ptr, (struct yetty_ynet_capture *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ynet_capture_to(struct yetty_ynet_capture *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_ynet_capture_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ynet_capture_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ynet_capture_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}


struct yetty_ycore_void_result yetty_ynet_load_file(struct yetty_yclass_object * obj, const char * path)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynet", (yetty_yclass_method_id_t)yetty_ynet_load_file);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ynet_load_file: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ynet_load_file: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ynet_load_file: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ynet_load_file: dispatch_lookup failed");
    return ((yetty_ynet_load_file_fn)dispatch_impl_r.value)(obj, path);
}

struct yetty_ycore_uint32_result yetty_ynet_packet_count(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynet", (yetty_yclass_method_id_t)yetty_ynet_packet_count);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_uint32, "yetty_ynet_packet_count: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_uint32, "yetty_ynet_packet_count: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, object_class_r, "yetty_ynet_packet_count: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, dispatch_impl_r, "yetty_ynet_packet_count: dispatch_lookup failed");
    return ((yetty_ynet_packet_count_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_float_result yetty_ynet_packet_time(struct yetty_yclass_object * obj, uint32_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynet", (yetty_yclass_method_id_t)yetty_ynet_packet_time);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_float, "yetty_ynet_packet_time: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_float, "yetty_ynet_packet_time: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, object_class_r, "yetty_ynet_packet_time: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, dispatch_impl_r, "yetty_ynet_packet_time: dispatch_lookup failed");
    return ((yetty_ynet_packet_time_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ycore_uint32_result yetty_ynet_packet_length(struct yetty_yclass_object * obj, uint32_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynet", (yetty_yclass_method_id_t)yetty_ynet_packet_length);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_uint32, "yetty_ynet_packet_length: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_uint32, "yetty_ynet_packet_length: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, object_class_r, "yetty_ynet_packet_length: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, dispatch_impl_r, "yetty_ynet_packet_length: dispatch_lookup failed");
    return ((yetty_ynet_packet_length_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_protocol(struct yetty_yclass_object * obj, uint32_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynet", (yetty_yclass_method_id_t)yetty_ynet_packet_protocol);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynet_packet_protocol: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynet_packet_protocol: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_ynet_packet_protocol: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_ynet_packet_protocol: dispatch_lookup failed");
    return ((yetty_ynet_packet_protocol_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_source(struct yetty_yclass_object * obj, uint32_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynet", (yetty_yclass_method_id_t)yetty_ynet_packet_source);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynet_packet_source: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynet_packet_source: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_ynet_packet_source: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_ynet_packet_source: dispatch_lookup failed");
    return ((yetty_ynet_packet_source_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_destination(struct yetty_yclass_object * obj, uint32_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynet", (yetty_yclass_method_id_t)yetty_ynet_packet_destination);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynet_packet_destination: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynet_packet_destination: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_ynet_packet_destination: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_ynet_packet_destination: dispatch_lookup failed");
    return ((yetty_ynet_packet_destination_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_info(struct yetty_yclass_object * obj, uint32_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynet", (yetty_yclass_method_id_t)yetty_ynet_packet_info);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynet_packet_info: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynet_packet_info: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_ynet_packet_info: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_ynet_packet_info: dispatch_lookup failed");
    return ((yetty_ynet_packet_info_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ycore_const_uint8_ptr_result yetty_ynet_packet_bytes(struct yetty_yclass_object * obj, uint32_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynet", (yetty_yclass_method_id_t)yetty_ynet_packet_bytes);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_uint8_ptr, "yetty_ynet_packet_bytes: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_uint8_ptr, "yetty_ynet_packet_bytes: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_uint8_ptr, object_class_r, "yetty_ynet_packet_bytes: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_uint8_ptr, dispatch_impl_r, "yetty_ynet_packet_bytes: dispatch_lookup failed");
    return ((yetty_ynet_packet_bytes_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ycore_uint32_result yetty_ynet_packet_caplen(struct yetty_yclass_object * obj, uint32_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynet", (yetty_yclass_method_id_t)yetty_ynet_packet_caplen);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_uint32, "yetty_ynet_packet_caplen: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_uint32, "yetty_ynet_packet_caplen: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, object_class_r, "yetty_ynet_packet_caplen: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, dispatch_impl_r, "yetty_ynet_packet_caplen: dispatch_lookup failed");
    return ((yetty_ynet_packet_caplen_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ycore_uint32_result yetty_ynet_flow_count(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynet", (yetty_yclass_method_id_t)yetty_ynet_flow_count);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_uint32, "yetty_ynet_flow_count: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_uint32, "yetty_ynet_flow_count: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, object_class_r, "yetty_ynet_flow_count: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, dispatch_impl_r, "yetty_ynet_flow_count: dispatch_lookup failed");
    return ((yetty_ynet_flow_count_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_ynet_flow_summary(struct yetty_yclass_object * obj, uint32_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynet", (yetty_yclass_method_id_t)yetty_ynet_flow_summary);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynet_flow_summary: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynet_flow_summary: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_ynet_flow_summary: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_ynet_flow_summary: dispatch_lookup failed");
    return ((yetty_ynet_flow_summary_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ydraw_drawable_list_result yetty_ynet_render(struct yetty_yclass_object * obj, uint32_t width, uint32_t height)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynet", (yetty_yclass_method_id_t)yetty_ynet_render);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ydraw_drawable_list, "yetty_ynet_render: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ydraw_drawable_list, "yetty_ynet_render: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, object_class_r, "yetty_ynet_render: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, dispatch_impl_r, "yetty_ynet_render: dispatch_lookup failed");
    return ((yetty_ynet_render_fn)dispatch_impl_r.value)(obj, width, height);
}

struct yetty_ycore_void_result yetty_ynet_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynet", (yetty_yclass_method_id_t)yetty_ynet_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ynet_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ynet_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ynet_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ynet_destroy: dispatch_lookup failed");
    return ((yetty_ynet_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_ynet_capture_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ynet_capture_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ynet_capture");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ynet_capture_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynet_capture_create: class accessor failed", class_accessor_r);
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
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ynet_capture");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_ynet_capture_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_ynet_capture";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynet_capture_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynet_capture_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ynet_capture_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

