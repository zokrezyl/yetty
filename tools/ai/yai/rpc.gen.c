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
struct yetty_yclass_ptr_result yetty_yai_claude_class_get(void) __attribute__((weak));
struct yetty_yclass_ptr_result yetty_yai_codex_class_get(void) __attribute__((weak));
struct yetty_yclass_ptr_result yetty_yai_emacs_class_get(void) __attribute__((weak));
struct yetty_yclass_ptr_result yetty_yai_vi_class_get(void) __attribute__((weak));
struct yetty_yclass_ptr_result yetty_yai_editor_class_get(void) __attribute__((weak));
struct yetty_yclass_ptr_result yetty_yai_engine_class_get(void) __attribute__((weak));
struct yetty_yclass_ptr_result yetty_yai_gemini_class_get(void) __attribute__((weak));
struct yetty_yclass_ptr_result yetty_yai_turn_engine_class_get(void) __attribute__((weak));
struct yetty_ycore_void_result yetty_yai_register(void);

/* ---- yai: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yai_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yai_claude") == 0 && yetty_yai_claude_class_get)
        return yetty_yai_claude_class_get();
    if (strcmp(name, "yetty_yai_codex") == 0 && yetty_yai_codex_class_get)
        return yetty_yai_codex_class_get();
    if (strcmp(name, "yetty_yai_emacs") == 0 && yetty_yai_emacs_class_get)
        return yetty_yai_emacs_class_get();
    if (strcmp(name, "yetty_yai_vi") == 0 && yetty_yai_vi_class_get)
        return yetty_yai_vi_class_get();
    if (strcmp(name, "yetty_yai_editor") == 0 && yetty_yai_editor_class_get)
        return yetty_yai_editor_class_get();
    if (strcmp(name, "yetty_yai_engine") == 0 && yetty_yai_engine_class_get)
        return yetty_yai_engine_class_get();
    if (strcmp(name, "yetty_yai_gemini") == 0 && yetty_yai_gemini_class_get)
        return yetty_yai_gemini_class_get();
    if (strcmp(name, "yetty_yai_turn_engine") == 0 && yetty_yai_turn_engine_class_get)
        return yetty_yai_turn_engine_class_get();
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yai: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yai_register(void)
{
    static bool registered = false;
    if (registered)
        return YETTY_OK_VOID();

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yai_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yai_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
