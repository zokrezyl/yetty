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

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;
struct yetty_ycore_void_result yetty_yflame_configure(struct yetty_yclass_object * obj, float width, float frame_height, float min_width, uint32_t flags);
struct yetty_ycore_void_result yetty_yflame_parse(struct yetty_yclass_object * obj, const char * input, size_t len);
struct yetty_ydraw_drawable_list_result yetty_yflame_render(struct yetty_yclass_object * obj);
struct yetty_ycore_int_result yetty_yflame_hit_test(struct yetty_yclass_object * obj, float x, float y);
struct yetty_ycore_void_result yetty_yflame_focus(struct yetty_yclass_object * obj, int32_t node_id);
struct yetty_ycore_void_result yetty_yflame_focus_parent(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yflame_reset(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yflame_set_highlight(struct yetty_yclass_object * obj, int32_t node_id);
struct yetty_ycore_void_result yetty_yflame_destroy(struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_yflame_configure_fn)(struct yetty_yclass_object *, float, float, float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_parse_fn)(struct yetty_yclass_object *, const char *, size_t);
typedef struct yetty_ydraw_drawable_list_result (*yetty_yflame_render_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_yflame_hit_test_fn)(struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yflame_focus_fn)(struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_focus_parent_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yflame_reset_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yflame_set_highlight_fn)(struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_destroy_fn)(struct yetty_yclass_object *);

[[maybe_unused]]
static yetty_yflame_configure_fn yetty_yflame_flame_yetty_yflame_configure_check = flame_configure;
[[maybe_unused]]
static yetty_yflame_parse_fn yetty_yflame_flame_yetty_yflame_parse_check = flame_parse;
[[maybe_unused]]
static yetty_yflame_render_fn yetty_yflame_flame_yetty_yflame_render_check = flame_render;
[[maybe_unused]]
static yetty_yflame_hit_test_fn yetty_yflame_flame_yetty_yflame_hit_test_check = flame_hit_test;
[[maybe_unused]]
static yetty_yflame_focus_fn yetty_yflame_flame_yetty_yflame_focus_check = flame_focus;
[[maybe_unused]]
static yetty_yflame_focus_parent_fn yetty_yflame_flame_yetty_yflame_focus_parent_check = flame_focus_parent;
[[maybe_unused]]
static yetty_yflame_reset_fn yetty_yflame_flame_yetty_yflame_reset_check = flame_reset;
[[maybe_unused]]
static yetty_yflame_set_highlight_fn yetty_yflame_flame_yetty_yflame_set_highlight_check = flame_set_highlight;
[[maybe_unused]]
static yetty_yflame_destroy_fn yetty_yflame_flame_yetty_yflame_destroy_check = flame_obj_destroy;

struct yetty_yclass_ptr_result yetty_yflame_flame_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yflame_flame");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yflame_flame",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yflame_flame),
        .data_align = _Alignof(struct yetty_yflame_flame),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yflame", "configure", (yetty_yclass_method_id_t)yetty_yflame_configure, (yetty_yclass_impl_t)flame_configure},
        {"yetty_yflame", "parse", (yetty_yclass_method_id_t)yetty_yflame_parse, (yetty_yclass_impl_t)flame_parse},
        {"yetty_yflame", "render", (yetty_yclass_method_id_t)yetty_yflame_render, (yetty_yclass_impl_t)flame_render},
        {"yetty_yflame", "hit_test", (yetty_yclass_method_id_t)yetty_yflame_hit_test, (yetty_yclass_impl_t)flame_hit_test},
        {"yetty_yflame", "focus", (yetty_yclass_method_id_t)yetty_yflame_focus, (yetty_yclass_impl_t)flame_focus},
        {"yetty_yflame", "focus_parent", (yetty_yclass_method_id_t)yetty_yflame_focus_parent, (yetty_yclass_impl_t)flame_focus_parent},
        {"yetty_yflame", "reset", (yetty_yclass_method_id_t)yetty_yflame_reset, (yetty_yclass_impl_t)flame_reset},
        {"yetty_yflame", "set_highlight", (yetty_yclass_method_id_t)yetty_yflame_set_highlight, (yetty_yclass_impl_t)flame_set_highlight},
        {"yetty_yflame", "destroy", (yetty_yclass_method_id_t)yetty_yflame_destroy, (yetty_yclass_impl_t)flame_obj_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yflame_flame_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yflame_flame_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yflame_flame_ptr_result yetty_yflame_flame_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yflame_flame_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yflame_flame_ptr, "yetty_yflame_flame_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yflame_flame_ptr, "yetty_yflame_flame_from: object_data", slice_r);
    return YETTY_OK(yetty_yflame_flame_ptr, (struct yetty_yflame_flame *)slice_r.value);
}

struct yetty_yclass_object *yetty_yflame_flame_to(struct yetty_yflame_flame *data)
{
    if (!data)
        return NULL;
    struct yetty_yclass_ptr_result class_r = yetty_yflame_flame_class_get();
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


struct yetty_ycore_void_result yetty_yflame_configure(struct yetty_yclass_object * obj, float width, float frame_height, float min_width, uint32_t flags)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yflame", (yetty_yclass_method_id_t)yetty_yflame_configure);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yflame_configure: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yflame_configure: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yflame_configure: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yflame_configure: dispatch_lookup failed");
    return ((yetty_yflame_configure_fn)dispatch_impl_r.value)(obj, width, frame_height, min_width, flags);
}

