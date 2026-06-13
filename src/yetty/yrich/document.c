/*
 * yrich:document base-class host TU.
 *
 * `struct yetty_yrich_document` is the yclass base class for every concrete
 * document kind (ydoc, spreadsheet, slides). The base owns the element list
 * (yclass element objects), the local selection, the operation log and the
 * undo/redo history. Concrete kinds inherit via `parent@yrich:document` and
 * override the document_* slots; all dispatch goes through yclass — there
 * is no ops vtable.
 *
 * Slot wire shapes: render/destroy/apply_op take raw pointers (drawable
 * list, operation) and are `local@`; the input slots, undo/redo and the
 * content-size queries carry only scalars or a by-value buffer and stay
 * wire-marshallable for RPC and FFI bindings.
 *
 * This TU deliberately does NOT include its own generated header
 * `yetty/yrich/document.h` — that header is a generated artifact for other
 * modules. The accessor/downcast the appended document.gen.c defines are
 * forward-declared below.
 */

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <yetty/yrich/yrich-command.h>
#include <yetty/yrich/yrich-operation.h>
#include <yetty/yrich/yrich-selection.h>
#include <yetty/yrich/yrich-types.h>

#include <yetty/yrich/element.h>

#include <yetty/ydraw-core/drawable-list.h>

#include <stdlib.h>
#include <string.h>

/* Accessor / downcast defined in the appended document.gen.c. */
YETTY_YRESULT_DECLARE(yetty_yrich_document_ptr, struct yetty_yrich_document *);
struct yetty_yclass_ptr_result yetty_yrich_document_class_get(void);
struct yetty_yrich_document_ptr_result yetty_yrich_document_from(struct yetty_yclass_object *obj);

/* Public stubs this TU calls before the foot include (generated in
 * methods.gen.c; the generated document.h publishes them for consumers). */
