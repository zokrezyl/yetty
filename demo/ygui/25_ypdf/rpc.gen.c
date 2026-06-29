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
struct yetty_yclass_ptr_result yetty_demoygui_25_ypdf_class_get(void);
struct yetty_ycore_void_result yetty_demoygui_25_ypdf_register(void);

/* ---- demoygui_25_ypdf: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_demoygui_25_ypdf_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_demoygui_25_ypdf") == 0) {
        return yetty_demoygui_25_ypdf_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- demoygui_25_ypdf: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_demoygui_25_ypdf_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_demoygui_25_ypdf_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_demoygui_25_ypdf_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
