/*
 * yrich-document.c — base document state.
 *
 * The base owns the element list, selection, op log, and undo history. It
 * provides default render/input handlers; subclasses override them via the
 * vtable.
 */

#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-element.h>
#include <yetty/yrich/yrich-operation.h>
#include <yetty/yrich/yrich-selection.h>

#include <yetty/ypaint-core/buffer.h>

#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yrich_document_init(struct yetty_yrich_document *doc)
{
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "yrich document NULL");
    }

    memset(doc, 0, sizeof(*doc));
    doc->bg_color = YETTY_YRICH_COLOR_WHITE;
    doc->dirty = true;
    doc->next_id = 0;
    doc->session_id = YETTY_YRICH_LOCAL_SESSION;
    yetty_yrich_selection_init(&doc->selection);
    yetty_yrich_op_log_init(&doc->op_log);
    yetty_yrich_history_init(&doc->history);
    return YETTY_OK_VOID();
}

void yetty_yrich_document_fini(struct yetty_yrich_document *doc)
{
    if (!doc) {
        return;
    }
    for (size_t i = 0; i < doc->element_count; i++) {
        yetty_yrich_element_destroy(doc->elements[i]);
    }
    free(doc->elements);
    doc->elements = NULL;
    doc->element_count = doc->element_capacity = 0;

    yetty_yrich_selection_clear(&doc->selection);
    yetty_yrich_op_log_clear(&doc->op_log);
    yetty_yrich_history_clear(&doc->history);
}

void yetty_yrich_document_destroy(struct yetty_yrich_document *doc)
{
    if (!doc) {
        return;
    }
    if (doc->ops && doc->ops->destroy) {
        doc->ops->destroy(doc);
        return;
    }
    yetty_yrich_document_fini(doc);
    free(doc);
}

/*=============================================================================
 * Element list
 *===========================================================================*/

yetty_yrich_element_id yetty_yrich_document_next_id(struct yetty_yrich_document *doc)
{
    if (!doc) {
        return YETTY_YRICH_INVALID_ELEMENT_ID;
    }
    return ++doc->next_id;
}

static int element_list_grow(struct yetty_yrich_document *doc)
{
    size_t new_cap = doc->element_capacity ? doc->element_capacity * 2 : 16;
    struct yetty_yrich_element **new_arr = realloc(doc->elements, new_cap * sizeof(*new_arr));
    if (!new_arr) {
        return -1;
    }
    doc->elements = new_arr;
    doc->element_capacity = new_cap;
    return 0;
}

struct yetty_ycore_void_result yetty_yrich_document_add_element(struct yetty_yrich_document *doc,
                                                                struct yetty_yrich_element *e)
{
    if (!doc || !e) {
        yetty_yrich_element_destroy(e);
        return YETTY_ERR(yetty_ycore_void, "yrich document_add_element: NULL");
    }

    if (doc->element_count == doc->element_capacity) {
        if (element_list_grow(doc) < 0) {
            yetty_yrich_element_destroy(e);
            return YETTY_ERR(yetty_ycore_void, "yrich element list grow failed");
        }
    }
    doc->elements[doc->element_count++] = e;

    if (doc->ops && doc->ops->on_element_added) {
        doc->ops->on_element_added(doc, e);
    }

    /* Log an Insert op (no serialised payload yet — sync layer will fill
	 * this in when ported). */
    struct yetty_yrich_operation_ptr_result op_r =
        yetty_yrich_document_create_op(doc, YETTY_YRICH_OP_INSERT);
    if (YETTY_IS_OK(op_r)) {
        op_r.value->u.insert.id = e->id;
        yetty_yrich_op_log_append(&doc->op_log, op_r.value);
    }

    yetty_yrich_document_mark_dirty(doc);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yrich_document_remove_element(
    struct yetty_yrich_document *doc, yetty_yrich_element_id id)
{
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "yrich remove_element: NULL doc");
    }
    for (size_t i = 0; i < doc->element_count; i++) {
        if (doc->elements[i]->id != id) {
            continue;
        }
        struct yetty_yrich_element *e = doc->elements[i];

        if (doc->ops && doc->ops->on_element_removed) {
            doc->ops->on_element_removed(doc, e);
        }

        memmove(&doc->elements[i], &doc->elements[i + 1],
                (doc->element_count - i - 1) * sizeof(*doc->elements));
        doc->element_count--;

        yetty_yrich_selection_remove(&doc->selection, id);

        struct yetty_yrich_operation_ptr_result op_r =
            yetty_yrich_document_create_op(doc, YETTY_YRICH_OP_DELETE);
        if (YETTY_IS_OK(op_r)) {
            op_r.value->u.del.id = id;
            yetty_yrich_op_log_append(&doc->op_log, op_r.value);
        } else {
            /* Element is already removed; op log is best-effort. Drop the
             * error so we don't leak its chain. */
            yetty_ycore_error_destroy(op_r.error);
        }

        yetty_yrich_element_destroy(e);
        yetty_yrich_document_mark_dirty(doc);
        return YETTY_OK_VOID();
    }
    return YETTY_ERR(yetty_ycore_void, "yrich remove_element: id not found");
}

