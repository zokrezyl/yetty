/* GENERATED — do not edit. */
#include "yetty/yfigure/figure.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h> /* NULL, size_t */
/* The folded-in public stubs, rpc skeletons + create() and the
 * registration hooks (formerly methods.gen.c / rpc.gen.c) need
 * these. All header-guarded, so re-including what the hand-written
 * .c already pulled in is harmless; the class's OWN header is
 * still never included (that would redefine its expose'd types). */
#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> /* malloc/free for buffer-arg marshalling */
#include <string.h>

struct yetty_ycore_char_ptr_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_target;
struct yetty_yfigure_figure;
struct yetty_ywire_wire_statemachine;
struct yetty_ycore_void_result yetty_yfigure_destroy(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_render(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj,
                                                    struct yetty_ydraw_target *target);
struct yetty_ycore_void_result yetty_yfigure_constructor(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_add_child(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       struct yetty_yfigure_figure *child,
                                                       uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_remove_child_by_id(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj,
                                                                uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_raise_child_by_id(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj,
                                                               uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_process_records(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj,
                                                             struct yetty_ycore_buffer bytes);
struct yetty_ycore_void_result yetty_yfigure_process_input(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_ywire_wire_statemachine *statemachine);
struct yetty_ycore_void_result yetty_yfigure_process_bytes(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj,
                                                           const uint8_t *bytes, size_t bytes_len);
struct yetty_ycore_char_ptr_result yetty_yfigure_dump_state(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj,
                                                            int indent);
typedef struct yetty_ycore_void_result (*yetty_yfigure_destroy_fn)(struct yetty_yclass_ctx *,
                                                                   struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_render_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *,
                                                                  struct yetty_ydraw_target *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_constructor_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_add_child_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *,
                                                                     struct yetty_yfigure_figure *,
                                                                     uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_remove_child_by_id_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_raise_child_by_id_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_records_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_input_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *,
    struct yetty_ywire_wire_statemachine *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_bytes_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, const uint8_t *, size_t);
typedef struct yetty_ycore_char_ptr_result (*yetty_yfigure_dump_state_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int);

/* ===== class accessors ===== */

[[maybe_unused]]
static yetty_yfigure_destroy_fn yetty_yfigure_container_yetty_yfigure_destroy_check =
    container_destroy;
[[maybe_unused]]
static yetty_yfigure_render_fn yetty_yfigure_container_yetty_yfigure_render_check =
    container_render;
[[maybe_unused]]
static yetty_yfigure_constructor_fn yetty_yfigure_container_yetty_yfigure_constructor_check =
    yetty_yfigure_container_constructor_impl;
[[maybe_unused]]
static yetty_yfigure_add_child_fn yetty_yfigure_container_yetty_yfigure_add_child_check =
    yetty_yfigure_container_add_child_impl;
[[maybe_unused]]
static yetty_yfigure_remove_child_by_id_fn
    yetty_yfigure_container_yetty_yfigure_remove_child_by_id_check =
        yetty_yfigure_container_remove_child_by_id_impl;
[[maybe_unused]]
static yetty_yfigure_raise_child_by_id_fn
    yetty_yfigure_container_yetty_yfigure_raise_child_by_id_check =
        yetty_yfigure_container_raise_child_by_id_impl;
[[maybe_unused]]
static yetty_yfigure_process_records_fn
    yetty_yfigure_container_yetty_yfigure_process_records_check =
        yetty_yfigure_container_process_records_impl;
[[maybe_unused]]
static yetty_yfigure_process_input_fn yetty_yfigure_container_yetty_yfigure_process_input_check =
    container_process_input_slot;
[[maybe_unused]]
static yetty_yfigure_process_bytes_fn yetty_yfigure_container_yetty_yfigure_process_bytes_check =
    container_process_bytes_slot;
[[maybe_unused]]
static yetty_yfigure_dump_state_fn yetty_yfigure_container_yetty_yfigure_dump_state_check =
    container_dump_state_slot;

struct yetty_yclass_ptr_result yetty_yfigure_container_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yfigure_container");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yfigure_container",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yfigure_container),
        .data_align = _Alignof(struct yetty_yfigure_container),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy,
         (yetty_yclass_impl_t)container_destroy},
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render,
         (yetty_yclass_impl_t)container_render},
        {"yetty_yfigure", "constructor", (yetty_yclass_method_id_t)yetty_yfigure_constructor,
         (yetty_yclass_impl_t)yetty_yfigure_container_constructor_impl},
        {"yetty_yfigure", "add_child", (yetty_yclass_method_id_t)yetty_yfigure_add_child,
         (yetty_yclass_impl_t)yetty_yfigure_container_add_child_impl},
        {"yetty_yfigure", "remove_child_by_id",
         (yetty_yclass_method_id_t)yetty_yfigure_remove_child_by_id,
         (yetty_yclass_impl_t)yetty_yfigure_container_remove_child_by_id_impl},
        {"yetty_yfigure", "raise_child_by_id",
         (yetty_yclass_method_id_t)yetty_yfigure_raise_child_by_id,
         (yetty_yclass_impl_t)yetty_yfigure_container_raise_child_by_id_impl},
        {"yetty_yfigure", "process_records",
         (yetty_yclass_method_id_t)yetty_yfigure_process_records,
         (yetty_yclass_impl_t)yetty_yfigure_container_process_records_impl},
        {"yetty_yfigure", "process_input", (yetty_yclass_method_id_t)yetty_yfigure_process_input,
         (yetty_yclass_impl_t)container_process_input_slot},
        {"yetty_yfigure", "process_bytes", (yetty_yclass_method_id_t)yetty_yfigure_process_bytes,
         (yetty_yclass_impl_t)container_process_bytes_slot},
        {"yetty_yfigure", "dump_state", (yetty_yclass_method_id_t)yetty_yfigure_dump_state,
         (yetty_yclass_impl_t)container_dump_state_slot},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yfigure_container_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yfigure_container_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yfigure_container_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yfigure_container_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yfigure_container_ptr_result yetty_yfigure_container_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yfigure_container_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yfigure_container_ptr,
                         "yetty_yfigure_container_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yfigure_container_ptr, "yetty_yfigure_container_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_yfigure_container_ptr, (struct yetty_yfigure_container *)slice_r.value);
}

