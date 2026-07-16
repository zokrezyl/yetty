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

struct yetty_yclass_object_ptr_result;
struct yetty_ycore_char_ptr_result;
struct yetty_ycore_const_char_ptr_result;
struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_yjupyter_client_open(struct yetty_yclass_object * obj, const char * base_url, const char * token);
struct yetty_ycore_char_ptr_result yetty_yjupyter_client_execute(struct yetty_yclass_object * obj, const char * code, const char * tag);
struct yetty_yclass_object_ptr_result yetty_yjupyter_client_poll(struct yetty_yclass_object * obj, int timeout_ms);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_client_kernel_state(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_client_tag_for(struct yetty_yclass_object * obj, const char * parent_msg_id);
struct yetty_ycore_void_result yetty_yjupyter_client_close(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yjupyter_client_destroy(struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_client_open_fn)(struct yetty_yclass_object *, const char *, const char *);
typedef struct yetty_ycore_char_ptr_result (*yetty_yjupyter_client_execute_fn)(struct yetty_yclass_object *, const char *, const char *);
typedef struct yetty_yclass_object_ptr_result (*yetty_yjupyter_client_poll_fn)(struct yetty_yclass_object *, int);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_client_kernel_state_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_client_tag_for_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_client_close_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_client_destroy_fn)(struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_yjupyter_client_open_fn yetty_yjupyter_client_yetty_yjupyter_client_open_check = client_open;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_client_execute_fn yetty_yjupyter_client_yetty_yjupyter_client_execute_check = client_execute;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_client_poll_fn yetty_yjupyter_client_yetty_yjupyter_client_poll_check = client_poll;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_client_kernel_state_fn yetty_yjupyter_client_yetty_yjupyter_client_kernel_state_check = client_kernel_state;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_client_tag_for_fn yetty_yjupyter_client_yetty_yjupyter_client_tag_for_check = client_tag_for;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_client_close_fn yetty_yjupyter_client_yetty_yjupyter_client_close_check = client_close;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_client_destroy_fn yetty_yjupyter_client_yetty_yjupyter_client_destroy_check = client_destroy;

struct yetty_yclass_ptr_result yetty_yjupyter_client_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yjupyter_client");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yjupyter_client",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yjupyter_client),
        .data_align = _Alignof(struct yetty_yjupyter_client),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yjupyter", "client_open", (yetty_yclass_method_id_t)yetty_yjupyter_client_open, (yetty_yclass_impl_t)client_open},
        {"yetty_yjupyter", "client_execute", (yetty_yclass_method_id_t)yetty_yjupyter_client_execute, (yetty_yclass_impl_t)client_execute},
        {"yetty_yjupyter", "client_poll", (yetty_yclass_method_id_t)yetty_yjupyter_client_poll, (yetty_yclass_impl_t)client_poll},
        {"yetty_yjupyter", "client_kernel_state", (yetty_yclass_method_id_t)yetty_yjupyter_client_kernel_state, (yetty_yclass_impl_t)client_kernel_state},
        {"yetty_yjupyter", "client_tag_for", (yetty_yclass_method_id_t)yetty_yjupyter_client_tag_for, (yetty_yclass_impl_t)client_tag_for},
        {"yetty_yjupyter", "client_close", (yetty_yclass_method_id_t)yetty_yjupyter_client_close, (yetty_yclass_impl_t)client_close},
        {"yetty_yjupyter", "client_destroy", (yetty_yclass_method_id_t)yetty_yjupyter_client_destroy, (yetty_yclass_impl_t)client_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yjupyter_client_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yjupyter_client_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yjupyter_client_ptr_result yetty_yjupyter_client_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yjupyter_client_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yjupyter_client_ptr, "yetty_yjupyter_client_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yjupyter_client_ptr, "yetty_yjupyter_client_from: object_data", slice_r);
    return YETTY_OK(yetty_yjupyter_client_ptr, (struct yetty_yjupyter_client *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_client_to(struct yetty_yjupyter_client *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_yjupyter_client_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_yjupyter_client_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yjupyter_client_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}


struct yetty_ycore_void_result yetty_yjupyter_client_open(struct yetty_yclass_object * obj, const char * base_url, const char * token)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_client_open);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_client_open: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_client_open: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yjupyter_client_open: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yjupyter_client_open: dispatch_lookup failed");
    return ((yetty_yjupyter_client_open_fn)dispatch_impl_r.value)(obj, base_url, token);
}

struct yetty_ycore_char_ptr_result yetty_yjupyter_client_execute(struct yetty_yclass_object * obj, const char * code, const char * tag)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_client_execute);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yjupyter_client_execute: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yjupyter_client_execute: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r, "yetty_yjupyter_client_execute: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r, "yetty_yjupyter_client_execute: dispatch_lookup failed");
    return ((yetty_yjupyter_client_execute_fn)dispatch_impl_r.value)(obj, code, tag);
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_client_poll(struct yetty_yclass_object * obj, int timeout_ms)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_client_poll);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yjupyter_client_poll: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yjupyter_client_poll: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_class_r, "yetty_yjupyter_client_poll: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, dispatch_impl_r, "yetty_yjupyter_client_poll: dispatch_lookup failed");
    return ((yetty_yjupyter_client_poll_fn)dispatch_impl_r.value)(obj, timeout_ms);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_client_kernel_state(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_client_kernel_state);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_client_kernel_state: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_client_kernel_state: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_yjupyter_client_kernel_state: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_yjupyter_client_kernel_state: dispatch_lookup failed");
    return ((yetty_yjupyter_client_kernel_state_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_client_tag_for(struct yetty_yclass_object * obj, const char * parent_msg_id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_client_tag_for);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_client_tag_for: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_client_tag_for: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_yjupyter_client_tag_for: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_yjupyter_client_tag_for: dispatch_lookup failed");
    return ((yetty_yjupyter_client_tag_for_fn)dispatch_impl_r.value)(obj, parent_msg_id);
}

struct yetty_ycore_void_result yetty_yjupyter_client_close(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_client_close);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_client_close: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_client_close: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yjupyter_client_close: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yjupyter_client_close: dispatch_lookup failed");
    return ((yetty_yjupyter_client_close_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_yjupyter_client_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_client_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_client_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_client_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yjupyter_client_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yjupyter_client_destroy: dispatch_lookup failed");
    return ((yetty_yjupyter_client_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_client_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yjupyter_client_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yjupyter_client");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yjupyter_client_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yjupyter_client_create: class accessor failed", class_accessor_r);
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
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yjupyter_client");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_yjupyter_client_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yjupyter_client";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yjupyter_client_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yjupyter_client_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yjupyter_client_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

