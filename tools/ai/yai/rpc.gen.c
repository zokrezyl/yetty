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
struct yetty_yclass_ptr_result yetty_yai_claude_class_get(void);
struct yetty_yclass_ptr_result yetty_yai_codex_class_get(void);
struct yetty_yclass_ptr_result yetty_yai_emacs_class_get(void);
struct yetty_yclass_ptr_result yetty_yai_vi_class_get(void);
struct yetty_yclass_ptr_result yetty_yai_editor_class_get(void);
struct yetty_yclass_ptr_result yetty_yai_engine_class_get(void);
struct yetty_yclass_ptr_result yetty_yai_gemini_class_get(void);
struct yetty_yclass_ptr_result yetty_yai_turn_engine_class_get(void);
struct yetty_ycore_void_result yetty_yai_register(void);

/* ---- yai: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yai_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yai_claude") == 0) {
        return yetty_yai_claude_class_get();
    }
    if (strcmp(name, "yetty_yai_codex") == 0) {
        return yetty_yai_codex_class_get();
    }
    if (strcmp(name, "yetty_yai_emacs") == 0) {
        return yetty_yai_emacs_class_get();
    }
    if (strcmp(name, "yetty_yai_vi") == 0) {
        return yetty_yai_vi_class_get();
    }
    if (strcmp(name, "yetty_yai_editor") == 0) {
        return yetty_yai_editor_class_get();
    }
    if (strcmp(name, "yetty_yai_engine") == 0) {
        return yetty_yai_engine_class_get();
    }
    if (strcmp(name, "yetty_yai_gemini") == 0) {
        return yetty_yai_gemini_class_get();
    }
    if (strcmp(name, "yetty_yai_turn_engine") == 0) {
        return yetty_yai_turn_engine_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yai: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yai_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yai_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yai_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
