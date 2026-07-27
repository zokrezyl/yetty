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
struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_object *obj, float x,
                                                         float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_object *obj, float x,
                                                           float y, int button);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_press_fn)(struct yetty_yclass_object *,
                                                                       float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_release_fn)(
    struct yetty_yclass_object *, float, float, int);

YETTY_MAYBE_UNUSED
static yetty_ygui_widget_on_press_fn yetty_ygui_clickable_yetty_ygui_widget_on_press_check =
    clickable_on_press;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_on_release_fn yetty_ygui_clickable_yetty_ygui_widget_on_release_check =
    clickable_on_release;

struct yetty_yclass_ptr_result yetty_ygui_clickable_mixin_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ygui_clickable");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_clickable",
        .type = YETTY_YCLASS_TYPE_MIXIN,
        .data_size = sizeof(struct yetty_ygui_clickable),
        .data_align = _Alignof(struct yetty_ygui_clickable),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "widget_on_press", (yetty_yclass_method_id_t)yetty_ygui_widget_on_press,
         (yetty_yclass_impl_t)clickable_on_press},
        {"yetty_ygui", "widget_on_release", (yetty_yclass_method_id_t)yetty_ygui_widget_on_release,
         (yetty_yclass_impl_t)clickable_on_release},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ygui_clickable_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_clickable_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygui_clickable_ptr_result yetty_ygui_clickable_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ygui_clickable_mixin_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ygui_clickable_ptr, "yetty_ygui_clickable_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ygui_clickable_ptr, "yetty_ygui_clickable_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ygui_clickable_ptr, (struct yetty_ygui_clickable *)slice_r.value);
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_ygui_clickable_mixin_get(void);
struct yetty_ycore_void_result yetty_ygui_clickable_register(void);

/* ---- ygui_clickable: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ygui_clickable_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ygui_clickable") == 0) {
        return yetty_ygui_clickable_mixin_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ygui_clickable: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ygui_clickable_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ygui_clickable_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ygui_clickable_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
