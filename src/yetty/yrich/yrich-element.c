/*
 * yrich-element.c — vtable forwarders for the element base class.
 *
 * Each helper is a thin if-non-NULL guard around a vtable slot. Subclasses
 * that don't supply an op get sensible defaults (no-op for state mutators,
 * bounds-based hit-test, non-editable).
 */

#include <yetty/yrich/yrich-element.h>

#include <stddef.h>

void yetty_yrich_element_destroy(struct yetty_yrich_element *e)
{
    if (!e) {
        return;
    }
    if (e->ops && e->ops->destroy) {
        e->ops->destroy(e);
    }
}

struct yetty_yrich_rect yetty_yrich_element_bounds(const struct yetty_yrich_element *e)
{
    struct yetty_yrich_rect zero = {0};
    if (!e || !e->ops || !e->ops->bounds) {
        return zero;
    }
    return e->ops->bounds(e);
}

bool yetty_yrich_element_default_hit_test(const struct yetty_yrich_element *e, float x, float y)
{
    struct yetty_yrich_rect r = yetty_yrich_element_bounds(e);
    return yetty_yrich_rect_contains(&r, x, y);
}

bool yetty_yrich_element_hit_test(const struct yetty_yrich_element *e, float x, float y)
{
    if (!e || !e->ops) {
        return false;
    }
    if (e->ops->hit_test) {
        return e->ops->hit_test(e, x, y);
    }
    return yetty_yrich_element_default_hit_test(e, x, y);
}

struct yetty_ycore_void_result yetty_yrich_element_render(struct yetty_yrich_element *e,
                                                          struct yetty_ydraw_drawable_list *buf,
                                                          uint32_t layer, bool selected)
{
    if (!e || !e->ops || !e->ops->render) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_element_render: NULL or no render");
    }
    return e->ops->render(e, buf, layer, selected);
}

bool yetty_yrich_element_is_editable(const struct yetty_yrich_element *e)
{
    if (!e || !e->ops || !e->ops->is_editable) {
        return false;
    }
    return e->ops->is_editable(e);
}

void yetty_yrich_element_begin_edit(struct yetty_yrich_element *e)
{
    if (!e || !e->ops || !e->ops->begin_edit) {
        return;
    }
    e->ops->begin_edit(e);
}

void yetty_yrich_element_end_edit(struct yetty_yrich_element *e)
{
    if (!e || !e->ops || !e->ops->end_edit) {
        return;
    }
    e->ops->end_edit(e);
}

bool yetty_yrich_element_is_editing(const struct yetty_yrich_element *e)
{
    if (!e || !e->ops || !e->ops->is_editing) {
        return false;
    }
    return e->ops->is_editing(e);
}

void yetty_yrich_element_insert_text(struct yetty_yrich_element *e, const char *text,
                                     size_t text_len)
{
    if (!e || !e->ops || !e->ops->insert_text) {
        return;
    }
    e->ops->insert_text(e, text, text_len);
}

void yetty_yrich_element_delete_sel(struct yetty_yrich_element *e)
{
    if (!e || !e->ops || !e->ops->delete_sel) {
        return;
    }
    e->ops->delete_sel(e);
}
