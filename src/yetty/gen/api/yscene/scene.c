/* GENERATED — do not edit. */
#include <yetty/api/yscene/scene.h>

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

struct yetty_ycore_uint32_result;
struct yetty_ycore_uint64_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_registry;
struct yetty_ycore_void_result yetty_yscene_constructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yscene_set_registry(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list_registry *registry);
struct yetty_ycore_void_result yetty_yscene_node_declare(struct yetty_yclass_object *obj,
                                                         uint64_t external_id,
                                                         uint64_t parent_external_id);
struct yetty_ycore_void_result yetty_yscene_node_set_transform(struct yetty_yclass_object *obj,
                                                               uint64_t external_id, float m00,
                                                               float m01, float m10, float m11,
                                                               float translate_x,
                                                               float translate_y);
struct yetty_ycore_void_result yetty_yscene_node_set_clip(struct yetty_yclass_object *obj,
                                                          uint64_t external_id, float min_x,
                                                          float min_y, float max_x, float max_y);
struct yetty_ycore_void_result yetty_yscene_node_clear_clip(struct yetty_yclass_object *obj,
                                                            uint64_t external_id);
struct yetty_ycore_void_result yetty_yscene_node_set_opacity(struct yetty_yclass_object *obj,
                                                             uint64_t external_id, float opacity);
struct yetty_ycore_void_result yetty_yscene_node_set_z(struct yetty_yclass_object *obj,
                                                       uint64_t external_id, int32_t paint_z);
struct yetty_ycore_void_result yetty_yscene_node_set_content(struct yetty_yclass_object *obj,
                                                             uint64_t external_id,
                                                             struct yetty_ycore_buffer content);
struct yetty_ycore_void_result yetty_yscene_node_append_batch(struct yetty_yclass_object *obj,
                                                              uint64_t external_id,
                                                              struct yetty_ycore_buffer content);
struct yetty_ycore_void_result yetty_yscene_node_replace_batch(struct yetty_yclass_object *obj,
                                                               uint64_t external_id,
                                                               uint32_t batch_index,
                                                               struct yetty_ycore_buffer content);
struct yetty_ycore_void_result yetty_yscene_node_remove_batch(struct yetty_yclass_object *obj,
                                                              uint64_t external_id,
                                                              uint32_t batch_index);
struct yetty_ycore_void_result yetty_yscene_node_delete(struct yetty_yclass_object *obj,
                                                        uint64_t external_id);
