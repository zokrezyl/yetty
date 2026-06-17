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

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;
struct yetty_yrich_rect;
struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_bounds(struct yetty_yclass_object *obj,
                                                          struct yetty_yrich_rect *out_bounds);
struct yetty_ycore_int_result yetty_yrich_element_hit_test(struct yetty_yclass_object *obj, float x,
                                                           float y);
struct yetty_ycore_void_result yetty_yrich_element_render(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *drawable_list,
    uint32_t layer, int selected);
struct yetty_ycore_int_result yetty_yrich_element_is_editable(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_begin_edit(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_end_edit(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_yrich_element_is_editing(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_insert_text(struct yetty_yclass_object *obj,
                                                               struct yetty_ycore_buffer text);
struct yetty_ycore_void_result yetty_yrich_element_delete_sel(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_yrich_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_destroy_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_bounds_fn)(
    struct yetty_yclass_object *, struct yetty_yrich_rect *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_hit_test_fn)(
    struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_render_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *, uint32_t, int);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editable_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_begin_edit_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_end_edit_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editing_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_insert_text_fn)(
    struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_delete_sel_fn)(
    struct yetty_yclass_object *);

[[maybe_unused]]
static yetty_yrich_constructor_fn yetty_yrich_element_yetty_yrich_constructor_check =
    element_constructor;
[[maybe_unused]]
static yetty_yrich_element_destroy_fn yetty_yrich_element_yetty_yrich_element_destroy_check =
    element_default_destroy;
[[maybe_unused]]
static yetty_yrich_element_bounds_fn yetty_yrich_element_yetty_yrich_element_bounds_check =
    element_default_bounds;
[[maybe_unused]]
static yetty_yrich_element_hit_test_fn yetty_yrich_element_yetty_yrich_element_hit_test_check =
    element_default_hit_test;
[[maybe_unused]]
static yetty_yrich_element_render_fn yetty_yrich_element_yetty_yrich_element_render_check =
    element_default_render;
[[maybe_unused]]
static yetty_yrich_element_is_editable_fn
    yetty_yrich_element_yetty_yrich_element_is_editable_check = element_default_is_editable;
[[maybe_unused]]
static yetty_yrich_element_begin_edit_fn yetty_yrich_element_yetty_yrich_element_begin_edit_check =
    element_default_begin_edit;
[[maybe_unused]]
static yetty_yrich_element_end_edit_fn yetty_yrich_element_yetty_yrich_element_end_edit_check =
    element_default_end_edit;
[[maybe_unused]]
static yetty_yrich_element_is_editing_fn yetty_yrich_element_yetty_yrich_element_is_editing_check =
    element_default_is_editing;
[[maybe_unused]]
static yetty_yrich_element_insert_text_fn
    yetty_yrich_element_yetty_yrich_element_insert_text_check = element_default_insert_text;
[[maybe_unused]]
static yetty_yrich_element_delete_sel_fn yetty_yrich_element_yetty_yrich_element_delete_sel_check =
    element_default_delete_sel;

struct yetty_yclass_ptr_result yetty_yrich_element_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yrich_element");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yrich_element",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yrich_element),
        .data_align = _Alignof(struct yetty_yrich_element),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yrich", "constructor", (yetty_yclass_method_id_t)yetty_yrich_constructor,
         (yetty_yclass_impl_t)element_constructor},
        {"yetty_yrich", "element_destroy", (yetty_yclass_method_id_t)yetty_yrich_element_destroy,
         (yetty_yclass_impl_t)element_default_destroy},
        {"yetty_yrich", "element_bounds", (yetty_yclass_method_id_t)yetty_yrich_element_bounds,
         (yetty_yclass_impl_t)element_default_bounds},
        {"yetty_yrich", "element_hit_test", (yetty_yclass_method_id_t)yetty_yrich_element_hit_test,
         (yetty_yclass_impl_t)element_default_hit_test},
        {"yetty_yrich", "element_render", (yetty_yclass_method_id_t)yetty_yrich_element_render,
         (yetty_yclass_impl_t)element_default_render},
        {"yetty_yrich", "element_is_editable",
         (yetty_yclass_method_id_t)yetty_yrich_element_is_editable,
         (yetty_yclass_impl_t)element_default_is_editable},
        {"yetty_yrich", "element_begin_edit",
         (yetty_yclass_method_id_t)yetty_yrich_element_begin_edit,
         (yetty_yclass_impl_t)element_default_begin_edit},
        {"yetty_yrich", "element_end_edit", (yetty_yclass_method_id_t)yetty_yrich_element_end_edit,
         (yetty_yclass_impl_t)element_default_end_edit},
        {"yetty_yrich", "element_is_editing",
         (yetty_yclass_method_id_t)yetty_yrich_element_is_editing,
         (yetty_yclass_impl_t)element_default_is_editing},
        {"yetty_yrich", "element_insert_text",
         (yetty_yclass_method_id_t)yetty_yrich_element_insert_text,
         (yetty_yclass_impl_t)element_default_insert_text},
        {"yetty_yrich", "element_delete_sel",
         (yetty_yclass_method_id_t)yetty_yrich_element_delete_sel,
         (yetty_yclass_impl_t)element_default_delete_sel},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yrich_element_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_element_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yrich_element_ptr_result yetty_yrich_element_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yrich_element_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yrich_element_ptr, "yetty_yrich_element_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yrich_element_ptr, "yetty_yrich_element_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yrich_element_ptr, (struct yetty_yrich_element *)slice_r.value);
}