struct yetty_ycore_void_result yetty_yflame_parse(struct yetty_yclass_object * obj, const char * input, size_t len)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yflame", (yetty_yclass_method_id_t)yetty_yflame_parse);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yflame_parse: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yflame_parse: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yflame_parse: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yflame_parse: dispatch_lookup failed");
    return ((yetty_yflame_parse_fn)dispatch_impl_r.value)(obj, input, len);
}

struct yetty_ydraw_drawable_list_result yetty_yflame_render(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yflame", (yetty_yclass_method_id_t)yetty_yflame_render);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ydraw_drawable_list, "yetty_yflame_render: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ydraw_drawable_list, "yetty_yflame_render: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, object_class_r, "yetty_yflame_render: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, dispatch_impl_r, "yetty_yflame_render: dispatch_lookup failed");
    return ((yetty_yflame_render_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_int_result yetty_yflame_hit_test(struct yetty_yclass_object * obj, float x, float y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yflame", (yetty_yclass_method_id_t)yetty_yflame_hit_test);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_yflame_hit_test: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_yflame_hit_test: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_yflame_hit_test: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_yflame_hit_test: dispatch_lookup failed");
    return ((yetty_yflame_hit_test_fn)dispatch_impl_r.value)(obj, x, y);
}

struct yetty_ycore_void_result yetty_yflame_focus(struct yetty_yclass_object * obj, int32_t node_id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yflame", (yetty_yclass_method_id_t)yetty_yflame_focus);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yflame_focus: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yflame_focus: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yflame_focus: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yflame_focus: dispatch_lookup failed");
    return ((yetty_yflame_focus_fn)dispatch_impl_r.value)(obj, node_id);
}

struct yetty_ycore_void_result yetty_yflame_focus_parent(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yflame", (yetty_yclass_method_id_t)yetty_yflame_focus_parent);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yflame_focus_parent: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yflame_focus_parent: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yflame_focus_parent: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yflame_focus_parent: dispatch_lookup failed");
    return ((yetty_yflame_focus_parent_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_yflame_reset(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yflame", (yetty_yclass_method_id_t)yetty_yflame_reset);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yflame_reset: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yflame_reset: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yflame_reset: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yflame_reset: dispatch_lookup failed");
    return ((yetty_yflame_reset_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_yflame_set_highlight(struct yetty_yclass_object * obj, int32_t node_id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yflame", (yetty_yclass_method_id_t)yetty_yflame_set_highlight);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yflame_set_highlight: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yflame_set_highlight: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yflame_set_highlight: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yflame_set_highlight: dispatch_lookup failed");
    return ((yetty_yflame_set_highlight_fn)dispatch_impl_r.value)(obj, node_id);
}

struct yetty_ycore_void_result yetty_yflame_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yflame", (yetty_yclass_method_id_t)yetty_yflame_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yflame_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yflame_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yflame_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yflame_destroy: dispatch_lookup failed");
    return ((yetty_yflame_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_yflame_flame_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yflame_flame_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yflame_flame");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yflame_flame_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yflame_flame_create: class accessor failed", class_accessor_r);
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
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yflame_flame");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_yflame_flame_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yflame_flame";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yflame_flame_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yflame_flame_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yflame_flame_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