struct yetty_yclass_object *yetty_yfigure_container_to(struct yetty_yfigure_container *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yfigure_container_class_get();
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

/* ===== public method stubs (was methods.gen.c) ===== */

struct yetty_ycore_void_result yetty_yfigure_constructor(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_constructor);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_constructor: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_constructor: NULL object");
    }

    struct yetty_yclass_ctx *rpc_ctx = ctx;
    if (rpc_ctx && rpc_ctx->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(rpc_ctx->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_constructor: ensure_remote_id failed");
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
            yetty_yclass_rpc_call(rpc_ctx->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_constructor: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_constructor: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yfigure_constructor: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_constructor: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_constructor: dispatch_lookup failed");
        return ((yetty_yfigure_constructor_fn)dispatch_impl_r.value)(ctx, obj);
    }
}

struct yetty_ycore_void_result yetty_yfigure_add_child(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       struct yetty_yfigure_figure *child,
                                                       uint32_t id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_add_child);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_add_child: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_add_child: NULL object");
    }

    struct yetty_yclass_ctx *rpc_ctx = ctx;
    if (rpc_ctx && rpc_ctx->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(rpc_ctx->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_add_child: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t child_handle;
            uint32_t id;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            container_of((struct yetty_yclass_object *)child, struct yetty_yclass_proxy, header)
                ->handle,
            id};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(rpc_ctx->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_add_child: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_add_child: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yfigure_add_child: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_add_child: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_add_child: dispatch_lookup failed");
        return ((yetty_yfigure_add_child_fn)dispatch_impl_r.value)(ctx, obj, child, id);
    }
}

struct yetty_ycore_void_result yetty_yfigure_remove_child_by_id(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj,
                                                                uint32_t id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_remove_child_by_id);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yfigure_remove_child_by_id: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_remove_child_by_id: NULL object");
    }

    struct yetty_yclass_ctx *rpc_ctx = ctx;
    if (rpc_ctx && rpc_ctx->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(rpc_ctx->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_remove_child_by_id: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t id;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            id};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(rpc_ctx->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_remove_child_by_id: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yfigure_remove_child_by_id: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yfigure_remove_child_by_id: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_remove_child_by_id: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_remove_child_by_id: dispatch_lookup failed");
        return ((yetty_yfigure_remove_child_by_id_fn)dispatch_impl_r.value)(ctx, obj, id);
    }
}