struct yetty_ycore_float_result yetty_yrich_document_content_width(struct yetty_yclass_ctx *ctx,
                                                                   struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_document_content_height(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_undo(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_redo(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj);

/*===========================================================================
 * Document base data slice — the first slice in every document object.
 *=========================================================================*/

struct [[clang::annotate("class@yrich:document")]] yetty_yrich_document {
    /* Elements — flat array of element objects, insertion ordered, owned. */
    struct yetty_yclass_object **elements;
    size_t element_count;
    size_t element_capacity;

    yetty_yrich_element_id next_id;

    struct yetty_yrich_selection selection;
    struct yetty_yrich_op_log op_log;
    struct yetty_yrich_history history;

    /* Render target — caller-owned. */
    struct yetty_ydraw_drawable_list *buffer;
    uint32_t bg_color; /* packed ABGR */

    int dirty;

    yetty_yrich_sync_cb sync_cb;
    void *sync_userdata;

    yetty_yrich_dirty_cb dirty_cb;
    void *dirty_userdata;

    yetty_yrich_session_id session_id;
};

/* Reach this object's document base-data slice. document.c has the struct,
 * so it touches fields directly through this helper; other yrich .c go
 * through the exposed accessors below. */
static struct yetty_yrich_document_ptr_result ddata(struct yetty_yclass_object *obj)
{
    return yetty_yrich_document_from(obj);
}

/*===========================================================================
 * Super invokers — subclass overrides chain to the parent class's impl.
 * Exposed so the concrete-kind TUs (ydoc.c, spreadsheet.c, slides.c) and
 * the element subclasses can chain their constructor/destroy overrides.
 *=========================================================================*/

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_super_void(struct yetty_yclass_object *obj,
                                                      const struct yetty_yclass *self_class,
                                                      yetty_yclass_method_id_t method_id)
{
    if (!obj || !self_class || !method_id) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_super_void: NULL arg");
    }
    struct yetty_yclass_method_slot_result slot_res =
        yetty_yclass_method_slot_get("yetty_yrich", method_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_res, "yetty_yrich_super_void: slot lookup");
    struct yetty_yclass_ptr_result parent_res = yetty_yclass_parent(self_class);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, parent_res, "yetty_yrich_super_void: parent lookup");
    if (!parent_res.value) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_impl_t_result impl_res =
        yetty_yclass_dispatch_lookup(parent_res.value, slot_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, impl_res, "yetty_yrich_super_void: dispatch lookup");
    if (!impl_res.value) {
        return YETTY_OK_VOID();
    }
    typedef struct yetty_ycore_void_result (*fn_t)(struct yetty_yclass_ctx *,
                                                   struct yetty_yclass_object *);
    return ((fn_t)impl_res.value)(NULL, obj);
}

/*===========================================================================
 * Module constructor slot — auto-called by every generated <class>_create.
 *=========================================================================*/

[[clang::annotate("virtual@yrich:document:constructor")]]
static struct yetty_ycore_void_result document_constructor(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_constructor: data_get");
    struct yetty_yrich_document *doc = data_res.value;
    doc->bg_color = YETTY_YRICH_COLOR_WHITE;
    doc->dirty = 1;
    doc->session_id = YETTY_YRICH_LOCAL_SESSION;
    yetty_yrich_selection_init(&doc->selection);
    yetty_yrich_op_log_init(&doc->op_log);
    yetty_yrich_history_init(&doc->history);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Lifecycle — document_destroy frees owned state and the object itself.
 * Concrete kinds override, free their own fields and chain up LAST via
 * yetty_yrich_super_void (the base impl frees the object).
 *=========================================================================*/

[[clang::annotate("virtual@yrich:document:document_destroy")]] [[clang::annotate(
    "local@yrich:document_destroy")]]
static struct yetty_ycore_void_result document_default_destroy(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_destroy: data_get");
    struct yetty_yrich_document *doc = data_res.value;

    /* Best-effort teardown: run every step, stash the first error. */
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    int have_err = 0;
    for (size_t i = 0; i < doc->element_count; i++) {
        struct yetty_ycore_void_result destroy_res =
            yetty_yrich_element_destroy(NULL, doc->elements[i]);
        if (YETTY_IS_ERR(destroy_res)) {
            if (!have_err) {
                first_err = destroy_res;
                have_err = 1;
            } else {
                yetty_ycore_error_destroy(destroy_res.error);
            }
        }
    }
    free(doc->elements);
    doc->elements = NULL;
    doc->element_count = doc->element_capacity = 0;

    yetty_yrich_selection_clear(&doc->selection);
    yetty_yrich_op_log_clear(&doc->op_log);
    yetty_yrich_history_clear(&doc->history);

    struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
    if (YETTY_IS_ERR(free_res)) {
        if (have_err) {
            yetty_ycore_error_destroy(free_res.error);
        } else {
            first_err = free_res;
            have_err = 1;
        }
    }
    if (have_err) {
        return YETTY_ERR(yetty_ycore_void, "document_destroy: teardown failed", first_err);
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Dirty tracking
 *=========================================================================*/

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_document_mark_dirty(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_mark_dirty: data_get");
    struct yetty_yrich_document *doc = data_res.value;
    doc->dirty = 1;
    if (doc->dirty_cb) {
        doc->dirty_cb(obj, doc->dirty_userdata);
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
int yetty_yrich_document_is_dirty(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return 0;
    }
    return data_res.value->dirty;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_document_clear_dirty(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_clear_dirty: data_get");
    data_res.value->dirty = 0;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Render target / colors
 *=========================================================================*/

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_document_set_buffer(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *drawable_list)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_set_buffer: data_get");
    data_res.value->buffer = drawable_list;
    return yetty_yrich_document_mark_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ydraw_drawable_list *yetty_yrich_document_buffer(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return NULL;
    }
    return data_res.value->buffer;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_document_set_bg_color(struct yetty_yclass_object *obj,
                                                                 uint32_t color)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_set_bg_color: data_get");
    data_res.value->bg_color = color;
    return yetty_yrich_document_mark_dirty(obj);
}

[[clang::annotate("expose")]]
uint32_t yetty_yrich_document_bg_color(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return 0;
    }
    return data_res.value->bg_color;
}

/*===========================================================================
 * Callbacks
 *=========================================================================*/

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_document_set_dirty_cb(struct yetty_yclass_object *obj,
                                                                 yetty_yrich_dirty_cb callback,
                                                                 void *userdata)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_set_dirty_cb: data_get");
    data_res.value->dirty_cb = callback;
    data_res.value->dirty_userdata = userdata;
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_document_set_sync_cb(struct yetty_yclass_object *obj,
                                                                yetty_yrich_sync_cb callback,
                                                                void *userdata)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_set_sync_cb: data_get");
    data_res.value->sync_cb = callback;
    data_res.value->sync_userdata = userdata;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Element list
 *=========================================================================*/

[[clang::annotate("expose")]]
struct yetty_yrich_element_id_result yetty_yrich_document_next_id(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_yrich_element_id, data_res, "document_next_id: data_get");
    return YETTY_OK(yetty_yrich_element_id, ++data_res.value->next_id);
}

static struct yetty_ycore_void_result element_list_grow(struct yetty_yrich_document *doc)
{
    size_t new_cap = doc->element_capacity ? doc->element_capacity * 2 : 16;
    struct yetty_yclass_object **new_arr = realloc(doc->elements, new_cap * sizeof(*new_arr));
    if (!new_arr) {
        return YETTY_ERR(yetty_ycore_void, "yrich element list grow failed");
    }
    doc->elements = new_arr;
    doc->element_capacity = new_cap;
    return YETTY_OK_VOID();
}

/* Allocate an operation tagged with the document's logical clock + session. */
[[clang::annotate("expose")]]
struct yetty_yrich_operation_ptr_result yetty_yrich_document_create_op(
    struct yetty_yclass_object *obj, uint32_t op_type)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_yrich_operation_ptr, data_res, "document_create_op: data_get");
    struct yetty_yrich_document *doc = data_res.value;
    uint64_t timestamp = ++doc->op_log.current_ts;
    return yetty_yrich_operation_create(op_type, timestamp, doc->session_id);
}

/* Take ownership of the element object. Generates an Insert op (the op log
 * is best-effort bookkeeping; its failures are absorbed here). */
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_document_add_element(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *element_obj)
{
    if (!obj || !element_obj) {
        if (element_obj) {
            struct yetty_ycore_void_result destroy_res =
                yetty_yrich_element_destroy(NULL, element_obj);
            if (YETTY_IS_ERR(destroy_res)) {
                yetty_ycore_error_destroy(destroy_res.error);
            }
        }
        return YETTY_ERR(yetty_ycore_void, "document_add_element: NULL arg");
    }
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_add_element: data_get");
    struct yetty_yrich_document *doc = data_res.value;

    if (doc->element_count == doc->element_capacity) {
        struct yetty_ycore_void_result grow_res = element_list_grow(doc);
        if (YETTY_IS_ERR(grow_res)) {
            struct yetty_ycore_void_result destroy_res =
                yetty_yrich_element_destroy(NULL, element_obj);
            if (YETTY_IS_ERR(destroy_res)) {
                yetty_ycore_error_destroy(destroy_res.error);
            }
            return YETTY_ERR(yetty_ycore_void, "document_add_element: grow failed", grow_res);
        }
    }
    doc->elements[doc->element_count++] = element_obj;

    struct yetty_yrich_operation_ptr_result op_res =
        yetty_yrich_document_create_op(obj, YETTY_YRICH_OP_INSERT);
    if (YETTY_IS_OK(op_res)) {
        struct yetty_yrich_element_id_result id_res = yetty_yrich_element_id_value(element_obj);
        op_res.value->u.insert.id = YETTY_IS_OK(id_res) ? id_res.value : 0;
        if (YETTY_IS_ERR(id_res)) {
            yetty_ycore_error_destroy(id_res.error);
        }
        struct yetty_ycore_void_result append_res =
            yetty_yrich_op_log_append(&doc->op_log, op_res.value);
        if (YETTY_IS_ERR(append_res)) {
            yetty_ycore_error_destroy(append_res.error);
        }
    } else {
        yetty_ycore_error_destroy(op_res.error);
    }

    return yetty_yrich_document_mark_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_document_remove_element(struct yetty_yclass_object *obj,
                                                                   yetty_yrich_element_id id)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_remove_element: data_get");
    struct yetty_yrich_document *doc = data_res.value;

    for (size_t i = 0; i < doc->element_count; i++) {
        struct yetty_yrich_element_id_result id_res =
            yetty_yrich_element_id_value(doc->elements[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "document_remove_element: id_get");
        if (id_res.value != id) {
            continue;
        }
        struct yetty_yclass_object *element_obj = doc->elements[i];

        memmove(&doc->elements[i], &doc->elements[i + 1],
                (doc->element_count - i - 1) * sizeof(*doc->elements));
        doc->element_count--;

        yetty_yrich_selection_remove(&doc->selection, id);

        /* Op log is best-effort — the element is already unlinked. */
        struct yetty_yrich_operation_ptr_result op_res =
            yetty_yrich_document_create_op(obj, YETTY_YRICH_OP_DELETE);
        if (YETTY_IS_OK(op_res)) {
            op_res.value->u.del.id = id;
            struct yetty_ycore_void_result append_res =
                yetty_yrich_op_log_append(&doc->op_log, op_res.value);
            if (YETTY_IS_ERR(append_res)) {
                yetty_ycore_error_destroy(append_res.error);
            }
        } else {
            yetty_ycore_error_destroy(op_res.error);
        }

        struct yetty_ycore_void_result destroy_res = yetty_yrich_element_destroy(NULL, element_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, destroy_res,
                            "document_remove_element: element destroy");
        return yetty_yrich_document_mark_dirty(obj);
    }
    return YETTY_ERR(yetty_ycore_void, "document_remove_element: id not found");
}

[[clang::annotate("expose")]]
struct yetty_yclass_object *yetty_yrich_document_find(struct yetty_yclass_object *obj,
                                                      yetty_yrich_element_id id)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return NULL;
    }
    struct yetty_yrich_document *doc = data_res.value;
    for (size_t i = 0; i < doc->element_count; i++) {
        struct yetty_yrich_element_id_result id_res =
            yetty_yrich_element_id_value(doc->elements[i]);
        if (YETTY_IS_ERR(id_res)) {
            yetty_ycore_error_destroy(id_res.error);
            continue;
        }
        if (id_res.value == id) {
            return doc->elements[i];
        }
    }
    return NULL;
}

/* Hit-test in reverse z-order (last drawn = topmost). */
[[clang::annotate("expose")]]
struct yetty_yclass_object *yetty_yrich_document_element_at(struct yetty_yclass_object *obj,
                                                            float x, float y)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return NULL;
    }
    struct yetty_yrich_document *doc = data_res.value;
    for (size_t i = doc->element_count; i > 0; i--) {
        struct yetty_yclass_object *element_obj = doc->elements[i - 1];
        struct yetty_ycore_int_result hit_res =
            yetty_yrich_element_hit_test(NULL, element_obj, x, y);
        if (YETTY_IS_ERR(hit_res)) {
            /* A failed hit-test on one element must not hide the rest. */
            yetty_ycore_error_destroy(hit_res.error);
            continue;
        }
        if (hit_res.value) {
            return element_obj;
        }
    }
    return NULL;
}

