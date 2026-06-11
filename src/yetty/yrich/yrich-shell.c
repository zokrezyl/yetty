/*
 * yrich-shell.c — ygui-decorated editor shells for yrich documents.
 *
 * Composes the public ygui widget API into a complete editor surface for
 * each of the three yrich document kinds (ydoc, yspreadsheet, yslides):
 *
 *     vbox(root)
 *       hbox(toolbar)   — labelled action buttons
 *       scrollarea      — hosts the yetty_ygui_yrich_view (the document)
 *       statusbar       — document info / position
 *
 * The document lives inside the view (which owns it); toolbar buttons
 * subscribe to CLICK and drive the document through its public API.
 */
#include <yetty/ygui/event.h>
#include <yetty/ygui/widget.h>
#include <yetty/ygui/widget.h>
#include <yetty/ygui/widgets/button.h>
#include <yetty/ygui/widgets/hbox.h>
#include <yetty/ygui/widgets/scrollarea.h>
#include <yetty/ygui/widgets/statusbar.h>
#include <yetty/ygui/widgets/vbox.h>
#include <yetty/ygui/widgets/yrich_view.h>
#include <yetty/yrich/yrich-shell.h>

#include <yetty/yrich/ydoc.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yslides.h>
#include <yetty/yrich/yspreadsheet.h>

#include <stdio.h>
#include <string.h>

/*-----------------------------------------------------------------------------
 * Small composition helpers.
 *---------------------------------------------------------------------------*/

static struct yetty_yclass_object_ptr_result add_child(const struct yetty_yclass *cls,
                                                       struct yetty_yclass_object *parent,
                                                       const char *css)
{
    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_add(parent, cls);
    if (YETTY_IS_ERR(r)) {
        return r;
    }
    if (css) {
        struct yetty_ycore_void_result cr = yetty_ygui_widget_apply_css(r.value, css);
        if (YETTY_IS_ERR(cr)) {
            return YETTY_ERR(yetty_yclass_object_ptr, "add_child: apply_css", cr);
        }
    }
    return r;
}

static struct yetty_yclass_object_ptr_result add_button(struct yetty_yclass_object *toolbar,
                                                        const char *label, yetty_ygui_event_cb cb,
                                                        void *userdata)
{
    struct yetty_yclass_object_ptr_result br = add_child(
        yetty_ygui_class_expect(yetty_ygui_button_class_get(), "yetty_ygui_button_class_get"),
        toolbar, NULL);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, br, "add_button: add");
    struct yetty_ycore_void_result lr = yetty_ygui_button_set_label(br.value, label);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, lr, "add_button: label");
    if (cb) {
        struct yetty_ycore_void_result sr =
            yetty_ygui_object_subscribe(br.value, YETTY_YGUI_EVENT_CLICK, cb, userdata);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, sr, "add_button: subscribe");
    }
    return br;
}

/* Fit the view to the document content size so the scrollarea can scroll
 * the full extent, then refresh the statusbar text. */
static struct yetty_ycore_void_result fit_and_status(struct yetty_yrich_editor *editor,
                                                     const char *kind)
{
    float cw = 0.0f, ch = 0.0f;
    struct yetty_ycore_void_result szr = yetty_ygui_yrich_view_content_size(editor->view, &cw, &ch);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, szr, "fit_and_status: content_size");
    if (cw < 1.0f) {
        cw = 1.0f;
    }
    if (ch < 1.0f) {
        ch = 1.0f;
    }
    struct yetty_ycore_void_result sr = yetty_ygui_widget_set_size(editor->view, cw, ch);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "fit_and_status: set_size");
    char left[128];
    snprintf(left, sizeof(left), "%s  %.0f x %.0f", kind, cw, ch);
    struct yetty_ycore_void_result lr = yetty_ygui_statusbar_set_left(editor->statusbar, left);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "fit_and_status: status left");
    return yetty_ygui_yrich_view_invalidate(editor->view);
}

/* Build the shared skeleton: root vbox + toolbar + scrollarea(view) +
 * statusbar. The document is attached by the caller. */
static struct yetty_ycore_void_result build_skeleton(struct yetty_yclass_object *parent,
                                                     struct yetty_yrich_editor *out)
{
    memset(out, 0, sizeof(*out));