struct yetty_ycore_void_result yetty_yfigure_raise_child_by_id(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj,
                                                               uint32_t id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_raise_child_by_id);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yfigure_raise_child_by_id: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_raise_child_by_id: NULL object");
    }

    struct yetty_yclass_ctx *rpc_ctx = ctx;
    if (rpc_ctx && rpc_ctx->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(rpc_ctx->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_raise_child_by_id: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t id;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            id};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(rpc_ctx->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_raise_child_by_id: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yfigure_raise_child_by_id: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yfigure_raise_child_by_id: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_raise_child_by_id: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_raise_child_by_id: dispatch_lookup failed");
        return ((yetty_yfigure_raise_child_by_id_fn)dispatch_impl_r.value)(ctx, obj, id);
    }
}

struct yetty_ycore_void_result yetty_yfigure_process_records(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj,
                                                             struct yetty_ycore_buffer bytes)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_process_records);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yfigure_process_records: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_process_records: NULL object");
    }

    struct yetty_yclass_ctx *rpc_ctx = ctx;
    if (rpc_ctx && rpc_ctx->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(rpc_ctx->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_process_records: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t bytes_len;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            (uint32_t)bytes.size};
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)bytes.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_process_records: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, bytes.data, bytes.size);
        body_offset += bytes.size;
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(rpc_ctx->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, body_buf,
                                  body_total, resp_buf, sizeof(resp_buf));
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_process_records: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_process_records: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yfigure_process_records: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_process_records: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_process_records: dispatch_lookup failed");
        return ((yetty_yfigure_process_records_fn)dispatch_impl_r.value)(ctx, obj, bytes);
    }
}

