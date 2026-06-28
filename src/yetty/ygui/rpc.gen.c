/* GENERATED — do not edit. */
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yclass/class.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_ygui_framework_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_primitive_widget_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_widget_class_get(void);
size_t yetty_ygui_constructor_skel(const void *, size_t, void *, size_t);
size_t yetty_ygui_destructor_skel(const void *, size_t, void *, size_t);
size_t yetty_ygui_widget_on_press_skel(const void *, size_t, void *, size_t);
size_t yetty_ygui_widget_on_release_skel(const void *, size_t, void *, size_t);
size_t yetty_ygui_widget_on_motion_skel(const void *, size_t, void *, size_t);
size_t yetty_ygui_widget_on_scroll_skel(const void *, size_t, void *, size_t);
struct yetty_ycore_void_result yetty_ygui_mixins_register(void);
struct yetty_ycore_void_result yetty_ygui_widgets_register(void);
struct yetty_ycore_void_result yetty_ygui_register(void);

/* ---- ygui: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ygui_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ygui_framework") == 0) {
        return yetty_ygui_framework_class_get();
    }
    if (strcmp(name, "yetty_ygui_primitive_widget") == 0) {
        return yetty_ygui_primitive_widget_class_get();
    }
    if (strcmp(name, "yetty_ygui_widget") == 0) {
        return yetty_ygui_widget_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ygui: slot -> skel, name-keyed static data --------------- */

struct yetty_ygui_skel_row {
    const char *name;
    yetty_yclass_rpc_skel_fn fn;
};

static const struct yetty_ygui_skel_row yetty_ygui_skel_rows[] = {
    {"yetty_ygui_constructor", yetty_ygui_constructor_skel},
    {"yetty_ygui_destructor", yetty_ygui_destructor_skel},
    {"yetty_ygui_widget_on_press", yetty_ygui_widget_on_press_skel},
    {"yetty_ygui_widget_on_release", yetty_ygui_widget_on_release_skel},
    {"yetty_ygui_widget_on_motion", yetty_ygui_widget_on_motion_skel},
    {"yetty_ygui_widget_on_scroll", yetty_ygui_widget_on_scroll_skel},
};

/* Signature dictated by the skel-lookup hook contract; a miss is absorbed
 * into a NULL return at this boundary. */
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_ygui_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) {
        yetty_ycore_error_destroy(slot_name_r.error);
        return NULL;
    }
    const char *name = slot_name_r.value;
    for (size_t i = 0; i < sizeof(yetty_ygui_skel_rows) / sizeof(yetty_ygui_skel_rows[0]); ++i) {
        if (strcmp(yetty_ygui_skel_rows[i].name, name) == 0) {
            return yetty_ygui_skel_rows[i].fn;
        }
    }
    return NULL;
}

/* ---- ygui: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ygui_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ygui_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ygui_register: add_accessor_lookup");
    {
        struct yetty_ycore_void_result add_skel_r =
            yetty_yclass_rpc_add_skel_lookup(yetty_ygui_skel_lookup);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,
                            "yetty_ygui_register: rpc_add_skel_lookup");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_mixins_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_mixins");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_widgets_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_widgets");
    }
    registered = true;
    return YETTY_OK_VOID();
}
