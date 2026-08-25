/* GENERATED — do not edit. */
#include "yetty/gen/impl/ytermsink/sink.h"
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

struct yetty_yclass_object_ptr_result;
struct yetty_yclass_object_ptr_result yetty_yterminal_figure_root_container(
    struct yetty_yclass_object *obj);
typedef struct yetty_yclass_object_ptr_result (*yetty_yterminal_figure_root_container_fn)(
    struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_ytermsink_pty_write_fn
    yetty_yterminal_terminal_yetty_ytermsink_pty_write_terminal_sink_pty_write_check =
        terminal_sink_pty_write;
YETTY_MAYBE_UNUSED
static yetty_ytermsink_mouse_sub_fn
    yetty_yterminal_terminal_yetty_ytermsink_mouse_sub_terminal_sink_mouse_sub_check =
        terminal_sink_mouse_sub;
YETTY_MAYBE_UNUSED
static yetty_ytermsink_clipboard_write_fn
    yetty_yterminal_terminal_yetty_ytermsink_clipboard_write_terminal_sink_clipboard_write_check =
        terminal_sink_clipboard_write;
YETTY_MAYBE_UNUSED
static yetty_ytermsink_sixel_write_fn
    yetty_yterminal_terminal_yetty_ytermsink_sixel_write_terminal_sink_sixel_write_check =
        terminal_sink_sixel_write;
YETTY_MAYBE_UNUSED
static yetty_ytermsink_request_render_fn
    yetty_yterminal_terminal_yetty_ytermsink_request_render_terminal_sink_request_render_check =
        terminal_sink_request_render;
YETTY_MAYBE_UNUSED
static yetty_ytermsink_set_title_fn
    yetty_yterminal_terminal_yetty_ytermsink_set_title_terminal_sink_set_title_check =
        terminal_sink_set_title;
YETTY_MAYBE_UNUSED
static yetty_yterminal_figure_root_container_fn
    yetty_yterminal_terminal_yetty_yterminal_figure_root_container_terminal_figure_root_container_check =
        terminal_figure_root_container;

struct yetty_yclass_ptr_result yetty_yterminal_terminal_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yterminal_terminal");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yterminal_terminal",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yterminal_terminal),
        .data_align = _Alignof(struct yetty_yterminal_terminal),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ytermsink", "pty_write", (yetty_yclass_method_id_t)yetty_ytermsink_pty_write,
         (yetty_yclass_impl_t)terminal_sink_pty_write},
        {"yetty_ytermsink", "mouse_sub", (yetty_yclass_method_id_t)yetty_ytermsink_mouse_sub,
         (yetty_yclass_impl_t)terminal_sink_mouse_sub},
        {"yetty_ytermsink", "clipboard_write",
         (yetty_yclass_method_id_t)yetty_ytermsink_clipboard_write,
         (yetty_yclass_impl_t)terminal_sink_clipboard_write},
        {"yetty_ytermsink", "sixel_write", (yetty_yclass_method_id_t)yetty_ytermsink_sixel_write,
         (yetty_yclass_impl_t)terminal_sink_sixel_write},
        {"yetty_ytermsink", "request_render",
         (yetty_yclass_method_id_t)yetty_ytermsink_request_render,
         (yetty_yclass_impl_t)terminal_sink_request_render},
        {"yetty_ytermsink", "set_title", (yetty_yclass_method_id_t)yetty_ytermsink_set_title,
         (yetty_yclass_impl_t)terminal_sink_set_title},
        {"yetty_yterminal", "figure_root_container",
         (yetty_yclass_method_id_t)yetty_yterminal_figure_root_container,
         (yetty_yclass_impl_t)terminal_figure_root_container},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ytermsink_sink_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yterminal_terminal_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yterminal_terminal_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yterminal_terminal_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yterminal_terminal_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yterminal_terminal_ptr_result yetty_yterminal_terminal_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yterminal_terminal_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yterminal_terminal_ptr,
                         "yetty_yterminal_terminal_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yterminal_terminal_ptr, "yetty_yterminal_terminal_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_yterminal_terminal_ptr, (struct yetty_yterminal_terminal *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yterminal_terminal_to(
    struct yetty_yterminal_terminal *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_yterminal_terminal_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_yterminal_terminal_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_yterminal_terminal_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yterminal_figure_root_container_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yterminal_figure_root_container_skel(const void *body, size_t body_len, void *resp,
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
                                "[skel] yetty_yterminal_figure_root_container: handle_resolve",
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
    struct yetty_yclass_object_ptr_result call_r =
        yetty_yterminal_figure_root_container((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1 + sizeof(uint64_t)) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yterminal_figure_root_container",
                                call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    uint64_t object_handle = 0;
    if (call_r.value) {
        struct yetty_yclass_handle_result handle_r =
            yetty_yclass_rpc_register_object_dedup(call_r.value);
        if (YETTY_IS_ERR(handle_r)) {
            yetty_ycore_error_print(
                stderr, "[skel] yetty_yterminal_figure_root_container: register returned object",
                handle_r.error);
            ((uint8_t *)resp)[0] = 1;
            size_t err_bytes =
                yetty_ycore_error_serialize(handle_r.error, (uint8_t *)resp + 1, resp_max - 1);
            yetty_ycore_error_destroy(handle_r.error);
            return 1 + err_bytes;
        }
        object_handle = handle_r.value;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &object_handle, sizeof(object_handle));
    return 1 + sizeof(object_handle);
}

struct yetty_yclass_object_ptr_result yetty_yterminal_terminal_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yterminal_terminal_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yterminal_terminal");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_yterminal_terminal_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yterminal_terminal_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yterminal_terminal_create: class accessor failed",
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
struct yetty_yclass_ptr_result yetty_yterminal_terminal_class_get(void);
size_t yetty_yterminal_figure_root_container_skel(const void *, size_t, void *, size_t);
struct yetty_ycore_void_result yetty_yterminal_register(void);

/* ---- yterminal: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yterminal_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yterminal_terminal") == 0) {
        return yetty_yterminal_terminal_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yterminal: slot -> skel, name-keyed static data --------------- */

struct yetty_yterminal_skel_row {
    const char *name;
    yetty_yclass_rpc_skel_fn fn;
};

static const struct yetty_yterminal_skel_row yetty_yterminal_skel_rows[] = {
    {"yetty_yterminal_figure_root_container", yetty_yterminal_figure_root_container_skel},
};

/* Signature dictated by the skel-lookup hook contract; a miss is absorbed
 * into a NULL return at this boundary. */
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_yterminal_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) {
        yetty_ycore_error_destroy(slot_name_r.error);
        return NULL;
    }
    const char *name = slot_name_r.value;
    for (size_t i = 0; i < sizeof(yetty_yterminal_skel_rows) / sizeof(yetty_yterminal_skel_rows[0]);
         ++i) {
        if (strcmp(yetty_yterminal_skel_rows[i].name, name) == 0) {
            return yetty_yterminal_skel_rows[i].fn;
        }
    }
    return NULL;
}

/* ---- yterminal: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yterminal_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yterminal_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yterminal_register: add_accessor_lookup");
    {
        struct yetty_ycore_void_result add_skel_r =
            yetty_yclass_rpc_add_skel_lookup(yetty_yterminal_skel_lookup);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,
                            "yetty_yterminal_register: rpc_add_skel_lookup");
    }
    registered = true;
    return YETTY_OK_VOID();
}
