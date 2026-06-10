/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YGUI_METHODS_GEN_H
#define YETTY_YCLASSGEN_YGUI_METHODS_GEN_H

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ygui_emit_ctx;

struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_ctx *yclass_ctx,
                                                         struct yetty_yclass_object *yclass_obj,
                                                         float x, float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_ctx *yclass_ctx,
                                                           struct yetty_yclass_object *yclass_obj,
                                                           float x, float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_ctx *yclass_ctx,
                                                          struct yetty_yclass_object *yclass_obj,
                                                          float x, float y);
struct yetty_ycore_void_result yetty_ygui_widget_emit_body(struct yetty_yclass_ctx *yclass_ctx,
                                                           struct yetty_yclass_object *yclass_obj,
                                                           struct yetty_ygui_emit_ctx *ctx);
struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ygui_widget_on_scroll(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj, float x,
                                                          float y, float dx, float dy);
struct yetty_ycore_void_result yetty_ygui_widget_paint(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       struct yetty_ygui_emit_ctx *emit_ctx);
struct yetty_ycore_void_result yetty_ygui_widget_emit_container(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_ygui_emit_ctx *emit_ctx);

typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_press_fn)(struct yetty_yclass_ctx *,
                                                                       struct yetty_yclass_object *,
                                                                       float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_release_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_motion_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_emit_body_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
typedef struct yetty_ycore_void_result (*yetty_ygui_constructor_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui_destructor_fn)(struct yetty_yclass_ctx *,
                                                                   struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_scroll_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_paint_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *,
                                                                     struct yetty_ygui_emit_ctx *);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_emit_container_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);

#endif
