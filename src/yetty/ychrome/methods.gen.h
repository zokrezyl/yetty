/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YCHROME_METHODS_GEN_H
#define YETTY_YCLASSGEN_YCHROME_METHODS_GEN_H

#include <yetty/ychrome/methods.h>

typedef struct yetty_ycore_void_result (*yetty_ychrome_configure_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *,
                                                                     struct yetty_yclass_object *,
                                                                     float, float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ychrome_set_size_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *,
                                                                    float, float);
typedef struct yetty_ycore_void_result (*yetty_ychrome_destroy_fn)(struct yetty_yclass_ctx *,
                                                                   struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ychrome_edge_cursor_at_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_int_result (*yetty_ychrome_handle_event_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, const struct yetty_yui_event *);

#endif
