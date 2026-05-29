/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include "yetty/ygui/widgets/ydraw_embed.h"
#include "yetty/ygui/widgets/ymarkdown.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

__attribute__((unused))
static yetty_ygui_constructor_fn yetty_ygui_ymarkdown_yetty_ygui_constructor_check = ymd_constructor;
__attribute__((unused))
static yetty_ygui_destructor_fn yetty_ygui_ymarkdown_yetty_ygui_destructor_check = ymd_destructor;
__attribute__((unused))
static yetty_ygui_widget_emit_body_fn yetty_ygui_ymarkdown_yetty_ygui_widget_emit_body_check = ymd_emit_body;

struct yetty_yclass_ptr_result yetty_ygui_ymarkdown_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ygui_ymarkdown");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_ymarkdown",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct ymarkdown_data),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor, (yetty_yclass_impl_t)ymd_constructor},
        {"yetty_ygui", "destructor", (yetty_yclass_method_id_t)yetty_ygui_destructor, (yetty_yclass_impl_t)ymd_destructor},
        {"yetty_ygui", "widget_emit_body", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_body, (yetty_yclass_impl_t)ymd_emit_body},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ygui_ydraw_embed_class_get();
    if (YETTY_IS_ERR(parent_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_ymarkdown_class_get: parent accessor failed", parent_class_r);
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_ymarkdown_class_get: class_register failed", register_class_r);
    cls = register_class_r.value;
    return register_class_r;
}