/* ===== rpc skeletons + create (was rpc.gen.c) ===== */

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. */
YETTY_EXTERNAL_CALLBACK
static size_t yetty_yfigure_constructor_skel(const void *body, size_t body_len, void *resp,
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
    struct yetty_yclass_ctx local_ctx = {0};
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_constructor: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yfigure_constructor(&local_ctx, (struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_constructor", call_r.error);
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
 * status wire response at this boundary. */
YETTY_EXTERNAL_CALLBACK
static size_t yetty_yfigure_add_child_skel(const void *body, size_t body_len, void *resp,
                                           size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint64_t child_handle;
        uint32_t id;
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
    struct yetty_yclass_ctx local_ctx = {0};
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_add_child: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_yclass_void_ptr_result child_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.child_handle);
    if (YETTY_IS_ERR(child_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_add_child: handle_resolve",
                                child_resolve_r.error);
        yetty_ycore_error_destroy(child_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yfigure_add_child(&local_ctx, (struct yetty_yclass_object *)obj_resolve_r.value,
                                (struct yetty_yfigure_figure *)child_resolve_r.value, wire_args.id);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_add_child", call_r.error);
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
 * status wire response at this boundary. */
YETTY_EXTERNAL_CALLBACK
static size_t yetty_yfigure_remove_child_by_id_skel(const void *body, size_t body_len, void *resp,
                                                    size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t id;
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
    struct yetty_yclass_ctx local_ctx = {0};
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_remove_child_by_id: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r = yetty_yfigure_remove_child_by_id(
        &local_ctx, (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.id);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_remove_child_by_id", call_r.error);
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
 * status wire response at this boundary. */
YETTY_EXTERNAL_CALLBACK
static size_t yetty_yfigure_raise_child_by_id_skel(const void *body, size_t body_len, void *resp,
                                                   size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t id;
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
    struct yetty_yclass_ctx local_ctx = {0};
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_raise_child_by_id: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r = yetty_yfigure_raise_child_by_id(
        &local_ctx, (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.id);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_raise_child_by_id", call_r.error);
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
 * status wire response at this boundary. */
YETTY_EXTERNAL_CALLBACK
static size_t yetty_yfigure_process_records_skel(const void *body, size_t body_len, void *resp,
                                                 size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t bytes_len;
    } wire_args;
#pragma pack(pop)
    if (body_len < sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    if (body_len != sizeof(wire_args) + (size_t)wire_args.bytes_len) {
        return 0;
    }
    size_t body_offset = sizeof(wire_args);
    struct yetty_ycore_buffer bytes_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.bytes_len,
        .capacity = (size_t)wire_args.bytes_len,
    };
    body_offset += (size_t)wire_args.bytes_len;
    struct yetty_yclass_ctx local_ctx = {0};
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_process_records: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r = yetty_yfigure_process_records(
        &local_ctx, (struct yetty_yclass_object *)obj_resolve_r.value, bytes_buf);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_process_records", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

struct yetty_yclass_object_ptr_result yetty_yfigure_container_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yfigure_container");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yfigure_container_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yfigure_container_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) {
            return alloc_r;
        }
        struct yetty_ycore_void_result ctor_r = yetty_yfigure_constructor(ctx, alloc_r.value);
        if (YETTY_IS_ERR(ctor_r)) {
            struct yetty_ycore_void_result free_r = yetty_yclass_object_free(alloc_r.value);
            if (YETTY_IS_ERR(free_r)) {
                yetty_ycore_error_destroy(free_r.error);
            }
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_yfigure_container_create: constructor failed", ctor_r);
        }
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yfigure_container");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(
                stderr,
                "yetty_yfigure_container_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yfigure_container";
    struct yetty_ycore_size_result create_call_r =
        yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name,
                              strlen(class_name), &handle, sizeof(handle));
    if (YETTY_IS_ERR(create_call_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yfigure_container_create: CREATE call failed", create_call_r);
    }
    if (create_call_r.value != sizeof(handle) || !handle) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yfigure_container_create: CREATE returned no/invalid handle");
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
                         "yetty_yfigure_container_create: calloc(proxy) failed");
    }
    proxy->header.klass = klass;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

/* ---- yfigure/container: class name -> accessor ---------------------- */
static struct yetty_yclass_ptr_result yetty_yfigure_container_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yfigure_container") == 0) {
        return yetty_yfigure_container_class_get();
    }
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

struct yetty_yfigure_container_skel_row {
    const char *name;
    yetty_yclass_rpc_skel_fn fn;
};
static const struct yetty_yfigure_container_skel_row yetty_yfigure_container_skel_rows[] = {
    {"yetty_yfigure_constructor", yetty_yfigure_constructor_skel},
    {"yetty_yfigure_add_child", yetty_yfigure_add_child_skel},
    {"yetty_yfigure_remove_child_by_id", yetty_yfigure_remove_child_by_id_skel},
    {"yetty_yfigure_raise_child_by_id", yetty_yfigure_raise_child_by_id_skel},
    {"yetty_yfigure_process_records", yetty_yfigure_process_records_skel}};
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_yfigure_container_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) {
        yetty_ycore_error_destroy(slot_name_r.error);
        return NULL;
    }
    const char *name = slot_name_r.value;
    for (size_t i = 0; i < sizeof(yetty_yfigure_container_skel_rows) /
                               sizeof(yetty_yfigure_container_skel_rows[0]);
         ++i) {
        if (strcmp(yetty_yfigure_container_skel_rows[i].name, name) == 0) {
            return yetty_yfigure_container_skel_rows[i].fn;
        }
    }
    return NULL;
}

struct yetty_ycore_void_result yetty_yfigure_container_register_hooks(void)
{
    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yfigure_container_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yfigure_container_register_hooks: accessor");
    {
        struct yetty_ycore_void_result add_skel_r =
            yetty_yclass_rpc_add_skel_lookup(yetty_yfigure_container_skel_lookup);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,
                            "yetty_yfigure_container_register_hooks: skel");
    }
    return YETTY_OK_VOID();
}

/* ===== module registration (was rpc.gen.c) ========================== */
struct yetty_ycore_void_result yetty_yfigure_container_register_hooks(void);
struct yetty_ycore_void_result yetty_yfigure_figure_register_hooks(void);

struct yetty_ycore_void_result yetty_yfigure_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_yfigure_container_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_yfigure_register: container");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_yfigure_figure_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_yfigure_register: figure");
    }
    registered = true;
    return YETTY_OK_VOID();
}
