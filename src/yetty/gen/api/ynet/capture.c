/* GENERATED — do not edit. */
#include <yetty/api/ynet/capture.h>

#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h>  /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h>  /* malloc/free for buffer marshalling */
#include <string.h>  /* memcpy/strlen */

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