/*===========================================================================
 * Selection
 *=========================================================================*/

[[clang::annotate("expose")]]
struct yetty_yrich_selection *yetty_yrich_document_selection(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return NULL;
    }
    return &data_res.value->selection;
}

[[clang::annotate("expose")]]
int yetty_yrich_document_is_selected(struct yetty_yclass_object *obj, yetty_yrich_element_id id)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return 0;
    }
    const struct yetty_yrich_selection *selection = &data_res.value->selection;
    if (selection->kind == YETTY_YRICH_SEL_ELEMENTS) {
        return yetty_yrich_selection_contains(selection, id) ? 1 : 0;
    }
    if (selection->kind == YETTY_YRICH_SEL_TEXT) {
        return selection->u.text.element_id == id ? 1 : 0;
    }
    return 0;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_document_clear_selection(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_clear_selection: data_get");
    yetty_yrich_selection_clear(&data_res.value->selection);
    return yetty_yrich_document_mark_dirty(obj);
}

/*===========================================================================
 * Content size slots — concrete kinds override.
 *=========================================================================*/

[[clang::annotate("virtual@yrich:document:document_content_width")]]
static struct yetty_ycore_float_result document_default_content_width(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    (void)ctx;
    (void)obj;
    return YETTY_OK(yetty_ycore_float, 0.0f);
}

