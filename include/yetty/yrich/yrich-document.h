#ifndef YETTY_YRICH_YRICH_DOCUMENT_H
#define YETTY_YRICH_YRICH_DOCUMENT_H

/*
 * yrich-document — base class for editable documents.
 *
 * Owns:
 *   - element list (insertion-ordered)
 *   - local selection
 *   - operation log + undo/redo history
 *   - render target (ypaint buffer)
 *
 * Subclasses (yspreadsheet, yslides, ydoc) embed this struct and install a
 * vtable that overrides render(), input handlers, and operation application.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/yrich/yrich-command.h>
#include <yetty/yrich/yrich-element.h>
#include <yetty/yrich/yrich-operation.h>
#include <yetty/yrich/yrich-selection.h>
#include <yetty/yrich/yrich-types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ypaint_core_buffer;
struct yetty_yrich_document;

/* Outgoing-operations callback — used by future sync layer. */
typedef void (*yetty_yrich_sync_cb)(struct yetty_yrich_document *doc,
                                    struct yetty_yrich_operation *const *ops, size_t op_count,
                                    void *userdata);

/* Dirty notification — fired when state changes between renders. */
typedef void (*yetty_yrich_dirty_cb)(struct yetty_yrich_document *doc, void *userdata);

/*=============================================================================
 * Vtable — the methods subclasses customise.
 *===========================================================================*/

struct yetty_yrich_document_ops {
    void (*destroy)(struct yetty_yrich_document *self);

    /* Document logical content size (without margins). */
    float (*content_width)(const struct yetty_yrich_document *self);
    float (*content_height)(const struct yetty_yrich_document *self);

    /* Render document to self->buffer. The default implementation iterates
	 * elements; subclasses override for layered rendering (slides, etc). */
    struct yetty_ycore_void_result (*render)(struct yetty_yrich_document *self);

    /* Apply operation against subclass-specific state. The base class still
	 * runs its own bookkeeping (element list, op log). */
    void (*apply_op)(struct yetty_yrich_document *self, const struct yetty_yrich_operation *op);

    /* Element lifecycle hooks. */
    void (*on_element_added)(struct yetty_yrich_document *self, struct yetty_yrich_element *e);
    void (*on_element_removed)(struct yetty_yrich_document *self, struct yetty_yrich_element *e);

    /* Input — default handlers are provided in the base. */
    void (*on_mouse_down)(struct yetty_yrich_document *self, float x, float y, uint32_t button,
                          struct yetty_yrich_input_mods mods);
    void (*on_mouse_up)(struct yetty_yrich_document *self, float x, float y, uint32_t button,
                        struct yetty_yrich_input_mods mods);
    void (*on_mouse_drag)(struct yetty_yrich_document *self, float x, float y, uint32_t button,
                          struct yetty_yrich_input_mods mods);
    void (*on_mouse_double_click)(struct yetty_yrich_document *self, float x, float y,
                                  uint32_t button, struct yetty_yrich_input_mods mods);
    void (*on_key_down)(struct yetty_yrich_document *self, uint32_t key,
                        struct yetty_yrich_input_mods mods);
    void (*on_text_input)(struct yetty_yrich_document *self, const char *text, size_t text_len);
};

/*=============================================================================
 * Base struct
 *===========================================================================*/

struct yetty_yrich_document {
    const struct yetty_yrich_document_ops *ops;

    /* Elements — flat array, insertion ordered. */
    struct yetty_yrich_element **elements;
    size_t element_count;
    size_t element_capacity;

    yetty_yrich_element_id next_id;

    struct yetty_yrich_selection selection;
    struct yetty_yrich_op_log op_log;
    struct yetty_yrich_history history;

    /* Render target — caller-owned. */
    struct yetty_ypaint_core_buffer *buffer;
    uint32_t bg_color; /* packed ABGR */

    bool dirty;

    yetty_yrich_sync_cb sync_cb;
    void *sync_userdata;

    yetty_yrich_dirty_cb dirty_cb;
    void *dirty_userdata;

    yetty_yrich_session_id session_id;
};

/*=============================================================================
 * Lifecycle helpers
 *===========================================================================*/

/* Initialise the base portion of a document. Subclasses call this from their
 * own create() before installing their vtable. */
struct yetty_ycore_void_result yetty_yrich_document_init(struct yetty_yrich_document *doc);

/* Free resources owned by the base; subclass destroy invokes this last. */
void yetty_yrich_document_fini(struct yetty_yrich_document *doc);

/* Polymorphic destroy. */
void yetty_yrich_document_destroy(struct yetty_yrich_document *doc);

/*=============================================================================
 * Element management
 *===========================================================================*/

/* Generate a fresh element id (monotonic). */
yetty_yrich_element_id yetty_yrich_document_next_id(struct yetty_yrich_document *doc);

