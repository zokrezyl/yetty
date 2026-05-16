#ifndef YETTY_YRICH_YRICH_ELEMENT_H
#define YETTY_YRICH_YRICH_ELEMENT_H

/*
 * yrich-element — base class for selectable/editable document elements.
 *
 * Vtable-based polymorphism following docs/c-coding-style.md. Subclasses embed
 * struct yetty_yrich_element as their first field, install an ops table, and
 * are upcast/downcast with container_of.
 *
 * The base type carries the element id; it does NOT own bounds or text — that
 * is subclass-private state queried via the vtable. This mirrors the C++
 * Element base, which only stores the id.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/yrich/yrich-types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_draw_list;
struct yetty_yrich_element;

YETTY_YRESULT_DECLARE(yetty_yrich_element_ptr, struct yetty_yrich_element *);

/*=============================================================================
 * Vtable
 *
 * destroy:       free the element (handles NULL, cascades to children)
 * bounds:        axis-aligned bounding box
 * hit_test:      point-in-element test (default uses bounds)
 * render:        emit ydraw primitives at the given base layer
 * is_editable:   element supports text/edit operations
 * begin_edit:    enter edit mode
 * end_edit:      leave edit mode
 * is_editing:    currently editing
 * insert_text:   append text at cursor (UTF-8)
 * delete_sel:    delete current selection or character before cursor
 *===========================================================================*/

struct yetty_yrich_element_ops {
    void (*destroy)(struct yetty_yrich_element *self);

    struct yetty_yrich_rect (*bounds)(const struct yetty_yrich_element *self);
    bool (*hit_test)(const struct yetty_yrich_element *self, float x, float y);

    struct yetty_ycore_void_result (*render)(struct yetty_yrich_element *self,
                                             struct yetty_ydraw_draw_list *buf, uint32_t layer,
                                             bool selected);

    bool (*is_editable)(const struct yetty_yrich_element *self);
    void (*begin_edit)(struct yetty_yrich_element *self);
    void (*end_edit)(struct yetty_yrich_element *self);
    bool (*is_editing)(const struct yetty_yrich_element *self);

    void (*insert_text)(struct yetty_yrich_element *self, const char *text, size_t text_len);
    void (*delete_sel)(struct yetty_yrich_element *self);
};

struct yetty_yrich_element {
    const struct yetty_yrich_element_ops *ops;
    yetty_yrich_element_id id;
};

/*=============================================================================
 * Public element API — thin wrappers that funnel through the vtable.
 *===========================================================================*/

void yetty_yrich_element_destroy(struct yetty_yrich_element *e);

static inline yetty_yrich_element_id yetty_yrich_element_id_get(const struct yetty_yrich_element *e)
{
    return e ? e->id : YETTY_YRICH_INVALID_ELEMENT_ID;
}

struct yetty_yrich_rect yetty_yrich_element_bounds(const struct yetty_yrich_element *e);

bool yetty_yrich_element_hit_test(const struct yetty_yrich_element *e, float x, float y);

struct yetty_ycore_void_result yetty_yrich_element_render(struct yetty_yrich_element *e,
                                                          struct yetty_ydraw_draw_list *buf,
                                                          uint32_t layer, bool selected);

bool yetty_yrich_element_is_editable(const struct yetty_yrich_element *e);
void yetty_yrich_element_begin_edit(struct yetty_yrich_element *e);
void yetty_yrich_element_end_edit(struct yetty_yrich_element *e);
bool yetty_yrich_element_is_editing(const struct yetty_yrich_element *e);

void yetty_yrich_element_insert_text(struct yetty_yrich_element *e, const char *text,
                                     size_t text_len);
void yetty_yrich_element_delete_sel(struct yetty_yrich_element *e);

/* Default hit_test implementation — point-in-bounds. Subclasses can use this
 * directly or override. */
bool yetty_yrich_element_default_hit_test(const struct yetty_yrich_element *e, float x, float y);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YRICH_ELEMENT_H */
