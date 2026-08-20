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
static yetty_yrich_constructor_fn
    yetty_yrich_document_yetty_yrich_constructor_document_constructor_check = document_constructor;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_destroy_fn
    yetty_yrich_document_yetty_yrich_document_destroy_document_default_destroy_check =
        document_default_destroy;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_content_width_fn
    yetty_yrich_document_yetty_yrich_document_content_width_document_default_content_width_check =
        document_default_content_width;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_content_height_fn
    yetty_yrich_document_yetty_yrich_document_content_height_document_default_content_height_check =
        document_default_content_height;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_render_fn
    yetty_yrich_document_yetty_yrich_document_render_document_default_render_check =
        document_default_render;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_apply_op_fn
    yetty_yrich_document_yetty_yrich_document_apply_op_document_default_apply_op_check =
        document_default_apply_op;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_undo_fn
    yetty_yrich_document_yetty_yrich_document_undo_document_default_undo_check =
        document_default_undo;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_redo_fn
    yetty_yrich_document_yetty_yrich_document_redo_document_default_redo_check =
        document_default_redo;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_mouse_down_fn
    yetty_yrich_document_yetty_yrich_document_on_mouse_down_document_default_on_mouse_down_check =
        document_default_on_mouse_down;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_mouse_up_fn
    yetty_yrich_document_yetty_yrich_document_on_mouse_up_document_default_on_mouse_up_check =
        document_default_on_mouse_up;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_mouse_drag_fn
    yetty_yrich_document_yetty_yrich_document_on_mouse_drag_document_default_on_mouse_drag_check =
        document_default_on_mouse_drag;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_mouse_double_click_fn
    yetty_yrich_document_yetty_yrich_document_on_mouse_double_click_document_default_on_mouse_double_click_check =
        document_default_on_mouse_double_click;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_key_down_fn
    yetty_yrich_document_yetty_yrich_document_on_key_down_document_default_on_key_down_check =
        document_default_on_key_down;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_text_input_fn
    yetty_yrich_document_yetty_yrich_document_on_text_input_document_default_on_text_input_check =
        document_default_on_text_input;

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

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yrich_constructor: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yrich_constructor: dispatch_lookup failed");
    return ((yetty_yrich_constructor_fn)dispatch_impl_r.value)(obj);
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
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_yrich_document_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yrich_document_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_document_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
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
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yrich_document_create: constructor failed",
                         ctor_r);
    }
    return alloc_r;
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_yrich_document_class_get(void);
size_t yetty_yrich_document_content_width_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_content_height_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_undo_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_redo_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_on_mouse_down_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_on_mouse_up_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_on_mouse_drag_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_on_mouse_double_click_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_on_key_down_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_document_on_text_input_skel(const void *, size_t, void *, size_t);
struct yetty_ycore_void_result yetty_yrich_document_register(void);

/* ---- yrich_document: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yrich_document_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yrich_document") == 0) {
        return yetty_yrich_document_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yrich_document: slot -> skel, name-keyed static data --------------- */

struct yetty_yrich_document_skel_row {
    const char *name;
    yetty_yclass_rpc_skel_fn fn;
};

static const struct yetty_yrich_document_skel_row yetty_yrich_document_skel_rows[] = {
    {"yetty_yrich_document_content_width", yetty_yrich_document_content_width_skel},
    {"yetty_yrich_document_content_height", yetty_yrich_document_content_height_skel},
    {"yetty_yrich_document_undo", yetty_yrich_document_undo_skel},
    {"yetty_yrich_document_redo", yetty_yrich_document_redo_skel},
    {"yetty_yrich_document_on_mouse_down", yetty_yrich_document_on_mouse_down_skel},
    {"yetty_yrich_document_on_mouse_up", yetty_yrich_document_on_mouse_up_skel},
    {"yetty_yrich_document_on_mouse_drag", yetty_yrich_document_on_mouse_drag_skel},
    {"yetty_yrich_document_on_mouse_double_click", yetty_yrich_document_on_mouse_double_click_skel},
    {"yetty_yrich_document_on_key_down", yetty_yrich_document_on_key_down_skel},
    {"yetty_yrich_document_on_text_input", yetty_yrich_document_on_text_input_skel},
};

/* Signature dictated by the skel-lookup hook contract; a miss is absorbed
 * into a NULL return at this boundary. */
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_yrich_document_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) {
        yetty_ycore_error_destroy(slot_name_r.error);
        return NULL;
    }
    const char *name = slot_name_r.value;
    for (size_t i = 0;
         i < sizeof(yetty_yrich_document_skel_rows) / sizeof(yetty_yrich_document_skel_rows[0]);
         ++i) {
        if (strcmp(yetty_yrich_document_skel_rows[i].name, name) == 0) {
            return yetty_yrich_document_skel_rows[i].fn;
        }
    }
    return NULL;
}

/* ---- yrich_document: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yrich_document_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yrich_document_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yrich_document_register: add_accessor_lookup");
    {
        struct yetty_ycore_void_result add_skel_r =
            yetty_yclass_rpc_add_skel_lookup(yetty_yrich_document_skel_lookup);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,
                            "yetty_yrich_document_register: rpc_add_skel_lookup");
    }
    registered = true;
    return YETTY_OK_VOID();
}
