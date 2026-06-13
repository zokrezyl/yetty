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
#include <yetty/ygui/widgets/menubar.h>
#include <yetty/ygui/widgets/popup_menu.h>
#include <yetty/ygui/widgets/scrollarea.h>
#include <yetty/ygui/widgets/statusbar.h>
#include <yetty/ygui/widgets/vbox.h>
#include <yetty/ygui/widgets/yrich_view.h>
#include <yetty/yrich/yrich-shell.h>

#include <yetty/yrich/yrich-operation.h>
#include <yetty/yrich/yrich-types.h>

#include <yetty/yrich/document.h>
#include <yetty/yrich/slides.h>
#include <yetty/yrich/spreadsheet.h>
#include <yetty/yrich/ydoc.h>
#include <yetty/yrich/yrich-yaml.h>

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
            yetty_ygui_widget_subscribe(br.value, YETTY_YGUI_EVENT_CLICK, cb, userdata);
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

/* Build the shared skeleton: root vbox + optional menubar + toolbar +
 * scrollarea(view) + statusbar. The document is attached by the caller. */
static struct yetty_ycore_void_result build_skeleton(struct yetty_yclass_object *parent,
                                                     struct yetty_yrich_editor *out,
                                                     int with_menubar)
{
    memset(out, 0, sizeof(*out));

    struct yetty_yclass_object_ptr_result rootr = add_child(
        yetty_ygui_class_expect(yetty_ygui_vbox_class_get(), "yetty_ygui_vbox_class_get"), parent,
        "flex-grow: 1; align-self: stretch; align-items: stretch; gap: 0; padding: 0;");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rootr, "build_skeleton: root");
    out->root = rootr.value;

    if (with_menubar) {
        struct yetty_yclass_object_ptr_result menubar_res = add_child(
            yetty_ygui_class_expect(yetty_ygui_menubar_class_get(), "yetty_ygui_menubar_class_get"),
            out->root, "height: 30px; gap: 2px; padding: 2px;");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, menubar_res, "build_skeleton: menubar");
        out->menubar = menubar_res.value;
    }

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
    struct yetty_yclass_object *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_ycore_void_result r = yetty_yrich_document_undo(NULL, doc);
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
    struct yetty_yclass_object *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_ycore_void_result r = yetty_yrich_document_redo(NULL, doc);
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
    struct yetty_yclass_object *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        const char *text = "New paragraph";
        struct yetty_yclass_object_ptr_result pr =
            yetty_yrich_ydoc_add_paragraph(doc, text, strlen(text));
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
    struct yetty_yclass_object *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_ycore_void_result r = yetty_yrich_slides_prev(NULL, doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "act_slide_prev");
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
    struct yetty_yclass_object *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_ycore_void_result r = yetty_yrich_slides_next(NULL, doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "act_slide_next");
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
    struct yetty_yclass_object *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_yrich_slide_ptr_result slide_res = yetty_yrich_slides_add_slide(doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, slide_res, "act_slide_add");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

/*-----------------------------------------------------------------------------
 * ydoc format actions — shared by the Format menu, the toolbar buttons and
 * (via Ctrl shortcuts) the document itself. userdata is the yrich_view.
 *---------------------------------------------------------------------------*/

