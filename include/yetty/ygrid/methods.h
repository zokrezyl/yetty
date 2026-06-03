/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YGRID_METHODS_H
#define YETTY_YCLASSGEN_YGRID_METHODS_H

#include <yetty/yclass/class.h>
#include <yetty/ycore/types.h>
#include "yetty/ygrid/types.h"

struct yetty_ycore_void_result;

struct yetty_ycore_void_result yetty_ygrid_add_record(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_ycore_buffer record);
struct yetty_ycore_void_result yetty_ygrid_clear(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ygrid_destroy(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ygrid_process_bytes(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_ycore_buffer payload);
struct yetty_ycore_void_result yetty_ygrid_reset_content(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);

#endif