    struct yetty_yclass_object_ptr_result rootr = add_child(
        yetty_ygui_class_expect(yetty_ygui_vbox_class_get(), "yetty_ygui_vbox_class_get"), parent,
        "flex-grow: 1; align-self: stretch; align-items: stretch; gap: 0; padding: 0;");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rootr, "build_skeleton: root");
    out->root = rootr.value;

    struct yetty_yclass_object_ptr_result barr =
        add_child(yetty_ygui_class_expect(yetty_ygui_hbox_class_get(), "yetty_ygui_hbox_class_get"),
                  out->root, "height: 34px; gap: 6px; padding: 4px; align-items: center;");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, barr, "build_skeleton: toolbar");
    out->toolbar = barr.value;

    struct yetty_yclass_object_ptr_result scr =
        add_child(yetty_ygui_class_expect(yetty_ygui_scrollarea_class_get(),
                                          "yetty_ygui_scrollarea_class_get"),
                  out->root, "flex-grow: 1; align-items: stretch;");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scr, "build_skeleton: scrollarea");

    struct yetty_yclass_object_ptr_result viewr =
        add_child(yetty_ygui_class_expect(yetty_ygui_yrich_view_class_get(),
                                          "yetty_ygui_yrich_view_class_get"),
                  scr.value, NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, viewr, "build_skeleton: view");
    out->view = viewr.value;

    struct yetty_yclass_object_ptr_result statr = add_child(
        yetty_ygui_class_expect(yetty_ygui_statusbar_class_get(), "yetty_ygui_statusbar_class_get"),
        out->root, "height: 24px;");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, statr, "build_skeleton: statusbar");
    out->statusbar = statr.value;

    return YETTY_OK_VOID();
}

/*-----------------------------------------------------------------------------
 * Toolbar action callbacks — userdata is the yrich_view object.
 *---------------------------------------------------------------------------*/