static struct yetty_ycore_void_result ydoc_format_apply(struct yetty_yclass_object *view,
                                                        uint32_t format_flag)
{
    struct yetty_yclass_object *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_ycore_void_result toggle_res =
            yetty_yrich_ydoc_toggle_format(NULL, doc, format_flag);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, toggle_res, "ydoc_format_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result ydoc_font_size_apply(struct yetty_yclass_object *view,
                                                           float delta)
{
    struct yetty_yclass_object *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_ycore_void_result size_res =
            yetty_yrich_ydoc_change_font_size(NULL, doc, delta);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, size_res, "ydoc_font_size_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

#define YDOC_FORMAT_BUTTON_CB(name, flag)                                                          \
    static struct yetty_ycore_void_result name(                                                    \
        struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *target,                          \
        const struct yetty_ygui_event *event, void *userdata)                                      \
    {                                                                                              \
        (void)ctx;                                                                                 \
        (void)target;                                                                              \
        (void)event;                                                                               \
        return ydoc_format_apply(userdata, flag);                                                  \
    }

YDOC_FORMAT_BUTTON_CB(act_format_bold, YETTY_YRICH_FMT_BOLD)
YDOC_FORMAT_BUTTON_CB(act_format_italic, YETTY_YRICH_FMT_ITALIC)
YDOC_FORMAT_BUTTON_CB(act_format_underline, YETTY_YRICH_FMT_UNDERLINE)
YDOC_FORMAT_BUTTON_CB(act_format_strike, YETTY_YRICH_FMT_STRIKE)

static struct yetty_ycore_void_result act_font_larger(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *target,
                                                      const struct yetty_ygui_event *event,
                                                      void *userdata)
{
    (void)ctx;
    (void)target;
    (void)event;
    return ydoc_font_size_apply(userdata, 2.0f);
}

static struct yetty_ycore_void_result act_font_smaller(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *target,
                                                       const struct yetty_ygui_event *event,
                                                       void *userdata)
{
    (void)ctx;
    (void)target;
    (void)event;
    return ydoc_font_size_apply(userdata, -2.0f);
}

/*-----------------------------------------------------------------------------
 * ydoc menu items — popup_menu callbacks. userdata is the yrich_view.
 *---------------------------------------------------------------------------*/

static struct yetty_ycore_void_result menu_file_new(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *menu,
                                                    int item_index, void *userdata)
{
    (void)ctx;
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_ycore_void_result clear_res = yetty_yrich_ydoc_clear(doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_res, "menu_file_new");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result menu_file_save(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *menu,
                                                     int item_index, void *userdata)
{
    (void)ctx;
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object *doc = yetty_ygui_yrich_view_document(view);
    if (!doc) {
        return YETTY_OK_VOID();
    }
    const char *path = yetty_yrich_ydoc_source_path(doc);
    if (!path) {
        path = "untitled.ydoc.yaml";
    }
    return yetty_yrich_ydoc_save_yaml_file(doc, path);
}

static struct yetty_ycore_void_result menu_edit_undo(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *menu,
                                                     int item_index, void *userdata)
{
    (void)ctx;
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_ycore_void_result undo_res = yetty_yrich_document_undo(NULL, doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, undo_res, "menu_edit_undo");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result menu_edit_redo(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *menu,
                                                     int item_index, void *userdata)
{
    (void)ctx;
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_ycore_void_result redo_res = yetty_yrich_document_redo(NULL, doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, redo_res, "menu_edit_redo");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

#define YDOC_FORMAT_MENU_CB(name, flag)                                                            \
    static struct yetty_ycore_void_result name(struct yetty_yclass_ctx *ctx,                       \
                                               struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)ctx;                                                                                 \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return ydoc_format_apply(userdata, flag);                                                  \
    }

YDOC_FORMAT_MENU_CB(menu_format_bold, YETTY_YRICH_FMT_BOLD)
YDOC_FORMAT_MENU_CB(menu_format_italic, YETTY_YRICH_FMT_ITALIC)
YDOC_FORMAT_MENU_CB(menu_format_underline, YETTY_YRICH_FMT_UNDERLINE)
YDOC_FORMAT_MENU_CB(menu_format_strike, YETTY_YRICH_FMT_STRIKE)

static struct yetty_ycore_void_result menu_format_larger(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *menu,
                                                         int item_index, void *userdata)
{
    (void)ctx;
    (void)menu;
    (void)item_index;
    return ydoc_font_size_apply(userdata, 2.0f);
}

static struct yetty_ycore_void_result menu_format_smaller(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *menu,
                                                          int item_index, void *userdata)
{
    (void)ctx;
    (void)menu;
    (void)item_index;
    return ydoc_font_size_apply(userdata, -2.0f);
}

static struct yetty_ycore_void_result ydoc_alignment_apply(struct yetty_yclass_object *view,
                                                           uint32_t halign)
{
    struct yetty_yclass_object *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_ycore_void_result align_res =
            yetty_yrich_ydoc_set_alignment(NULL, doc, halign);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, align_res, "ydoc_alignment_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result ydoc_heading_apply(struct yetty_yclass_object *view,
                                                         uint32_t level)
{
    struct yetty_yclass_object *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_ycore_void_result heading_res = yetty_yrich_ydoc_set_heading(NULL, doc, level);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, heading_res, "ydoc_heading_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result ydoc_color_apply(struct yetty_yclass_object *view,
                                                       uint32_t color)
{
    struct yetty_yclass_object *doc = yetty_ygui_yrich_view_document(view);
    if (doc) {
        struct yetty_ycore_void_result color_res =
            yetty_yrich_ydoc_set_text_color(NULL, doc, color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, color_res, "ydoc_color_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

#define YDOC_MENU_ALIGN_CB(name, value)                                                            \
    static struct yetty_ycore_void_result name(struct yetty_yclass_ctx *ctx,                       \
                                               struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)ctx;                                                                                 \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return ydoc_alignment_apply(userdata, value);                                              \
    }

YDOC_MENU_ALIGN_CB(menu_align_left, YETTY_YRICH_HALIGN_LEFT)
YDOC_MENU_ALIGN_CB(menu_align_center, YETTY_YRICH_HALIGN_CENTER)
YDOC_MENU_ALIGN_CB(menu_align_right, YETTY_YRICH_HALIGN_RIGHT)

#define YDOC_MENU_HEADING_CB(name, value)                                                          \
    static struct yetty_ycore_void_result name(struct yetty_yclass_ctx *ctx,                       \
                                               struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)ctx;                                                                                 \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return ydoc_heading_apply(userdata, value);                                                \
    }

YDOC_MENU_HEADING_CB(menu_heading_normal, 0)
YDOC_MENU_HEADING_CB(menu_heading_1, 1)
YDOC_MENU_HEADING_CB(menu_heading_2, 2)
YDOC_MENU_HEADING_CB(menu_heading_3, 3)

#define YDOC_MENU_COLOR_CB(name, value)                                                            \
    static struct yetty_ycore_void_result name(struct yetty_yclass_ctx *ctx,                       \
                                               struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)ctx;                                                                                 \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return ydoc_color_apply(userdata, value);                                                  \
    }

YDOC_MENU_COLOR_CB(menu_color_default, YETTY_YRICH_COLOR_BLACK)
YDOC_MENU_COLOR_CB(menu_color_red, YETTY_YRICH_RGBA(200, 30, 30, 255))
YDOC_MENU_COLOR_CB(menu_color_green, YETTY_YRICH_RGBA(20, 140, 60, 255))
YDOC_MENU_COLOR_CB(menu_color_blue, YETTY_YRICH_RGBA(30, 60, 200, 255))

#define YDOC_ALIGN_BUTTON_CB(name, value)                                                          \
    static struct yetty_ycore_void_result name(                                                    \
        struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *target,                          \
        const struct yetty_ygui_event *event, void *userdata)                                      \
    {                                                                                              \
        (void)ctx;                                                                                 \
        (void)target;                                                                              \
        (void)event;                                                                               \
        return ydoc_alignment_apply(userdata, value);                                              \
    }

YDOC_ALIGN_BUTTON_CB(act_align_left, YETTY_YRICH_HALIGN_LEFT)
YDOC_ALIGN_BUTTON_CB(act_align_center, YETTY_YRICH_HALIGN_CENTER)
YDOC_ALIGN_BUTTON_CB(act_align_right, YETTY_YRICH_HALIGN_RIGHT)

/* One popup menu, attached under the editor root as a floating overlay. */
static struct yetty_yclass_object_ptr_result add_menu(struct yetty_yrich_editor *out,
                                                      const char *label)
{
    struct yetty_yclass_object_ptr_result menu_res =
        add_child(yetty_ygui_class_expect(yetty_ygui_popup_menu_class_get(),
                                          "yetty_ygui_popup_menu_class_get"),
                  out->root, NULL);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, menu_res, "add_menu: popup");
    struct yetty_ycore_void_result bind_res =
        yetty_ygui_menubar_add(out->menubar, label, menu_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, bind_res, "add_menu: menubar bind");
    return menu_res;
}

static struct yetty_ycore_void_result build_ydoc_menus(struct yetty_yrich_editor *out)
{
    struct yetty_yclass_object_ptr_result file_res = add_menu(out, "File");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, file_res, "ydoc menus: File");
    struct yetty_ycore_void_result item_res =
        yetty_ygui_popup_menu_add_item(file_res.value, "New", menu_file_new, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: New");
    item_res = yetty_ygui_popup_menu_add_item(file_res.value, "Save", menu_file_save, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Save");

    struct yetty_yclass_object_ptr_result edit_res = add_menu(out, "Edit");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, edit_res, "ydoc menus: Edit");
    item_res = yetty_ygui_popup_menu_add_item(edit_res.value, "Undo        Ctrl+Z", menu_edit_undo,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Undo");
    item_res = yetty_ygui_popup_menu_add_item(edit_res.value, "Redo        Ctrl+Y", menu_edit_redo,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Redo");

    struct yetty_yclass_object_ptr_result format_res = add_menu(out, "Format");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, format_res, "ydoc menus: Format");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Bold        Ctrl+B",
                                              menu_format_bold, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Bold");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Italic      Ctrl+I",
                                              menu_format_italic, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Italic");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Underline   Ctrl+U",
                                              menu_format_underline, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Underline");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Strikethrough", menu_format_strike,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Strikethrough");
    item_res = yetty_ygui_popup_menu_add_separator(format_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: separator");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Larger text", menu_format_larger,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Larger");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Smaller text", menu_format_smaller,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Smaller");
    item_res = yetty_ygui_popup_menu_add_separator(format_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: separator 2");
    item_res =
        yetty_ygui_popup_menu_add_item(format_res.value, "Align left", menu_align_left, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: align left");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Align center", menu_align_center,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: align center");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Align right", menu_align_right,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: align right");
    item_res = yetty_ygui_popup_menu_add_separator(format_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: separator 3");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Text: default", menu_color_default,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: color default");
    item_res =
        yetty_ygui_popup_menu_add_item(format_res.value, "Text: red", menu_color_red, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: color red");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Text: green", menu_color_green,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: color green");
    item_res =
        yetty_ygui_popup_menu_add_item(format_res.value, "Text: blue", menu_color_blue, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: color blue");

    struct yetty_yclass_object_ptr_result styles_res = add_menu(out, "Styles");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, styles_res, "ydoc menus: Styles");
    item_res = yetty_ygui_popup_menu_add_item(styles_res.value, "Normal text", menu_heading_normal,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: normal");
    item_res =
        yetty_ygui_popup_menu_add_item(styles_res.value, "Heading 1", menu_heading_1, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: h1");
    item_res =
        yetty_ygui_popup_menu_add_item(styles_res.value, "Heading 2", menu_heading_2, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: h2");
    item_res =
        yetty_ygui_popup_menu_add_item(styles_res.value, "Heading 3", menu_heading_3, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: h3");
    return YETTY_OK_VOID();
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
    struct yetty_ycore_void_result sk = build_skeleton(parent, out, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sk, "ydoc_editor_create: skeleton");

    struct yetty_yclass_object_ptr_result dr = yetty_yrich_ydoc_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "ydoc_editor_create: ydoc_create");
    out->doc = dr.value;
    struct yetty_ycore_void_result ar = yetty_ygui_yrich_view_set_document(out->view, out->doc, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "ydoc_editor_create: set_document");

    struct yetty_ycore_void_result menus_res = build_ydoc_menus(out);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, menus_res, "ydoc_editor_create: menus");

    struct yetty_yclass_object_ptr_result b0 =
        add_button(out->toolbar, "Undo", act_undo, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b0, "ydoc_editor_create: undo");
    struct yetty_yclass_object_ptr_result b1 =
        add_button(out->toolbar, "Redo", act_redo, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b1, "ydoc_editor_create: redo");
    struct yetty_yclass_object_ptr_result b2 =
        add_button(out->toolbar, "+ Paragraph", act_add_paragraph, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b2, "ydoc_editor_create: add_paragraph");
    struct yetty_yclass_object_ptr_result b3 =
        add_button(out->toolbar, "B", act_format_bold, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b3, "ydoc_editor_create: bold");
    struct yetty_yclass_object_ptr_result b4 =
        add_button(out->toolbar, "I", act_format_italic, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b4, "ydoc_editor_create: italic");
    struct yetty_yclass_object_ptr_result b5 =
        add_button(out->toolbar, "U", act_format_underline, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b5, "ydoc_editor_create: underline");
    struct yetty_yclass_object_ptr_result b6 =
        add_button(out->toolbar, "S", act_format_strike, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b6, "ydoc_editor_create: strike");
    struct yetty_yclass_object_ptr_result b7 =
        add_button(out->toolbar, "A+", act_font_larger, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b7, "ydoc_editor_create: larger");
    struct yetty_yclass_object_ptr_result b8 =
        add_button(out->toolbar, "A-", act_font_smaller, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b8, "ydoc_editor_create: smaller");
    struct yetty_yclass_object_ptr_result b9 =
        add_button(out->toolbar, "L", act_align_left, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b9, "ydoc_editor_create: align left");
    struct yetty_yclass_object_ptr_result b10 =
        add_button(out->toolbar, "C", act_align_center, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b10, "ydoc_editor_create: align center");
    struct yetty_yclass_object_ptr_result b11 =
        add_button(out->toolbar, "R", act_align_right, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, b11, "ydoc_editor_create: align right");

    return fit_and_status(out, "ydoc");
}

struct yetty_ycore_void_result yetty_yrich_ysheet_editor_create(struct yetty_yclass_object *parent,
                                                                struct yetty_yrich_editor *out)
{
    if (!out) {
        return YETTY_ERR(yetty_ycore_void, "ysheet_editor_create: NULL out");
    }
    struct yetty_ycore_void_result sk = build_skeleton(parent, out, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sk, "ysheet_editor_create: skeleton");

    struct yetty_yclass_object_ptr_result dr = yetty_yrich_spreadsheet_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "ysheet_editor_create: spreadsheet_create");
    struct yetty_ycore_void_result grid_res =
        yetty_yrich_spreadsheet_set_grid_size(NULL, dr.value, 20, 8);
    if (YETTY_IS_ERR(grid_res)) {
        struct yetty_ycore_void_result destroy_res = yetty_yrich_document_destroy(NULL, dr.value);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        return YETTY_ERR(yetty_ycore_void, "ysheet_editor_create: set_grid_size", grid_res);
    }
    out->doc = dr.value;
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
    struct yetty_ycore_void_result sk = build_skeleton(parent, out, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sk, "yslide_editor_create: skeleton");

    struct yetty_yclass_object_ptr_result dr = yetty_yrich_slides_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "yslide_editor_create: slides_create");
    struct yetty_yrich_slide_ptr_result slide_res = yetty_yrich_slides_add_slide(dr.value);
    if (YETTY_IS_ERR(slide_res)) {
        struct yetty_ycore_void_result destroy_res = yetty_yrich_document_destroy(NULL, dr.value);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        return YETTY_ERR(yetty_ycore_void, "yslide_editor_create: add_slide failed", slide_res);
    }
    out->doc = dr.value;
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
