#ifndef YETTY_YUI_VIEW_H
#define YETTY_YUI_VIEW_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yui/tile.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yui_view;
struct yetty_ycore_event;
struct yetty_yrender_target;

/* Result types */
YETTY_YRESULT_DECLARE(yetty_yui_view_ptr, struct yetty_yui_view *);

/* View ops vtable */
struct yetty_yui_view_ops {
    void (*destroy)(struct yetty_yui_view *self);
    struct yetty_ycore_void_result (*render)(struct yetty_yui_view *self,
                                             struct yetty_yrender_target *render_target);
    void (*set_bounds)(struct yetty_yui_view *self, struct yetty_yui_rect bounds);
    struct yetty_ycore_int_result (*on_event)(struct yetty_yui_view *self,
                                              const struct yetty_ycore_event *event);
};

/* View base - embed as first member in subclasses */
struct yetty_yui_view {
    const struct yetty_yui_view_ops *ops;
    yetty_ycore_object_id id;
    struct yetty_yui_rect bounds;
};

/* View operations - dispatch through vtable */
void yetty_yui_view_destroy(struct yetty_yui_view *view);

struct yetty_ycore_void_result yetty_yui_view_render(struct yetty_yui_view *view,
                                                     struct yetty_yrender_target *render_target);

void yetty_yui_view_set_bounds(struct yetty_yui_view *view, struct yetty_yui_rect bounds);

struct yetty_ycore_int_result yetty_yui_view_on_event(struct yetty_yui_view *view,
                                                      const struct yetty_ycore_event *event);

/* ID generation for subclasses */
yetty_ycore_object_id yetty_yui_view_next_id(void);

/* Accessors */
yetty_ycore_object_id yetty_yui_view_id(const struct yetty_yui_view *view);
struct yetty_yui_rect yetty_yui_view_bounds(const struct yetty_yui_view *view);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YUI_VIEW_H */
