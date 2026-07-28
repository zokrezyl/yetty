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

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;
struct yetty_yui_event;
struct yetty_ycore_void_result yetty_ychrome_configure(struct yetty_yclass_object *obj,
                                                       struct yetty_yclass_object *window_chrome,
                                                       float caption_height, float edge_size,
                                                       uint32_t flags);
struct yetty_ycore_void_result yetty_ychrome_set_size(struct yetty_yclass_object *obj, float width,
                                                      float height);
struct yetty_ycore_void_result yetty_ychrome_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ychrome_edge_cursor_at(struct yetty_yclass_object *obj, float x,
                                                           float y);
struct yetty_ydraw_drawable_list_result yetty_ychrome_render(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ychrome_handle_event(struct yetty_yclass_object *obj,
                                                         const struct yetty_yui_event *event);
typedef struct yetty_ycore_void_result (*yetty_ychrome_configure_fn)(struct yetty_yclass_object *,
                                                                     struct yetty_yclass_object *,
                                                                     float, float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ychrome_set_size_fn)(struct yetty_yclass_object *,
                                                                    float, float);
typedef struct yetty_ycore_void_result (*yetty_ychrome_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ychrome_edge_cursor_at_fn)(
    struct yetty_yclass_object *, float, float);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ychrome_render_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ychrome_handle_event_fn)(
    struct yetty_yclass_object *, const struct yetty_yui_event *);

YETTY_MAYBE_UNUSED
static yetty_ychrome_configure_fn yetty_ychrome_chrome_yetty_ychrome_configure_check =
    chrome_configure;
YETTY_MAYBE_UNUSED
static yetty_ychrome_set_size_fn yetty_ychrome_chrome_yetty_ychrome_set_size_check =
    chrome_set_size;
YETTY_MAYBE_UNUSED
static yetty_ychrome_destroy_fn yetty_ychrome_chrome_yetty_ychrome_destroy_check = chrome_destroy;
YETTY_MAYBE_UNUSED
static yetty_ychrome_edge_cursor_at_fn yetty_ychrome_chrome_yetty_ychrome_edge_cursor_at_check =
    chrome_edge_cursor_at;
YETTY_MAYBE_UNUSED
static yetty_ychrome_render_fn yetty_ychrome_chrome_yetty_ychrome_render_check = chrome_render;
YETTY_MAYBE_UNUSED
static yetty_ychrome_handle_event_fn yetty_ychrome_chrome_yetty_ychrome_handle_event_check =
    chrome_handle_event;

struct yetty_yclass_ptr_result yetty_ychrome_chrome_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ychrome_chrome");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ychrome_chrome",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ychrome_chrome),
        .data_align = _Alignof(struct yetty_ychrome_chrome),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ychrome", "configure", (yetty_yclass_method_id_t)yetty_ychrome_configure,
         (yetty_yclass_impl_t)chrome_configure},
        {"yetty_ychrome", "set_size", (yetty_yclass_method_id_t)yetty_ychrome_set_size,
         (yetty_yclass_impl_t)chrome_set_size},
        {"yetty_ychrome", "destroy", (yetty_yclass_method_id_t)yetty_ychrome_destroy,
         (yetty_yclass_impl_t)chrome_destroy},
        {"yetty_ychrome", "edge_cursor_at", (yetty_yclass_method_id_t)yetty_ychrome_edge_cursor_at,
         (yetty_yclass_impl_t)chrome_edge_cursor_at},
        {"yetty_ychrome", "render", (yetty_yclass_method_id_t)yetty_ychrome_render,
         (yetty_yclass_impl_t)chrome_render},
        {"yetty_ychrome", "handle_event", (yetty_yclass_method_id_t)yetty_ychrome_handle_event,
         (yetty_yclass_impl_t)chrome_handle_event},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ychrome_chrome_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ychrome_chrome_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ychrome_chrome_ptr_result yetty_ychrome_chrome_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ychrome_chrome_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ychrome_chrome_ptr, "yetty_ychrome_chrome_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ychrome_chrome_ptr, "yetty_ychrome_chrome_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ychrome_chrome_ptr, (struct yetty_ychrome_chrome *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ychrome_chrome_to(struct yetty_ychrome_chrome *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ychrome_chrome_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ychrome_chrome_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ychrome_chrome_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_ychrome_chrome_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ychrome_chrome_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ychrome_chrome");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ychrome_chrome_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ychrome_chrome_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ychrome_chrome_create: class accessor failed", class_accessor_r);
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
struct yetty_yclass_ptr_result yetty_ychrome_chrome_class_get(void);
struct yetty_ycore_void_result yetty_ychrome_register(void);

/* ---- ychrome: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ychrome_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ychrome_chrome") == 0) {
        return yetty_ychrome_chrome_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ychrome: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ychrome_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ychrome_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ychrome_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