struct yetty_yclass_object *yetty_yrich_element_to(struct yetty_yrich_element *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yrich_element_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yetty_ycore_error_destroy(class_r.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    if (YETTY_IS_ERR(offset_r)) {
        yetty_ycore_error_destroy(offset_r.error);
        return NULL;
    }
    return (struct yetty_yclass_object *)((char *)data - offset_r.value);
}

struct yetty_ycore_void_result yetty_yrich_element_destroy(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_element_destroy);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_element_destroy: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_element_destroy: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yrich_element_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yrich_element_destroy: dispatch_lookup failed");
    return ((yetty_yrich_element_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_yrich_element_bounds(struct yetty_yclass_object *obj,
                                                          struct yetty_yrich_rect *out_bounds)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_element_bounds);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_element_bounds: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_element_bounds: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yrich_element_bounds: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yrich_element_bounds: dispatch_lookup failed");
    return ((yetty_yrich_element_bounds_fn)dispatch_impl_r.value)(obj, out_bounds);
}

struct yetty_ycore_int_result yetty_yrich_element_hit_test(struct yetty_yclass_object *obj, float x,
                                                           float y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_element_hit_test);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_yrich_element_hit_test: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_yrich_element_hit_test: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_yrich_element_hit_test: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            x, y};
#pragma pack(pop)
        uint8_t resp_buf[1 + sizeof(int)];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_yrich_element_hit_test: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_int, "yetty_yrich_element_hit_test: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_yrich_element_hit_test: remote impl returned error");
        }
        if (response_len != sizeof(resp_buf)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_yrich_element_hit_test: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_yrich_element_hit_test: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_yrich_element_hit_test: dispatch_lookup failed");
        return ((yetty_yrich_element_hit_test_fn)dispatch_impl_r.value)(obj, x, y);
    }
}

struct yetty_ycore_void_result yetty_yrich_element_render(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *drawable_list,
    uint32_t layer, int selected)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_element_render);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_element_render: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_element_render: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yrich_element_render: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yrich_element_render: dispatch_lookup failed");
    return ((yetty_yrich_element_render_fn)dispatch_impl_r.value)(obj, drawable_list, layer,
                                                                  selected);
}

struct yetty_ycore_int_result yetty_yrich_element_is_editable(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_element_is_editable);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_yrich_element_is_editable: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_yrich_element_is_editable: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_yrich_element_is_editable: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle};
#pragma pack(pop)
        uint8_t resp_buf[1 + sizeof(int)];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_yrich_element_is_editable: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_yrich_element_is_editable: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_yrich_element_is_editable: remote impl returned error");
        }
        if (response_len != sizeof(resp_buf)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_yrich_element_is_editable: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_yrich_element_is_editable: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_yrich_element_is_editable: dispatch_lookup failed");
        return ((yetty_yrich_element_is_editable_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_yrich_element_begin_edit(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_element_begin_edit);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_element_begin_edit: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_element_begin_edit: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_element_begin_edit: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_element_begin_edit: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_element_begin_edit: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_element_begin_edit: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_element_begin_edit: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_element_begin_edit: dispatch_lookup failed");
        return ((yetty_yrich_element_begin_edit_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_yrich_element_end_edit(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_element_end_edit);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_element_end_edit: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_element_end_edit: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_element_end_edit: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_element_end_edit: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_element_end_edit: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_element_end_edit: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_element_end_edit: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_element_end_edit: dispatch_lookup failed");
        return ((yetty_yrich_element_end_edit_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_int_result yetty_yrich_element_is_editing(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_element_is_editing);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_yrich_element_is_editing: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_yrich_element_is_editing: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_yrich_element_is_editing: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle};
#pragma pack(pop)
        uint8_t resp_buf[1 + sizeof(int)];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_yrich_element_is_editing: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_int, "yetty_yrich_element_is_editing: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_yrich_element_is_editing: remote impl returned error");
        }
        if (response_len != sizeof(resp_buf)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_yrich_element_is_editing: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_yrich_element_is_editing: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_yrich_element_is_editing: dispatch_lookup failed");
        return ((yetty_yrich_element_is_editing_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_yrich_element_insert_text(struct yetty_yclass_object *obj,
                                                               struct yetty_ycore_buffer text)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_element_insert_text);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_element_insert_text: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_element_insert_text: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_element_insert_text: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t text_len;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            (uint32_t)text.size};
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)text.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_element_insert_text: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, text.data, text.size);
        body_offset += text.size;
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, body_buf,
                                  body_total, resp_buf, sizeof(resp_buf));
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_element_insert_text: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_element_insert_text: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_element_insert_text: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_element_insert_text: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_element_insert_text: dispatch_lookup failed");
        return ((yetty_yrich_element_insert_text_fn)dispatch_impl_r.value)(obj, text);
    }
}

