/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YFIGURE_METHODS_GEN_H
#define YETTY_YCLASSGEN_YFIGURE_METHODS_GEN_H

#include <yetty/yfigure/methods.h>

typedef struct yetty_ycore_void_result (*yetty_yfigure_destroy_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_render_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ydraw_target *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_constructor_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_add_child_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_yfigure_figure *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_remove_child_by_id_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_raise_child_by_id_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_records_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_input_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ywire_wire_statemachine *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_bytes_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, const uint8_t *, size_t);
typedef struct yetty_ycore_char_ptr_result (*yetty_yfigure_dump_state_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, int);
typedef struct yetty_ycore_void_result (*yetty_yfigure_reset_content_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_scroll_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_content_size_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);

#endif
