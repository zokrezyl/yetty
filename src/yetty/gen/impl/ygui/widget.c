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
struct yetty_ygui_emit_ctx;
struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_object *obj, float x,
                                                         float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_object *obj, float x,
                                                           float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_object *obj, float x,
                                                          float y);
struct yetty_ycore_int_result yetty_ygui_widget_on_scroll(struct yetty_yclass_object *obj, float x,
                                                          float y, float dx, float dy);
struct yetty_ycore_void_result yetty_ygui_widget_paint(struct yetty_yclass_object *obj,
                                                       struct yetty_ygui_emit_ctx *emit_ctx);
struct yetty_ycore_void_result yetty_ygui_widget_emit_container(
    struct yetty_yclass_object *obj, struct yetty_ygui_emit_ctx *emit_ctx);
struct yetty_ycore_void_result yetty_ygui_widget_emit_body(struct yetty_yclass_object *obj,
                                                           struct yetty_ygui_emit_ctx *emit_ctx);
typedef struct yetty_ycore_void_result (*yetty_ygui_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui_destructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_press_fn)(struct yetty_yclass_object *,
                                                                       float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_release_fn)(
    struct yetty_yclass_object *, float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_motion_fn)(
    struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_scroll_fn)(
    struct yetty_yclass_object *, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_paint_fn)(struct yetty_yclass_object *,
                                                                     struct yetty_ygui_emit_ctx *);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_emit_container_fn)(
    struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_emit_body_fn)(
    struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);

YETTY_MAYBE_UNUSED
static yetty_ygui_constructor_fn
    yetty_ygui_widget_yetty_ygui_constructor_widget_default_constructor_check =
        widget_default_constructor;
YETTY_MAYBE_UNUSED
static yetty_ygui_destructor_fn
    yetty_ygui_widget_yetty_ygui_destructor_widget_default_destructor_check =
        widget_default_destructor;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_on_press_fn
    yetty_ygui_widget_yetty_ygui_widget_on_press_widget_default_on_press_check =
        widget_default_on_press;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_on_release_fn
    yetty_ygui_widget_yetty_ygui_widget_on_release_widget_default_on_release_check =
        widget_default_on_release;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_on_motion_fn
    yetty_ygui_widget_yetty_ygui_widget_on_motion_widget_default_on_motion_check =
        widget_default_on_motion;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_on_scroll_fn
    yetty_ygui_widget_yetty_ygui_widget_on_scroll_widget_default_on_scroll_check =
        widget_default_on_scroll;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_paint_fn
    yetty_ygui_widget_yetty_ygui_widget_paint_widget_default_paint_check = widget_default_paint;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_emit_container_fn
    yetty_ygui_widget_yetty_ygui_widget_emit_container_widget_default_emit_container_check =
        widget_default_emit_container;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_emit_body_fn
    yetty_ygui_widget_yetty_ygui_widget_emit_body_widget_default_emit_body_check =
        widget_default_emit_body;