struct yetty_yrich_element *yetty_yrich_document_find(const struct yetty_yrich_document *doc,
                                                      yetty_yrich_element_id id)
{
    if (!doc) {
        return NULL;
    }
    for (size_t i = 0; i < doc->element_count; i++) {
        if (doc->elements[i]->id == id) {
            return doc->elements[i];
        }
    }
    return NULL;
}

struct yetty_yrich_element *yetty_yrich_document_element_at(const struct yetty_yrich_document *doc,
                                                            float x, float y)
{
    if (!doc) {
        return NULL;
    }
    for (size_t i = doc->element_count; i > 0; i--) {
        struct yetty_yrich_element *e = doc->elements[i - 1];
        if (yetty_yrich_element_hit_test(e, x, y)) {
            return e;
        }
    }
    return NULL;
}

/*=============================================================================
 * Selection
 *===========================================================================*/

bool yetty_yrich_document_is_selected(const struct yetty_yrich_document *doc,
                                      yetty_yrich_element_id id)
{
    if (!doc) {
        return false;
    }
    const struct yetty_yrich_selection *s = &doc->selection;
    if (s->kind == YETTY_YRICH_SEL_ELEMENTS) {
        return yetty_yrich_selection_contains(s, id);
    }
    if (s->kind == YETTY_YRICH_SEL_TEXT) {
        return s->u.text.element_id == id;
    }
    return false;
}

void yetty_yrich_document_clear_selection(struct yetty_yrich_document *doc)
{
    if (!doc) {
        return;
    }
    yetty_yrich_selection_clear(&doc->selection);
    yetty_yrich_document_mark_dirty(doc);
}

/*=============================================================================
 * Render
 *===========================================================================*/

void yetty_yrich_document_set_buffer(struct yetty_yrich_document *doc,
                                     struct yetty_ypaint_core_buffer *buf)
{
    if (!doc) {
        return;
    }
    doc->buffer = buf;
    yetty_yrich_document_mark_dirty(doc);
}

void yetty_yrich_document_set_bg_color(struct yetty_yrich_document *doc, uint32_t color)
{
    if (!doc) {
        return;
    }
    doc->bg_color = color;
    yetty_yrich_document_mark_dirty(doc);
}

/* Internal: clear buffer and set scene bounds before re-emitting primitives.
 * Called from default_render and subclass render(); split out so subclasses
 * that build complex scenes can compose. */
static void clear_target(struct yetty_yrich_document *doc)
{
    if (!doc || !doc->buffer) {
        return;
    }

    yetty_ypaint_core_buffer_clear(doc->buffer);

    float w = yetty_yrich_document_content_width(doc);
    float h = yetty_yrich_document_content_height(doc);
    yetty_ypaint_core_buffer_set_scene_bounds(doc->buffer, 0.0f, 0.0f, w, h);
}

struct yetty_ycore_void_result yetty_yrich_document_default_render(struct yetty_yrich_document *doc)
{
    if (!doc || !doc->buffer) {
        return YETTY_ERR(yetty_ycore_void, "yrich document_default_render: NULL doc/buffer");
    }

    clear_target(doc);

    uint32_t layer = 0;
    for (size_t i = 0; i < doc->element_count; i++) {
        struct yetty_yrich_element *e = doc->elements[i];
        bool selected = yetty_yrich_document_is_selected(doc, e->id);
        struct yetty_ycore_void_result r =
            yetty_yrich_element_render(e, doc->buffer, layer++, selected);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yrich document_default_render: element failed");
    }

    doc->dirty = false;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yrich_document_render(struct yetty_yrich_document *doc)
{
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "yrich document_render: NULL doc");
    }
    if (doc->ops && doc->ops->render) {
        return doc->ops->render(doc);
    }
    return yetty_yrich_document_default_render(doc);
}

