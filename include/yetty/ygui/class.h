/*
 * ygui-class.h — class system: descriptor, registration, dispatch.
 *
 * The class is the runtime identity for a kind of object. It carries:
 *   - name, type (regular or mixin)
 *   - parent class pointer (NULL for root / for mixins)
 *   - mixin list (NULL for mixins themselves)
 *   - per-class data slice size
 *   - resolved dispatch table (slot -> implementation fn)
 *   - per-instance offsets for every reachable data slice (self + parent
 *     chain + mixins, transitively).
 *
 * Methods are identified at runtime by the address of their public stub
 * function — the result of YETTY_YGUI_DECLARE_METHOD in a header.
 */
#ifndef YETTY_YGUI_CLASS_H
#define YETTY_YGUI_CLASS_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_object;
struct yetty_ygui_class;
struct yetty_ygui_runtime;

YETTY_YRESULT_DECLARE(yetty_ygui_object_ptr, struct yetty_ygui_object *);
YETTY_YRESULT_DECLARE(yetty_ygui_class_ptr, const struct yetty_ygui_class *);
YETTY_YRESULT_DECLARE(yetty_ygui_framework_ptr, struct yetty_ygui_runtime *);

/* A method ID is the address of the public stub function. */
typedef void (*yetty_ygui_method_id_t)(void);
typedef void (*yetty_ygui_impl_t)(void);

/* Unique slot index allocated for a method id. UINT32_MAX = unresolved. */
typedef uint32_t yetty_ygui_method_slot;
#define YETTY_YGUI_METHOD_SLOT_UNDEFINED UINT32_MAX

enum yetty_ygui_class_type {
    YETTY_YGUI_CLASS_TYPE_REGULAR = 0,
    YETTY_YGUI_CLASS_TYPE_MIXIN = 1,
};

/* Per-class descriptor — used at registration to compute layout and ops.
 *
 * The class system is intentionally generic: it knows nothing about
 * widgets, figures, or wire kinds. Concrete behaviour (how a widget
 * paints itself, whether it's a primitive that lives inside the shared
 * ygrid or a figure that owns its own receiver-side instance) is
 * expressed via virtual methods — emit_container, emit_body, paint —
 * dispatched through the class hierarchy. */
struct yetty_ygui_class_descriptor {
    const char *name;
    enum yetty_ygui_class_type type;
    size_t data_size;
};

/* (method_id, implementation) pair. Classes declare an array of these
 * listing only the methods they override. */
struct yetty_ygui_op {
    yetty_ygui_method_id_t method_id;
    yetty_ygui_impl_t impl;
};

#define YETTY_YGUI_OP(method_id, impl)                                                             \
    {(yetty_ygui_method_id_t)(method_id), (yetty_ygui_impl_t)(impl)}

/*-----------------------------------------------------------------------------
 * Method declaration / definition macros.
 *
 * Declaration sits in a header (the public stub prototype). The
 * framework's .c writes each stub body by hand using the dispatch
 * helpers — the boilerplate is small enough that a generic macro adds
 * more friction than it saves.
 *---------------------------------------------------------------------------*/
#define YETTY_YGUI_DECLARE_METHOD(name, result_type, signature) result_type name signature

/* Return (and lazily allocate) the slot for a method id. */
yetty_ygui_method_slot yetty_ygui_method_slot_get(yetty_ygui_method_id_t method_id);

/* Resolve a slot against a class's dispatch table. Returns NULL when the
 * class hasn't registered an override for this slot. */
yetty_ygui_impl_t yetty_ygui_class_dispatch_lookup(const struct yetty_ygui_class *cls,
                                                   yetty_ygui_method_slot slot);

/* Resolve a slot against the PARENT chain of `self_class` (skips self).
 * Walks parent and parent->parent etc. plus mixins-of-parent in the same
 * resolution order used at registration. Used by super invokers. */
yetty_ygui_impl_t yetty_ygui_class_dispatch_lookup_super(const struct yetty_ygui_class *self_class,
                                                         yetty_ygui_method_slot slot);

/* Return the resolved class of an object (its leaf class pointer). */
const struct yetty_ygui_class *yetty_ygui_object_class(const struct yetty_ygui_object *obj);

/*-----------------------------------------------------------------------------
 * Class registration.
 *
 * Called once per class on first accessor invocation. Walks the parent
 * chain + mixins to compute the instance layout, allocates the dispatch
 * table, and resolves the (method_id, impl) ops into slot indices.
 *---------------------------------------------------------------------------*/
struct yetty_ygui_class_ptr_result yetty_ygui_class_register(
    const struct yetty_ygui_class_descriptor *desc, const struct yetty_ygui_op *ops,
    size_t ops_count, const struct yetty_ygui_class *parent,
    const struct yetty_ygui_class *const *mixins, size_t mixin_count);

/*-----------------------------------------------------------------------------
 * YETTY_YGUI_DEFINE_CLASS — emit the accessor function.
 *
 *   YETTY_YGUI_DEFINE_CLASS(accessor_fn, desc_ptr, ops_arr, parent_or_NULL,
 *                           mixin1, ..., NULL);
 *
 * The mixin list is variadic and MUST be NULL-terminated. The macro
 * drops the trailing NULL from the count. The accessor caches its class
 * pointer in a function-static; returns NULL only on first-call
 * registration failure (a fatal startup error logged inside register).
 *---------------------------------------------------------------------------*/
#define YETTY_YGUI_DEFINE_CLASS(accessor, desc, ops_arr, parent, ...)                              \
    const struct yetty_ygui_class *accessor(void)                                                  \
    {                                                                                              \
        static const struct yetty_ygui_class *cls = NULL;                                          \
        if (!cls) {                                                                                \
            const struct yetty_ygui_class *_mixins[] = {__VA_ARGS__};                              \
            size_t _mixin_count = sizeof(_mixins) / sizeof(_mixins[0]);                            \
            if (_mixin_count > 0 && _mixins[_mixin_count - 1] == NULL) {                           \
                _mixin_count--;                                                                    \
            }                                                                                      \
            struct yetty_ygui_class_ptr_result _r = yetty_ygui_class_register(                     \
                (desc), (ops_arr), sizeof(ops_arr) / sizeof((ops_arr)[0]), (parent), _mixins,      \
                _mixin_count);                                                                     \
            if (YETTY_IS_ERR(_r)) {                                                                \
                return NULL;                                                                       \
            }                                                                                      \
            cls = _r.value;                                                                        \
        }                                                                                          \
        return cls;                                                                                \
    }

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_CLASS_H */