struct yetty_ycore_void_result yetty_yscene_zero(struct yetty_yclass_object *obj);
struct yetty_ycore_uint64_result yetty_yscene_commit(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yscene_layout_barrier_begin(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yscene_layout_barrier_end(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yscene_terminal_grid_generation(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yscene_terminal_grid_create(struct yetty_yclass_object *obj,
                                                                 uint32_t rows, uint32_t cols,
                                                                 float cell_width,
                                                                 float cell_height);
struct yetty_ycore_void_result yetty_yscene_terminal_grid_write(struct yetty_yclass_object *obj,
                                                                struct yetty_ycore_buffer bytes);
struct yetty_ycore_void_result yetty_yscene_terminal_write_content(struct yetty_yclass_object *obj,
                                                                   struct yetty_ycore_buffer vt,
                                                                   struct yetty_ycore_buffer rich);
struct yetty_ycore_void_result yetty_yscene_terminal_grid_resize(struct yetty_yclass_object *obj,
                                                                 uint32_t rows, uint32_t cols);
struct yetty_ycore_uint32_result yetty_yscene_terminal_reply_pending(
    struct yetty_yclass_object *obj);
struct yetty_ycore_uint64_result yetty_yscene_terminal_reply_word(struct yetty_yclass_object *obj,
                                                                  uint32_t word_index);
struct yetty_ycore_void_result yetty_yscene_terminal_reply_consume(struct yetty_yclass_object *obj,
                                                                   uint32_t byte_count);
struct yetty_ycore_uint64_result yetty_yscene_input_event_head(struct yetty_yclass_object *obj);
struct yetty_ycore_uint64_result yetty_yscene_input_event_word(struct yetty_yclass_object *obj,
                                                               uint32_t word_index);
struct yetty_ycore_void_result yetty_yscene_input_event_pop(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yscene_dispatch_key(struct yetty_yclass_object *obj,
                                                           uint32_t input_class,
                                                           struct yetty_ycore_buffer bytes);
struct yetty_ycore_void_result yetty_yscene_note_key_intake(struct yetty_yclass_object *obj,
                                                            uint32_t input_class,
                                                            struct yetty_ycore_buffer bytes);
struct yetty_ycore_void_result yetty_yscene_set_terminal_selection(
    struct yetty_yclass_object *obj, uint32_t start_row, uint32_t start_col, uint32_t end_row,
    uint32_t end_col, uint32_t active);
struct yetty_ycore_uint64_result yetty_yscene_dispatch_pointer(struct yetty_yclass_object *obj,
                                                               uint32_t local_x, uint32_t local_y,
                                                               uint32_t kind, uint32_t button,
                                                               uint32_t mods, uint32_t pressed);
struct yetty_ycore_void_result yetty_yscene_apply_content_transaction(
    struct yetty_yclass_object *obj, struct yetty_ycore_buffer rich);
typedef struct yetty_ycore_void_result (*yetty_yscene_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yscene_set_registry_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list_registry *);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_declare_fn)(struct yetty_yclass_object *,
                                                                       uint64_t, uint64_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_set_transform_fn)(
    struct yetty_yclass_object *, uint64_t, float, float, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_set_clip_fn)(
    struct yetty_yclass_object *, uint64_t, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_clear_clip_fn)(
    struct yetty_yclass_object *, uint64_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_set_opacity_fn)(
    struct yetty_yclass_object *, uint64_t, float);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_set_z_fn)(struct yetty_yclass_object *,
                                                                     uint64_t, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_set_content_fn)(
    struct yetty_yclass_object *, uint64_t, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_append_batch_fn)(
    struct yetty_yclass_object *, uint64_t, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_replace_batch_fn)(
    struct yetty_yclass_object *, uint64_t, uint32_t, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_remove_batch_fn)(
    struct yetty_yclass_object *, uint64_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_delete_fn)(struct yetty_yclass_object *,
                                                                      uint64_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_zero_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_uint64_result (*yetty_yscene_commit_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yscene_layout_barrier_begin_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yscene_layout_barrier_end_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_uint32_result (*yetty_yscene_terminal_grid_generation_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yscene_terminal_grid_create_fn)(
    struct yetty_yclass_object *, uint32_t, uint32_t, float, float);
typedef struct yetty_ycore_void_result (*yetty_yscene_terminal_grid_write_fn)(
    struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yscene_terminal_write_content_fn)(
    struct yetty_yclass_object *, struct yetty_ycore_buffer, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yscene_terminal_grid_resize_fn)(
    struct yetty_yclass_object *, uint32_t, uint32_t);
typedef struct yetty_ycore_uint32_result (*yetty_yscene_terminal_reply_pending_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_uint64_result (*yetty_yscene_terminal_reply_word_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_terminal_reply_consume_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_uint64_result (*yetty_yscene_input_event_head_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_uint64_result (*yetty_yscene_input_event_word_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_input_event_pop_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_uint32_result (*yetty_yscene_dispatch_key_fn)(
    struct yetty_yclass_object *, uint32_t, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yscene_note_key_intake_fn)(
    struct yetty_yclass_object *, uint32_t, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yscene_set_terminal_selection_fn)(
    struct yetty_yclass_object *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef struct yetty_ycore_uint64_result (*yetty_yscene_dispatch_pointer_fn)(
    struct yetty_yclass_object *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_apply_content_transaction_fn)(
    struct yetty_yclass_object *, struct yetty_ycore_buffer);

struct yetty_ycore_void_result yetty_yscene_set_registry(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list_registry *registry)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_set_registry);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yscene_set_registry: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_set_registry: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yscene_set_registry: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yscene_set_registry: dispatch_lookup failed");
    return ((yetty_yscene_set_registry_fn)dispatch_impl_r.value)(obj, registry);
}

struct yetty_ycore_void_result yetty_yscene_node_declare(struct yetty_yclass_object *obj,
                                                         uint64_t external_id,
                                                         uint64_t parent_external_id)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_node_declare: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_node_declare");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_node_declare: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t external_id;
            uint64_t parent_external_id;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            external_id, parent_external_id};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_node_declare", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_node_declare: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_node_declare);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_node_declare: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_node_declare: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_node_declare: dispatch_lookup failed");
        return ((yetty_yscene_node_declare_fn)dispatch_impl_r.value)(obj, external_id,
                                                                     parent_external_id);
    }
}

struct yetty_ycore_void_result yetty_yscene_node_set_transform(struct yetty_yclass_object *obj,
                                                               uint64_t external_id, float m00,
                                                               float m01, float m10, float m11,
                                                               float translate_x, float translate_y)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_node_set_transform: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_node_set_transform");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_node_set_transform: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t external_id;
            float m00;
            float m01;
            float m10;
            float m11;
            float translate_x;
            float translate_y;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            external_id,
            m00,
            m01,
            m10,
            m11,
            translate_x,
            translate_y};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_void(obj->session, remote_id, "yetty_yscene_node_set_transform",
                                       &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_node_set_transform: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_node_set_transform);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_node_set_transform: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_node_set_transform: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_node_set_transform: dispatch_lookup failed");
        return ((yetty_yscene_node_set_transform_fn)dispatch_impl_r.value)(
            obj, external_id, m00, m01, m10, m11, translate_x, translate_y);
    }
}

struct yetty_ycore_void_result yetty_yscene_node_set_clip(struct yetty_yclass_object *obj,
                                                          uint64_t external_id, float min_x,
                                                          float min_y, float max_x, float max_y)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_node_set_clip: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_node_set_clip");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_node_set_clip: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t external_id;
            float min_x;
            float min_y;
            float max_x;
            float max_y;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            external_id,
            min_x,
            min_y,
            max_x,
            max_y};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_node_set_clip", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_node_set_clip: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_node_set_clip);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_node_set_clip: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_node_set_clip: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_node_set_clip: dispatch_lookup failed");
        return ((yetty_yscene_node_set_clip_fn)dispatch_impl_r.value)(obj, external_id, min_x,
                                                                      min_y, max_x, max_y);
    }
}

