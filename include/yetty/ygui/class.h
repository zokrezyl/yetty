/*
 * ygui-class.h — thin layer on top of yclass.
 *
 * ygui no longer carries its own class system: every classes-and-
 * methods primitive comes from `<yclass/class.h>`. This header
 * supplies the small ygui-only conveniences that wrap the yclass
 * surface — domain constant + slot-lookup / dispatch helpers tuned
 * to ygui's object header.
 *
 * All `yetty_ygui_<class>_class_get(void)` accessors return
 * `struct yetty_yclass_ptr_result`; method-id function pointers are
 * `yetty_yclass_method_id_t`; slot ids are `yetty_yclass_method_slot`;
 * dispatch lookups go through `yetty_yclass_dispatch_lookup`.
 */
#ifndef YETTY_YGUI_CLASS_H
#define YETTY_YGUI_CLASS_H

#include <stddef.h>
#include <stdint.h>

#include <yclass/class.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_object;
struct yetty_ygui_runtime;

YETTY_YRESULT_DECLARE(yetty_ygui_object_ptr, struct yetty_ygui_object *);
YETTY_YRESULT_DECLARE(yetty_ygui_framework_ptr, struct yetty_ygui_runtime *);

/* Slot-domain string every ygui class registers under. */
#define YETTY_YGUI_DOMAIN "yetty_ygui"

/* Convenience: look up the slot owned by `method_id` in ygui's domain.
 * Returns UINT32_MAX (== YETTY_YCLASS_METHOD_SLOT_UNDEFINED) when the
 * id isn't registered. */
yetty_yclass_method_slot
yetty_ygui_method_slot_get(yetty_yclass_method_id_t method_id);

/* Resolve `slot` against `cls`'s dispatch table. Returns NULL on miss
 * (matches the existing ygui call-site convention that a missing
 * override is a no-op, not an error). */
yetty_yclass_impl_t
yetty_ygui_dispatch_lookup(const struct yetty_yclass *cls, yetty_yclass_method_slot slot);

/* Walk up the parent chain (skipping the leaf) and return the first
 * non-NULL dispatch entry for `slot`. ygui's super invokers use this
 * to chain into the inherited impl. */
yetty_yclass_impl_t
yetty_ygui_dispatch_lookup_super(const struct yetty_yclass *self_class,
                                 yetty_yclass_method_slot slot);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_CLASS_H */
