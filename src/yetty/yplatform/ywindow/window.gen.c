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

struct yetty_yclass_void_ptr_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_yplatform_window_open(struct yetty_yclass_object * obj, int width, int height, const char * title);
struct yetty_ycore_void_result yetty_yplatform_window_destroy(struct yetty_yclass_object * obj);
struct yetty_yclass_void_ptr_result yetty_yplatform_window_create_surface(struct yetty_yclass_object * obj, void * instance);
struct yetty_ycore_void_result yetty_yplatform_window_get_size(struct yetty_yclass_object * obj, int * width, int * height);
struct yetty_ycore_void_result yetty_yplatform_window_get_framebuffer_size(struct yetty_yclass_object * obj, int * width, int * height);
struct yetty_ycore_void_result yetty_yplatform_window_get_content_scale(struct yetty_yclass_object * obj, float * xscale, float * yscale);
struct yetty_ycore_int_result yetty_yplatform_window_should_close(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yplatform_window_set_title(struct yetty_yclass_object * obj, const char * title);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_open_fn)(struct yetty_yclass_object *, int, int, const char *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_yclass_void_ptr_result (*yetty_yplatform_window_create_surface_fn)(struct yetty_yclass_object *, void *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_get_size_fn)(struct yetty_yclass_object *, int *, int *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_get_framebuffer_size_fn)(struct yetty_yclass_object *, int *, int *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_get_content_scale_fn)(struct yetty_yclass_object *, float *, float *);
typedef struct yetty_ycore_int_result (*yetty_yplatform_window_should_close_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_set_title_fn)(struct yetty_yclass_object *, const char *);

[[maybe_unused]]
static yetty_yplatform_window_open_fn yetty_yplatform_window_yetty_yplatform_window_open_check = window_default_open;
[[maybe_unused]]
static yetty_yplatform_window_destroy_fn yetty_yplatform_window_yetty_yplatform_window_destroy_check = window_default_destroy;
[[maybe_unused]]
static yetty_yplatform_window_create_surface_fn yetty_yplatform_window_yetty_yplatform_window_create_surface_check = window_default_create_surface;
[[maybe_unused]]
static yetty_yplatform_window_get_size_fn yetty_yplatform_window_yetty_yplatform_window_get_size_check = window_default_get_size;
[[maybe_unused]]
static yetty_yplatform_window_get_framebuffer_size_fn yetty_yplatform_window_yetty_yplatform_window_get_framebuffer_size_check = window_default_get_framebuffer_size;
[[maybe_unused]]
static yetty_yplatform_window_get_content_scale_fn yetty_yplatform_window_yetty_yplatform_window_get_content_scale_check = window_default_get_content_scale;
[[maybe_unused]]
static yetty_yplatform_window_should_close_fn yetty_yplatform_window_yetty_yplatform_window_should_close_check = window_default_should_close;
[[maybe_unused]]
static yetty_yplatform_window_set_title_fn yetty_yplatform_window_yetty_yplatform_window_set_title_check = window_default_set_title;

struct yetty_yclass_ptr_result yetty_yplatform_window_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yplatform_window");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yplatform_window",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yplatform_window),
        .data_align = _Alignof(struct yetty_yplatform_window),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yplatform", "window_open", (yetty_yclass_method_id_t)yetty_yplatform_window_open, (yetty_yclass_impl_t)window_default_open},
        {"yetty_yplatform", "window_destroy", (yetty_yclass_method_id_t)yetty_yplatform_window_destroy, (yetty_yclass_impl_t)window_default_destroy},
        {"yetty_yplatform", "window_create_surface", (yetty_yclass_method_id_t)yetty_yplatform_window_create_surface, (yetty_yclass_impl_t)window_default_create_surface},
        {"yetty_yplatform", "window_get_size", (yetty_yclass_method_id_t)yetty_yplatform_window_get_size, (yetty_yclass_impl_t)window_default_get_size},
        {"yetty_yplatform", "window_get_framebuffer_size", (yetty_yclass_method_id_t)yetty_yplatform_window_get_framebuffer_size, (yetty_yclass_impl_t)window_default_get_framebuffer_size},
        {"yetty_yplatform", "window_get_content_scale", (yetty_yclass_method_id_t)yetty_yplatform_window_get_content_scale, (yetty_yclass_impl_t)window_default_get_content_scale},
        {"yetty_yplatform", "window_should_close", (yetty_yclass_method_id_t)yetty_yplatform_window_should_close, (yetty_yclass_impl_t)window_default_should_close},
        {"yetty_yplatform", "window_set_title", (yetty_yclass_method_id_t)yetty_yplatform_window_set_title, (yetty_yclass_impl_t)window_default_set_title},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yplatform_window_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yplatform_window_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yplatform_window_ptr_result yetty_yplatform_window_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yplatform_window_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yplatform_window_ptr, "yetty_yplatform_window_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yplatform_window_ptr, "yetty_yplatform_window_from: object_data", slice_r);
    return YETTY_OK(yetty_yplatform_window_ptr, (struct yetty_yplatform_window *)slice_r.value);
}

