/* GENERATED — do not edit. */
#include "yetty/yflame/methods.gen.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_yflame_configure_fn yetty_yflame_flame_yetty_yflame_configure_check = flame_configure;
[[maybe_unused]]
static yetty_yflame_parse_fn yetty_yflame_flame_yetty_yflame_parse_check = flame_parse;
[[maybe_unused]]
static yetty_yflame_render_fn yetty_yflame_flame_yetty_yflame_render_check = flame_render;
[[maybe_unused]]
static yetty_yflame_hit_test_fn yetty_yflame_flame_yetty_yflame_hit_test_check = flame_hit_test;
[[maybe_unused]]
static yetty_yflame_focus_fn yetty_yflame_flame_yetty_yflame_focus_check = flame_focus;
[[maybe_unused]]
static yetty_yflame_focus_parent_fn yetty_yflame_flame_yetty_yflame_focus_parent_check =
    flame_focus_parent;
[[maybe_unused]]
static yetty_yflame_reset_fn yetty_yflame_flame_yetty_yflame_reset_check = flame_reset;
[[maybe_unused]]
static yetty_yflame_set_highlight_fn yetty_yflame_flame_yetty_yflame_set_highlight_check =
    flame_set_highlight;
[[maybe_unused]]
static yetty_yflame_destroy_fn yetty_yflame_flame_yetty_yflame_destroy_check = flame_obj_destroy;

struct yetty_yclass_ptr_result yetty_yflame_flame_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yflame_flame");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yflame_flame",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct flame_data),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yflame", "configure", (yetty_yclass_method_id_t)yetty_yflame_configure,
         (yetty_yclass_impl_t)flame_configure},
        {"yetty_yflame", "parse", (yetty_yclass_method_id_t)yetty_yflame_parse,
         (yetty_yclass_impl_t)flame_parse},
        {"yetty_yflame", "render", (yetty_yclass_method_id_t)yetty_yflame_render,
         (yetty_yclass_impl_t)flame_render},
        {"yetty_yflame", "hit_test", (yetty_yclass_method_id_t)yetty_yflame_hit_test,
         (yetty_yclass_impl_t)flame_hit_test},
        {"yetty_yflame", "focus", (yetty_yclass_method_id_t)yetty_yflame_focus,
         (yetty_yclass_impl_t)flame_focus},
        {"yetty_yflame", "focus_parent", (yetty_yclass_method_id_t)yetty_yflame_focus_parent,
         (yetty_yclass_impl_t)flame_focus_parent},
        {"yetty_yflame", "reset", (yetty_yclass_method_id_t)yetty_yflame_reset,
         (yetty_yclass_impl_t)flame_reset},
        {"yetty_yflame", "set_highlight", (yetty_yclass_method_id_t)yetty_yflame_set_highlight,
         (yetty_yclass_impl_t)flame_set_highlight},
        {"yetty_yflame", "destroy", (yetty_yclass_method_id_t)yetty_yflame_destroy,
         (yetty_yclass_impl_t)flame_obj_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yflame_flame_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yflame_flame_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}