struct yetty_ycore_void_result yetty_yrich_element_delete_sel(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_element_delete_sel);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_element_delete_sel: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_element_delete_sel: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_element_delete_sel: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_element_delete_sel: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_element_delete_sel: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_element_delete_sel: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_element_delete_sel: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_element_delete_sel: dispatch_lookup failed");
        return ((yetty_yrich_element_delete_sel_fn)dispatch_impl_r.value)(obj);
    }
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_element_hit_test_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_element_hit_test_skel(const void *body, size_t body_len, void *resp,
                                         size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        float x;
        float y;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_element_hit_test: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_int_result call_r = yetty_yrich_element_hit_test(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.x, wire_args.y);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_element_hit_test", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    if (resp_max < 1 + sizeof(call_r.value)) {
        return 0;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_element_is_editable_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_element_is_editable_skel(const void *body, size_t body_len, void *resp,
                                            size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_element_is_editable: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_int_result call_r =
        yetty_yrich_element_is_editable((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_element_is_editable", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    if (resp_max < 1 + sizeof(call_r.value)) {
        return 0;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_element_begin_edit_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_element_begin_edit_skel(const void *body, size_t body_len, void *resp,
                                           size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_element_begin_edit: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yrich_element_begin_edit((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_element_begin_edit", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_element_end_edit_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_element_end_edit_skel(const void *body, size_t body_len, void *resp,
                                         size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_element_end_edit: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yrich_element_end_edit((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_element_end_edit", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_element_is_editing_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_element_is_editing_skel(const void *body, size_t body_len, void *resp,
                                           size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_element_is_editing: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_int_result call_r =
        yetty_yrich_element_is_editing((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_element_is_editing", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    if (resp_max < 1 + sizeof(call_r.value)) {
        return 0;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_element_insert_text_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_element_insert_text_skel(const void *body, size_t body_len, void *resp,
                                            size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t text_len;
    } wire_args;
#pragma pack(pop)
    if (body_len < sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    if (body_len != sizeof(wire_args) + (size_t)wire_args.text_len) {
        return 0;
    }
    size_t body_offset = sizeof(wire_args);
    struct yetty_ycore_buffer text_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.text_len,
        .capacity = (size_t)wire_args.text_len,
    };
    body_offset += (size_t)wire_args.text_len;
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_element_insert_text: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_element_insert_text(
        (struct yetty_yclass_object *)obj_resolve_r.value, text_buf);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_element_insert_text", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_element_delete_sel_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_element_delete_sel_skel(const void *body, size_t body_len, void *resp,
                                           size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_element_delete_sel: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yrich_element_delete_sel((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_element_delete_sel", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_element_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_element_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yrich_element");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yrich_element_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_element_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) {
            return alloc_r;
        }
        struct yetty_ycore_void_result ctor_r = yetty_yrich_constructor(alloc_r.value);
        if (YETTY_IS_ERR(ctor_r)) {
            struct yetty_ycore_void_result free_r = yetty_yclass_object_free(alloc_r.value);
            if (YETTY_IS_ERR(free_r)) {
                yetty_ycore_error_destroy(free_r.error);
            }
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_yrich_element_create: constructor failed", ctor_r);
        }
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yrich_element");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(
                stderr,
                "yetty_yrich_element_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yrich_element";
    struct yetty_ycore_size_result create_call_r =
        yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name,
                              strlen(class_name), &handle, sizeof(handle));
    if (YETTY_IS_ERR(create_call_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yrich_element_create: CREATE call failed",
                         create_call_r);
    }
    if (create_call_r.value != sizeof(handle) || !handle) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_element_create: CREATE returned no/invalid handle");
    }

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_element_create: calloc(proxy) failed");
    }
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}