struct yetty_yclass_object *yetty_yplatform_window_to(struct yetty_yplatform_window *data)
{
    if (!data)
        return NULL;
    struct yetty_yclass_ptr_result class_r = yetty_yplatform_window_class_get();
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


struct yetty_ycore_void_result yetty_yplatform_window_open(struct yetty_yclass_object * obj, int width, int height, const char * title)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_open);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_open: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_open: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yplatform_window_open: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yplatform_window_open: dispatch_lookup failed");
    return ((yetty_yplatform_window_open_fn)dispatch_impl_r.value)(obj, width, height, title);
}

struct yetty_ycore_void_result yetty_yplatform_window_get_size(struct yetty_yclass_object * obj, int * width, int * height)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_get_size);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_get_size: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_get_size: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yplatform_window_get_size: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yplatform_window_get_size: dispatch_lookup failed");
    return ((yetty_yplatform_window_get_size_fn)dispatch_impl_r.value)(obj, width, height);
}

struct yetty_ycore_void_result yetty_yplatform_window_get_framebuffer_size(struct yetty_yclass_object * obj, int * width, int * height)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_get_framebuffer_size);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_get_framebuffer_size: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_get_framebuffer_size: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yplatform_window_get_framebuffer_size: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yplatform_window_get_framebuffer_size: dispatch_lookup failed");
    return ((yetty_yplatform_window_get_framebuffer_size_fn)dispatch_impl_r.value)(obj, width, height);
}

struct yetty_ycore_void_result yetty_yplatform_window_get_content_scale(struct yetty_yclass_object * obj, float * xscale, float * yscale)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_get_content_scale);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_get_content_scale: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_get_content_scale: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yplatform_window_get_content_scale: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yplatform_window_get_content_scale: dispatch_lookup failed");
    return ((yetty_yplatform_window_get_content_scale_fn)dispatch_impl_r.value)(obj, xscale, yscale);
}

struct yetty_ycore_int_result yetty_yplatform_window_should_close(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_should_close);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_yplatform_window_should_close: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_yplatform_window_should_close: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_yplatform_window_should_close: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_yplatform_window_should_close: dispatch_lookup failed");
    return ((yetty_yplatform_window_should_close_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_yplatform_window_set_title(struct yetty_yclass_object * obj, const char * title)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_set_title);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_set_title: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_set_title: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yplatform_window_set_title: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yplatform_window_set_title: dispatch_lookup failed");
    return ((yetty_yplatform_window_set_title_fn)dispatch_impl_r.value)(obj, title);
}

struct yetty_ycore_void_result yetty_yplatform_window_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yplatform_window_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yplatform_window_destroy: dispatch_lookup failed");
    return ((yetty_yplatform_window_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_void_ptr_result yetty_yplatform_window_create_surface(struct yetty_yclass_object * obj, void * instance)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_create_surface);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_yclass_void_ptr, "yetty_yplatform_window_create_surface: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_yclass_void_ptr, "yetty_yplatform_window_create_surface: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, object_class_r, "yetty_yplatform_window_create_surface: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, dispatch_impl_r, "yetty_yplatform_window_create_surface: dispatch_lookup failed");
    return ((yetty_yplatform_window_create_surface_fn)dispatch_impl_r.value)(obj, instance);
}

struct yetty_yclass_object_ptr_result yetty_yplatform_window_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yplatform_window_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yplatform_window");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yplatform_window_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yplatform_window_create: class accessor failed", class_accessor_r);
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
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yplatform_window");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_yplatform_window_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yplatform_window";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yplatform_window_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yplatform_window_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yplatform_window_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

