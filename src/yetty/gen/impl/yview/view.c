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
struct yetty_ydraw_drawable_list;
struct yetty_ycore_void_result yetty_yview_configure(struct yetty_yclass_object *obj, int fd,
                                                     uint32_t child_id, uint32_t kind,
                                                     uint32_t bg_color, float min_x, float min_y,
                                                     float max_x, float max_y);
struct yetty_ycore_void_result yetty_yview_set_content(
    struct yetty_yclass_object *obj, const struct yetty_ydraw_drawable_list *content);
struct yetty_ycore_void_result yetty_yview_set_text(struct yetty_yclass_object *obj,
                                                    const char *text, float font_size);
struct yetty_ycore_void_result yetty_yview_set_plot(struct yetty_yclass_object *obj,
                                                    const char *expr, float x_min, float x_max,
                                                    float y_min, float y_max);
struct yetty_ycore_void_result yetty_yview_set_content_size(struct yetty_yclass_object *obj,
                                                            float content_w, float content_h);
struct yetty_ycore_void_result yetty_yview_scroll_to(struct yetty_yclass_object *obj,
                                                     float scroll_x, float scroll_y);
struct yetty_ycore_void_result yetty_yview_scroll_by(struct yetty_yclass_object *obj, float delta_x,
                                                     float delta_y);
struct yetty_ycore_void_result yetty_yview_set_rect(struct yetty_yclass_object *obj, float min_x,
                                                    float min_y, float max_x, float max_y);
struct yetty_ycore_void_result yetty_yview_destroy(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_yview_configure_fn)(struct yetty_yclass_object *,
                                                                   int, uint32_t, uint32_t,
                                                                   uint32_t, float, float, float,
                                                                   float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_content_fn)(
    struct yetty_yclass_object *, const struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_void_result (*yetty_yview_set_text_fn)(struct yetty_yclass_object *,
                                                                  const char *, float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_plot_fn)(struct yetty_yclass_object *,
                                                                  const char *, float, float, float,
                                                                  float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_content_size_fn)(
    struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_scroll_to_fn)(struct yetty_yclass_object *,
                                                                   float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_scroll_by_fn)(struct yetty_yclass_object *,
                                                                   float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_rect_fn)(struct yetty_yclass_object *,
                                                                  float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_destroy_fn)(struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_yview_configure_fn yetty_yview_view_yetty_yview_configure_check = view_configure;
YETTY_MAYBE_UNUSED
static yetty_yview_set_content_fn yetty_yview_view_yetty_yview_set_content_check = view_set_content;
YETTY_MAYBE_UNUSED
static yetty_yview_set_text_fn yetty_yview_view_yetty_yview_set_text_check = view_set_text;
YETTY_MAYBE_UNUSED
static yetty_yview_set_plot_fn yetty_yview_view_yetty_yview_set_plot_check = view_set_plot;
YETTY_MAYBE_UNUSED
static yetty_yview_set_content_size_fn yetty_yview_view_yetty_yview_set_content_size_check =
    view_set_content_size;
YETTY_MAYBE_UNUSED
static yetty_yview_scroll_to_fn yetty_yview_view_yetty_yview_scroll_to_check = view_scroll_to;
YETTY_MAYBE_UNUSED
static yetty_yview_scroll_by_fn yetty_yview_view_yetty_yview_scroll_by_check = view_scroll_by;
YETTY_MAYBE_UNUSED
static yetty_yview_set_rect_fn yetty_yview_view_yetty_yview_set_rect_check = view_set_rect;
YETTY_MAYBE_UNUSED
static yetty_yview_destroy_fn yetty_yview_view_yetty_yview_destroy_check = view_destroy;

struct yetty_yclass_ptr_result yetty_yview_view_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yview_view");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yview_view",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yview_view),
        .data_align = _Alignof(struct yetty_yview_view),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yview", "configure", (yetty_yclass_method_id_t)yetty_yview_configure,
         (yetty_yclass_impl_t)view_configure},
        {"yetty_yview", "set_content", (yetty_yclass_method_id_t)yetty_yview_set_content,
         (yetty_yclass_impl_t)view_set_content},
        {"yetty_yview", "set_text", (yetty_yclass_method_id_t)yetty_yview_set_text,
         (yetty_yclass_impl_t)view_set_text},
        {"yetty_yview", "set_plot", (yetty_yclass_method_id_t)yetty_yview_set_plot,
         (yetty_yclass_impl_t)view_set_plot},
        {"yetty_yview", "set_content_size", (yetty_yclass_method_id_t)yetty_yview_set_content_size,
         (yetty_yclass_impl_t)view_set_content_size},
        {"yetty_yview", "scroll_to", (yetty_yclass_method_id_t)yetty_yview_scroll_to,
         (yetty_yclass_impl_t)view_scroll_to},
        {"yetty_yview", "scroll_by", (yetty_yclass_method_id_t)yetty_yview_scroll_by,
         (yetty_yclass_impl_t)view_scroll_by},
        {"yetty_yview", "set_rect", (yetty_yclass_method_id_t)yetty_yview_set_rect,
         (yetty_yclass_impl_t)view_set_rect},
        {"yetty_yview", "destroy", (yetty_yclass_method_id_t)yetty_yview_destroy,
         (yetty_yclass_impl_t)view_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yview_view_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yview_view_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yview_view_ptr_result yetty_yview_view_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yview_view_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yview_view_ptr, "yetty_yview_view_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yview_view_ptr, "yetty_yview_view_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yview_view_ptr, (struct yetty_yview_view *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yview_view_to(struct yetty_yview_view *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_yview_view_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_yview_view_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yview_view_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_yview_view_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yview_view_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yview_view");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_yview_view_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yview_view_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yview_view_create: class accessor failed",
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
struct yetty_yclass_ptr_result yetty_yview_view_class_get(void);
struct yetty_ycore_void_result yetty_yview_register(void);

/* ---- yview: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yview_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yview_view") == 0) {
        return yetty_yview_view_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yview: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yview_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yview_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yview_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
