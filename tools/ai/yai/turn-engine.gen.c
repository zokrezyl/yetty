/* GENERATED — do not edit. */
#include "yetty/yai/engine.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h>  /* NULL, size_t */

struct yai_app;
struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_yai_on_child_exit(struct yetty_yclass_object * obj, struct yai_app * app, int64_t exit_status);
struct yetty_ycore_void_result yetty_yai_on_child_eof(struct yetty_yclass_object * obj, struct yai_app * app);
struct yetty_ycore_void_result yetty_yai_interrupt(struct yetty_yclass_object * obj, struct yai_app * app);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_exit_fn)(struct yetty_yclass_object *, struct yai_app *, int64_t);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_eof_fn)(struct yetty_yclass_object *, struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_interrupt_fn)(struct yetty_yclass_object *, struct yai_app *);

[[maybe_unused]]
static yetty_yai_on_child_exit_fn yetty_yai_turn_engine_yetty_yai_on_child_exit_check = turn_engine_on_child_exit;
[[maybe_unused]]
static yetty_yai_on_child_eof_fn yetty_yai_turn_engine_yetty_yai_on_child_eof_check = turn_engine_on_child_eof;
[[maybe_unused]]
static yetty_yai_interrupt_fn yetty_yai_turn_engine_yetty_yai_interrupt_check = turn_engine_interrupt;

struct yetty_yclass_ptr_result yetty_yai_turn_engine_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yai_turn_engine");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yai_turn_engine",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yai_turn_engine),
        .data_align = _Alignof(struct yetty_yai_turn_engine),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yai", "on_child_exit", (yetty_yclass_method_id_t)yetty_yai_on_child_exit, (yetty_yclass_impl_t)turn_engine_on_child_exit},
        {"yetty_yai", "on_child_eof", (yetty_yclass_method_id_t)yetty_yai_on_child_eof, (yetty_yclass_impl_t)turn_engine_on_child_eof},
        {"yetty_yai", "interrupt", (yetty_yclass_method_id_t)yetty_yai_interrupt, (yetty_yclass_impl_t)turn_engine_interrupt},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yai_engine_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yai_turn_engine_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yai_turn_engine_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yai_turn_engine_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yai_turn_engine_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yai_turn_engine_ptr_result yetty_yai_turn_engine_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yai_turn_engine_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yai_turn_engine_ptr, "yetty_yai_turn_engine_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yai_turn_engine_ptr, "yetty_yai_turn_engine_from: object_data", slice_r);
    return YETTY_OK(yetty_yai_turn_engine_ptr, (struct yetty_yai_turn_engine *)slice_r.value);
}

struct yetty_yclass_object *yetty_yai_turn_engine_to(struct yetty_yai_turn_engine *data)
{
    if (!data)
        return NULL;
    struct yetty_yclass_ptr_result class_r = yetty_yai_turn_engine_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yetty_ycore_error_destroy(class_r.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    if (YETTY_IS_ERR(offset_r)) {
        yetty_ycore_error_destroy(offset_r.error);
        return NULL;
    }
    return (struct yetty_yclass_object *)((char *)data - offset_r.value);
}
