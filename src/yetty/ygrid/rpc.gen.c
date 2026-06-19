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
struct yetty_yclass_ptr_result yetty_ygrid_grid_class_get(void) __attribute__((weak));
size_t yetty_ygrid_add_record_skel(const void *, size_t, void *, size_t) __attribute__((weak));
size_t yetty_ygrid_clear_skel(const void *, size_t, void *, size_t) __attribute__((weak));
size_t yetty_ygrid_destroy_skel(const void *, size_t, void *, size_t) __attribute__((weak));
struct yetty_ycore_void_result yetty_ygrid_register(void);

/* ---- ygrid: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ygrid_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ygrid_grid") == 0 && yetty_ygrid_grid_class_get) {
        return yetty_ygrid_grid_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ygrid: slot -> skel, name-keyed static data --------------- */

struct yetty_ygrid_skel_row {
    const char *name;
    yetty_yclass_rpc_skel_fn fn;
};

static const struct yetty_ygrid_skel_row yetty_ygrid_skel_rows[] = {
    {"yetty_ygrid_add_record", yetty_ygrid_add_record_skel},
    {"yetty_ygrid_clear", yetty_ygrid_clear_skel},
    {"yetty_ygrid_destroy", yetty_ygrid_destroy_skel},
};

/* Signature dictated by the skel-lookup hook contract; a miss is absorbed
 * into a NULL return at this boundary. */
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_ygrid_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) {
        yetty_ycore_error_destroy(slot_name_r.error);
        return NULL;
    }
    const char *name = slot_name_r.value;
    for (size_t i = 0; i < sizeof(yetty_ygrid_skel_rows) / sizeof(yetty_ygrid_skel_rows[0]); ++i) {
        /* .fn may be a weak ref (NULL when its class isn't linked); skip it. */
        if (yetty_ygrid_skel_rows[i].fn && strcmp(yetty_ygrid_skel_rows[i].name, name) == 0) {
            return yetty_ygrid_skel_rows[i].fn;
        }
    }
    return NULL;
}

/* ---- ygrid: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ygrid_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ygrid_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ygrid_register: add_accessor_lookup");
    {
        struct yetty_ycore_void_result add_skel_r =
            yetty_yclass_rpc_add_skel_lookup(yetty_ygrid_skel_lookup);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,
                            "yetty_ygrid_register: rpc_add_skel_lookup");
    }
    registered = true;
    return YETTY_OK_VOID();
}
