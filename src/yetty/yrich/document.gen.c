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

struct yetty_ycore_float_result;
struct yetty_ycore_void_result;
struct yetty_yrich_operation;
struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_document_content_width(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_document_content_height(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_render(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_apply_op(struct yetty_yclass_object *obj,
                                                             struct yetty_yrich_operation *op,
                                                             int local_flag);
struct yetty_ycore_void_result yetty_yrich_document_undo(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_redo(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_down(struct yetty_yclass_object *obj,
                                                                  float x, float y, uint32_t button,
                                                                  uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_up(struct yetty_yclass_object *obj,
                                                                float x, float y, uint32_t button,
                                                                uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_drag(struct yetty_yclass_object *obj,
                                                                  float x, float y, uint32_t button,
                                                                  uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_double_click(
    struct yetty_yclass_object *obj, float x, float y, uint32_t button, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_key_down(struct yetty_yclass_object *obj,
                                                                uint32_t key, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_text_input(struct yetty_yclass_object *obj,
                                                                  struct yetty_ycore_buffer text);
typedef struct yetty_ycore_void_result (*yetty_yrich_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_destroy_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_width_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_height_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_render_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_apply_op_fn)(
    struct yetty_yclass_object *, struct yetty_yrich_operation *, int);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_undo_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_redo_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_down_fn)(
    struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_up_fn)(
    struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_drag_fn)(
    struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_double_click_fn)(
    struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_key_down_fn)(
    struct yetty_yclass_object *, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_text_input_fn)(
    struct yetty_yclass_object *, struct yetty_ycore_buffer);

YETTY_MAYBE_UNUSED
static yetty_yrich_constructor_fn yetty_yrich_document_yetty_yrich_constructor_check =
    document_constructor;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_destroy_fn yetty_yrich_document_yetty_yrich_document_destroy_check =
    document_default_destroy;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_content_width_fn
    yetty_yrich_document_yetty_yrich_document_content_width_check = document_default_content_width;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_content_height_fn
    yetty_yrich_document_yetty_yrich_document_content_height_check =
        document_default_content_height;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_render_fn yetty_yrich_document_yetty_yrich_document_render_check =
    document_default_render;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_apply_op_fn yetty_yrich_document_yetty_yrich_document_apply_op_check =
    document_default_apply_op;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_undo_fn yetty_yrich_document_yetty_yrich_document_undo_check =
    document_default_undo;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_redo_fn yetty_yrich_document_yetty_yrich_document_redo_check =
    document_default_redo;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_mouse_down_fn
    yetty_yrich_document_yetty_yrich_document_on_mouse_down_check = document_default_on_mouse_down;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_mouse_up_fn
    yetty_yrich_document_yetty_yrich_document_on_mouse_up_check = document_default_on_mouse_up;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_mouse_drag_fn
    yetty_yrich_document_yetty_yrich_document_on_mouse_drag_check = document_default_on_mouse_drag;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_mouse_double_click_fn
    yetty_yrich_document_yetty_yrich_document_on_mouse_double_click_check =
        document_default_on_mouse_double_click;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_key_down_fn
    yetty_yrich_document_yetty_yrich_document_on_key_down_check = document_default_on_key_down;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_text_input_fn
    yetty_yrich_document_yetty_yrich_document_on_text_input_check = document_default_on_text_input;

struct yetty_yclass_ptr_result yetty_yrich_document_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yrich_document");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yrich_document",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yrich_document),
        .data_align = _Alignof(struct yetty_yrich_document),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yrich", "constructor", (yetty_yclass_method_id_t)yetty_yrich_constructor,
         (yetty_yclass_impl_t)document_constructor},
        {"yetty_yrich", "document_destroy", (yetty_yclass_method_id_t)yetty_yrich_document_destroy,
         (yetty_yclass_impl_t)document_default_destroy},
        {"yetty_yrich", "document_content_width",
         (yetty_yclass_method_id_t)yetty_yrich_document_content_width,
         (yetty_yclass_impl_t)document_default_content_width},
        {"yetty_yrich", "document_content_height",
         (yetty_yclass_method_id_t)yetty_yrich_document_content_height,
         (yetty_yclass_impl_t)document_default_content_height},
        {"yetty_yrich", "document_render", (yetty_yclass_method_id_t)yetty_yrich_document_render,
         (yetty_yclass_impl_t)document_default_render},
        {"yetty_yrich", "document_apply_op",
         (yetty_yclass_method_id_t)yetty_yrich_document_apply_op,
         (yetty_yclass_impl_t)document_default_apply_op},
        {"yetty_yrich", "document_undo", (yetty_yclass_method_id_t)yetty_yrich_document_undo,
         (yetty_yclass_impl_t)document_default_undo},
        {"yetty_yrich", "document_redo", (yetty_yclass_method_id_t)yetty_yrich_document_redo,
         (yetty_yclass_impl_t)document_default_redo},
        {"yetty_yrich", "document_on_mouse_down",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_down,
         (yetty_yclass_impl_t)document_default_on_mouse_down},
        {"yetty_yrich", "document_on_mouse_up",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_up,
         (yetty_yclass_impl_t)document_default_on_mouse_up},
        {"yetty_yrich", "document_on_mouse_drag",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_drag,
         (yetty_yclass_impl_t)document_default_on_mouse_drag},
        {"yetty_yrich", "document_on_mouse_double_click",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_double_click,
         (yetty_yclass_impl_t)document_default_on_mouse_double_click},
        {"yetty_yrich", "document_on_key_down",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_key_down,
         (yetty_yclass_impl_t)document_default_on_key_down},
        {"yetty_yrich", "document_on_text_input",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_text_input,
         (yetty_yclass_impl_t)document_default_on_text_input},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yrich_document_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_document_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yrich_document_ptr_result yetty_yrich_document_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yrich_document_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yrich_document_ptr, "yetty_yrich_document_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yrich_document_ptr, "yetty_yrich_document_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_yrich_document_ptr, (struct yetty_yrich_document *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yrich_document_to(struct yetty_yrich_document *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_yrich_document_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_yrich_document_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yrich_document_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_constructor);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_constructor: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_constructor: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_constructor: ensure_remote_id failed");
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
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_constructor: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_constructor: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_void_result remote_error =
                YETTY_ERR(yetty_ycore_void, "yetty_yrich_constructor: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        free(resp_buf);
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_constructor: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_constructor: dispatch_lookup failed");
        return ((yetty_yrich_constructor_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_destroy(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_destroy);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_document_destroy: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_destroy: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yrich_document_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yrich_document_destroy: dispatch_lookup failed");
    return ((yetty_yrich_document_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_float_result yetty_yrich_document_content_width(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_content_width);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_float,
                             "yetty_yrich_document_content_width: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_float, "yetty_yrich_document_content_width: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, remote_id_r,
                            "yetty_yrich_document_content_width: ensure_remote_id failed");
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
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, rpc_call_r,
                            "yetty_yrich_document_content_width: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_float,
                             "yetty_yrich_document_content_width: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_float_result remote_error =
                YETTY_ERR(yetty_ycore_float,
                          "yetty_yrich_document_content_width: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(float)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_float,
                             "yetty_yrich_document_content_width: truncated RPC payload");
        }
        float return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_float, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, object_class_r,
                            "yetty_yrich_document_content_width: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, dispatch_impl_r,
                            "yetty_yrich_document_content_width: dispatch_lookup failed");
        return ((yetty_yrich_document_content_width_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_float_result yetty_yrich_document_content_height(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_content_height);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_float,
                             "yetty_yrich_document_content_height: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_float, "yetty_yrich_document_content_height: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, remote_id_r,
                            "yetty_yrich_document_content_height: ensure_remote_id failed");
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
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, rpc_call_r,
                            "yetty_yrich_document_content_height: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_float,
                             "yetty_yrich_document_content_height: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_float_result remote_error =
                YETTY_ERR(yetty_ycore_float,
                          "yetty_yrich_document_content_height: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(float)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_float,
                             "yetty_yrich_document_content_height: truncated RPC payload");
        }
        float return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_float, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, object_class_r,
                            "yetty_yrich_document_content_height: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, dispatch_impl_r,
                            "yetty_yrich_document_content_height: dispatch_lookup failed");
        return ((yetty_yrich_document_content_height_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_render(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_render);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_document_render: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_render: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yrich_document_render: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yrich_document_render: dispatch_lookup failed");
    return ((yetty_yrich_document_render_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_yrich_document_apply_op(struct yetty_yclass_object *obj,
                                                             struct yetty_yrich_operation *op,
                                                             int local_flag)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_apply_op);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_document_apply_op: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_apply_op: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yrich_document_apply_op: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yrich_document_apply_op: dispatch_lookup failed");
    return ((yetty_yrich_document_apply_op_fn)dispatch_impl_r.value)(obj, op, local_flag);
}

struct yetty_ycore_void_result yetty_yrich_document_undo(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_undo);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_undo: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_undo: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_document_undo: ensure_remote_id failed");
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
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_document_undo: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_undo: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_void_result remote_error = YETTY_ERR(
                yetty_ycore_void, "yetty_yrich_document_undo: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        free(resp_buf);
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_document_undo: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_document_undo: dispatch_lookup failed");
        return ((yetty_yrich_document_undo_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_redo(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_redo);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_redo: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_redo: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_document_redo: ensure_remote_id failed");
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
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_document_redo: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_redo: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_void_result remote_error = YETTY_ERR(
                yetty_ycore_void, "yetty_yrich_document_redo: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        free(resp_buf);
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_document_redo: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_document_redo: dispatch_lookup failed");
        return ((yetty_yrich_document_redo_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_on_mouse_down(struct yetty_yclass_object *obj,
                                                                  float x, float y, uint32_t button,
                                                                  uint32_t mods)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_down);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_document_on_mouse_down: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_mouse_down: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_document_on_mouse_down: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
            uint32_t button;
            uint32_t mods;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            x, y, button, mods};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_document_on_mouse_down: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_document_on_mouse_down: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_void_result remote_error = YETTY_ERR(
                yetty_ycore_void, "yetty_yrich_document_on_mouse_down: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        free(resp_buf);
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_document_on_mouse_down: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_document_on_mouse_down: dispatch_lookup failed");
        return ((yetty_yrich_document_on_mouse_down_fn)dispatch_impl_r.value)(obj, x, y, button,
                                                                              mods);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_on_mouse_up(struct yetty_yclass_object *obj,
                                                                float x, float y, uint32_t button,
                                                                uint32_t mods)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_up);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_document_on_mouse_up: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_mouse_up: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_document_on_mouse_up: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
            uint32_t button;
            uint32_t mods;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            x, y, button, mods};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_document_on_mouse_up: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_document_on_mouse_up: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_void_result remote_error = YETTY_ERR(
                yetty_ycore_void, "yetty_yrich_document_on_mouse_up: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        free(resp_buf);
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_document_on_mouse_up: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_document_on_mouse_up: dispatch_lookup failed");
        return ((yetty_yrich_document_on_mouse_up_fn)dispatch_impl_r.value)(obj, x, y, button,
                                                                            mods);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_on_mouse_drag(struct yetty_yclass_object *obj,
                                                                  float x, float y, uint32_t button,
                                                                  uint32_t mods)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_drag);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_document_on_mouse_drag: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_mouse_drag: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_document_on_mouse_drag: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
            uint32_t button;
            uint32_t mods;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            x, y, button, mods};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_document_on_mouse_drag: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_document_on_mouse_drag: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_void_result remote_error = YETTY_ERR(
                yetty_ycore_void, "yetty_yrich_document_on_mouse_drag: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        free(resp_buf);
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_document_on_mouse_drag: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_document_on_mouse_drag: dispatch_lookup failed");
        return ((yetty_yrich_document_on_mouse_drag_fn)dispatch_impl_r.value)(obj, x, y, button,
                                                                              mods);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_on_mouse_double_click(
    struct yetty_yclass_object *obj, float x, float y, uint32_t button, uint32_t mods)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_double_click);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_document_on_mouse_double_click: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yrich_document_on_mouse_double_click: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_document_on_mouse_double_click: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
            uint32_t button;
            uint32_t mods;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            x, y, button, mods};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_document_on_mouse_double_click: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_document_on_mouse_double_click: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_void_result remote_error =
                YETTY_ERR(yetty_ycore_void,
                          "yetty_yrich_document_on_mouse_double_click: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        free(resp_buf);
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_document_on_mouse_double_click: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_document_on_mouse_double_click: dispatch_lookup failed");
        return ((yetty_yrich_document_on_mouse_double_click_fn)dispatch_impl_r.value)(obj, x, y,
                                                                                      button, mods);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_on_key_down(struct yetty_yclass_object *obj,
                                                                uint32_t key, uint32_t mods)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_on_key_down);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_document_on_key_down: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_key_down: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_document_on_key_down: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t key;
            uint32_t mods;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            key, mods};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_document_on_key_down: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_document_on_key_down: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_void_result remote_error = YETTY_ERR(
                yetty_ycore_void, "yetty_yrich_document_on_key_down: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        free(resp_buf);
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_document_on_key_down: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_document_on_key_down: dispatch_lookup failed");
        return ((yetty_yrich_document_on_key_down_fn)dispatch_impl_r.value)(obj, key, mods);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_on_text_input(struct yetty_yclass_object *obj,
                                                                  struct yetty_ycore_buffer text)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_on_text_input);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_document_on_text_input: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_text_input: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_document_on_text_input: ensure_remote_id failed");
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
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_text_input: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, text.data, text.size);
        body_offset += text.size;
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, body_buf,
                                        body_total, &resp_buf, &response_len);
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_document_on_text_input: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_document_on_text_input: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_void_result remote_error = YETTY_ERR(
                yetty_ycore_void, "yetty_yrich_document_on_text_input: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        free(resp_buf);
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_document_on_text_input: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_document_on_text_input: dispatch_lookup failed");
        return ((yetty_yrich_document_on_text_input_fn)dispatch_impl_r.value)(obj, text);
    }
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_constructor_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_constructor_skel(const void *body, size_t body_len, void *resp, size_t resp_max)
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
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_constructor: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yrich_constructor((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_constructor", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_document_content_width_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_document_content_width_skel(const void *body, size_t body_len, void *resp,
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
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_content_width: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_float_result call_r =
        yetty_yrich_document_content_width((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_content_width", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_document_content_height_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_document_content_height_skel(const void *body, size_t body_len, void *resp,
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
        yetty_ycore_error_print(stderr,
                                "[skel] yetty_yrich_document_content_height: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_float_result call_r =
        yetty_yrich_document_content_height((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_content_height", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_document_undo_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_document_undo_skel(const void *body, size_t body_len, void *resp,
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
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_undo: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yrich_document_undo((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_undo", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_document_redo_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_document_redo_skel(const void *body, size_t body_len, void *resp,
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
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_redo: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yrich_document_redo((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_redo", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_document_on_mouse_down_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_document_on_mouse_down_skel(const void *body, size_t body_len, void *resp,
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
        uint32_t button;
        uint32_t mods;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_on_mouse_down: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_document_on_mouse_down(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.x, wire_args.y,
        wire_args.button, wire_args.mods);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_on_mouse_down", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_document_on_mouse_up_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_document_on_mouse_up_skel(const void *body, size_t body_len, void *resp,
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
        uint32_t button;
        uint32_t mods;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_on_mouse_up: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_document_on_mouse_up(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.x, wire_args.y,
        wire_args.button, wire_args.mods);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_on_mouse_up", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_document_on_mouse_drag_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_document_on_mouse_drag_skel(const void *body, size_t body_len, void *resp,
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
        uint32_t button;
        uint32_t mods;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_on_mouse_drag: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_document_on_mouse_drag(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.x, wire_args.y,
        wire_args.button, wire_args.mods);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_on_mouse_drag", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_document_on_mouse_double_click_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_document_on_mouse_double_click_skel(const void *body, size_t body_len,
                                                       void *resp, size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        float x;
        float y;
        uint32_t button;
        uint32_t mods;
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
        yetty_ycore_error_print(stderr,
                                "[skel] yetty_yrich_document_on_mouse_double_click: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_document_on_mouse_double_click(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.x, wire_args.y,
        wire_args.button, wire_args.mods);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_on_mouse_double_click",
                                call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_document_on_key_down_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_document_on_key_down_skel(const void *body, size_t body_len, void *resp,
                                             size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t key;
        uint32_t mods;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_on_key_down: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_document_on_key_down(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.key, wire_args.mods);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_on_key_down", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_document_on_text_input_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_document_on_text_input_skel(const void *body, size_t body_len, void *resp,
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
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_on_text_input: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_document_on_text_input(
        (struct yetty_yclass_object *)obj_resolve_r.value, text_buf);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_document_on_text_input", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_document_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_document_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yrich_document");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yrich_document_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_document_create: class accessor failed", class_accessor_r);
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
                             "yetty_yrich_document_create: constructor failed", ctor_r);
        }
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yrich_document");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(
                stderr,
                "yetty_yrich_document_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yrich_document";
    struct yetty_ycore_size_result create_call_r =
        yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name,
                              strlen(class_name), &handle, sizeof(handle));
    if (YETTY_IS_ERR(create_call_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yrich_document_create: CREATE call failed",
                         create_call_r);
    }
    if (create_call_r.value != sizeof(handle) || !handle) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_document_create: CREATE returned no/invalid handle");
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
                         "yetty_yrich_document_create: calloc(proxy) failed");
    }
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}
