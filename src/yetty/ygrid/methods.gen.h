/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YGRID_METHODS_GEN_H
#define YETTY_YCLASSGEN_YGRID_METHODS_GEN_H

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_ycore_void_result;

struct yetty_ycore_void_result yetty_ygrid_add_record(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj,
                                                      struct yetty_ycore_buffer record);
struct yetty_ycore_void_result yetty_ygrid_clear(struct yetty_yclass_ctx *ctx,
                                                 struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygrid_destroy(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_ygrid_add_record_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *,
                                                                    struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_ygrid_clear_fn)(struct yetty_yclass_ctx *,
                                                               struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygrid_destroy_fn)(struct yetty_yclass_ctx *,
                                                                 struct yetty_yclass_object *);

#endif