struct yetty_ycore_void_result yetty_yscene_node_clear_clip(struct yetty_yclass_object *obj,
                                                            uint64_t external_id)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_node_clear_clip: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_node_clear_clip");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_node_clear_clip: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t external_id;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            external_id};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_node_clear_clip", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_node_clear_clip: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_node_clear_clip);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_node_clear_clip: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_node_clear_clip: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_node_clear_clip: dispatch_lookup failed");
        return ((yetty_yscene_node_clear_clip_fn)dispatch_impl_r.value)(obj, external_id);
    }
}

struct yetty_ycore_void_result yetty_yscene_node_set_opacity(struct yetty_yclass_object *obj,
                                                             uint64_t external_id, float opacity)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_node_set_opacity: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_node_set_opacity");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_node_set_opacity: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t external_id;
            float opacity;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            external_id, opacity};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_void(obj->session, remote_id, "yetty_yscene_node_set_opacity",
                                       &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_node_set_opacity: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_node_set_opacity);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_node_set_opacity: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_node_set_opacity: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_node_set_opacity: dispatch_lookup failed");
        return ((yetty_yscene_node_set_opacity_fn)dispatch_impl_r.value)(obj, external_id, opacity);
    }
}

struct yetty_ycore_void_result yetty_yscene_node_set_z(struct yetty_yclass_object *obj,
                                                       uint64_t external_id, int32_t paint_z)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_node_set_z: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_node_set_z");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_node_set_z: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t external_id;
            int32_t paint_z;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            external_id, paint_z};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_node_set_z", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_node_set_z: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_node_set_z);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_node_set_z: method_slot_get failed", method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_node_set_z: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_node_set_z: dispatch_lookup failed");
        return ((yetty_yscene_node_set_z_fn)dispatch_impl_r.value)(obj, external_id, paint_z);
    }
}

struct yetty_ycore_void_result yetty_yscene_node_set_content(struct yetty_yclass_object *obj,
                                                             uint64_t external_id,
                                                             struct yetty_ycore_buffer content)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_node_set_content: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_node_set_content");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_node_set_content: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t external_id;
            uint32_t content_len;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            external_id, (uint32_t)content.size};
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)content.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yscene_node_set_content: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, content.data, content.size);
        body_offset += content.size;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_node_set_content", body_buf, body_total);
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_node_set_content: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_node_set_content);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_node_set_content: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_node_set_content: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_node_set_content: dispatch_lookup failed");
        return ((yetty_yscene_node_set_content_fn)dispatch_impl_r.value)(obj, external_id, content);
    }
}

