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
struct yetty_ycore_void_result yetty_ymusic_configure(struct yetty_yclass_object *obj, float width,
                                                      float staff_space, uint32_t flags);
struct yetty_ycore_void_result yetty_ymusic_parse(struct yetty_yclass_object *obj,
                                                  const char *input, size_t len);
struct yetty_ydraw_drawable_list_result yetty_ymusic_render(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ymusic_hit_test(struct yetty_yclass_object *obj, float x,
                                                    float y);
struct yetty_ycore_void_result yetty_ymusic_set_highlight(struct yetty_yclass_object *obj,
                                                          int32_t element_id);
struct yetty_ycore_void_result yetty_ymusic_destroy(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_ymusic_configure_fn)(struct yetty_yclass_object *,
                                                                    float, float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ymusic_parse_fn)(struct yetty_yclass_object *,
                                                                const char *, size_t);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ymusic_render_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ymusic_hit_test_fn)(struct yetty_yclass_object *,
                                                                  float, float);
typedef struct yetty_ycore_void_result (*yetty_ymusic_set_highlight_fn)(
    struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_ymusic_destroy_fn)(struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_ymusic_configure_fn yetty_ymusic_music_yetty_ymusic_configure_check = music_configure;
YETTY_MAYBE_UNUSED
static yetty_ymusic_parse_fn yetty_ymusic_music_yetty_ymusic_parse_check = music_parse;
YETTY_MAYBE_UNUSED
static yetty_ymusic_render_fn yetty_ymusic_music_yetty_ymusic_render_check = music_render;
YETTY_MAYBE_UNUSED
static yetty_ymusic_hit_test_fn yetty_ymusic_music_yetty_ymusic_hit_test_check = music_hit_test;
YETTY_MAYBE_UNUSED
static yetty_ymusic_set_highlight_fn yetty_ymusic_music_yetty_ymusic_set_highlight_check =
    music_set_highlight;
YETTY_MAYBE_UNUSED
static yetty_ymusic_destroy_fn yetty_ymusic_music_yetty_ymusic_destroy_check = music_obj_destroy;

struct yetty_yclass_ptr_result yetty_ymusic_music_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ymusic_music");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ymusic_music",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ymusic_music),
        .data_align = _Alignof(struct yetty_ymusic_music),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ymusic", "configure", (yetty_yclass_method_id_t)yetty_ymusic_configure,
         (yetty_yclass_impl_t)music_configure},
        {"yetty_ymusic", "parse", (yetty_yclass_method_id_t)yetty_ymusic_parse,
         (yetty_yclass_impl_t)music_parse},
        {"yetty_ymusic", "render", (yetty_yclass_method_id_t)yetty_ymusic_render,
         (yetty_yclass_impl_t)music_render},
        {"yetty_ymusic", "hit_test", (yetty_yclass_method_id_t)yetty_ymusic_hit_test,
         (yetty_yclass_impl_t)music_hit_test},
        {"yetty_ymusic", "set_highlight", (yetty_yclass_method_id_t)yetty_ymusic_set_highlight,
         (yetty_yclass_impl_t)music_set_highlight},
        {"yetty_ymusic", "destroy", (yetty_yclass_method_id_t)yetty_ymusic_destroy,
         (yetty_yclass_impl_t)music_obj_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ymusic_music_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ymusic_music_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ymusic_music_ptr_result yetty_ymusic_music_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ymusic_music_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ymusic_music_ptr, "yetty_ymusic_music_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ymusic_music_ptr, "yetty_ymusic_music_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ymusic_music_ptr, (struct yetty_ymusic_music *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ymusic_music_to(struct yetty_ymusic_music *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ymusic_music_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ymusic_music_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ymusic_music_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_ymusic_music_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ymusic_music_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ymusic_music");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ymusic_music_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ymusic_music_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ymusic_music_create: class accessor failed", class_accessor_r);
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
struct yetty_yclass_ptr_result yetty_ymusic_music_class_get(void);
struct yetty_ycore_void_result yetty_ymusic_register(void);

/* ---- ymusic: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ymusic_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ymusic_music") == 0) {
        return yetty_ymusic_music_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ymusic: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ymusic_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ymusic_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ymusic_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