[[clang::annotate("virtual@yrich:document:document_content_height")]]
static struct yetty_ycore_float_result document_default_content_height(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    (void)ctx;
    (void)obj;
    return YETTY_OK(yetty_ycore_float, 0.0f);
}

/*===========================================================================
 * Render slot — default iterates elements in order.
 *=========================================================================*/

[[clang::annotate("virtual@yrich:document:document_render")]] [[clang::annotate(
    "local@yrich:document_render")]]
static struct yetty_ycore_void_result document_default_render(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_render: data_get");
    struct yetty_yrich_document *doc = data_res.value;
    if (!doc->buffer) {
        return YETTY_ERR(yetty_ycore_void, "document_render: NULL buffer");
    }

    yetty_ydraw_drawable_list_clear(doc->buffer);
    struct yetty_ycore_float_result width_res = yetty_yrich_document_content_width(NULL, obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, width_res, "document_render: content_width");
    struct yetty_ycore_float_result height_res = yetty_yrich_document_content_height(NULL, obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, height_res, "document_render: content_height");
    yetty_ydraw_drawable_list_set_scene_bounds(doc->buffer, 0.0f, 0.0f, width_res.value,
                                               height_res.value);

    uint32_t layer = 0;
    for (size_t i = 0; i < doc->element_count; i++) {
        struct yetty_yclass_object *element_obj = doc->elements[i];
        struct yetty_yrich_element_id_result id_res = yetty_yrich_element_id_value(element_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "document_render: element id");
        int selected = yetty_yrich_document_is_selected(obj, id_res.value);
        struct yetty_ycore_void_result render_res =
            yetty_yrich_element_render(NULL, element_obj, doc->buffer, layer, selected);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "document_render: element failed");
        layer += 4; /* room for an element's selection / decoration / cursor layers */
    }

    doc->dirty = 0;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Operations / commands
 *=========================================================================*/

