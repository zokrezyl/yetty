/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YFIGURE_METHODS_H
#define YETTY_YCLASSGEN_YFIGURE_METHODS_H

#include <yclass/class.h>
#include <yetty/ycore/types.h>
#include "yetty/yfigure/types.h"

struct yetty_ycore_void_result;
struct yetty_yfigure_figure;

struct yetty_ycore_void_result yetty_yfigure_constructor(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yfigure_add_child(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_yfigure_figure * child, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_remove_child_by_id(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_raise_child_by_id(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_process_records(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_ycore_buffer bytes);

#endif