struct yetty_ycore_void_result yetty_yscene_node_append_batch(struct yetty_yclass_object *obj,
                                                              uint64_t external_id,
                                                              struct yetty_ycore_buffer content)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_node_append_batch: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_node_append_batch");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_node_append_batch: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t external_id;
            uint32_t content_len;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            external_id, (uint32_t)content.size};
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)content.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yscene_node_append_batch: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, content.data, content.size);
        body_offset += content.size;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_node_append_batch", body_buf, body_total);
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_node_append_batch: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_node_append_batch);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_node_append_batch: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_node_append_batch: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_node_append_batch: dispatch_lookup failed");
        return ((yetty_yscene_node_append_batch_fn)dispatch_impl_r.value)(obj, external_id,
                                                                          content);
    }
}

struct yetty_ycore_void_result yetty_yscene_node_replace_batch(struct yetty_yclass_object *obj,
                                                               uint64_t external_id,
                                                               uint32_t batch_index,
                                                               struct yetty_ycore_buffer content)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_node_replace_batch: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_node_replace_batch");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_node_replace_batch: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t external_id;
            uint32_t batch_index;
            uint32_t content_len;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            external_id, batch_index, (uint32_t)content.size};
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)content.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yscene_node_replace_batch: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, content.data, content.size);
        body_offset += content.size;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_node_replace_batch", body_buf, body_total);
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_node_replace_batch: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_node_replace_batch);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_node_replace_batch: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_node_replace_batch: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_node_replace_batch: dispatch_lookup failed");
        return ((yetty_yscene_node_replace_batch_fn)dispatch_impl_r.value)(obj, external_id,
                                                                           batch_index, content);
    }
}

struct yetty_ycore_void_result yetty_yscene_node_remove_batch(struct yetty_yclass_object *obj,
                                                              uint64_t external_id,
                                                              uint32_t batch_index)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_node_remove_batch: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_node_remove_batch");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_node_remove_batch: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t external_id;
            uint32_t batch_index;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            external_id, batch_index};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_void(obj->session, remote_id, "yetty_yscene_node_remove_batch",
                                       &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_node_remove_batch: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_node_remove_batch);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_node_remove_batch: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_node_remove_batch: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_node_remove_batch: dispatch_lookup failed");
        return ((yetty_yscene_node_remove_batch_fn)dispatch_impl_r.value)(obj, external_id,
                                                                          batch_index);
    }
}

struct yetty_ycore_void_result yetty_yscene_node_delete(struct yetty_yclass_object *obj,
                                                        uint64_t external_id)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_node_delete: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_node_delete");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_node_delete: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t external_id;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            external_id};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_node_delete", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_node_delete: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_node_delete);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_node_delete: method_slot_get failed", method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_node_delete: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_node_delete: dispatch_lookup failed");
        return ((yetty_yscene_node_delete_fn)dispatch_impl_r.value)(obj, external_id);
    }
}

