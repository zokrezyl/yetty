#include <yetty/yui-core/view.h>

/*=============================================================================
 * Object ID generation
 *===========================================================================*/

static uint64_t g_next_view_id = 1;

static yetty_ycore_object_id next_view_id(void)
{
    return g_next_view_id++;
}

/*=============================================================================
 * ID generation for subclasses
 *===========================================================================*/

yetty_ycore_object_id yetty_yui_view_next_id(void)
{
    return next_view_id();
}

/*=============================================================================
 * View public API
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yui_view_destroy(struct yetty_yui_view *view)
{
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yui_view_destroy: NULL view");
    }
    if (!view->ops || !view->ops->destroy) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yui_view_destroy: destroy not implemented");
    }
    return view->ops->destroy(view);
}

struct yetty_ycore_void_result yetty_yui_view_render(struct yetty_yui_view *view,
                                                     struct yetty_ydraw_target *render_target,
                                                     int force_redraw)
{
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "view is NULL");
    }
    if (!view->ops || !view->ops->render) {
        return YETTY_ERR(yetty_ycore_void, "render not implemented");
    }
    return view->ops->render(view, render_target, force_redraw);
}

struct yetty_ycore_void_result yetty_yui_view_set_bounds(struct yetty_yui_view *view,
                                                         struct yetty_yui_rect bounds)
{
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yui_view_set_bounds: NULL view");
    }
    view->bounds = bounds;
    if (view->ops && view->ops->set_bounds) {
        return view->ops->set_bounds(view, bounds);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_int_result yetty_yui_view_on_event(struct yetty_yui_view *view,
                                                      const struct yetty_yui_event *event)
{
    if (!view) {
        return YETTY_ERR(yetty_ycore_int, "view is NULL");
    }
    if (!view->ops || !view->ops->on_event) {
        return YETTY_ERR(yetty_ycore_int, "on_event not implemented");
    }
    return view->ops->on_event(view, event);
}

yetty_ycore_object_id yetty_yui_view_id(const struct yetty_yui_view *view)
{
    return view ? view->id : YETTY_YCORE_OBJECT_ID_NONE;
}

struct yetty_yui_rect yetty_yui_view_bounds(const struct yetty_yui_view *view)
{
    if (!view) {
        return (struct yetty_yui_rect){0, 0, 0, 0};
    }
    return view->bounds;
}
