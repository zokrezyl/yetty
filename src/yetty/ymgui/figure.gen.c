/* GENERATED — do not edit. */
#include "yetty/yfigure/figure.h"
#include "yetty/ymgui/figure.h"
#include "yetty/ymgui/methods.gen.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

struct yetty_yclass_ptr_result yetty_ymgui_figure_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ymgui_figure");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ymgui_figure",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ymgui_figure),
    };
    static const struct yetty_yclass_op ops[] = {

    };
    struct yetty_yclass_ptr_result _mixin0_r = yetty_yfigure_figure_mixin_get();
    if (YETTY_IS_ERR(_mixin0_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ymgui_figure_class_get: mixin0 accessor failed", _mixin0_r);
    const struct yetty_yclass *mixins[] = { _mixin0_r.value };
    struct yetty_yclass_ptr_result _r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, mixins, 1);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ymgui_figure_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