struct yetty_ycore_void_result yetty_yscene_zero(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_zero: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yscene_zero");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_zero: ensure_remote_id_by_name failed");
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
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_zero", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_yscene_zero: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_zero);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void, "yetty_yscene_zero: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_zero: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_zero: dispatch_lookup failed");
        return ((yetty_yscene_zero_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_uint64_result yetty_yscene_commit(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_uint64, "yetty_yscene_commit: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yscene_commit");
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, remote_id_r,
                            "yetty_yscene_commit: ensure_remote_id_by_name failed");
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
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, rpc_call_r, "yetty_yscene_commit: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint64, "yetty_yscene_commit: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_uint64_result remote_error =
                YETTY_ERR(yetty_ycore_uint64, "yetty_yscene_commit: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(uint64_t)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint64, "yetty_yscene_commit: truncated RPC payload");
        }
        uint64_t return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_uint64, return_value);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_commit);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_uint64, "yetty_yscene_commit: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, object_class_r,
                            "yetty_yscene_commit: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, dispatch_impl_r,
                            "yetty_yscene_commit: dispatch_lookup failed");
        return ((yetty_yscene_commit_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_yscene_layout_barrier_begin(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_layout_barrier_begin: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_layout_barrier_begin");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_layout_barrier_begin: ensure_remote_id_by_name failed");
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
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_void(obj->session, remote_id, "yetty_yscene_layout_barrier_begin",
                                       &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_layout_barrier_begin: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_layout_barrier_begin);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_layout_barrier_begin: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_layout_barrier_begin: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_layout_barrier_begin: dispatch_lookup failed");
        return ((yetty_yscene_layout_barrier_begin_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_yscene_layout_barrier_end(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_layout_barrier_end: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_layout_barrier_end");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_layout_barrier_end: ensure_remote_id_by_name failed");
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
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_void(obj->session, remote_id, "yetty_yscene_layout_barrier_end",
                                       &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_layout_barrier_end: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_layout_barrier_end);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_layout_barrier_end: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_layout_barrier_end: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_layout_barrier_end: dispatch_lookup failed");
        return ((yetty_yscene_layout_barrier_end_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_uint32_result yetty_yscene_terminal_grid_generation(
    struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_uint32, "yetty_yscene_terminal_grid_generation: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_terminal_grid_generation");
        YETTY_RETURN_IF_ERR(
            yetty_ycore_uint32, remote_id_r,
            "yetty_yscene_terminal_grid_generation: ensure_remote_id_by_name failed");
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
        YETTY_RETURN_IF_ERR(yetty_ycore_uint32, rpc_call_r,
                            "yetty_yscene_terminal_grid_generation: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint32,
                             "yetty_yscene_terminal_grid_generation: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_uint32_result remote_error =
                YETTY_ERR(yetty_ycore_uint32,
                          "yetty_yscene_terminal_grid_generation: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(uint32_t)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint32,
                             "yetty_yscene_terminal_grid_generation: truncated RPC payload");
        }
        uint32_t return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_uint32, return_value);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_terminal_grid_generation);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_uint32,
                                 "yetty_yscene_terminal_grid_generation: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint32, object_class_r,
                            "yetty_yscene_terminal_grid_generation: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint32, dispatch_impl_r,
                            "yetty_yscene_terminal_grid_generation: dispatch_lookup failed");
        return ((yetty_yscene_terminal_grid_generation_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_yscene_terminal_grid_create(struct yetty_yclass_object *obj,
                                                                 uint32_t rows, uint32_t cols,
                                                                 float cell_width,
                                                                 float cell_height)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_terminal_grid_create: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_terminal_grid_create");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_terminal_grid_create: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t rows;
            uint32_t cols;
            float cell_width;
            float cell_height;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            rows, cols, cell_width, cell_height};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_void(obj->session, remote_id, "yetty_yscene_terminal_grid_create",
                                       &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_terminal_grid_create: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_terminal_grid_create);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_terminal_grid_create: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_terminal_grid_create: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_terminal_grid_create: dispatch_lookup failed");
        return ((yetty_yscene_terminal_grid_create_fn)dispatch_impl_r.value)(
            obj, rows, cols, cell_width, cell_height);
    }
}

struct yetty_ycore_void_result yetty_yscene_terminal_grid_write(struct yetty_yclass_object *obj,
                                                                struct yetty_ycore_buffer bytes)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_terminal_grid_write: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_terminal_grid_write");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_terminal_grid_write: ensure_remote_id_by_name failed");
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
            return YETTY_ERR(yetty_ycore_void, "yetty_yscene_terminal_grid_write: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, bytes.data, bytes.size);
        body_offset += bytes.size;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_terminal_grid_write", body_buf, body_total);
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_terminal_grid_write: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_terminal_grid_write);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_terminal_grid_write: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_terminal_grid_write: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_terminal_grid_write: dispatch_lookup failed");
        return ((yetty_yscene_terminal_grid_write_fn)dispatch_impl_r.value)(obj, bytes);
    }
}

