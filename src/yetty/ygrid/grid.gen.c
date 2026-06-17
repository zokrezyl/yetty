/* GENERATED — do not edit. */
#include "yetty/yfigure/figure.h"
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

struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_ygrid_add_record(struct yetty_yclass_object *obj,
                                                      struct yetty_ycore_buffer record);
struct yetty_ycore_void_result yetty_ygrid_clear(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygrid_destroy(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_ygrid_add_record_fn)(struct yetty_yclass_object *,
                                                                    struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_ygrid_clear_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygrid_destroy_fn)(struct yetty_yclass_object *);

[[maybe_unused]]
static yetty_yfigure_render_fn yetty_ygrid_grid_yetty_yfigure_render_check = ygrid_render_slot;
[[maybe_unused]]
static yetty_yfigure_destroy_fn yetty_ygrid_grid_yetty_yfigure_destroy_check = ygrid_destroy_slot;
[[maybe_unused]]
static yetty_ygrid_add_record_fn yetty_ygrid_grid_yetty_ygrid_add_record_check =
    yetty_ygrid_grid_add_record_impl;
[[maybe_unused]]
static yetty_ygrid_clear_fn yetty_ygrid_grid_yetty_ygrid_clear_check = yetty_ygrid_grid_clear_impl;
[[maybe_unused]]
static yetty_ygrid_destroy_fn yetty_ygrid_grid_yetty_ygrid_destroy_check =
    yetty_ygrid_grid_destroy_impl;
[[maybe_unused]]
static yetty_yfigure_process_bytes_fn yetty_ygrid_grid_yetty_yfigure_process_bytes_check =
    yetty_ygrid_grid_process_bytes_impl;
[[maybe_unused]]
static yetty_yfigure_reset_content_fn yetty_ygrid_grid_yetty_yfigure_reset_content_check =
    yetty_ygrid_grid_reset_content_impl;
[[maybe_unused]]
static yetty_yfigure_dump_state_fn yetty_ygrid_grid_yetty_yfigure_dump_state_check =
    yetty_ygrid_grid_dump_state_impl;
[[maybe_unused]]
static yetty_yfigure_set_scroll_fn yetty_ygrid_grid_yetty_yfigure_set_scroll_check =
    yetty_ygrid_grid_set_scroll_impl;
[[maybe_unused]]
static yetty_yfigure_set_content_size_fn yetty_ygrid_grid_yetty_yfigure_set_content_size_check =
    yetty_ygrid_grid_set_content_size_impl;

struct yetty_yclass_ptr_result yetty_ygrid_grid_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ygrid_grid");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygrid_grid",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ygrid_grid),
        .data_align = _Alignof(struct yetty_ygrid_grid),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render,
         (yetty_yclass_impl_t)ygrid_render_slot},
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy,
         (yetty_yclass_impl_t)ygrid_destroy_slot},
        {"yetty_ygrid", "add_record", (yetty_yclass_method_id_t)yetty_ygrid_add_record,
         (yetty_yclass_impl_t)yetty_ygrid_grid_add_record_impl},
        {"yetty_ygrid", "clear", (yetty_yclass_method_id_t)yetty_ygrid_clear,
         (yetty_yclass_impl_t)yetty_ygrid_grid_clear_impl},
        {"yetty_ygrid", "destroy", (yetty_yclass_method_id_t)yetty_ygrid_destroy,
         (yetty_yclass_impl_t)yetty_ygrid_grid_destroy_impl},
        {"yetty_yfigure", "process_bytes", (yetty_yclass_method_id_t)yetty_yfigure_process_bytes,
         (yetty_yclass_impl_t)yetty_ygrid_grid_process_bytes_impl},
        {"yetty_yfigure", "reset_content", (yetty_yclass_method_id_t)yetty_yfigure_reset_content,
         (yetty_yclass_impl_t)yetty_ygrid_grid_reset_content_impl},
        {"yetty_yfigure", "dump_state", (yetty_yclass_method_id_t)yetty_yfigure_dump_state,
         (yetty_yclass_impl_t)yetty_ygrid_grid_dump_state_impl},
        {"yetty_yfigure", "set_scroll", (yetty_yclass_method_id_t)yetty_yfigure_set_scroll,
         (yetty_yclass_impl_t)yetty_ygrid_grid_set_scroll_impl},
        {"yetty_yfigure", "set_content_size",
         (yetty_yclass_method_id_t)yetty_yfigure_set_content_size,
         (yetty_yclass_impl_t)yetty_ygrid_grid_set_content_size_impl},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ygrid_grid_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygrid_grid_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ygrid_grid_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygrid_grid_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygrid_grid_ptr_result yetty_ygrid_grid_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ygrid_grid_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ygrid_grid_ptr, "yetty_ygrid_grid_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ygrid_grid_ptr, "yetty_ygrid_grid_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ygrid_grid_ptr, (struct yetty_ygrid_grid *)slice_r.value);
}

