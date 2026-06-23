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
#ifdef YETTY_PLATFORM_ANDROID
struct yetty_yclass_ptr_result yetty_yplatform_android_clipboard_class_get(void);
#endif
struct yetty_yclass_ptr_result yetty_yplatform_clipboard_class_get(void) __attribute__((weak));
#ifdef YETTY_PLATFORM_GLFW
struct yetty_yclass_ptr_result yetty_yplatform_glfw_clipboard_class_get(void);
#endif
#ifdef YETTY_PLATFORM_IOS
struct yetty_yclass_ptr_result yetty_yplatform_ios_clipboard_class_get(void);
#endif
#ifdef YETTY_PLATFORM_WEBASM
struct yetty_yclass_ptr_result yetty_yplatform_webasm_clipboard_class_get(void);
#endif
struct yetty_ycore_void_result yetty_yplatform_yclipboard_register(void);

/* ---- yplatform_yclipboard: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yplatform_yclipboard_accessor_lookup(const char *name)
{
#ifdef YETTY_PLATFORM_ANDROID
    if (strcmp(name, "yetty_yplatform_android_clipboard") == 0)
        return yetty_yplatform_android_clipboard_class_get();
#endif
    if (strcmp(name, "yetty_yplatform_clipboard") == 0 && yetty_yplatform_clipboard_class_get)
        return yetty_yplatform_clipboard_class_get();
#ifdef YETTY_PLATFORM_GLFW
    if (strcmp(name, "yetty_yplatform_glfw_clipboard") == 0)
        return yetty_yplatform_glfw_clipboard_class_get();
#endif
#ifdef YETTY_PLATFORM_IOS
    if (strcmp(name, "yetty_yplatform_ios_clipboard") == 0)
        return yetty_yplatform_ios_clipboard_class_get();
#endif
#ifdef YETTY_PLATFORM_WEBASM
    if (strcmp(name, "yetty_yplatform_webasm_clipboard") == 0)
        return yetty_yplatform_webasm_clipboard_class_get();
#endif
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yplatform_yclipboard: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yplatform_yclipboard_register(void)
{
    static bool registered = false;
    if (registered)
        return YETTY_OK_VOID();

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yplatform_yclipboard_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yplatform_yclipboard_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
