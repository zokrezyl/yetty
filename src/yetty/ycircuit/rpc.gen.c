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
struct yetty_yclass_ptr_result yetty_ycircuit_circuit_class_get(void) __attribute__((weak));
struct yetty_ycore_void_result yetty_ycircuit_register(void);

/* ---- ycircuit: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ycircuit_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ycircuit_circuit") == 0 && yetty_ycircuit_circuit_class_get) {
        return yetty_ycircuit_circuit_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ycircuit: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ycircuit_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ycircuit_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ycircuit_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
