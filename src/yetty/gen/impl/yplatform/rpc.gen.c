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
struct yetty_ycore_void_result yetty_yplatform_yclipboard_register(void);
struct yetty_ycore_void_result yetty_yplatform_yplatform_register(void);
struct yetty_ycore_void_result yetty_yplatform_ywindow_register(void);
struct yetty_ycore_void_result yetty_yplatform_ywindow_chrome_register(void);
struct yetty_ycore_void_result yetty_yplatform_register(void);


/* ---- yplatform: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yplatform_register(void)
{
    static bool registered = false;
    if (registered)
        return YETTY_OK_VOID();

    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yplatform_yclipboard_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yplatform_register: submodule yplatform_yclipboard");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yplatform_yplatform_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yplatform_register: submodule yplatform_yplatform");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yplatform_ywindow_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yplatform_register: submodule yplatform_ywindow");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yplatform_ywindow_chrome_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yplatform_register: submodule yplatform_ywindow_chrome");
    }
    registered = true;
    return YETTY_OK_VOID();
}