/* Apply an operation. `local_flag` non-zero marks a locally-originated op
 * (forwarded to the sync callback). Concrete kinds override to apply the
 * op to their own state and chain up for the base bookkeeping. */
[[clang::annotate("virtual@yrich:document:document_apply_op")]] [[clang::annotate(
    "local@yrich:document_apply_op")]]
static struct yetty_ycore_void_result document_default_apply_op(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj,
                                                                struct yetty_yrich_operation *op,
                                                                int local_flag)
{
    (void)ctx;
    if (!op) {
        return YETTY_ERR(yetty_ycore_void, "document_apply_op: NULL op");
    }
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_apply_op: data_get");
    struct yetty_yrich_document *doc = data_res.value;

    if (!yetty_yrich_operation_is_presence(op)) {
        /* Log retains a clone-like reference (type/ts/author only; payload
		 * stays with the caller's op). The log is best-effort: absorb its
		 * failures here. */
        struct yetty_yrich_operation_ptr_result clone_res =
            yetty_yrich_operation_create(op->type, op->timestamp, op->author);
        if (YETTY_IS_OK(clone_res)) {
            struct yetty_ycore_void_result append_res =
                yetty_yrich_op_log_append(&doc->op_log, clone_res.value);
            if (YETTY_IS_ERR(append_res)) {
                yetty_ycore_error_destroy(append_res.error);
            }
        } else {
            yetty_ycore_error_destroy(clone_res.error);
        }
    }

    if (local_flag && doc->sync_cb) {
        struct yetty_yrich_operation *batch[1] = {op};
        doc->sync_cb(obj, batch, 1, doc->sync_userdata);
    }

    return yetty_yrich_document_mark_dirty(obj);
}

