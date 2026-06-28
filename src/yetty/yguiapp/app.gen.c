/* GENERATED — do not edit. */
#include "yetty/yapp/app.h"
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
struct yetty_ycore_void_result yetty_yguiapp_build(struct yetty_yclass_object *app,
                                                   struct yetty_yclass_object *root);
typedef struct yetty_ycore_void_result (*yetty_yguiapp_build_fn)(struct yetty_yclass_object *,
                                                                 struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_yguiapp_build_fn yetty_yguiapp_app_yetty_yguiapp_build_check = yguiapp_default_build;
YETTY_MAYBE_UNUSED
static yetty_yapp_init_fn yetty_yguiapp_app_yetty_yapp_init_check = yguiapp_init;
YETTY_MAYBE_UNUSED
static yetty_yapp_run_fn yetty_yguiapp_app_yetty_yapp_run_check = yguiapp_run;
YETTY_MAYBE_UNUSED
static yetty_yapp_quit_fn yetty_yguiapp_app_yetty_yapp_quit_check = yguiapp_quit;

struct yetty_yclass_ptr_result yetty_yguiapp_app_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yguiapp_app");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yguiapp_app",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yguiapp_app),
        .data_align = _Alignof(struct yetty_yguiapp_app),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yguiapp", "build", (yetty_yclass_method_id_t)yetty_yguiapp_build,
         (yetty_yclass_impl_t)yguiapp_default_build},
        {"yetty_yapp", "init", (yetty_yclass_method_id_t)yetty_yapp_init,
         (yetty_yclass_impl_t)yguiapp_init},
        {"yetty_yapp", "run", (yetty_yclass_method_id_t)yetty_yapp_run,
         (yetty_yclass_impl_t)yguiapp_run},
        {"yetty_yapp", "quit", (yetty_yclass_method_id_t)yetty_yapp_quit,
         (yetty_yclass_impl_t)yguiapp_quit},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yapp_app_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yguiapp_app_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yguiapp_app_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yguiapp_app_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yguiapp_app_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yguiapp_app_ptr_result yetty_yguiapp_app_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yguiapp_app_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yguiapp_app_ptr, "yetty_yguiapp_app_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yguiapp_app_ptr, "yetty_yguiapp_app_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yguiapp_app_ptr, (struct yetty_yguiapp_app *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yguiapp_app_to(struct yetty_yguiapp_app *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_yguiapp_app_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_yguiapp_app_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yguiapp_app_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_yguiapp_app_root_get(struct yetty_yclass_object *obj)
{
    struct yetty_yguiapp_app_ptr_result data = yetty_yguiapp_app_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yguiapp_app_root_get: data block", data);
    }
    return YETTY_OK(yetty_yclass_object_ptr, data.value->root);
}

struct yetty_ycore_void_result yetty_yguiapp_build(struct yetty_yclass_object *app,
                                                   struct yetty_yclass_object *root)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yguiapp", (yetty_yclass_method_id_t)yetty_yguiapp_build);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yguiapp_build: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!app) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yguiapp_build: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yguiapp_build: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yguiapp_build: dispatch_lookup failed");
    return ((yetty_yguiapp_build_fn)dispatch_impl_r.value)(app, root);
}

struct yetty_yclass_object_ptr_result yetty_yguiapp_app_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yguiapp_app_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yguiapp_app");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yguiapp_app_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yguiapp_app_create: class accessor failed",
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
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yguiapp_app");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(
                stderr, "yetty_yguiapp_app_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yguiapp_app";
    struct yetty_ycore_size_result create_call_r =
        yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name,
                              strlen(class_name), &handle, sizeof(handle));
    if (YETTY_IS_ERR(create_call_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yguiapp_app_create: CREATE call failed",
                         create_call_r);
    }
    if (create_call_r.value != sizeof(handle) || !handle) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yguiapp_app_create: CREATE returned no/invalid handle");
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
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yguiapp_app_create: calloc(proxy) failed");
    }
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}
