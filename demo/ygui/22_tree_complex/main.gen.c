/* GENERATED — do not edit. */
#include "yetty/yguiapp/app.h"
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

YETTY_MAYBE_UNUSED
static yetty_yguiapp_build_fn yetty_demoygui_22_tree_complex_yetty_yguiapp_build_check = build;

struct yetty_yclass_ptr_result yetty_demoygui_22_tree_complex_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_demoygui_22_tree_complex");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_demoygui_22_tree_complex",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_demoygui_22_tree_complex),
        .data_align = _Alignof(struct yetty_demoygui_22_tree_complex),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yguiapp", "build", (yetty_yclass_method_id_t)yetty_yguiapp_build,
         (yetty_yclass_impl_t)build},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yguiapp_app_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_demoygui_22_tree_complex_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_demoygui_22_tree_complex_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_demoygui_22_tree_complex_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_demoygui_22_tree_complex_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_demoygui_22_tree_complex_ptr_result yetty_demoygui_22_tree_complex_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_demoygui_22_tree_complex_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_demoygui_22_tree_complex_ptr,
                         "yetty_demoygui_22_tree_complex_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_demoygui_22_tree_complex_ptr,
                         "yetty_demoygui_22_tree_complex_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_demoygui_22_tree_complex_ptr,
                    (struct yetty_demoygui_22_tree_complex *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_demoygui_22_tree_complex_to(
    struct yetty_demoygui_22_tree_complex *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_demoygui_22_tree_complex_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_demoygui_22_tree_complex_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_demoygui_22_tree_complex_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_demoygui_22_tree_complex_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_demoygui_22_tree_complex_create(
    struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_demoygui_22_tree_complex");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_demoygui_22_tree_complex_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_demoygui_22_tree_complex_create: class accessor failed",
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
        struct yetty_ycore_void_result translate_class_r = yetty_yclass_rpc_session_translate_class(
            ctx->session, "yetty_demoygui_22_tree_complex");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                                    "yetty_demoygui_22_tree_complex_create: translate_class "
                                    "(degraded — will lazy-resolve)",
                                    translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_demoygui_22_tree_complex";
    struct yetty_ycore_size_result create_call_r =
        yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name,
                              strlen(class_name), &handle, sizeof(handle));
    if (YETTY_IS_ERR(create_call_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_demoygui_22_tree_complex_create: CREATE call failed",
                         create_call_r);
    }
    if (create_call_r.value != sizeof(handle) || !handle) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_demoygui_22_tree_complex_create: CREATE returned no/invalid handle");
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
                         "yetty_demoygui_22_tree_complex_create: calloc(proxy) failed");
    }
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}
