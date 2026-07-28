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

struct yetty_ycore_const_char_ptr_result;
struct yetty_ycore_const_uint8_ptr_result;
struct yetty_ycore_float_result;
struct yetty_ycore_uint32_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;
struct yetty_ycore_void_result yetty_ynet_load_file(struct yetty_yclass_object *obj,
                                                    const char *path);
struct yetty_ycore_uint32_result yetty_ynet_packet_count(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_ynet_packet_time(struct yetty_yclass_object *obj,
                                                       uint32_t index);
struct yetty_ycore_uint32_result yetty_ynet_packet_length(struct yetty_yclass_object *obj,
                                                          uint32_t index);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_protocol(struct yetty_yclass_object *obj,
                                                                    uint32_t index);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_source(struct yetty_yclass_object *obj,
                                                                  uint32_t index);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_destination(
    struct yetty_yclass_object *obj, uint32_t index);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_info(struct yetty_yclass_object *obj,
                                                                uint32_t index);
struct yetty_ycore_const_uint8_ptr_result yetty_ynet_packet_bytes(struct yetty_yclass_object *obj,
                                                                  uint32_t index);
struct yetty_ycore_uint32_result yetty_ynet_packet_caplen(struct yetty_yclass_object *obj,
                                                          uint32_t index);
struct yetty_ycore_uint32_result yetty_ynet_flow_count(struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_ynet_flow_summary(struct yetty_yclass_object *obj,
                                                                 uint32_t index);
struct yetty_ydraw_drawable_list_result yetty_ynet_render(struct yetty_yclass_object *obj,
                                                          uint32_t width, uint32_t height);
struct yetty_ycore_void_result yetty_ynet_destroy(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_ynet_load_file_fn)(struct yetty_yclass_object *,
                                                                  const char *);
typedef struct yetty_ycore_uint32_result (*yetty_ynet_packet_count_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_ynet_packet_time_fn)(struct yetty_yclass_object *,
                                                                     uint32_t);
typedef struct yetty_ycore_uint32_result (*yetty_ynet_packet_length_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynet_packet_protocol_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynet_packet_source_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynet_packet_destination_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynet_packet_info_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_const_uint8_ptr_result (*yetty_ynet_packet_bytes_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_uint32_result (*yetty_ynet_packet_caplen_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_uint32_result (*yetty_ynet_flow_count_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynet_flow_summary_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ynet_render_fn)(
    struct yetty_yclass_object *, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ynet_destroy_fn)(struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_ynet_load_file_fn yetty_ynet_capture_yetty_ynet_load_file_check = capture_load_file;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_count_fn yetty_ynet_capture_yetty_ynet_packet_count_check =
    capture_packet_count;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_time_fn yetty_ynet_capture_yetty_ynet_packet_time_check =
    capture_packet_time;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_length_fn yetty_ynet_capture_yetty_ynet_packet_length_check =
    capture_packet_length;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_protocol_fn yetty_ynet_capture_yetty_ynet_packet_protocol_check =
    capture_packet_protocol;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_source_fn yetty_ynet_capture_yetty_ynet_packet_source_check =
    capture_packet_source;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_destination_fn yetty_ynet_capture_yetty_ynet_packet_destination_check =
    capture_packet_destination;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_info_fn yetty_ynet_capture_yetty_ynet_packet_info_check =
    capture_packet_info;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_bytes_fn yetty_ynet_capture_yetty_ynet_packet_bytes_check =
    capture_packet_bytes;
YETTY_MAYBE_UNUSED
static yetty_ynet_packet_caplen_fn yetty_ynet_capture_yetty_ynet_packet_caplen_check =
    capture_packet_caplen;
YETTY_MAYBE_UNUSED
static yetty_ynet_flow_count_fn yetty_ynet_capture_yetty_ynet_flow_count_check = capture_flow_count;
YETTY_MAYBE_UNUSED
static yetty_ynet_flow_summary_fn yetty_ynet_capture_yetty_ynet_flow_summary_check =
    capture_flow_summary;
YETTY_MAYBE_UNUSED
static yetty_ynet_render_fn yetty_ynet_capture_yetty_ynet_render_check = capture_render;
YETTY_MAYBE_UNUSED
static yetty_ynet_destroy_fn yetty_ynet_capture_yetty_ynet_destroy_check = capture_destroy;

struct yetty_yclass_ptr_result yetty_ynet_capture_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ynet_capture");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ynet_capture",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ynet_capture),
        .data_align = _Alignof(struct yetty_ynet_capture),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ynet", "load_file", (yetty_yclass_method_id_t)yetty_ynet_load_file,
         (yetty_yclass_impl_t)capture_load_file},
        {"yetty_ynet", "packet_count", (yetty_yclass_method_id_t)yetty_ynet_packet_count,
         (yetty_yclass_impl_t)capture_packet_count},
        {"yetty_ynet", "packet_time", (yetty_yclass_method_id_t)yetty_ynet_packet_time,
         (yetty_yclass_impl_t)capture_packet_time},
        {"yetty_ynet", "packet_length", (yetty_yclass_method_id_t)yetty_ynet_packet_length,
         (yetty_yclass_impl_t)capture_packet_length},
        {"yetty_ynet", "packet_protocol", (yetty_yclass_method_id_t)yetty_ynet_packet_protocol,
         (yetty_yclass_impl_t)capture_packet_protocol},
        {"yetty_ynet", "packet_source", (yetty_yclass_method_id_t)yetty_ynet_packet_source,
         (yetty_yclass_impl_t)capture_packet_source},
        {"yetty_ynet", "packet_destination",
         (yetty_yclass_method_id_t)yetty_ynet_packet_destination,
         (yetty_yclass_impl_t)capture_packet_destination},
        {"yetty_ynet", "packet_info", (yetty_yclass_method_id_t)yetty_ynet_packet_info,
         (yetty_yclass_impl_t)capture_packet_info},
        {"yetty_ynet", "packet_bytes", (yetty_yclass_method_id_t)yetty_ynet_packet_bytes,
         (yetty_yclass_impl_t)capture_packet_bytes},
        {"yetty_ynet", "packet_caplen", (yetty_yclass_method_id_t)yetty_ynet_packet_caplen,
         (yetty_yclass_impl_t)capture_packet_caplen},
        {"yetty_ynet", "flow_count", (yetty_yclass_method_id_t)yetty_ynet_flow_count,
         (yetty_yclass_impl_t)capture_flow_count},
        {"yetty_ynet", "flow_summary", (yetty_yclass_method_id_t)yetty_ynet_flow_summary,
         (yetty_yclass_impl_t)capture_flow_summary},
        {"yetty_ynet", "render", (yetty_yclass_method_id_t)yetty_ynet_render,
         (yetty_yclass_impl_t)capture_render},
        {"yetty_ynet", "destroy", (yetty_yclass_method_id_t)yetty_ynet_destroy,
         (yetty_yclass_impl_t)capture_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ynet_capture_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ynet_capture_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ynet_capture_ptr_result yetty_ynet_capture_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ynet_capture_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ynet_capture_ptr, "yetty_ynet_capture_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ynet_capture_ptr, "yetty_ynet_capture_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ynet_capture_ptr, (struct yetty_ynet_capture *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ynet_capture_to(struct yetty_ynet_capture *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ynet_capture_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ynet_capture_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ynet_capture_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_ynet_capture_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ynet_capture_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ynet_capture");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ynet_capture_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ynet_capture_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynet_capture_create: class accessor failed", class_accessor_r);
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
struct yetty_yclass_ptr_result yetty_ynet_capture_class_get(void);
struct yetty_ycore_void_result yetty_ynet_register(void);

/* ---- ynet: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ynet_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ynet_capture") == 0) {
        return yetty_ynet_capture_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ynet: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ynet_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ynet_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ynet_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