struct yetty_ycore_void_result yetty_yscene_terminal_write_content(struct yetty_yclass_object *obj,
                                                                   struct yetty_ycore_buffer vt,
                                                                   struct yetty_ycore_buffer rich)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_terminal_write_content: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_terminal_write_content");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_terminal_write_content: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t vt_len;
            uint32_t rich_len;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            (uint32_t)vt.size, (uint32_t)rich.size};
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)vt.size + (size_t)rich.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yscene_terminal_write_content: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, vt.data, vt.size);
        body_offset += vt.size;
        memcpy(body_buf + body_offset, rich.data, rich.size);
        body_offset += rich.size;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_terminal_write_content", body_buf, body_total);
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_terminal_write_content: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_terminal_write_content);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_terminal_write_content: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_terminal_write_content: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_terminal_write_content: dispatch_lookup failed");
        return ((yetty_yscene_terminal_write_content_fn)dispatch_impl_r.value)(obj, vt, rich);
    }
}

struct yetty_ycore_void_result yetty_yscene_terminal_grid_resize(struct yetty_yclass_object *obj,
                                                                 uint32_t rows, uint32_t cols)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_terminal_grid_resize: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_terminal_grid_resize");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_terminal_grid_resize: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t rows;
            uint32_t cols;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            rows, cols};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_void(obj->session, remote_id, "yetty_yscene_terminal_grid_resize",
                                       &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_terminal_grid_resize: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_terminal_grid_resize);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_terminal_grid_resize: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_terminal_grid_resize: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_terminal_grid_resize: dispatch_lookup failed");
        return ((yetty_yscene_terminal_grid_resize_fn)dispatch_impl_r.value)(obj, rows, cols);
    }
}

struct yetty_ycore_uint32_result yetty_yscene_terminal_reply_pending(
    struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_uint32, "yetty_yscene_terminal_reply_pending: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_terminal_reply_pending");
        YETTY_RETURN_IF_ERR(yetty_ycore_uint32, remote_id_r,
                            "yetty_yscene_terminal_reply_pending: ensure_remote_id_by_name failed");
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
        YETTY_RETURN_IF_ERR(yetty_ycore_uint32, rpc_call_r,
                            "yetty_yscene_terminal_reply_pending: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint32,
                             "yetty_yscene_terminal_reply_pending: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_uint32_result remote_error =
                YETTY_ERR(yetty_ycore_uint32,
                          "yetty_yscene_terminal_reply_pending: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(uint32_t)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint32,
                             "yetty_yscene_terminal_reply_pending: truncated RPC payload");
        }
        uint32_t return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_uint32, return_value);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_terminal_reply_pending);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_uint32,
                                 "yetty_yscene_terminal_reply_pending: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint32, object_class_r,
                            "yetty_yscene_terminal_reply_pending: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint32, dispatch_impl_r,
                            "yetty_yscene_terminal_reply_pending: dispatch_lookup failed");
        return ((yetty_yscene_terminal_reply_pending_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_uint64_result yetty_yscene_terminal_reply_word(struct yetty_yclass_object *obj,
                                                                  uint32_t word_index)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_uint64, "yetty_yscene_terminal_reply_word: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_terminal_reply_word");
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, remote_id_r,
                            "yetty_yscene_terminal_reply_word: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t word_index;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            word_index};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, rpc_call_r,
                            "yetty_yscene_terminal_reply_word: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint64,
                             "yetty_yscene_terminal_reply_word: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_uint64_result remote_error = YETTY_ERR(
                yetty_ycore_uint64, "yetty_yscene_terminal_reply_word: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(uint64_t)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint64,
                             "yetty_yscene_terminal_reply_word: truncated RPC payload");
        }
        uint64_t return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_uint64, return_value);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_terminal_reply_word);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_uint64,
                                 "yetty_yscene_terminal_reply_word: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, object_class_r,
                            "yetty_yscene_terminal_reply_word: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, dispatch_impl_r,
                            "yetty_yscene_terminal_reply_word: dispatch_lookup failed");
        return ((yetty_yscene_terminal_reply_word_fn)dispatch_impl_r.value)(obj, word_index);
    }
}

struct yetty_ycore_void_result yetty_yscene_terminal_reply_consume(struct yetty_yclass_object *obj,
                                                                   uint32_t byte_count)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_terminal_reply_consume: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_terminal_reply_consume");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_terminal_reply_consume: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t byte_count;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            byte_count};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_terminal_reply_consume", &wire_args,
            sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_terminal_reply_consume: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_terminal_reply_consume);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_terminal_reply_consume: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_terminal_reply_consume: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_terminal_reply_consume: dispatch_lookup failed");
        return ((yetty_yscene_terminal_reply_consume_fn)dispatch_impl_r.value)(obj, byte_count);
    }
}