/* Drop the undo/redo stacks — used after destructive resets (File > New)
 * whose recorded ops reference elements that no longer exist. */
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_document_clear_history(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_clear_history: data_get");
    yetty_yrich_history_clear(&data_res.value->history);
    return YETTY_OK_VOID();
}

/* Run a command and push it onto the history. Takes ownership of cmd. */
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_document_execute(struct yetty_yclass_object *obj,
                                                            struct yetty_yrich_command *cmd)
{
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_yrich_command_destroy(cmd);
        return YETTY_ERR(yetty_ycore_void, "document_execute: data_get", data_res);
    }
    return yetty_yrich_history_execute(&data_res.value->history, cmd, obj);
}

[[clang::annotate("virtual@yrich:document:document_undo")]]
static struct yetty_ycore_void_result document_default_undo(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_undo: data_get");
    struct yetty_ycore_void_result undo_res =
        yetty_yrich_history_undo(&data_res.value->history, obj);
    struct yetty_ycore_void_result dirty_res = yetty_yrich_document_mark_dirty(obj);
    if (YETTY_IS_ERR(dirty_res)) {
        yetty_ycore_error_destroy(dirty_res.error);
    }
    return undo_res;
}

[[clang::annotate("virtual@yrich:document:document_redo")]]
static struct yetty_ycore_void_result document_default_redo(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_redo: data_get");
    struct yetty_ycore_void_result redo_res =
        yetty_yrich_history_redo(&data_res.value->history, obj);
    struct yetty_ycore_void_result dirty_res = yetty_yrich_document_mark_dirty(obj);
    if (YETTY_IS_ERR(dirty_res)) {
        yetty_ycore_error_destroy(dirty_res.error);
    }
    return redo_res;
}

/*===========================================================================
 * Input slots — defaults; concrete kinds override as needed. Modifiers are
 * a YETTY_YRICH_MOD_* bitmask so the slots stay wire-marshallable.
 *=========================================================================*/

[[clang::annotate("virtual@yrich:document:document_on_mouse_down")]]
static struct yetty_ycore_void_result document_default_on_mouse_down(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, float x, float y,
    uint32_t button, uint32_t mods)
{
    (void)ctx;
    if (button != YETTY_YRICH_MOUSE_LEFT) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_on_mouse_down: data_get");
    struct yetty_yrich_document *doc = data_res.value;

    struct yetty_yclass_object *element_obj = yetty_yrich_document_element_at(obj, x, y);
    if (!element_obj) {
        return yetty_yrich_document_clear_selection(obj);
    }
    struct yetty_yrich_element_id_result id_res = yetty_yrich_element_id_value(element_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "document_on_mouse_down: element id");
    yetty_yrich_element_id id = id_res.value;

    struct yetty_ycore_void_result select_res;
    if (mods & YETTY_YRICH_MOD_SHIFT) {
        select_res = yetty_yrich_selection_extend_element(&doc->selection, id);
    } else if (mods & YETTY_YRICH_MOD_CTRL) {
        if (doc->selection.kind == YETTY_YRICH_SEL_ELEMENTS) {
            select_res = yetty_yrich_selection_toggle(&doc->selection, id);
        } else {
            select_res = yetty_yrich_selection_select_element(&doc->selection, id);
        }
    } else {
        select_res = yetty_yrich_selection_select_element(&doc->selection, id);
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, select_res, "document_on_mouse_down: selection update");
    return yetty_yrich_document_mark_dirty(obj);
}