/* Take ownership of element. Generates an Insert op and appends to log. */
struct yetty_ycore_void_result yetty_yrich_document_add_element(struct yetty_yrich_document *doc,
                                                                struct yetty_yrich_element *e);

/* Remove + destroy element matching id. Generates a Delete op. */
struct yetty_ycore_void_result yetty_yrich_document_remove_element(
    struct yetty_yrich_document *doc, yetty_yrich_element_id id);

struct yetty_yrich_element *yetty_yrich_document_find(const struct yetty_yrich_document *doc,
                                                      yetty_yrich_element_id id);

/* Hit-test in reverse z-order (last drawn = topmost). */
struct yetty_yrich_element *yetty_yrich_document_element_at(const struct yetty_yrich_document *doc,
                                                            float x, float y);

/*=============================================================================
 * Selection
 *===========================================================================*/

bool yetty_yrich_document_is_selected(const struct yetty_yrich_document *doc,
                                      yetty_yrich_element_id id);

void yetty_yrich_document_clear_selection(struct yetty_yrich_document *doc);

/*=============================================================================
 * Render
 *===========================================================================*/

/* Set/get the ypaint buffer used as render target. Document does not own it. */
void yetty_yrich_document_set_buffer(struct yetty_yrich_document *doc,
                                     struct yetty_ypaint_core_buffer *buf);

void yetty_yrich_document_set_bg_color(struct yetty_yrich_document *doc, uint32_t color);

/* Polymorphic render entry point. */
struct yetty_ycore_void_result yetty_yrich_document_render(struct yetty_yrich_document *doc);

/* Default render — iterate elements in order. Subclasses can call this. */
struct yetty_ycore_void_result yetty_yrich_document_default_render(
    struct yetty_yrich_document *doc);

float yetty_yrich_document_content_width(const struct yetty_yrich_document *doc);
float yetty_yrich_document_content_height(const struct yetty_yrich_document *doc);

/*=============================================================================
 * Dirty
 *===========================================================================*/

void yetty_yrich_document_mark_dirty(struct yetty_yrich_document *doc);

bool yetty_yrich_document_is_dirty(const struct yetty_yrich_document *doc);
void yetty_yrich_document_clear_dirty(struct yetty_yrich_document *doc);

/*=============================================================================
 * Operations / commands
 *===========================================================================*/

/* Allocate an operation tagged with the document's logical clock + session. */
struct yetty_yrich_operation_ptr_result yetty_yrich_document_create_op(
    struct yetty_yrich_document *doc, uint32_t op_type);

/* Apply an operation locally (or remotely if local=false). The document
 * appends non-presence ops to the log, calls subclass apply_op, and notifies
 * the sync callback when local=true. */
struct yetty_ycore_void_result yetty_yrich_document_apply_op(struct yetty_yrich_document *doc,
                                   struct yetty_yrich_operation *op, bool local);

/* Run a command and push it onto the history. Takes ownership of cmd. */
struct yetty_ycore_void_result yetty_yrich_document_execute(struct yetty_yrich_document *doc,
                                                            struct yetty_yrich_command *cmd);

struct yetty_ycore_void_result yetty_yrich_document_undo(struct yetty_yrich_document *doc);
struct yetty_ycore_void_result yetty_yrich_document_redo(struct yetty_yrich_document *doc);

/*=============================================================================
 * Input — default forwarders. Subclass vtable overrides them as needed.
 *===========================================================================*/

void yetty_yrich_document_on_mouse_down(struct yetty_yrich_document *doc, float x, float y,
                                        uint32_t button, struct yetty_yrich_input_mods mods);
void yetty_yrich_document_on_mouse_up(struct yetty_yrich_document *doc, float x, float y,
                                      uint32_t button, struct yetty_yrich_input_mods mods);
void yetty_yrich_document_on_mouse_drag(struct yetty_yrich_document *doc, float x, float y,
                                        uint32_t button, struct yetty_yrich_input_mods mods);
void yetty_yrich_document_on_mouse_double_click(struct yetty_yrich_document *doc, float x, float y,
                                                uint32_t button,
                                                struct yetty_yrich_input_mods mods);
struct yetty_ycore_void_result yetty_yrich_document_on_key_down(struct yetty_yrich_document *doc,
                                                                uint32_t key,
                                      struct yetty_yrich_input_mods mods);
void yetty_yrich_document_on_text_input(struct yetty_yrich_document *doc, const char *text,
                                        size_t text_len);

/* Default implementations exported for subclass reuse. */
void yetty_yrich_document_default_on_mouse_down(struct yetty_yrich_document *doc, float x, float y,
                                                uint32_t button,
                                                struct yetty_yrich_input_mods mods);
struct yetty_ycore_void_result yetty_yrich_document_default_on_key_down(
    struct yetty_yrich_document *doc, uint32_t key,
                                              struct yetty_yrich_input_mods mods);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YRICH_DOCUMENT_H */
