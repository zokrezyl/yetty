/*
 * ygui-class.c — convenience wrappers over yclass.
 *
 * ygui owns nothing of its own here: the slot allocator, dispatch
 * table, parent / mixin chain walk, and method-id registry all live
 * in `<yclass/class.h>`. This file:
 *   - resolves an ygui method id to its yclass slot (without
 *     allocating — the slot must already have been registered, which
 *     happens at class registration time inside the codegen-emitted
 *     accessor),
 *   - delegates dispatch lookup,
 *   - walks the parent chain for super dispatch.
 *
 * The historical `struct yetty_ygui_class` wrapper is gone; classes
 * registered through the codegen-emitted accessors hold their identity
 * directly as `const struct yetty_yclass *`.
 */

#include <yetty/ygui/class.h>

#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

yetty_yclass_method_slot yetty_ygui_method_slot_get(yetty_yclass_method_id_t method_id)
{
    if (!method_id) {
        return YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    }
    struct yetty_yclass_method_slot_result r =
        yetty_yclass_method_slot_get(YETTY_YGUI_DOMAIN, method_id);
    if (YETTY_IS_ERR(r)) {
        /* Not registered yet — caller treats UNDEFINED as "no impl",
         * matching the historical ygui contract that a never-overridden
         * method silently no-ops. */
        yetty_ycore_error_destroy(r.error);
        return YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    }
    return r.value;
}

yetty_yclass_impl_t yetty_ygui_dispatch_lookup(const struct yetty_yclass *cls,
                                               yetty_yclass_method_slot slot)
{
    if (!cls || slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        return NULL;
    }
    struct yetty_yclass_impl_t_result r = yetty_yclass_dispatch_lookup(cls, slot);
    if (YETTY_IS_ERR(r)) {
        /* yclass returns ERR when the class has no impl on this slot;
         * ygui's convention is NULL = no override. Drop the error. */
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    return r.value;
}

yetty_yclass_impl_t yetty_ygui_dispatch_lookup_super(const struct yetty_yclass *self_class,
                                                     yetty_yclass_method_slot slot)
{
    if (!self_class) {
        return NULL;
    }
    struct yetty_yclass_ptr_result pr = yetty_yclass_parent(self_class);
    if (YETTY_IS_ERR(pr)) {
        yetty_ycore_error_destroy(pr.error);
        return NULL;
    }
    if (!pr.value) {
        return NULL;
    }
    return yetty_ygui_dispatch_lookup(pr.value, slot);
}
