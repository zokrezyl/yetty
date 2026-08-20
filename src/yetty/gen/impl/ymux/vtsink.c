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

struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_ymux_feed(struct yetty_yclass_object *obj, uint64_t generation,
                                               struct yetty_ycore_buffer bytes);
typedef struct yetty_ycore_void_result (*yetty_ymux_feed_fn)(struct yetty_yclass_object *, uint64_t,
                                                             struct yetty_ycore_buffer);

YETTY_MAYBE_UNUSED
static yetty_ymux_feed_fn yetty_ymux_vtsink_yetty_ymux_feed_check = vtsink_feed_impl;

struct yetty_yclass_ptr_result yetty_ymux_vtsink_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ymux_vtsink");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ymux_vtsink",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ymux_vtsink),
        .data_align = _Alignof(struct yetty_ymux_vtsink),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ymux", "feed", (yetty_yclass_method_id_t)yetty_ymux_feed,
         (yetty_yclass_impl_t)vtsink_feed_impl},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ymux_vtsink_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ymux_vtsink_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ymux_vtsink_ptr_result yetty_ymux_vtsink_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ymux_vtsink_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ymux_vtsink_ptr, "yetty_ymux_vtsink_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ymux_vtsink_ptr, "yetty_ymux_vtsink_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ymux_vtsink_ptr, (struct yetty_ymux_vtsink *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ymux_vtsink_to(struct yetty_ymux_vtsink *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ymux_vtsink_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ymux_vtsink_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ymux_vtsink_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_ymux_feed_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_ymux_feed_skel(const void *body, size_t body_len, void *resp, size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint64_t generation;
        uint32_t bytes_len;
    } wire_args;
#pragma pack(pop)
    if (body_len < sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    if (body_len != sizeof(wire_args) + (size_t)wire_args.bytes_len) {
        return 0;
    }
    size_t body_offset = sizeof(wire_args);
    struct yetty_ycore_buffer bytes_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.bytes_len,
        .capacity = (size_t)wire_args.bytes_len,
    };
    body_offset += (size_t)wire_args.bytes_len;
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ymux_feed: handle_resolve",
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
    struct yetty_ycore_void_result call_r = yetty_ymux_feed(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.generation, bytes_buf);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ymux_feed", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

struct yetty_yclass_object_ptr_result yetty_ymux_vtsink_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ymux_vtsink_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ymux_vtsink");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ymux_vtsink_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ymux_vtsink_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ymux_vtsink_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_ymux_vtsink_class_get(void);
size_t yetty_ymux_feed_skel(const void *, size_t, void *, size_t);
struct yetty_ycore_void_result yetty_ymux_vtsink_register(void);

/* ---- ymux_vtsink: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ymux_vtsink_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ymux_vtsink") == 0) {
        return yetty_ymux_vtsink_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ymux_vtsink: slot -> skel, name-keyed static data --------------- */

struct yetty_ymux_vtsink_skel_row {
    const char *name;
    yetty_yclass_rpc_skel_fn fn;
};

static const struct yetty_ymux_vtsink_skel_row yetty_ymux_vtsink_skel_rows[] = {
    {"yetty_ymux_feed", yetty_ymux_feed_skel},
};

/* Signature dictated by the skel-lookup hook contract; a miss is absorbed
 * into a NULL return at this boundary. */
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_ymux_vtsink_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) {
        yetty_ycore_error_destroy(slot_name_r.error);
        return NULL;
    }
    const char *name = slot_name_r.value;
    for (size_t i = 0;
         i < sizeof(yetty_ymux_vtsink_skel_rows) / sizeof(yetty_ymux_vtsink_skel_rows[0]); ++i) {
        if (strcmp(yetty_ymux_vtsink_skel_rows[i].name, name) == 0) {
            return yetty_ymux_vtsink_skel_rows[i].fn;
        }
    }
    return NULL;
}

/* ---- ymux_vtsink: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ymux_vtsink_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ymux_vtsink_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ymux_vtsink_register: add_accessor_lookup");
    {
        struct yetty_ycore_void_result add_skel_r =
            yetty_yclass_rpc_add_skel_lookup(yetty_ymux_vtsink_skel_lookup);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,
                            "yetty_ymux_vtsink_register: rpc_add_skel_lookup");
    }
    registered = true;
    return YETTY_OK_VOID();
}
