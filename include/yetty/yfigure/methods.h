/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YFIGURE_METHODS_H
#define YETTY_YCLASSGEN_YFIGURE_METHODS_H

#include <yetty/yclass/class.h>
#include <yetty/ycore/types.h>
#include "yetty/yfigure/types.h"

struct yetty_ycore_char_ptr_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_target;
struct yetty_yfigure_figure;
struct yetty_ywire_wire_statemachine;

struct yetty_ycore_void_result yetty_yfigure_destroy(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yfigure_render(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_ydraw_target * target);
struct yetty_ycore_void_result yetty_yfigure_constructor(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yfigure_add_child(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_yfigure_figure * child, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_remove_child_by_id(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_raise_child_by_id(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_process_records(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_ycore_buffer bytes);
struct yetty_ycore_void_result yetty_yfigure_process_input(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_ywire_wire_statemachine * statemachine);
struct yetty_ycore_void_result yetty_yfigure_process_bytes(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, const uint8_t * bytes, size_t bytes_len);
struct yetty_ycore_char_ptr_result yetty_yfigure_dump_state(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, int indent);
struct yetty_ycore_void_result yetty_yfigure_reset_content(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);

#endif
