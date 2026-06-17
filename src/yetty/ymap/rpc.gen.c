/* GENERATED — do not edit. */
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yclass/class.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Forward decls — these accessors and skels are defined in each
 * class's own <stem>.gen.c; the lookup tables below name them
 * across translation units. */
struct yetty_yclass_ptr_result yetty_ymap_map_class_get(void);
struct yetty_ycore_void_result yetty_ymap_register(void);

/* ---- ymap: class name → accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ymap_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ymap_map") == 0) {
        return yetty_ymap_map_class_get();
    }
    /* "Not mine": OK with NULL value — yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ymap: explicit yclass-RPC hook registration ------------- */

/* Installs this module's server-side discovery hooks: the accessor
 * lookup feeds yetty_yclass_by_name()'s registry-miss path, and (when
 * the module exposes wire methods) the skel lookup feeds RPC skeleton
 * dispatch. Call once when the yclass RPC / remote-object server is
 * brought up — idempotent, so repeated calls (several hosts, re-init)
 * are no-ops. This replaces the former load-time installer: a module
 * merely being linked no longer mutates global state before main(),
 * and there is no abort() path on a constructor. */
struct yetty_ycore_void_result yetty_ymap_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ymap_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ymap_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