float yetty_yrich_document_content_width(const struct yetty_yrich_document *doc)
{
    if (!doc || !doc->ops || !doc->ops->content_width) {
        return 0.0f;
    }
    return doc->ops->content_width(doc);
}

float yetty_yrich_document_content_height(const struct yetty_yrich_document *doc)
{
    if (!doc || !doc->ops || !doc->ops->content_height) {
        return 0.0f;
    }
    return doc->ops->content_height(doc);
}

/*=============================================================================
 * Dirty
 *===========================================================================*/

void yetty_yrich_document_mark_dirty(struct yetty_yrich_document *doc)
{
    if (!doc) {
        return;
    }
    doc->dirty = true;
    if (doc->dirty_cb) {
        doc->dirty_cb(doc, doc->dirty_userdata);
    }
}

bool yetty_yrich_document_is_dirty(const struct yetty_yrich_document *doc)
{
    return doc && doc->dirty;
}

void yetty_yrich_document_clear_dirty(struct yetty_yrich_document *doc)
{
    if (doc) {
        doc->dirty = false;
    }
}

/*=============================================================================
 * Operations / commands
 *===========================================================================*/

struct yetty_yrich_operation_ptr_result yetty_yrich_document_create_op(
    struct yetty_yrich_document *doc, uint32_t op_type)
{
    if (!doc) {
        return YETTY_ERR(yetty_yrich_operation_ptr, "yrich create_op: NULL doc");
    }
    uint64_t ts = ++doc->op_log.current_ts;
    return yetty_yrich_operation_create(op_type, ts, doc->session_id);
}

struct yetty_ycore_void_result yetty_yrich_document_apply_op(struct yetty_yrich_document *doc,
                                                             struct yetty_yrich_operation *op,
                                                             bool local)
{
    if (!doc || !op) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_apply_op: NULL doc/op");
    }

    if (doc->ops && doc->ops->apply_op) {
        doc->ops->apply_op(doc, op);
    }

    if (!yetty_yrich_operation_is_presence(op)) {
        /* Log retains ownership of a clone-like reference. The simple
		 * contract: callers of apply_op pass an op they still own. We
		 * dup the type/ts/author into a fresh log entry that carries
		 * empty payloads, since payload bytes can be large. The full
		 * payload stays with the caller's op. */
        struct yetty_yrich_operation_ptr_result r =
            yetty_yrich_operation_create(op->type, op->timestamp, op->author);
        if (YETTY_IS_OK(r)) {
            yetty_yrich_op_log_append(&doc->op_log, r.value);
        } else {
            yetty_ycore_error_destroy(r.error);
        }
    }

    if (local && doc->sync_cb) {
        struct yetty_yrich_operation *batch[1] = {op};
        doc->sync_cb(doc, batch, 1, doc->sync_userdata);
    }

    yetty_yrich_document_mark_dirty(doc);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yrich_document_execute(struct yetty_yrich_document *doc,
                                                            struct yetty_yrich_command *cmd)
{
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "yrich execute: NULL doc");
    }
    return yetty_yrich_history_execute(&doc->history, cmd, doc);
}

struct yetty_ycore_void_result yetty_yrich_document_undo(struct yetty_yrich_document *doc)
{
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_undo: NULL doc");
    }
    struct yetty_ycore_void_result r = yetty_yrich_history_undo(&doc->history, doc);
    yetty_yrich_document_mark_dirty(doc);
    return r;
}

struct yetty_ycore_void_result yetty_yrich_document_redo(struct yetty_yrich_document *doc)
{
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_redo: NULL doc");
    }
    struct yetty_ycore_void_result r = yetty_yrich_history_redo(&doc->history, doc);
    yetty_yrich_document_mark_dirty(doc);
    return r;
}

/*=============================================================================
 * Default input handlers
 *===========================================================================*/

void yetty_yrich_document_default_on_mouse_down(struct yetty_yrich_document *doc, float x, float y,
                                                uint32_t button, struct yetty_yrich_input_mods mods)
{
    if (!doc || button != YETTY_YRICH_MOUSE_LEFT) {
        return;
    }

    struct yetty_yrich_element *e = yetty_yrich_document_element_at(doc, x, y);
    if (!e) {
        yetty_yrich_document_clear_selection(doc);
        return;
    }

    if (mods.shift) {
        yetty_yrich_selection_extend_element(&doc->selection, e->id);
    } else if (mods.ctrl) {
        if (doc->selection.kind == YETTY_YRICH_SEL_ELEMENTS) {
            yetty_yrich_selection_toggle(&doc->selection, e->id);
        } else {
            yetty_yrich_selection_select_element(&doc->selection, e->id);
        }
    } else {
        yetty_yrich_selection_select_element(&doc->selection, e->id);
    }

    yetty_yrich_document_mark_dirty(doc);
}