struct yetty_yclass_object *yetty_ygrid_grid_to(struct yetty_ygrid_grid *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_ygrid_grid_class_get();
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

struct yetty_ycore_void_result yetty_ygrid_add_record(struct yetty_yclass_object *obj,
                                                      struct yetty_ycore_buffer record)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygrid", (yetty_yclass_method_id_t)yetty_ygrid_add_record);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_add_record: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_add_record: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_ygrid_add_record: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t record_len;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            (uint32_t)record.size};
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)record.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_add_record: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, record.data, record.size);
        body_offset += record.size;
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, body_buf,
                                  body_total, resp_buf, sizeof(resp_buf));
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_ygrid_add_record: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_add_record: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ygrid_add_record: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_ygrid_add_record: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_ygrid_add_record: dispatch_lookup failed");
        return ((yetty_ygrid_add_record_fn)dispatch_impl_r.value)(obj, record);
    }
}

struct yetty_ycore_void_result yetty_ygrid_clear(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygrid", (yetty_yclass_method_id_t)yetty_ygrid_clear);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_clear: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_clear: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_ygrid_clear: ensure_remote_id failed");
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
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_ygrid_clear: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_clear: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_clear: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_ygrid_clear: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_ygrid_clear: dispatch_lookup failed");
        return ((yetty_ygrid_clear_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_ygrid_destroy(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygrid", (yetty_yclass_method_id_t)yetty_ygrid_destroy);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_destroy: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_destroy: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_ygrid_destroy: ensure_remote_id failed");
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
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_ygrid_destroy: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_destroy: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_destroy: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_ygrid_destroy: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_ygrid_destroy: dispatch_lookup failed");
        return ((yetty_ygrid_destroy_fn)dispatch_impl_r.value)(obj);
    }
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_ygrid_add_record_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_ygrid_add_record_skel(const void *body, size_t body_len, void *resp, size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t record_len;
    } wire_args;
#pragma pack(pop)
    if (body_len < sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    if (body_len != sizeof(wire_args) + (size_t)wire_args.record_len) {
        return 0;
    }
    size_t body_offset = sizeof(wire_args);
    struct yetty_ycore_buffer record_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.record_len,
        .capacity = (size_t)wire_args.record_len,
    };
    body_offset += (size_t)wire_args.record_len;
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_add_record: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r =
        yetty_ygrid_add_record((struct yetty_yclass_object *)obj_resolve_r.value, record_buf);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_add_record", call_r.error);
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
size_t yetty_ygrid_clear_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_ygrid_clear_skel(const void *body, size_t body_len, void *resp, size_t resp_max)
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
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_clear: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r =
        yetty_ygrid_clear((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_clear", call_r.error);
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
size_t yetty_ygrid_destroy_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_ygrid_destroy_skel(const void *body, size_t body_len, void *resp, size_t resp_max)
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
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_destroy: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r =
        yetty_ygrid_destroy((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_destroy", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

struct yetty_yclass_object_ptr_result yetty_ygrid_grid_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ygrid_grid_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygrid_grid");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ygrid_grid_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygrid_grid_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) {
            return alloc_r;
        }
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygrid_grid");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(
                stderr, "yetty_ygrid_grid_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_ygrid_grid";
    struct yetty_ycore_size_result create_call_r =
        yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name,
                              strlen(class_name), &handle, sizeof(handle));
    if (YETTY_IS_ERR(create_call_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygrid_grid_create: CREATE call failed",
                         create_call_r);
    }
    if (create_call_r.value != sizeof(handle) || !handle) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygrid_grid_create: CREATE returned no/invalid handle");
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
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygrid_grid_create: calloc(proxy) failed");
    }
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}
