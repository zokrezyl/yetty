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
struct yetty_ycore_int_result;
struct yetty_ycore_uint64_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;
struct yetty_ycore_void_result yetty_yflame_configure(struct yetty_yclass_object *obj, float width,
                                                      float frame_height, float min_width,
                                                      uint32_t flags);
struct yetty_ycore_void_result yetty_yflame_parse(struct yetty_yclass_object *obj,
                                                  const char *input, size_t len);
struct yetty_ydraw_drawable_list_result yetty_yflame_render(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_yflame_hit_test(struct yetty_yclass_object *obj, float x,
                                                    float y);
struct yetty_ycore_void_result yetty_yflame_focus(struct yetty_yclass_object *obj, int32_t node_id);
struct yetty_ycore_void_result yetty_yflame_focus_parent(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yflame_reset(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yflame_set_highlight(struct yetty_yclass_object *obj,
                                                          int32_t node_id);
struct yetty_ycore_void_result yetty_yflame_highlight_name(struct yetty_yclass_object *obj,
                                                           const char *name, size_t len);
struct yetty_ycore_void_result yetty_yflame_focus_name(struct yetty_yclass_object *obj,
                                                       const char *name, size_t len);
struct yetty_ycore_void_result yetty_yflame_set_baseline(struct yetty_yclass_object *obj,
                                                         const char *folded, size_t len);
struct yetty_ycore_const_char_ptr_result yetty_yflame_node_name(struct yetty_yclass_object *obj,
                                                                int32_t id);
struct yetty_ycore_uint64_result yetty_yflame_node_value(struct yetty_yclass_object *obj,
                                                         int32_t id);
struct yetty_ycore_uint64_result yetty_yflame_root_value(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yflame_destroy(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_yflame_configure_fn)(struct yetty_yclass_object *,
                                                                    float, float, float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_parse_fn)(struct yetty_yclass_object *,
                                                                const char *, size_t);
typedef struct yetty_ydraw_drawable_list_result (*yetty_yflame_render_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_yflame_hit_test_fn)(struct yetty_yclass_object *,
                                                                  float, float);
typedef struct yetty_ycore_void_result (*yetty_yflame_focus_fn)(struct yetty_yclass_object *,
                                                                int32_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_focus_parent_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yflame_reset_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yflame_set_highlight_fn)(
    struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_highlight_name_fn)(
    struct yetty_yclass_object *, const char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_focus_name_fn)(struct yetty_yclass_object *,
                                                                     const char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_set_baseline_fn)(struct yetty_yclass_object *,
                                                                       const char *, size_t);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yflame_node_name_fn)(
    struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_uint64_result (*yetty_yflame_node_value_fn)(struct yetty_yclass_object *,
                                                                       int32_t);
typedef struct yetty_ycore_uint64_result (*yetty_yflame_root_value_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yflame_destroy_fn)(struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_yflame_configure_fn yetty_yflame_flame_yetty_yflame_configure_flame_configure_check =
    flame_configure;
YETTY_MAYBE_UNUSED
static yetty_yflame_parse_fn yetty_yflame_flame_yetty_yflame_parse_flame_parse_check = flame_parse;
YETTY_MAYBE_UNUSED
static yetty_yflame_render_fn yetty_yflame_flame_yetty_yflame_render_flame_render_check =
    flame_render;
YETTY_MAYBE_UNUSED
static yetty_yflame_hit_test_fn yetty_yflame_flame_yetty_yflame_hit_test_flame_hit_test_check =
    flame_hit_test;
YETTY_MAYBE_UNUSED
static yetty_yflame_focus_fn yetty_yflame_flame_yetty_yflame_focus_flame_focus_check = flame_focus;
YETTY_MAYBE_UNUSED
static yetty_yflame_focus_parent_fn
    yetty_yflame_flame_yetty_yflame_focus_parent_flame_focus_parent_check = flame_focus_parent;
YETTY_MAYBE_UNUSED
static yetty_yflame_reset_fn yetty_yflame_flame_yetty_yflame_reset_flame_reset_check = flame_reset;
YETTY_MAYBE_UNUSED
static yetty_yflame_set_highlight_fn
    yetty_yflame_flame_yetty_yflame_set_highlight_flame_set_highlight_check = flame_set_highlight;
YETTY_MAYBE_UNUSED
static yetty_yflame_highlight_name_fn
    yetty_yflame_flame_yetty_yflame_highlight_name_flame_highlight_name_check =
        flame_highlight_name;
YETTY_MAYBE_UNUSED
static yetty_yflame_focus_name_fn
    yetty_yflame_flame_yetty_yflame_focus_name_flame_focus_name_check = flame_focus_name;
YETTY_MAYBE_UNUSED
static yetty_yflame_set_baseline_fn
    yetty_yflame_flame_yetty_yflame_set_baseline_flame_set_baseline_check = flame_set_baseline;
YETTY_MAYBE_UNUSED
static yetty_yflame_node_name_fn yetty_yflame_flame_yetty_yflame_node_name_flame_node_name_check =
    flame_node_name;
YETTY_MAYBE_UNUSED
static yetty_yflame_node_value_fn
    yetty_yflame_flame_yetty_yflame_node_value_flame_node_value_check = flame_node_value;
YETTY_MAYBE_UNUSED
static yetty_yflame_root_value_fn
    yetty_yflame_flame_yetty_yflame_root_value_flame_root_value_check = flame_root_value;
YETTY_MAYBE_UNUSED
static yetty_yflame_destroy_fn yetty_yflame_flame_yetty_yflame_destroy_flame_obj_destroy_check =
    flame_obj_destroy;

struct yetty_yclass_ptr_result yetty_yflame_flame_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yflame_flame");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yflame_flame",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yflame_flame),
        .data_align = _Alignof(struct yetty_yflame_flame),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yflame", "configure", (yetty_yclass_method_id_t)yetty_yflame_configure,
         (yetty_yclass_impl_t)flame_configure},
        {"yetty_yflame", "parse", (yetty_yclass_method_id_t)yetty_yflame_parse,
         (yetty_yclass_impl_t)flame_parse},
        {"yetty_yflame", "render", (yetty_yclass_method_id_t)yetty_yflame_render,
         (yetty_yclass_impl_t)flame_render},
        {"yetty_yflame", "hit_test", (yetty_yclass_method_id_t)yetty_yflame_hit_test,
         (yetty_yclass_impl_t)flame_hit_test},
        {"yetty_yflame", "focus", (yetty_yclass_method_id_t)yetty_yflame_focus,
         (yetty_yclass_impl_t)flame_focus},
        {"yetty_yflame", "focus_parent", (yetty_yclass_method_id_t)yetty_yflame_focus_parent,
         (yetty_yclass_impl_t)flame_focus_parent},
        {"yetty_yflame", "reset", (yetty_yclass_method_id_t)yetty_yflame_reset,
         (yetty_yclass_impl_t)flame_reset},
        {"yetty_yflame", "set_highlight", (yetty_yclass_method_id_t)yetty_yflame_set_highlight,
         (yetty_yclass_impl_t)flame_set_highlight},
        {"yetty_yflame", "highlight_name", (yetty_yclass_method_id_t)yetty_yflame_highlight_name,
         (yetty_yclass_impl_t)flame_highlight_name},
        {"yetty_yflame", "focus_name", (yetty_yclass_method_id_t)yetty_yflame_focus_name,
         (yetty_yclass_impl_t)flame_focus_name},
        {"yetty_yflame", "set_baseline", (yetty_yclass_method_id_t)yetty_yflame_set_baseline,
         (yetty_yclass_impl_t)flame_set_baseline},
        {"yetty_yflame", "node_name", (yetty_yclass_method_id_t)yetty_yflame_node_name,
         (yetty_yclass_impl_t)flame_node_name},
        {"yetty_yflame", "node_value", (yetty_yclass_method_id_t)yetty_yflame_node_value,
         (yetty_yclass_impl_t)flame_node_value},
        {"yetty_yflame", "root_value", (yetty_yclass_method_id_t)yetty_yflame_root_value,
         (yetty_yclass_impl_t)flame_root_value},
        {"yetty_yflame", "destroy", (yetty_yclass_method_id_t)yetty_yflame_destroy,
         (yetty_yclass_impl_t)flame_obj_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yflame_flame_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yflame_flame_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yflame_flame_ptr_result yetty_yflame_flame_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yflame_flame_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yflame_flame_ptr, "yetty_yflame_flame_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yflame_flame_ptr, "yetty_yflame_flame_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yflame_flame_ptr, (struct yetty_yflame_flame *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yflame_flame_to(struct yetty_yflame_flame *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_yflame_flame_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_yflame_flame_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yflame_flame_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_yflame_flame_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yflame_flame_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yflame_flame");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_yflame_flame_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yflame_flame_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yflame_flame_create: class accessor failed", class_accessor_r);
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
struct yetty_yclass_ptr_result yetty_yflame_flame_class_get(void);
struct yetty_ycore_void_result yetty_yflame_register(void);

/* ---- yflame: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yflame_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yflame_flame") == 0) {
        return yetty_yflame_flame_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yflame: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yflame_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yflame_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yflame_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