struct yetty_ycore_void_result yetty_yrich_document_default_on_key_down(
    struct yetty_yrich_document *doc, uint32_t key, struct yetty_yrich_input_mods mods)
{
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "default_on_key_down: NULL doc");
    }

    if (mods.ctrl) {
        switch (key) {
        case YETTY_YRICH_KEY_Z:
            if (mods.shift) {
                return yetty_yrich_document_redo(doc);
            }
            return yetty_yrich_document_undo(doc);
        case YETTY_YRICH_KEY_Y:
            return yetty_yrich_document_redo(doc);
        default:
            break;
        }
    }

    if (key == YETTY_YRICH_KEY_DELETE || key == YETTY_YRICH_KEY_BACKSPACE) {
        /* Subclass-specific deletion happens via apply_op; nothing
		 * generic to do at the base. */
    }
    return YETTY_OK_VOID();
}

/* Forwarders — pick the vtable override if present, otherwise the default. */

#define DOC_FORWARD(slot, default_fn, ...)                                                         \
    do {                                                                                           \
        if (!doc)                                                                                  \
            return;                                                                                \
        if (doc->ops && doc->ops->slot) {                                                          \
            doc->ops->slot(__VA_ARGS__);                                                           \
            return;                                                                                \
        }                                                                                          \
        default_fn(__VA_ARGS__);                                                                   \
    } while (0)

void yetty_yrich_document_on_mouse_down(struct yetty_yrich_document *doc, float x, float y,
                                        uint32_t button, struct yetty_yrich_input_mods mods)
{
    DOC_FORWARD(on_mouse_down, yetty_yrich_document_default_on_mouse_down, doc, x, y, button, mods);
}

void yetty_yrich_document_on_mouse_up(struct yetty_yrich_document *doc, float x, float y,
                                      uint32_t button, struct yetty_yrich_input_mods mods)
{
    if (!doc) {
        return;
    }
    if (doc->ops && doc->ops->on_mouse_up) {
        doc->ops->on_mouse_up(doc, x, y, button, mods);
    }
    (void)x;
    (void)y;
    (void)button;
    (void)mods;
}

void yetty_yrich_document_on_mouse_drag(struct yetty_yrich_document *doc, float x, float y,
                                        uint32_t button, struct yetty_yrich_input_mods mods)
{
    if (!doc) {
        return;
    }
    if (doc->ops && doc->ops->on_mouse_drag) {
        doc->ops->on_mouse_drag(doc, x, y, button, mods);
    }
}

void yetty_yrich_document_on_mouse_double_click(struct yetty_yrich_document *doc, float x, float y,
                                                uint32_t button, struct yetty_yrich_input_mods mods)
{
    if (!doc) {
        return;
    }
    if (doc->ops && doc->ops->on_mouse_double_click) {
        doc->ops->on_mouse_double_click(doc, x, y, button, mods);
        return;
    }
    if (button != YETTY_YRICH_MOUSE_LEFT) {
        return;
    }
    struct yetty_yrich_element *e = yetty_yrich_document_element_at(doc, x, y);
    if (e && yetty_yrich_element_is_editable(e)) {
        yetty_yrich_element_begin_edit(e);
        yetty_yrich_document_mark_dirty(doc);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_on_key_down(
    struct yetty_yrich_document *doc, uint32_t key, struct yetty_yrich_input_mods mods)
{
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "document_on_key_down: NULL doc");
    }
    if (doc->ops && doc->ops->on_key_down) {
        doc->ops->on_key_down(doc, key, mods);
        return YETTY_OK_VOID();
    }
    return yetty_yrich_document_default_on_key_down(doc, key, mods);
}

void yetty_yrich_document_on_text_input(struct yetty_yrich_document *doc, const char *text,
                                        size_t text_len)
{
    if (!doc) {
        return;
    }
    if (doc->ops && doc->ops->on_text_input) {
        doc->ops->on_text_input(doc, text, text_len);
        return;
    }
    if (doc->selection.kind == YETTY_YRICH_SEL_TEXT) {
        struct yetty_yrich_element *e =
            yetty_yrich_document_find(doc, doc->selection.u.text.element_id);
        if (e) {
            yetty_yrich_element_insert_text(e, text, text_len);
            yetty_yrich_document_mark_dirty(doc);
        }
    }
}