struct yetty_ycore_uint64_result yetty_yscene_input_event_head(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_uint64, "yetty_yscene_input_event_head: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_input_event_head");
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, remote_id_r,
                            "yetty_yscene_input_event_head: ensure_remote_id_by_name failed");
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
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, rpc_call_r,
                            "yetty_yscene_input_event_head: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint64,
                             "yetty_yscene_input_event_head: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_uint64_result remote_error = YETTY_ERR(
                yetty_ycore_uint64, "yetty_yscene_input_event_head: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(uint64_t)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint64,
                             "yetty_yscene_input_event_head: truncated RPC payload");
        }
        uint64_t return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_uint64, return_value);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_input_event_head);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_uint64,
                                 "yetty_yscene_input_event_head: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, object_class_r,
                            "yetty_yscene_input_event_head: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, dispatch_impl_r,
                            "yetty_yscene_input_event_head: dispatch_lookup failed");
        return ((yetty_yscene_input_event_head_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_uint64_result yetty_yscene_input_event_word(struct yetty_yclass_object *obj,
                                                               uint32_t word_index)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_uint64, "yetty_yscene_input_event_word: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_input_event_word");
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, remote_id_r,
                            "yetty_yscene_input_event_word: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t word_index;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            word_index};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, rpc_call_r,
                            "yetty_yscene_input_event_word: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint64,
                             "yetty_yscene_input_event_word: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_uint64_result remote_error = YETTY_ERR(
                yetty_ycore_uint64, "yetty_yscene_input_event_word: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(uint64_t)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint64,
                             "yetty_yscene_input_event_word: truncated RPC payload");
        }
        uint64_t return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_uint64, return_value);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_input_event_word);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_uint64,
                                 "yetty_yscene_input_event_word: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, object_class_r,
                            "yetty_yscene_input_event_word: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, dispatch_impl_r,
                            "yetty_yscene_input_event_word: dispatch_lookup failed");
        return ((yetty_yscene_input_event_word_fn)dispatch_impl_r.value)(obj, word_index);
    }
}

struct yetty_ycore_void_result yetty_yscene_input_event_pop(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_input_event_pop: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_input_event_pop");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_input_event_pop: ensure_remote_id_by_name failed");
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
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_input_event_pop", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_input_event_pop: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_input_event_pop);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_input_event_pop: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_input_event_pop: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_input_event_pop: dispatch_lookup failed");
        return ((yetty_yscene_input_event_pop_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_uint32_result yetty_yscene_dispatch_key(struct yetty_yclass_object *obj,
                                                           uint32_t input_class,
                                                           struct yetty_ycore_buffer bytes)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_uint32, "yetty_yscene_dispatch_key: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_dispatch_key");
        YETTY_RETURN_IF_ERR(yetty_ycore_uint32, remote_id_r,
                            "yetty_yscene_dispatch_key: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t input_class;
            uint32_t bytes_len;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            input_class, (uint32_t)bytes.size};
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)bytes.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) {
            return YETTY_ERR(yetty_ycore_uint32, "yetty_yscene_dispatch_key: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, bytes.data, bytes.size);
        body_offset += bytes.size;
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, body_buf,
                                        body_total, &resp_buf, &response_len);
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint32, rpc_call_r,
                            "yetty_yscene_dispatch_key: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint32, "yetty_yscene_dispatch_key: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_uint32_result remote_error = YETTY_ERR(
                yetty_ycore_uint32, "yetty_yscene_dispatch_key: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(uint32_t)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint32,
                             "yetty_yscene_dispatch_key: truncated RPC payload");
        }
        uint32_t return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_uint32, return_value);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_dispatch_key);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_uint32,
                                 "yetty_yscene_dispatch_key: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint32, object_class_r,
                            "yetty_yscene_dispatch_key: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint32, dispatch_impl_r,
                            "yetty_yscene_dispatch_key: dispatch_lookup failed");
        return ((yetty_yscene_dispatch_key_fn)dispatch_impl_r.value)(obj, input_class, bytes);
    }
}

