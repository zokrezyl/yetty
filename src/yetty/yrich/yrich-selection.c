/*
 * yrich-selection.c — tagged-union selection state.
 *
 * The element-set kind owns a heap array of element ids; switching kinds
 * frees that array. All other kinds are POD and need no cleanup.
 */

#include <yetty/yrich/yrich-selection.h>

#include <stdlib.h>
#include <string.h>

void yetty_yrich_selection_init(struct yetty_yrich_selection *s)
{
    if (!s) {
        return;
    }
    yetty_yrich_selection_clear(s);
}

void yetty_yrich_selection_clear(struct yetty_yrich_selection *s)
{
    if (!s) {
        return;
    }
    if (s->kind == YETTY_YRICH_SEL_ELEMENTS) {
        free(s->u.elements.ids);
    }
    memset(s, 0, sizeof(*s));
    s->kind = YETTY_YRICH_SEL_NONE;
}

static struct yetty_ycore_void_result elements_grow(struct yetty_yrich_selection_elements *e)
{
    size_t new_cap = e->capacity ? e->capacity * 2 : 4;
    yetty_yrich_element_id *new_ids = realloc(e->ids, new_cap * sizeof(*new_ids));
    if (!new_ids) {
        return YETTY_ERR(yetty_ycore_void, "yrich selection: id array grow failed");
    }
    e->ids = new_ids;
    e->capacity = new_cap;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yrich_selection_select_element(struct yetty_yrich_selection *s,
                                                                    yetty_yrich_element_id id)
{
    if (!s) {
        return YETTY_ERR(yetty_ycore_void, "yrich selection_select_element: NULL selection");
    }
    yetty_yrich_selection_clear(s);
    s->kind = YETTY_YRICH_SEL_ELEMENTS;
    struct yetty_ycore_void_result grow_res = elements_grow(&s->u.elements);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, grow_res, "yrich selection_select_element: grow failed");
    s->u.elements.ids[0] = id;
    s->u.elements.count = 1;
    return YETTY_OK_VOID();
}

void yetty_yrich_selection_select_cells(struct yetty_yrich_selection *s,
                                        struct yetty_yrich_cell_range range,
                                        struct yetty_yrich_cell_addr active)
{
    if (!s) {
        return;
    }
    yetty_yrich_selection_clear(s);
    s->kind = YETTY_YRICH_SEL_CELLS;
    s->u.cells.range = range;
    s->u.cells.active = active;
}

void yetty_yrich_selection_select_cell(struct yetty_yrich_selection *s,
                                       struct yetty_yrich_cell_addr addr)
{
    struct yetty_yrich_cell_range r = {addr, addr};
    yetty_yrich_selection_select_cells(s, r, addr);
}

void yetty_yrich_selection_select_text(struct yetty_yrich_selection *s,
                                       yetty_yrich_element_id element_id, int32_t start,
                                       int32_t end)
{
    if (!s) {
        return;
    }
    yetty_yrich_selection_clear(s);
    s->kind = YETTY_YRICH_SEL_TEXT;
    s->u.text.element_id = element_id;
    s->u.text.start = start;
    s->u.text.end = end;
}

void yetty_yrich_selection_set_cursor(struct yetty_yrich_selection *s,
                                      yetty_yrich_element_id element_id, int32_t position)
{
    yetty_yrich_selection_select_text(s, element_id, position, position);
}

bool yetty_yrich_selection_contains(const struct yetty_yrich_selection *s,
                                    yetty_yrich_element_id id)
{
    if (!s || s->kind != YETTY_YRICH_SEL_ELEMENTS) {
        return false;
    }
    for (size_t i = 0; i < s->u.elements.count; i++) {
        if (s->u.elements.ids[i] == id) {
            return true;
        }
    }
    return false;
}

struct yetty_ycore_void_result yetty_yrich_selection_add(struct yetty_yrich_selection *s,
                                                         yetty_yrich_element_id id)
{
    if (!s) {
        return YETTY_ERR(yetty_ycore_void, "yrich selection_add: NULL selection");
    }
    if (s->kind != YETTY_YRICH_SEL_ELEMENTS) {
        yetty_yrich_selection_clear(s);
        s->kind = YETTY_YRICH_SEL_ELEMENTS;
    }
    if (yetty_yrich_selection_contains(s, id)) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrich_selection_elements *e = &s->u.elements;
    if (e->count == e->capacity) {
        struct yetty_ycore_void_result grow_res = elements_grow(e);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, grow_res, "yrich selection_add: grow failed");
    }
    e->ids[e->count++] = id;
    return YETTY_OK_VOID();
}

void yetty_yrich_selection_remove(struct yetty_yrich_selection *s, yetty_yrich_element_id id)
{
    if (!s || s->kind != YETTY_YRICH_SEL_ELEMENTS) {
        return;
    }
    struct yetty_yrich_selection_elements *e = &s->u.elements;
    for (size_t i = 0; i < e->count; i++) {
        if (e->ids[i] == id) {
            memmove(&e->ids[i], &e->ids[i + 1], (e->count - i - 1) * sizeof(*e->ids));
            e->count--;
            return;
        }
    }
}

struct yetty_ycore_void_result yetty_yrich_selection_toggle(struct yetty_yrich_selection *s,
                                                            yetty_yrich_element_id id)
{
    if (yetty_yrich_selection_contains(s, id)) {
        yetty_yrich_selection_remove(s, id);
        return YETTY_OK_VOID();
    }
    return yetty_yrich_selection_add(s, id);
}

struct yetty_ycore_void_result yetty_yrich_selection_extend_element(struct yetty_yrich_selection *s,
                                                                    yetty_yrich_element_id id)
{
    if (!s) {
        return YETTY_ERR(yetty_ycore_void, "yrich selection_extend_element: NULL selection");
    }
    if (s->kind == YETTY_YRICH_SEL_ELEMENTS) {
        return yetty_yrich_selection_add(s, id);
    }
    return yetty_yrich_selection_select_element(s, id);
}

void yetty_yrich_selection_extend_cell(struct yetty_yrich_selection *s,
                                       struct yetty_yrich_cell_addr addr)
{
    if (!s) {
        return;
    }
    if (s->kind == YETTY_YRICH_SEL_CELLS) {
        s->u.cells.range.end = addr;
        return;
    }
    yetty_yrich_selection_select_cell(s, addr);
}

void yetty_yrich_selection_extend_text(struct yetty_yrich_selection *s, int32_t position)
{
    if (!s || s->kind != YETTY_YRICH_SEL_TEXT) {
        return;
    }
    s->u.text.end = position;
}