[[clang::annotate("virtual@yrich:document:document_on_mouse_up")]]
static struct yetty_ycore_void_result document_default_on_mouse_up(struct yetty_yclass_ctx *ctx,
                                                                   struct yetty_yclass_object *obj,
                                                                   float x, float y,
                                                                   uint32_t button, uint32_t mods)
{
    (void)ctx;
    (void)obj;
    (void)x;
    (void)y;
    (void)button;
    (void)mods;
    return YETTY_OK_VOID();
}

[[clang::annotate("virtual@yrich:document:document_on_mouse_drag")]]
static struct yetty_ycore_void_result document_default_on_mouse_drag(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, float x, float y,
    uint32_t button, uint32_t mods)
{
    (void)ctx;
    (void)obj;
    (void)x;
    (void)y;
    (void)button;
    (void)mods;
    return YETTY_OK_VOID();
}

[[clang::annotate("virtual@yrich:document:document_on_mouse_double_click")]]
static struct yetty_ycore_void_result document_default_on_mouse_double_click(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, float x, float y,
    uint32_t button, uint32_t mods)
{
    (void)ctx;
    (void)mods;
    if (button != YETTY_YRICH_MOUSE_LEFT) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object *element_obj = yetty_yrich_document_element_at(obj, x, y);
    if (!element_obj) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_int_result editable_res = yetty_yrich_element_is_editable(NULL, element_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, editable_res, "document_on_mouse_double_click: editable");
    if (!editable_res.value) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result edit_res = yetty_yrich_element_begin_edit(NULL, element_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, edit_res, "document_on_mouse_double_click: begin_edit");
    return yetty_yrich_document_mark_dirty(obj);
}

[[clang::annotate("virtual@yrich:document:document_on_key_down")]]
static struct yetty_ycore_void_result document_default_on_key_down(struct yetty_yclass_ctx *ctx,
                                                                   struct yetty_yclass_object *obj,
                                                                   uint32_t key, uint32_t mods)
{
    (void)ctx;
    if (mods & YETTY_YRICH_MOD_CTRL) {
        switch (key) {
        case YETTY_YRICH_KEY_Z:
            if (mods & YETTY_YRICH_MOD_SHIFT) {
                return yetty_yrich_document_redo(NULL, obj);
            }
            return yetty_yrich_document_undo(NULL, obj);
        case YETTY_YRICH_KEY_Y:
            return yetty_yrich_document_redo(NULL, obj);
        default:
            break;
        }
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("virtual@yrich:document:document_on_text_input")]]
static struct yetty_ycore_void_result document_default_on_text_input(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, struct yetty_ycore_buffer text)
{
    (void)ctx;
    struct yetty_yrich_document_ptr_result data_res = ddata(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "document_on_text_input: data_get");
    struct yetty_yrich_document *doc = data_res.value;
    if (doc->selection.kind != YETTY_YRICH_SEL_TEXT) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object *element_obj =
        yetty_yrich_document_find(obj, doc->selection.u.text.element_id);
    if (!element_obj) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result insert_res =
        yetty_yrich_element_insert_text(NULL, element_obj, text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, insert_res, "document_on_text_input: insert_text");
    return yetty_yrich_document_mark_dirty(obj);
}

#ifdef YCLASS_CODEGEN
/* Header-destined: the shared yrich value types (callbacks, element id
 * Result) and the operation Result the public document API references. */
#include <yetty/yrich/yrich-operation.h>
#include <yetty/yrich/yrich-types.h>
#endif

#include "document.gen.c"
