/* GENERATED — do not edit. */
#include <yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include "yetty/ygrid/rpc.h"
#include "yetty/ygrid/methods.h"
#include <yclass/class.h>
#include "yetty/ygrid/grid.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t yetty_ygrid_add_record_skel(const void *body, size_t body_len,
                          void *resp, size_t resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        uint32_t record_len;
    } wire_args;
    if (body_len < sizeof(wire_args)) return 0;
    memcpy(&wire_args, body, sizeof(wire_args));
    if (body_len != sizeof(wire_args) + (size_t)wire_args.record_len) return 0;
    size_t body_offset = sizeof(wire_args);
    struct yetty_ycore_buffer record_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.record_len,
        .capacity = (size_t)wire_args.record_len,
    };
    body_offset += (size_t)wire_args.record_len;
    struct yetty_yclass_ctx local_ctx = {0};
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_ygrid_add_record: handle_resolve", obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) return 0;
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r = yetty_ygrid_add_record(&local_ctx, (struct yetty_yclass_object *)obj_resolve_r.value, record_buf);
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_add_record", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

static size_t yetty_ygrid_clear_skel(const void *body, size_t body_len,
                          void *resp, size_t resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } wire_args;
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) return 0;
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_ctx local_ctx = {0};
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_ygrid_clear: handle_resolve", obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) return 0;
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r = yetty_ygrid_clear(&local_ctx, (struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_clear", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

static size_t yetty_ygrid_destroy_skel(const void *body, size_t body_len,
                          void *resp, size_t resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } wire_args;
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) return 0;
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_ctx local_ctx = {0};
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_ygrid_destroy: handle_resolve", obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) return 0;
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r = yetty_ygrid_destroy(&local_ctx, (struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_destroy", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

static size_t yetty_ygrid_process_bytes_skel(const void *body, size_t body_len,
                          void *resp, size_t resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        uint32_t payload_len;
    } wire_args;
    if (body_len < sizeof(wire_args)) return 0;
    memcpy(&wire_args, body, sizeof(wire_args));
    if (body_len != sizeof(wire_args) + (size_t)wire_args.payload_len) return 0;
    size_t body_offset = sizeof(wire_args);
    struct yetty_ycore_buffer payload_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.payload_len,
        .capacity = (size_t)wire_args.payload_len,
    };
    body_offset += (size_t)wire_args.payload_len;
    struct yetty_yclass_ctx local_ctx = {0};
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_ygrid_process_bytes: handle_resolve", obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) return 0;
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r = yetty_ygrid_process_bytes(&local_ctx, (struct yetty_yclass_object *)obj_resolve_r.value, payload_buf);
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_process_bytes", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

static size_t yetty_ygrid_reset_content_skel(const void *body, size_t body_len,
                          void *resp, size_t resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } wire_args;
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) return 0;
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_ctx local_ctx = {0};
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_ygrid_reset_content: handle_resolve", obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) return 0;
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r = yetty_ygrid_reset_content(&local_ctx, (struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_reset_content", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

struct yetty_yclass_object_ptr_result yetty_ygrid_grid_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygrid_grid");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ygrid_grid_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygrid_grid_create: class accessor failed", class_accessor_r);
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
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygrid_grid");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygrid_grid_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_ygrid_grid";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygrid_grid_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygrid_grid_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygrid_grid_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

/* ---- ygrid: class name → accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ygrid_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ygrid_grid") == 0) return yetty_ygrid_grid_class_get();
    /* "Not mine": OK with NULL value — yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ygrid: slot → skel, name-keyed static data --------------- */

struct yetty_ygrid_skel_row { const char *name; yetty_yclass_rpc_skel_fn fn; };

static const struct yetty_ygrid_skel_row yetty_ygrid_skel_rows[] = {
    {"yetty_ygrid_add_record", yetty_ygrid_add_record_skel},
    {"yetty_ygrid_clear", yetty_ygrid_clear_skel},
    {"yetty_ygrid_destroy", yetty_ygrid_destroy_skel},
    {"yetty_ygrid_process_bytes", yetty_ygrid_process_bytes_skel},
    {"yetty_ygrid_reset_content", yetty_ygrid_reset_content_skel}
};

static yetty_yclass_rpc_skel_fn yetty_ygrid_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) { yetty_ycore_error_destroy(slot_name_r.error); return NULL; }
    const char *name = slot_name_r.value;
    for (size_t i = 0;
         i < sizeof(yetty_ygrid_skel_rows) / sizeof(yetty_ygrid_skel_rows[0]); ++i)
        if (strcmp(yetty_ygrid_skel_rows[i].name, name) == 0)
            return yetty_ygrid_skel_rows[i].fn;
    return NULL;
}

/* ---- ygrid: install hooks before main ------------------------- */

__attribute__((constructor))
static void yetty_ygrid_install_hooks(void)
{
    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ygrid_accessor_lookup);
    if (YETTY_IS_ERR(add_accessor_r)) {
        yetty_ycore_error_print(stderr, "yetty_ygrid_install_hooks", add_accessor_r.error);
        yetty_ycore_error_destroy(add_accessor_r.error);
        abort();
    }
    {
        struct yetty_ycore_void_result add_skel_r =
            yetty_yclass_rpc_add_skel_lookup(yetty_ygrid_skel_lookup);
        if (YETTY_IS_ERR(add_skel_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygrid_install_hooks: rpc_add_skel_lookup", add_skel_r.error);
            yetty_ycore_error_destroy(add_skel_r.error);
            abort();
        }
    }
}