struct yetty_yclass_ptr_result yetty_ygui_widget_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ygui_widget");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_widget",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ygui_widget),
        .data_align = _Alignof(struct yetty_ygui_widget),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor,
         (yetty_yclass_impl_t)widget_default_constructor},
        {"yetty_ygui", "destructor", (yetty_yclass_method_id_t)yetty_ygui_destructor,
         (yetty_yclass_impl_t)widget_default_destructor},
        {"yetty_ygui", "widget_on_press", (yetty_yclass_method_id_t)yetty_ygui_widget_on_press,
         (yetty_yclass_impl_t)widget_default_on_press},
        {"yetty_ygui", "widget_on_release", (yetty_yclass_method_id_t)yetty_ygui_widget_on_release,
         (yetty_yclass_impl_t)widget_default_on_release},
        {"yetty_ygui", "widget_on_motion", (yetty_yclass_method_id_t)yetty_ygui_widget_on_motion,
         (yetty_yclass_impl_t)widget_default_on_motion},
        {"yetty_ygui", "widget_on_scroll", (yetty_yclass_method_id_t)yetty_ygui_widget_on_scroll,
         (yetty_yclass_impl_t)widget_default_on_scroll},
        {"yetty_ygui", "widget_paint", (yetty_yclass_method_id_t)yetty_ygui_widget_paint,
         (yetty_yclass_impl_t)widget_default_paint},
        {"yetty_ygui", "widget_emit_container",
         (yetty_yclass_method_id_t)yetty_ygui_widget_emit_container,
         (yetty_yclass_impl_t)widget_default_emit_container},
        {"yetty_ygui", "widget_emit_body", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_body,
         (yetty_yclass_impl_t)widget_default_emit_body},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ygui_widget_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_widget_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygui_widget_ptr_result yetty_ygui_widget_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ygui_widget_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ygui_widget_ptr, "yetty_ygui_widget_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ygui_widget_ptr, "yetty_ygui_widget_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ygui_widget_ptr, (struct yetty_ygui_widget *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ygui_widget_to(struct yetty_ygui_widget *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ygui_widget_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ygui_widget_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ygui_widget_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_constructor);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ygui_constructor: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ygui_constructor: dispatch_lookup failed");
    return ((yetty_ygui_constructor_fn)dispatch_impl_r.value)(obj);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_ygui_destructor_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_ygui_destructor_skel(const void *body, size_t body_len, void *resp, size_t resp_max)
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
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_destructor: handle_resolve",
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
        yetty_ygui_destructor((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_destructor", call_r.error);
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
size_t yetty_ygui_widget_on_press_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_ygui_widget_on_press_skel(const void *body, size_t body_len, void *resp,
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
        int button;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_press: handle_resolve",
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
    struct yetty_ycore_int_result call_r =
        yetty_ygui_widget_on_press((struct yetty_yclass_object *)obj_resolve_r.value, wire_args.x,
                                   wire_args.y, wire_args.button);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_press", call_r.error);
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
size_t yetty_ygui_widget_on_release_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_ygui_widget_on_release_skel(const void *body, size_t body_len, void *resp,
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
        int button;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_release: handle_resolve",
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
    struct yetty_ycore_int_result call_r =
        yetty_ygui_widget_on_release((struct yetty_yclass_object *)obj_resolve_r.value, wire_args.x,
                                     wire_args.y, wire_args.button);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_release", call_r.error);
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
size_t yetty_ygui_widget_on_motion_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_ygui_widget_on_motion_skel(const void *body, size_t body_len, void *resp,
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
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_motion: handle_resolve",
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
    struct yetty_ycore_int_result call_r = yetty_ygui_widget_on_motion(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.x, wire_args.y);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_motion", call_r.error);
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
size_t yetty_ygui_widget_on_scroll_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_ygui_widget_on_scroll_skel(const void *body, size_t body_len, void *resp,
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
        float dx;
        float dy;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_scroll: handle_resolve",
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
    struct yetty_ycore_int_result call_r =
        yetty_ygui_widget_on_scroll((struct yetty_yclass_object *)obj_resolve_r.value, wire_args.x,
                                    wire_args.y, wire_args.dx, wire_args.dy);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_scroll", call_r.error);
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

struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_widget_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ygui_widget_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_widget");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ygui_widget_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ygui_widget_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_widget_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    struct yetty_ycore_void_result ctor_r = yetty_ygui_constructor(alloc_r.value);
    if (YETTY_IS_ERR(ctor_r)) {
        struct yetty_ycore_void_result free_r = yetty_yclass_object_free(alloc_r.value);
        if (YETTY_IS_ERR(free_r)) {
            yetty_ycore_error_destroy(free_r.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_widget_create: constructor failed",
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
struct yetty_yclass_ptr_result yetty_ygui_widget_class_get(void);
size_t yetty_ygui_destructor_skel(const void *, size_t, void *, size_t);
size_t yetty_ygui_widget_on_press_skel(const void *, size_t, void *, size_t);
size_t yetty_ygui_widget_on_release_skel(const void *, size_t, void *, size_t);
size_t yetty_ygui_widget_on_motion_skel(const void *, size_t, void *, size_t);
size_t yetty_ygui_widget_on_scroll_skel(const void *, size_t, void *, size_t);
struct yetty_ycore_void_result yetty_ygui_widget_register(void);

/* ---- ygui_widget: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ygui_widget_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ygui_widget") == 0) {
        return yetty_ygui_widget_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ygui_widget: slot -> skel, name-keyed static data --------------- */

struct yetty_ygui_widget_skel_row {
    const char *name;
    yetty_yclass_rpc_skel_fn fn;
};

static const struct yetty_ygui_widget_skel_row yetty_ygui_widget_skel_rows[] = {
    {"yetty_ygui_destructor", yetty_ygui_destructor_skel},
    {"yetty_ygui_widget_on_press", yetty_ygui_widget_on_press_skel},
    {"yetty_ygui_widget_on_release", yetty_ygui_widget_on_release_skel},
    {"yetty_ygui_widget_on_motion", yetty_ygui_widget_on_motion_skel},
    {"yetty_ygui_widget_on_scroll", yetty_ygui_widget_on_scroll_skel},
};

/* Signature dictated by the skel-lookup hook contract; a miss is absorbed
 * into a NULL return at this boundary. */
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_ygui_widget_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) {
        yetty_ycore_error_destroy(slot_name_r.error);
        return NULL;
    }
    const char *name = slot_name_r.value;
    for (size_t i = 0;
         i < sizeof(yetty_ygui_widget_skel_rows) / sizeof(yetty_ygui_widget_skel_rows[0]); ++i) {
        if (strcmp(yetty_ygui_widget_skel_rows[i].name, name) == 0) {
            return yetty_ygui_widget_skel_rows[i].fn;
        }
    }
    return NULL;
}

/* ---- ygui_widget: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ygui_widget_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ygui_widget_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ygui_widget_register: add_accessor_lookup");
    {
        struct yetty_ycore_void_result add_skel_r =
            yetty_yclass_rpc_add_skel_lookup(yetty_ygui_widget_skel_lookup);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,
                            "yetty_ygui_widget_register: rpc_add_skel_lookup");
    }
    registered = true;
    return YETTY_OK_VOID();
}
