/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include "yetty/ygui/widgets/dialog.h"
#include "yetty/ygui/widgets/vbox.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

__attribute__((unused))
static yetty_ygui_constructor_fn _yetty_ygui_dialog_yetty_ygui_constructor_check = dialog_constructor;
__attribute__((unused))
static yetty_ygui_destructor_fn _yetty_ygui_dialog_yetty_ygui_destructor_check = dialog_destructor;
__attribute__((unused))
static yetty_ygui_widget_paint_fn _yetty_ygui_dialog_yetty_ygui_widget_paint_check = dialog_paint;

struct yetty_yclass_ptr_result yetty_ygui_dialog_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ygui_dialog");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_dialog",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct dialog_data),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor, (yetty_yclass_impl_t)dialog_constructor},
        {"yetty_ygui", "destructor", (yetty_yclass_method_id_t)yetty_ygui_destructor, (yetty_yclass_impl_t)dialog_destructor},
        {"yetty_ygui", "widget_paint", (yetty_yclass_method_id_t)yetty_ygui_widget_paint, (yetty_yclass_impl_t)dialog_paint},
    };
    struct yetty_yclass_ptr_result _parent_r = yetty_ygui_vbox_class_get();
    if (YETTY_IS_ERR(_parent_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_dialog_class_get: parent accessor failed", _parent_r);
    struct yetty_yclass_ptr_result _r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              _parent_r.value, NULL, 0);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_dialog_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