static struct yetty_ycore_void_result act_undo(struct yetty_yclass_ctx *ctx,
                                               struct yetty_yclass_object *target,
                                               const struct yetty_ygui_event *event, void *userdata)
{
    (void)ctx;
    (void)target;
    (void)event;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yrich_document *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_ycore_void_result r = yetty_yrich_document_undo(doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "act_undo");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result act_redo(struct yetty_yclass_ctx *ctx,
                                               struct yetty_yclass_object *target,
                                               const struct yetty_ygui_event *event, void *userdata)
{
    (void)ctx;
    (void)target;
    (void)event;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yrich_document *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_ycore_void_result r = yetty_yrich_document_redo(doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "act_redo");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result act_add_paragraph(struct yetty_yclass_ctx *ctx,
                                                        struct yetty_yclass_object *target,
                                                        const struct yetty_ygui_event *event,
                                                        void *userdata)
{
    (void)ctx;
    (void)target;
    (void)event;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yrich_document *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_yrich_ydoc *d = (struct yetty_yrich_ydoc *)doc;
        const char *text = "New paragraph";
        struct yetty_yrich_paragraph_ptr_result pr =
            yetty_yrich_ydoc_add_paragraph(d, text, strlen(text));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "act_add_paragraph");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result act_slide_prev(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *target,
                                                     const struct yetty_ygui_event *event,
                                                     void *userdata)
{
    (void)ctx;
    (void)target;
    (void)event;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yrich_document *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        yetty_yrich_slides_prev((struct yetty_yrich_slides *)doc);
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result act_slide_next(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *target,
                                                     const struct yetty_ygui_event *event,
                                                     void *userdata)
{
    (void)ctx;
    (void)target;
    (void)event;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yrich_document *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        yetty_yrich_slides_next((struct yetty_yrich_slides *)doc);
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result act_slide_add(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *target,
                                                    const struct yetty_ygui_event *event,
                                                    void *userdata)
{
    (void)ctx;
    (void)target;
    (void)event;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yrich_document *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        yetty_yrich_slides_add_slide((struct yetty_yrich_slides *)doc);
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

/*-----------------------------------------------------------------------------
 * Public builders.
 *---------------------------------------------------------------------------*/

struct yetty_ycore_void_result yetty_yrich_ydoc_editor_create(struct yetty_yclass_object *parent,
                                                              struct yetty_yrich_editor *out)
{
    if (!out) {
        return YETTY_ERR(yetty_ycore_void, "ydoc_editor_create: NULL out");
    }
    struct yetty_ycore_void_result sk = build_skeleton(parent, out);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sk, "ydoc_editor_create: skeleton");

    struct yetty_yrich_ydoc_ptr_result dr = yetty_yrich_ydoc_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "ydoc_editor_create: ydoc_create");
    out->doc = &dr.value->base;
    struct yetty_ycore_void_result ar = yetty_ygui_yrich_view_set_document(out->view, out->doc, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "ydoc_editor_create: set_document");

    struct yetty_yclass_object_ptr_result b0 =
        add_button(out->toolbar, "Undo", act_undo, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b0, "ydoc_editor_create: undo");
    struct yetty_yclass_object_ptr_result b1 =
        add_button(out->toolbar, "Redo", act_redo, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b1, "ydoc_editor_create: redo");
    struct yetty_yclass_object_ptr_result b2 =
        add_button(out->toolbar, "+ Paragraph", act_add_paragraph, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b2, "ydoc_editor_create: add_paragraph");

    return fit_and_status(out, "ydoc");
}

struct yetty_ycore_void_result yetty_yrich_ysheet_editor_create(struct yetty_yclass_object *parent,
                                                                struct yetty_yrich_editor *out)
{
    if (!out) {
        return YETTY_ERR(yetty_ycore_void, "ysheet_editor_create: NULL out");
    }
    struct yetty_ycore_void_result sk = build_skeleton(parent, out);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sk, "ysheet_editor_create: skeleton");

    struct yetty_yrich_spreadsheet_ptr_result dr = yetty_yrich_spreadsheet_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "ysheet_editor_create: spreadsheet_create");
    yetty_yrich_spreadsheet_set_grid_size(dr.value, 20, 8);
    out->doc = &dr.value->base;
    struct yetty_ycore_void_result ar = yetty_ygui_yrich_view_set_document(out->view, out->doc, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "ysheet_editor_create: set_document");

    struct yetty_yclass_object_ptr_result b0 =
        add_button(out->toolbar, "Undo", act_undo, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b0, "ysheet_editor_create: undo");
    struct yetty_yclass_object_ptr_result b1 =
        add_button(out->toolbar, "Redo", act_redo, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b1, "ysheet_editor_create: redo");

    return fit_and_status(out, "ysheet");
}

struct yetty_ycore_void_result yetty_yrich_yslide_editor_create(struct yetty_yclass_object *parent,
                                                                struct yetty_yrich_editor *out)
{
    if (!out) {
        return YETTY_ERR(yetty_ycore_void, "yslide_editor_create: NULL out");
    }
    struct yetty_ycore_void_result sk = build_skeleton(parent, out);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sk, "yslide_editor_create: skeleton");

    struct yetty_yrich_slides_ptr_result dr = yetty_yrich_slides_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "yslide_editor_create: slides_create");
    yetty_yrich_slides_add_slide(dr.value);
    out->doc = &dr.value->base;
    struct yetty_ycore_void_result ar = yetty_ygui_yrich_view_set_document(out->view, out->doc, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "yslide_editor_create: set_document");

    struct yetty_yclass_object_ptr_result b0 =
        add_button(out->toolbar, "Prev", act_slide_prev, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b0, "yslide_editor_create: prev");
    struct yetty_yclass_object_ptr_result b1 =
        add_button(out->toolbar, "Next", act_slide_next, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b1, "yslide_editor_create: next");
    struct yetty_yclass_object_ptr_result b2 =
        add_button(out->toolbar, "+ Slide", act_slide_add, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b2, "yslide_editor_create: add");

    return fit_and_status(out, "yslide");
}

struct yetty_ycore_void_result yetty_yrich_editor_refresh(struct yetty_yrich_editor *editor)
{
    if (!editor || !editor->view) {
        return YETTY_ERR(yetty_ycore_void, "yrich_editor_refresh: NULL");
    }
    return fit_and_status(editor, "");
}
