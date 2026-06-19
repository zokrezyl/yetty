/* GENERATED — do not edit. */
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yclass/class.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Forward decls. A class tagged platform@<x> is guarded by
 * #ifdef YETTY_PLATFORM_<X> (registered only on that platform, where
 * CMake compiles it); a cross-platform class is a WEAK ref so the
 * lookup table never force-links an unused class into a minimal
 * consumer. The chained submodule registers are weak externs. */
struct yetty_ycore_void_result yetty_yplatform_yclipboard_register(void) __attribute__((weak));
struct yetty_ycore_void_result yetty_yplatform_yplatform_register(void) __attribute__((weak));
struct yetty_ycore_void_result yetty_yplatform_ywindow_register(void) __attribute__((weak));
struct yetty_ycore_void_result yetty_yplatform_ywindow_chrome_register(void) __attribute__((weak));
struct yetty_ycore_void_result yetty_yplatform_register(void);


/* ---- yplatform: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yplatform_register(void)
{
    static bool registered = false;
    if (registered)
        return YETTY_OK_VOID();

    /* Weak: the submodule's rpc.gen.c may not be compiled for this
     * platform — register it only when it is actually linked. */
    if (yetty_yplatform_yclipboard_register) {
        struct yetty_ycore_void_result sub_r = yetty_yplatform_yclipboard_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yplatform_register: submodule yplatform_yclipboard");
    }
    /* Weak: the submodule's rpc.gen.c may not be compiled for this
     * platform — register it only when it is actually linked. */
    if (yetty_yplatform_yplatform_register) {
        struct yetty_ycore_void_result sub_r = yetty_yplatform_yplatform_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yplatform_register: submodule yplatform_yplatform");
    }
    /* Weak: the submodule's rpc.gen.c may not be compiled for this
     * platform — register it only when it is actually linked. */
    if (yetty_yplatform_ywindow_register) {
        struct yetty_ycore_void_result sub_r = yetty_yplatform_ywindow_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yplatform_register: submodule yplatform_ywindow");
    }
    /* Weak: the submodule's rpc.gen.c may not be compiled for this
     * platform — register it only when it is actually linked. */
    if (yetty_yplatform_ywindow_chrome_register) {
        struct yetty_ycore_void_result sub_r = yetty_yplatform_ywindow_chrome_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yplatform_register: submodule yplatform_ywindow_chrome");
    }
    registered = true;
    return YETTY_OK_VOID();
}