struct yetty_ycore_void_result yetty_yscene_note_key_intake(struct yetty_yclass_object *obj,
                                                            uint32_t input_class,
                                                            struct yetty_ycore_buffer bytes)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_note_key_intake: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_note_key_intake");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_note_key_intake: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t input_class;
            uint32_t bytes_len;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            input_class, (uint32_t)bytes.size};
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)bytes.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yscene_note_key_intake: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, bytes.data, bytes.size);
        body_offset += bytes.size;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_note_key_intake", body_buf, body_total);
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_note_key_intake: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_note_key_intake);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_note_key_intake: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_note_key_intake: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_note_key_intake: dispatch_lookup failed");
        return ((yetty_yscene_note_key_intake_fn)dispatch_impl_r.value)(obj, input_class, bytes);
    }
}

struct yetty_ycore_void_result yetty_yscene_set_terminal_selection(
    struct yetty_yclass_object *obj, uint32_t start_row, uint32_t start_col, uint32_t end_row,
    uint32_t end_col, uint32_t active)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_set_terminal_selection: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_set_terminal_selection");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yscene_set_terminal_selection: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t start_row;
            uint32_t start_col;
            uint32_t end_row;
            uint32_t end_col;
            uint32_t active;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            start_row,
            start_col,
            end_row,
            end_col,
            active};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_set_terminal_selection", &wire_args,
            sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_set_terminal_selection: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_set_terminal_selection);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_set_terminal_selection: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_set_terminal_selection: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_set_terminal_selection: dispatch_lookup failed");
        return ((yetty_yscene_set_terminal_selection_fn)dispatch_impl_r.value)(
            obj, start_row, start_col, end_row, end_col, active);
    }
}

struct yetty_ycore_uint64_result yetty_yscene_dispatch_pointer(struct yetty_yclass_object *obj,
                                                               uint32_t local_x, uint32_t local_y,
                                                               uint32_t kind, uint32_t button,
                                                               uint32_t mods, uint32_t pressed)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_uint64, "yetty_yscene_dispatch_pointer: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_dispatch_pointer");
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, remote_id_r,
                            "yetty_yscene_dispatch_pointer: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t local_x;
            uint32_t local_y;
            uint32_t kind;
            uint32_t button;
            uint32_t mods;
            uint32_t pressed;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            local_x,
            local_y,
            kind,
            button,
            mods,
            pressed};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, rpc_call_r,
                            "yetty_yscene_dispatch_pointer: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint64,
                             "yetty_yscene_dispatch_pointer: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_uint64_result remote_error = YETTY_ERR(
                yetty_ycore_uint64, "yetty_yscene_dispatch_pointer: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(uint64_t)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_uint64,
                             "yetty_yscene_dispatch_pointer: truncated RPC payload");
        }
        uint64_t return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_uint64, return_value);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_dispatch_pointer);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_uint64,
                                 "yetty_yscene_dispatch_pointer: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, object_class_r,
                            "yetty_yscene_dispatch_pointer: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_uint64, dispatch_impl_r,
                            "yetty_yscene_dispatch_pointer: dispatch_lookup failed");
        return ((yetty_yscene_dispatch_pointer_fn)dispatch_impl_r.value)(
            obj, local_x, local_y, kind, button, mods, pressed);
    }
}

struct yetty_ycore_void_result yetty_yscene_apply_content_transaction(
    struct yetty_yclass_object *obj, struct yetty_ycore_buffer rich)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_apply_content_transaction: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yscene_apply_content_transaction");
        YETTY_RETURN_IF_ERR(
            yetty_ycore_void, remote_id_r,
            "yetty_yscene_apply_content_transaction: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t rich_len;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            (uint32_t)rich.size};
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)rich.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yscene_apply_content_transaction: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, rich.data, rich.size);
        body_offset += rich.size;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yscene_apply_content_transaction", body_buf,
            body_total);
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yscene_apply_content_transaction: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_apply_content_transaction);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yscene_apply_content_transaction: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yscene_apply_content_transaction: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yscene_apply_content_transaction: dispatch_lookup failed");
        return ((yetty_yscene_apply_content_transaction_fn)dispatch_impl_r.value)(obj, rich);
    }
}
