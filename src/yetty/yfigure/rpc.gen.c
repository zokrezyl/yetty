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
struct yetty_yclass_ptr_result yetty_yfigure_container_class_get(void);
struct yetty_yclass_ptr_result yetty_yfigure_figure_class_get(void);
size_t yetty_yfigure_constructor_skel(const void *, size_t, void *, size_t);
size_t yetty_yfigure_add_child_skel(const void *, size_t, void *, size_t);
size_t yetty_yfigure_remove_child_by_id_skel(const void *, size_t, void *, size_t);
size_t yetty_yfigure_raise_child_by_id_skel(const void *, size_t, void *, size_t);
size_t yetty_yfigure_process_records_skel(const void *, size_t, void *, size_t);
struct yetty_ycore_void_result yetty_yfigure_register(void);

/* ---- yfigure: class name → accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yfigure_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yfigure_container") == 0) {
        return yetty_yfigure_container_class_get();
    }
    if (strcmp(name, "yetty_yfigure_figure") == 0) {
        return yetty_yfigure_figure_class_get();
    }
    /* "Not mine": OK with NULL value — yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yfigure: slot → skel, name-keyed static data --------------- */

struct yetty_yfigure_skel_row {
    const char *name;
    yetty_yclass_rpc_skel_fn fn;
};

static const struct yetty_yfigure_skel_row yetty_yfigure_skel_rows[] = {
    {"yetty_yfigure_constructor", yetty_yfigure_constructor_skel},
    {"yetty_yfigure_add_child", yetty_yfigure_add_child_skel},
    {"yetty_yfigure_remove_child_by_id", yetty_yfigure_remove_child_by_id_skel},
    {"yetty_yfigure_raise_child_by_id", yetty_yfigure_raise_child_by_id_skel},
    {"yetty_yfigure_process_records", yetty_yfigure_process_records_skel}};

/* Signature is dictated by the skel-lookup hook contract (registered as a
 * fn-pointer via yetty_yclass_rpc_add_skel_lookup); a slot-name lookup
 * failure is absorbed into a NULL return at this boundary. */
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_yfigure_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) {
        yetty_ycore_error_destroy(slot_name_r.error);
        return NULL;
    }
    const char *name = slot_name_r.value;
    for (size_t i = 0; i < sizeof(yetty_yfigure_skel_rows) / sizeof(yetty_yfigure_skel_rows[0]);
         ++i) {
        if (strcmp(yetty_yfigure_skel_rows[i].name, name) == 0) {
            return yetty_yfigure_skel_rows[i].fn;
        }
    }
    return NULL;
}

/* ---- yfigure: explicit yclass-RPC hook registration ------------- */

/* Installs this module's server-side discovery hooks: the accessor
 * lookup feeds yetty_yclass_by_name()'s registry-miss path, and (when
 * the module exposes wire methods) the skel lookup feeds RPC skeleton
 * dispatch. Call once when the yclass RPC / remote-object server is
 * brought up — idempotent, so repeated calls (several hosts, re-init)
 * are no-ops. This replaces the former load-time installer: a module
 * merely being linked no longer mutates global state before main(),
 * and there is no abort() path on a constructor. */
struct yetty_ycore_void_result yetty_yfigure_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yfigure_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yfigure_register: add_accessor_lookup");
    {
        struct yetty_ycore_void_result add_skel_r =
            yetty_yclass_rpc_add_skel_lookup(yetty_yfigure_skel_lookup);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,
                            "yetty_yfigure_register: rpc_add_skel_lookup");
    }
    registered = true;
    return YETTY_OK_VOID();
}
