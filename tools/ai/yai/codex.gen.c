/* GENERATED — do not edit. */
#include "yetty/yai/turn-engine.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h>  /* NULL, size_t */
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
#include <stdlib.h>  /* malloc/free for buffer-arg marshalling */
#include <string.h>

struct yai_app;
struct yetty_ycore_void_result;
struct yyjson_val;
struct yetty_ycore_void_result yetty_yai_start(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app);
struct yetty_ycore_void_result yetty_yai_send_user_message(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app, const char * text);
struct yetty_ycore_void_result yetty_yai_describe_config(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app, char * out, size_t out_size);
struct yetty_ycore_void_result yetty_yai_config_knob(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app, char * out, size_t out_size);
struct yetty_ycore_void_result yetty_yai_handle_event(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app, struct yyjson_val * event);
typedef struct yetty_ycore_void_result (*yetty_yai_start_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_send_user_message_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yai_describe_config_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *, char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yai_config_knob_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *, char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yai_handle_event_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *, struct yyjson_val *);

/* ===== class accessors ===== */

[[maybe_unused]]
static yetty_yai_start_fn yetty_yai_codex_yetty_yai_start_check = codex_start;
[[maybe_unused]]
static yetty_yai_send_user_message_fn yetty_yai_codex_yetty_yai_send_user_message_check = codex_send_user_message;
[[maybe_unused]]
static yetty_yai_describe_config_fn yetty_yai_codex_yetty_yai_describe_config_check = codex_describe_config;
[[maybe_unused]]
static yetty_yai_config_knob_fn yetty_yai_codex_yetty_yai_config_knob_check = codex_config_knob;
[[maybe_unused]]
static yetty_yai_handle_event_fn yetty_yai_codex_yetty_yai_handle_event_check = codex_handle_event;

struct yetty_yclass_ptr_result yetty_yai_codex_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yai_codex");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yai_codex",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yai_codex),
        .data_align = _Alignof(struct yetty_yai_codex),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yai", "start", (yetty_yclass_method_id_t)yetty_yai_start, (yetty_yclass_impl_t)codex_start},
        {"yetty_yai", "send_user_message", (yetty_yclass_method_id_t)yetty_yai_send_user_message, (yetty_yclass_impl_t)codex_send_user_message},
        {"yetty_yai", "describe_config", (yetty_yclass_method_id_t)yetty_yai_describe_config, (yetty_yclass_impl_t)codex_describe_config},
        {"yetty_yai", "config_knob", (yetty_yclass_method_id_t)yetty_yai_config_knob, (yetty_yclass_impl_t)codex_config_knob},
        {"yetty_yai", "handle_event", (yetty_yclass_method_id_t)yetty_yai_handle_event, (yetty_yclass_impl_t)codex_handle_event},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yai_turn_engine_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yai_codex_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yai_codex_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yai_codex_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yai_codex_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yai_codex_ptr_result yetty_yai_codex_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yai_codex_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yai_codex_ptr, "yetty_yai_codex_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yai_codex_ptr, "yetty_yai_codex_from: object_data", slice_r);
    return YETTY_OK(yetty_yai_codex_ptr, (struct yetty_yai_codex *)slice_r.value);
}

struct yetty_yclass_object *yetty_yai_codex_to(struct yetty_yai_codex *data)
{
    if (!data)
        return NULL;
    struct yetty_yclass_ptr_result class_r = yetty_yai_codex_class_get();
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

/* ===== rpc skeletons + create (was rpc.gen.c) ===== */

struct yetty_yclass_object_ptr_result yetty_yai_codex_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yai_codex");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yai_codex_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yai_codex_create: class accessor failed", class_accessor_r);
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
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yai_codex");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_yai_codex_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yai_codex";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yai_codex_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yai_codex_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yai_codex_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}


/* ---- yai/codex: class name -> accessor ---------------------- */
static struct yetty_yclass_ptr_result yetty_yai_codex_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yai_codex") == 0) return yetty_yai_codex_class_get();
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

struct yetty_ycore_void_result yetty_yai_codex_register_hooks(void)
{
    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yai_codex_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yai_codex_register_hooks: accessor");
    return YETTY_OK_VOID();
}
