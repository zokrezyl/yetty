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
#include <yetty/yrich/yrich-export.h>
#include <yetty/yrich/yrich-yaml.h>

#include <stdio.h>
#include <stdlib.h>
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
    /* Explicit width AND height. Over the wire a button's content-fit size is
     * not resolved, so without a pinned width it collapses horizontally, and the
     * toolbar's `align-items: center` (unlike the menubar, which stretches its
     * children) leaves the button at zero height — either way it never paints.
     * Width sizes to the byte length (a slight over-estimate for multibyte glyphs
     * is harmless); height fits inside the 34px toolbar. */
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(br.value);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, layout_res, "add_button: layout_get");
        struct yetty_ygui_layout button_layout = *layout_res.value;
        button_layout.width = 24.0f + (float)strlen(label) * 10.0f;
        button_layout.height = 26.0f;
        struct yetty_ycore_void_result wr = yetty_ygui_widget_layout_set(br.value, &button_layout);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, wr, "add_button: layout_set");
    }
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

    /* Live document statistics on the right — ydoc only. The kind string is
     * empty on refresh, so gate on the document's actual class instead: an
     * exact minted-class-pointer compare (a downcast `_from` is not a type
     * test — it succeeds on any widget). */
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(editor->view);
    if (YETTY_IS_OK(doc_res) && doc_res.value) {
        struct yetty_yclass_ptr_result doc_class = yetty_yclass_object_class(doc_res.value);
        struct yetty_yclass_ptr_result ydoc_class = yetty_yrich_ydoc_class_get();
        int is_ydoc = YETTY_IS_OK(doc_class) && YETTY_IS_OK(ydoc_class) &&
                      doc_class.value == ydoc_class.value;
        if (YETTY_IS_ERR(doc_class)) {
            yetty_ycore_error_destroy(doc_class.error);
        }
        if (is_ydoc) {
            uint32_t words = 0;
            uint32_t chars = 0;
            uint32_t chars_no_spaces = 0;
            uint32_t paragraphs = 0;
            struct yetty_ycore_void_result count_res = yetty_yrich_ydoc_word_count(
                doc_res.value, &words, &chars, &chars_no_spaces, &paragraphs);
            if (YETTY_IS_OK(count_res)) {
                char right[128];
                snprintf(right, sizeof(right), "%u words  %u chars (%u no spaces)  %u paras", words,
                         chars, chars_no_spaces, paragraphs);
                struct yetty_ycore_void_result rr =
                    yetty_ygui_statusbar_set_right(editor->statusbar, right);
                if (YETTY_IS_ERR(rr)) {
                    yetty_ycore_error_destroy(rr.error);
                }
            } else {
                yetty_ycore_error_destroy(count_res.error);
            }
        }
    } else if (YETTY_IS_ERR(doc_res)) {
        yetty_ycore_error_destroy(doc_res.error);
    }
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

static struct yetty_ycore_void_result act_undo(struct yetty_yclass_object *target,
                                               const struct yetty_ygui_event *event, void *userdata)
{
    (void)target;
    (void)event;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "act_undo: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result r = yetty_yrich_document_undo(doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "act_undo");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result act_redo(struct yetty_yclass_object *target,
                                               const struct yetty_ygui_event *event, void *userdata)
{
    (void)target;
    (void)event;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "act_redo: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result r = yetty_yrich_document_redo(doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "act_redo");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result act_add_paragraph(struct yetty_yclass_object *target,
                                                        const struct yetty_ygui_event *event,
                                                        void *userdata)
{
    (void)target;
    (void)event;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "act_add_paragraph: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        const char *text = "New paragraph";
        struct yetty_yclass_object_ptr_result pr =
            yetty_yrich_ydoc_add_paragraph(doc, text, strlen(text));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "act_add_paragraph");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result act_slide_prev(struct yetty_yclass_object *target,
                                                     const struct yetty_ygui_event *event,
                                                     void *userdata)
{
    (void)target;
    (void)event;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "act_slide_prev: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result r = yetty_yrich_slides_prev(doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "act_slide_prev");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result act_slide_next(struct yetty_yclass_object *target,
                                                     const struct yetty_ygui_event *event,
                                                     void *userdata)
{
    (void)target;
    (void)event;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "act_slide_next: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result r = yetty_yrich_slides_next(doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "act_slide_next");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result act_slide_add(struct yetty_yclass_object *target,
                                                    const struct yetty_ygui_event *event,
                                                    void *userdata)
{
    (void)target;
    (void)event;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "act_slide_add: document");
    struct yetty_yclass_object *doc = doc_res.value;
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
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "ydoc_format_apply: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result toggle_res =
            yetty_yrich_ydoc_toggle_format(doc, format_flag);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, toggle_res, "ydoc_format_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result ydoc_font_size_apply(struct yetty_yclass_object *view,
                                                           float delta)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "ydoc_font_size_apply: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result size_res = yetty_yrich_ydoc_change_font_size(doc, delta);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, size_res, "ydoc_font_size_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

#define YDOC_FORMAT_BUTTON_CB(name, flag)                                                          \
    static struct yetty_ycore_void_result name(                                                    \
        struct yetty_yclass_object *target, const struct yetty_ygui_event *event, void *userdata)  \
    {                                                                                              \
        (void)target;                                                                              \
        (void)event;                                                                               \
        return ydoc_format_apply(userdata, flag);                                                  \
    }

YDOC_FORMAT_BUTTON_CB(act_format_bold, YETTY_YRICH_FMT_BOLD)
YDOC_FORMAT_BUTTON_CB(act_format_italic, YETTY_YRICH_FMT_ITALIC)
YDOC_FORMAT_BUTTON_CB(act_format_underline, YETTY_YRICH_FMT_UNDERLINE)
YDOC_FORMAT_BUTTON_CB(act_format_strike, YETTY_YRICH_FMT_STRIKE)

static struct yetty_ycore_void_result act_font_larger(struct yetty_yclass_object *target,
                                                      const struct yetty_ygui_event *event,
                                                      void *userdata)
{
    (void)target;
    (void)event;
    return ydoc_font_size_apply(userdata, 2.0f);
}

static struct yetty_ycore_void_result act_font_smaller(struct yetty_yclass_object *target,
                                                       const struct yetty_ygui_event *event,
                                                       void *userdata)
{
    (void)target;
    (void)event;
    return ydoc_font_size_apply(userdata, -2.0f);
}

/*-----------------------------------------------------------------------------
 * ydoc menu items — popup_menu callbacks. userdata is the yrich_view.
 *---------------------------------------------------------------------------*/

static struct yetty_ycore_void_result menu_file_new(struct yetty_yclass_object *menu,
                                                    int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_file_new: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result clear_res = yetty_yrich_ydoc_clear(doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_res, "menu_file_new");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result menu_file_save(struct yetty_yclass_object *menu,
                                                     int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_file_save: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (!doc) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_const_char_ptr_result path_res = yetty_yrich_ydoc_source_path(doc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, path_res, "menu_file_save: source path");
    const char *path = path_res.value;
    if (!path) {
        path = "untitled.ydoc.yaml";
    }
    return yetty_yrich_ydoc_save_yaml_file(doc, path);
}

/* Derive an export path next to the document's source (or "untitled"),
 * swapping in `extension` (e.g. ".md"). Writes into `out` (size `out_size`). */
static void export_path_for(struct yetty_yclass_object *doc, const char *extension, char *out,
                            size_t out_size)
{
    const char *base = "untitled";
    struct yetty_ycore_const_char_ptr_result path_res = yetty_yrich_ydoc_source_path(doc);
    if (YETTY_IS_OK(path_res) && path_res.value) {
        base = path_res.value;
    } else if (YETTY_IS_ERR(path_res)) {
        yetty_ycore_error_destroy(path_res.error);
    }
    /* Strip a known ".ydoc.yaml" / ".yaml" / existing extension tail. */
    size_t base_len = strlen(base);
    const char *dot = strrchr(base, '.');
    if (dot && dot != base) {
        base_len = (size_t)(dot - base);
        /* Also drop a ".ydoc" stem so foo.ydoc.yaml -> foo.md. */
        if (base_len > 5 && strncmp(base + base_len - 5, ".ydoc", 5) == 0) {
            base_len -= 5;
        }
    }
    if (base_len >= out_size) {
        base_len = out_size - 1;
    }
    snprintf(out, out_size, "%.*s%s", (int)base_len, base, extension);
}

static struct yetty_ycore_void_result menu_file_export_markdown(struct yetty_yclass_object *menu,
                                                                int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_file_export_markdown: document");
    if (doc_res.value) {
        char path[512];
        export_path_for(doc_res.value, ".md", path, sizeof(path));
        struct yetty_ycore_void_result res =
            yetty_yrich_ydoc_export_markdown_file(doc_res.value, path);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "menu_file_export_markdown");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result menu_file_export_html(struct yetty_yclass_object *menu,
                                                            int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_file_export_html: document");
    if (doc_res.value) {
        char path[512];
        export_path_for(doc_res.value, ".html", path, sizeof(path));
        struct yetty_ycore_void_result res = yetty_yrich_ydoc_export_html_file(doc_res.value, path);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "menu_file_export_html");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result menu_file_export_rtf(struct yetty_yclass_object *menu,
                                                           int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_file_export_rtf: document");
    if (doc_res.value) {
        char path[512];
        export_path_for(doc_res.value, ".rtf", path, sizeof(path));
        struct yetty_ycore_void_result res = yetty_yrich_ydoc_export_rtf_file(doc_res.value, path);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "menu_file_export_rtf");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result menu_file_export_text(struct yetty_yclass_object *menu,
                                                            int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_file_export_text: document");
    if (doc_res.value) {
        char path[512];
        export_path_for(doc_res.value, ".txt", path, sizeof(path));
        struct yetty_ycore_void_result res = yetty_yrich_ydoc_export_text_file(doc_res.value, path);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "menu_file_export_text");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result menu_edit_undo(struct yetty_yclass_object *menu,
                                                     int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_edit_undo: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result undo_res = yetty_yrich_document_undo(doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, undo_res, "menu_edit_undo");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result menu_edit_redo(struct yetty_yclass_object *menu,
                                                     int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_edit_redo: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result redo_res = yetty_yrich_document_redo(doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, redo_res, "menu_edit_redo");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

/* Edit > Find next — jumps to the next occurrence of the currently selected
 * text (select a word, then repeat to cycle through matches). */
static struct yetty_ycore_void_result menu_edit_find_next(struct yetty_yclass_object *menu,
                                                          int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_edit_find_next: document");
    if (doc_res.value) {
        struct yetty_ycore_char_ptr_result query_res =
            yetty_yrich_ydoc_selection_text(doc_res.value);
        if (YETTY_IS_OK(query_res) && query_res.value) {
            struct yetty_ycore_int_result found =
                yetty_yrich_ydoc_find_next(doc_res.value, query_res.value);
            if (YETTY_IS_ERR(found)) {
                yetty_ycore_error_destroy(found.error);
            }
            free(query_res.value);
        } else if (YETTY_IS_ERR(query_res)) {
            yetty_ycore_error_destroy(query_res.error);
        }
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

/* Edit > Find previous — the reverse of Find next. */
static struct yetty_ycore_void_result menu_edit_find_prev(struct yetty_yclass_object *menu,
                                                          int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_edit_find_prev: document");
    if (doc_res.value) {
        struct yetty_ycore_char_ptr_result query_res =
            yetty_yrich_ydoc_selection_text(doc_res.value);
        if (YETTY_IS_OK(query_res) && query_res.value) {
            struct yetty_ycore_int_result found =
                yetty_yrich_ydoc_find_prev(doc_res.value, query_res.value);
            if (YETTY_IS_ERR(found)) {
                yetty_ycore_error_destroy(found.error);
            }
            free(query_res.value);
        } else if (YETTY_IS_ERR(query_res)) {
            yetty_ycore_error_destroy(query_res.error);
        }
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

#define YDOC_FORMAT_MENU_CB(name, flag)                                                            \
    static struct yetty_ycore_void_result name(struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return ydoc_format_apply(userdata, flag);                                                  \
    }

YDOC_FORMAT_MENU_CB(menu_format_bold, YETTY_YRICH_FMT_BOLD)
YDOC_FORMAT_MENU_CB(menu_format_italic, YETTY_YRICH_FMT_ITALIC)
YDOC_FORMAT_MENU_CB(menu_format_underline, YETTY_YRICH_FMT_UNDERLINE)
YDOC_FORMAT_MENU_CB(menu_format_strike, YETTY_YRICH_FMT_STRIKE)
YDOC_FORMAT_MENU_CB(menu_format_superscript, YETTY_YRICH_FMT_SUPERSCRIPT)
YDOC_FORMAT_MENU_CB(menu_format_subscript, YETTY_YRICH_FMT_SUBSCRIPT)

static struct yetty_ycore_void_result menu_format_larger(struct yetty_yclass_object *menu,
                                                         int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    return ydoc_font_size_apply(userdata, 2.0f);
}

static struct yetty_ycore_void_result menu_format_smaller(struct yetty_yclass_object *menu,
                                                          int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    return ydoc_font_size_apply(userdata, -2.0f);
}

static struct yetty_ycore_void_result ydoc_alignment_apply(struct yetty_yclass_object *view,
                                                           uint32_t halign)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "ydoc_alignment_apply: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result align_res = yetty_yrich_ydoc_set_alignment(doc, halign);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, align_res, "ydoc_alignment_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result ydoc_heading_apply(struct yetty_yclass_object *view,
                                                         uint32_t level)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "ydoc_heading_apply: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result heading_res = yetty_yrich_ydoc_set_heading(doc, level);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, heading_res, "ydoc_heading_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result ydoc_color_apply(struct yetty_yclass_object *view,
                                                       uint32_t color)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "ydoc_color_apply: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result color_res = yetty_yrich_ydoc_set_text_color(doc, color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, color_res, "ydoc_color_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

#define YDOC_MENU_ALIGN_CB(name, value)                                                            \
    static struct yetty_ycore_void_result name(struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return ydoc_alignment_apply(userdata, value);                                              \
    }

YDOC_MENU_ALIGN_CB(menu_align_left, YETTY_YRICH_HALIGN_LEFT)
YDOC_MENU_ALIGN_CB(menu_align_center, YETTY_YRICH_HALIGN_CENTER)
YDOC_MENU_ALIGN_CB(menu_align_right, YETTY_YRICH_HALIGN_RIGHT)
YDOC_MENU_ALIGN_CB(menu_align_justify, YETTY_YRICH_HALIGN_JUSTIFY)

/* Lists — set/toggle the paragraph's list kind (re-applying clears it). */
static struct yetty_ycore_void_result ydoc_list_apply(struct yetty_yclass_object *view,
                                                      uint32_t kind)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "ydoc_list_apply: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result list_res = yetty_yrich_ydoc_set_list(doc, kind);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "ydoc_list_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

#define YDOC_MENU_LIST_CB(name, value)                                                             \
    static struct yetty_ycore_void_result name(struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return ydoc_list_apply(userdata, value);                                                   \
    }

YDOC_MENU_LIST_CB(menu_list_bullet, 1)
YDOC_MENU_LIST_CB(menu_list_numbered, 2)
YDOC_MENU_LIST_CB(menu_list_checklist, 3)

static struct yetty_ycore_void_result menu_check_toggle(struct yetty_yclass_object *menu,
                                                        int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_check_toggle: document");
    if (doc_res.value) {
        struct yetty_ycore_void_result toggle_res = yetty_yrich_ydoc_toggle_checked(doc_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, toggle_res, "menu_check_toggle");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

/* Format > Copy / Paint formatting. */
static struct yetty_ycore_void_result menu_copy_format(struct yetty_yclass_object *menu,
                                                       int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_copy_format: document");
    if (doc_res.value) {
        struct yetty_ycore_void_result res = yetty_yrich_ydoc_copy_format(doc_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "menu_copy_format");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result menu_paint_format(struct yetty_yclass_object *menu,
                                                        int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_paint_format: document");
    if (doc_res.value) {
        struct yetty_ycore_void_result res = yetty_yrich_ydoc_paint_format(doc_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "menu_paint_format");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

/* View > Show nonprinting characters. */
static struct yetty_ycore_void_result menu_toggle_nonprinting(struct yetty_yclass_object *menu,
                                                              int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_toggle_nonprinting: document");
    if (doc_res.value) {
        struct yetty_ycore_void_result res = yetty_yrich_ydoc_toggle_nonprinting(doc_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "menu_toggle_nonprinting");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

/* Insert > Table (dimensions carried in the callback). */
static struct yetty_ycore_void_result insert_table_apply(struct yetty_yclass_object *view,
                                                         uint32_t rows, uint32_t cols)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "insert_table_apply: document");
    if (doc_res.value) {
        struct yetty_ycore_void_result res =
            yetty_yrich_ydoc_insert_table(doc_res.value, rows, cols);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "insert_table_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

#define YDOC_MENU_TABLE_CB(name, rows, cols)                                                       \
    static struct yetty_ycore_void_result name(struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return insert_table_apply(userdata, (rows), (cols));                                       \
    }

YDOC_MENU_TABLE_CB(menu_table_2x2, 2, 2)
YDOC_MENU_TABLE_CB(menu_table_3x3, 3, 3)
YDOC_MENU_TABLE_CB(menu_table_3x2, 2, 3)

/* Table row/column edits around the active cell. */
static struct yetty_ycore_void_result table_edit_apply(struct yetty_yclass_object *view,
                                                       uint32_t op)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "table_edit_apply: document");
    if (doc_res.value) {
        struct yetty_ycore_void_result res = yetty_yrich_ydoc_table_edit(doc_res.value, op);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "table_edit_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

#define YDOC_MENU_TABLE_EDIT_CB(name, op)                                                          \
    static struct yetty_ycore_void_result name(struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return table_edit_apply(userdata, (op));                                                   \
    }

YDOC_MENU_TABLE_EDIT_CB(menu_table_insert_row, 0)
YDOC_MENU_TABLE_EDIT_CB(menu_table_insert_col, 1)
YDOC_MENU_TABLE_EDIT_CB(menu_table_delete_row, 2)
YDOC_MENU_TABLE_EDIT_CB(menu_table_delete_col, 3)

/* Insert > Table of contents. */
static struct yetty_ycore_void_result menu_insert_toc(struct yetty_yclass_object *menu,
                                                      int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_insert_toc: document");
    if (doc_res.value) {
        struct yetty_ycore_void_result res = yetty_yrich_ydoc_insert_toc(doc_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "menu_insert_toc");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

/* Insert > Link — turn the selection into a hyperlink whose URL is the selected
 * text (the sensible default when the selection is itself a URL). Without a text
 * field to type a distinct target, this is the no-input path; a URL that differs
 * from the display text needs the link editor (follow-up). */
static struct yetty_ycore_void_result menu_insert_link(struct yetty_yclass_object *menu,
                                                       int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_insert_link: document");
    if (doc_res.value) {
        struct yetty_ycore_char_ptr_result sel_res = yetty_yrich_ydoc_selection_text(doc_res.value);
        if (YETTY_IS_OK(sel_res) && sel_res.value && sel_res.value[0] != '\0') {
            struct yetty_ycore_void_result link_res =
                yetty_yrich_ydoc_set_link(doc_res.value, sel_res.value);
            if (YETTY_IS_ERR(link_res)) {
                yetty_ycore_error_destroy(link_res.error);
            }
            free(sel_res.value);
        } else if (YETTY_IS_ERR(sel_res)) {
            yetty_ycore_error_destroy(sel_res.error);
        } else {
            free(sel_res.value);
        }
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

/* Insert > Remove link — clear the hyperlink covering the caret/selection. */
static struct yetty_ycore_void_result menu_insert_remove_link(struct yetty_yclass_object *menu,
                                                              int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_insert_remove_link: document");
    if (doc_res.value) {
        struct yetty_ycore_void_result res = yetty_yrich_ydoc_remove_link(doc_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "menu_insert_remove_link");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

/* Insert > Bookmark — anchor a named bookmark at the caret paragraph, named
 * after the current selection (the no-text-input path). */
static struct yetty_ycore_void_result menu_insert_bookmark(struct yetty_yclass_object *menu,
                                                           int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_insert_bookmark: document");
    if (doc_res.value) {
        struct yetty_ycore_char_ptr_result sel_res = yetty_yrich_ydoc_selection_text(doc_res.value);
        if (YETTY_IS_OK(sel_res) && sel_res.value && sel_res.value[0] != '\0') {
            struct yetty_ycore_void_result set_res =
                yetty_yrich_ydoc_set_bookmark(doc_res.value, sel_res.value);
            if (YETTY_IS_ERR(set_res)) {
                yetty_ycore_error_destroy(set_res.error);
            }
        } else if (YETTY_IS_ERR(sel_res)) {
            yetty_ycore_error_destroy(sel_res.error);
        }
        free(sel_res.value);
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

/* Edit > Go to bookmark — jump to the bookmark named by the current selection. */
static struct yetty_ycore_void_result menu_edit_goto_bookmark(struct yetty_yclass_object *menu,
                                                              int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_edit_goto_bookmark: document");
    if (doc_res.value) {
        struct yetty_ycore_char_ptr_result sel_res = yetty_yrich_ydoc_selection_text(doc_res.value);
        if (YETTY_IS_OK(sel_res) && sel_res.value && sel_res.value[0] != '\0') {
            struct yetty_ycore_int_result go_res =
                yetty_yrich_ydoc_goto_bookmark(doc_res.value, sel_res.value);
            if (YETTY_IS_ERR(go_res)) {
                yetty_ycore_error_destroy(go_res.error);
            }
        } else if (YETTY_IS_ERR(sel_res)) {
            yetty_ycore_error_destroy(sel_res.error);
        }
        free(sel_res.value);
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

/* Insert > Page break. */
static struct yetty_ycore_void_result menu_insert_page_break(struct yetty_yclass_object *menu,
                                                             int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_insert_page_break: document");
    if (doc_res.value) {
        struct yetty_ycore_void_result res = yetty_yrich_ydoc_insert_page_break(doc_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "menu_insert_page_break");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

/* Insert > Horizontal rule. */
static struct yetty_ycore_void_result menu_insert_hrule(struct yetty_yclass_object *menu,
                                                        int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yclass_object *view = userdata;
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "menu_insert_hrule: document");
    if (doc_res.value) {
        struct yetty_ycore_void_result rule_res =
            yetty_yrich_ydoc_insert_horizontal_rule(doc_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rule_res, "menu_insert_hrule");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

/* Insert > Special character — types the given UTF-8 glyph at the caret
 * (undoable, via the normal text-input path). */
static struct yetty_ycore_void_result ydoc_insert_glyph(struct yetty_yclass_object *view,
                                                        const char *utf8)
{
    struct yetty_ycore_void_result feed_res =
        yetty_ygui_yrich_view_feed_text(view, utf8, strlen(utf8));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, feed_res, "ydoc_insert_glyph");
    return yetty_ygui_yrich_view_invalidate(view);
}

#define YDOC_MENU_GLYPH_CB(name, utf8)                                                             \
    static struct yetty_ycore_void_result name(struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return ydoc_insert_glyph(userdata, (utf8));                                                \
    }

YDOC_MENU_GLYPH_CB(menu_glyph_emdash, "\xE2\x80\x94")   /* — U+2014 */
YDOC_MENU_GLYPH_CB(menu_glyph_arrow, "\xE2\x86\x92")    /* → U+2192 */
YDOC_MENU_GLYPH_CB(menu_glyph_bullet, "\xE2\x80\xA2")   /* • U+2022 */
YDOC_MENU_GLYPH_CB(menu_glyph_check, "\xE2\x9C\x93")    /* ✓ U+2713 */
YDOC_MENU_GLYPH_CB(menu_glyph_copyright, "\xC2\xA9")    /* © U+00A9 */
YDOC_MENU_GLYPH_CB(menu_glyph_degree, "\xC2\xB0")       /* ° U+00B0 */
YDOC_MENU_GLYPH_CB(menu_glyph_euro, "\xE2\x82\xAC")     /* € U+20AC */
YDOC_MENU_GLYPH_CB(menu_glyph_times, "\xC3\x97")        /* × U+00D7 */
YDOC_MENU_GLYPH_CB(menu_glyph_ellipsis, "\xE2\x80\xA6") /* … U+2026 */

#define YDOC_MENU_HEADING_CB(name, value)                                                          \
    static struct yetty_ycore_void_result name(struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return ydoc_heading_apply(userdata, value);                                                \
    }

YDOC_MENU_HEADING_CB(menu_heading_normal, 0)
YDOC_MENU_HEADING_CB(menu_heading_1, 1)
YDOC_MENU_HEADING_CB(menu_heading_2, 2)
YDOC_MENU_HEADING_CB(menu_heading_3, 3)
YDOC_MENU_HEADING_CB(menu_heading_4, 4)
YDOC_MENU_HEADING_CB(menu_heading_5, 5)
YDOC_MENU_HEADING_CB(menu_heading_6, 6)
YDOC_MENU_HEADING_CB(menu_style_title, 7)
YDOC_MENU_HEADING_CB(menu_style_subtitle, 8)

#define YDOC_MENU_COLOR_CB(name, value)                                                            \
    static struct yetty_ycore_void_result name(struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return ydoc_color_apply(userdata, value);                                                  \
    }

YDOC_MENU_COLOR_CB(menu_color_default, YETTY_YRICH_COLOR_BLACK)
YDOC_MENU_COLOR_CB(menu_color_red, YETTY_YRICH_RGBA(200, 30, 30, 255))
YDOC_MENU_COLOR_CB(menu_color_orange, YETTY_YRICH_RGBA(220, 120, 20, 255))
YDOC_MENU_COLOR_CB(menu_color_green, YETTY_YRICH_RGBA(20, 140, 60, 255))
YDOC_MENU_COLOR_CB(menu_color_teal, YETTY_YRICH_RGBA(20, 150, 150, 255))
YDOC_MENU_COLOR_CB(menu_color_blue, YETTY_YRICH_RGBA(30, 60, 200, 255))
YDOC_MENU_COLOR_CB(menu_color_purple, YETTY_YRICH_RGBA(130, 50, 190, 255))
YDOC_MENU_COLOR_CB(menu_color_gray, YETTY_YRICH_RGBA(120, 120, 120, 255))

/* Highlight (text background) — a wash behind the selected glyphs. */
static struct yetty_ycore_void_result ydoc_highlight_apply(struct yetty_yclass_object *view,
                                                           uint32_t bg_color)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "ydoc_highlight_apply: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result hl_res = yetty_yrich_ydoc_set_highlight(doc, bg_color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hl_res, "ydoc_highlight_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

#define YDOC_MENU_HIGHLIGHT_CB(name, value)                                                        \
    static struct yetty_ycore_void_result name(struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return ydoc_highlight_apply(userdata, value);                                              \
    }

YDOC_MENU_HIGHLIGHT_CB(menu_highlight_yellow, YETTY_YRICH_RGBA(255, 235, 130, 255))
YDOC_MENU_HIGHLIGHT_CB(menu_highlight_green, YETTY_YRICH_RGBA(180, 240, 170, 255))
YDOC_MENU_HIGHLIGHT_CB(menu_highlight_pink, YETTY_YRICH_RGBA(255, 190, 210, 255))
YDOC_MENU_HIGHLIGHT_CB(menu_highlight_none, YETTY_YRICH_COLOR_TRANSPARENT)

/* Clear character formatting from the selection. */
static struct yetty_ycore_void_result ydoc_clear_format_apply(struct yetty_yclass_object *view)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "ydoc_clear_format_apply: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result clear_res = yetty_yrich_ydoc_clear_format(doc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_res, "ydoc_clear_format_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result menu_clear_format(struct yetty_yclass_object *menu,
                                                        int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    return ydoc_clear_format_apply(userdata);
}

/* Line spacing (paragraph-level multiplier). */
static struct yetty_ycore_void_result ydoc_line_spacing_apply(struct yetty_yclass_object *view,
                                                              float spacing)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "ydoc_line_spacing_apply: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result spacing_res =
            yetty_yrich_ydoc_set_line_spacing(doc, spacing);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, spacing_res, "ydoc_line_spacing_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

#define YDOC_MENU_LINESPACING_CB(name, value)                                                      \
    static struct yetty_ycore_void_result name(struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return ydoc_line_spacing_apply(userdata, value);                                           \
    }

YDOC_MENU_LINESPACING_CB(menu_linespacing_single, 1.0f)
YDOC_MENU_LINESPACING_CB(menu_linespacing_one_half, 1.5f)
YDOC_MENU_LINESPACING_CB(menu_linespacing_double, 2.0f)

/* Paragraph spacing (extra gap above/below the paragraph). */
static struct yetty_ycore_void_result ydoc_space_before_apply(struct yetty_yclass_object *view,
                                                              float px)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "ydoc_space_before_apply: document");
    if (doc_res.value) {
        struct yetty_ycore_void_result res = yetty_yrich_ydoc_set_space_before(doc_res.value, px);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ydoc_space_before_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result ydoc_space_after_apply(struct yetty_yclass_object *view,
                                                             float px)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "ydoc_space_after_apply: document");
    if (doc_res.value) {
        struct yetty_ycore_void_result res = yetty_yrich_ydoc_set_space_after(doc_res.value, px);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ydoc_space_after_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

#define YDOC_MENU_SPACE_BEFORE_CB(name, value)                                                     \
    static struct yetty_ycore_void_result name(struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return ydoc_space_before_apply(userdata, value);                                           \
    }
#define YDOC_MENU_SPACE_AFTER_CB(name, value)                                                      \
    static struct yetty_ycore_void_result name(struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return ydoc_space_after_apply(userdata, value);                                            \
    }

YDOC_MENU_SPACE_BEFORE_CB(menu_space_before_none, 0.0f)
YDOC_MENU_SPACE_BEFORE_CB(menu_space_before_some, 12.0f)
YDOC_MENU_SPACE_AFTER_CB(menu_space_after_none, 0.0f)
YDOC_MENU_SPACE_AFTER_CB(menu_space_after_some, 12.0f)

/* Paragraph indent (Increase / Decrease). */
static struct yetty_ycore_void_result ydoc_indent_apply(struct yetty_yclass_object *view,
                                                        int32_t direction)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "ydoc_indent_apply: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result indent_res = yetty_yrich_ydoc_adjust_indent(doc, direction);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, indent_res, "ydoc_indent_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

static struct yetty_ycore_void_result menu_indent_increase(struct yetty_yclass_object *menu,
                                                           int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    return ydoc_indent_apply(userdata, 1);
}

static struct yetty_ycore_void_result menu_indent_decrease(struct yetty_yclass_object *menu,
                                                           int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    return ydoc_indent_apply(userdata, -1);
}

/* Absolute font-size presets. */
static struct yetty_ycore_void_result ydoc_set_size_apply(struct yetty_yclass_object *view,
                                                          float size)
{
    struct yetty_yclass_object_ptr_result doc_res = yetty_ygui_yrich_view_document(view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "ydoc_set_size_apply: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (doc) {
        struct yetty_ycore_void_result size_res = yetty_yrich_ydoc_set_font_size(doc, size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, size_res, "ydoc_set_size_apply");
    }
    return yetty_ygui_yrich_view_invalidate(view);
}

#define YDOC_MENU_SIZE_CB(name, value)                                                             \
    static struct yetty_ycore_void_result name(struct yetty_yclass_object *menu, int item_index,   \
                                               void *userdata)                                     \
    {                                                                                              \
        (void)menu;                                                                                \
        (void)item_index;                                                                          \
        return ydoc_set_size_apply(userdata, value);                                               \
    }

YDOC_MENU_SIZE_CB(menu_size_12, 12.0f)
YDOC_MENU_SIZE_CB(menu_size_16, 16.0f)
YDOC_MENU_SIZE_CB(menu_size_20, 20.0f)
YDOC_MENU_SIZE_CB(menu_size_28, 28.0f)

#define YDOC_ALIGN_BUTTON_CB(name, value)                                                          \
    static struct yetty_ycore_void_result name(                                                    \
        struct yetty_yclass_object *target, const struct yetty_ygui_event *event, void *userdata)  \
    {                                                                                              \
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
    out->file_menu = file_res.value; /* exposed so the app can add Exit */
    struct yetty_ycore_void_result item_res =
        yetty_ygui_popup_menu_add_item(file_res.value, "New", menu_file_new, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: New");
    item_res = yetty_ygui_popup_menu_add_item(file_res.value, "Save", menu_file_save, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Save");
    item_res = yetty_ygui_popup_menu_add_separator(file_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: file separator");
    item_res = yetty_ygui_popup_menu_add_item(file_res.value, "Export as Markdown",
                                              menu_file_export_markdown, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: export markdown");
    item_res = yetty_ygui_popup_menu_add_item(file_res.value, "Export as HTML",
                                              menu_file_export_html, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: export html");
    item_res = yetty_ygui_popup_menu_add_item(file_res.value, "Export as RTF", menu_file_export_rtf,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: export rtf");
    item_res = yetty_ygui_popup_menu_add_item(file_res.value, "Export as text",
                                              menu_file_export_text, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: export text");

    struct yetty_yclass_object_ptr_result edit_res = add_menu(out, "Edit");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, edit_res, "ydoc menus: Edit");
    item_res = yetty_ygui_popup_menu_add_item(edit_res.value, "Undo        Ctrl+Z", menu_edit_undo,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Undo");
    item_res = yetty_ygui_popup_menu_add_item(edit_res.value, "Redo        Ctrl+Y", menu_edit_redo,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Redo");
    item_res = yetty_ygui_popup_menu_add_separator(edit_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: edit separator");
    item_res = yetty_ygui_popup_menu_add_item(edit_res.value, "Find next (selection)",
                                              menu_edit_find_next, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: find next");
    item_res = yetty_ygui_popup_menu_add_item(edit_res.value, "Find previous (selection)",
                                              menu_edit_find_prev, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: find prev");
    item_res = yetty_ygui_popup_menu_add_item(edit_res.value, "Go to bookmark (selection)",
                                              menu_edit_goto_bookmark, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: goto bookmark");

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
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Superscript",
                                              menu_format_superscript, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Superscript");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Subscript", menu_format_subscript,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Subscript");
    item_res = yetty_ygui_popup_menu_add_separator(format_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: separator");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Larger text", menu_format_larger,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Larger");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Smaller text", menu_format_smaller,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: Smaller");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Size 12", menu_size_12, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: size 12");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Size 16", menu_size_16, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: size 16");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Size 20", menu_size_20, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: size 20");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Size 28", menu_size_28, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: size 28");
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
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Align justify", menu_align_justify,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: align justify");
    item_res = yetty_ygui_popup_menu_add_separator(format_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: separator 3");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Text: default", menu_color_default,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: color default");
    item_res =
        yetty_ygui_popup_menu_add_item(format_res.value, "Text: red", menu_color_red, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: color red");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Text: orange", menu_color_orange,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: color orange");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Text: green", menu_color_green,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: color green");
    item_res =
        yetty_ygui_popup_menu_add_item(format_res.value, "Text: teal", menu_color_teal, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: color teal");
    item_res =
        yetty_ygui_popup_menu_add_item(format_res.value, "Text: blue", menu_color_blue, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: color blue");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Text: purple", menu_color_purple,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: color purple");
    item_res =
        yetty_ygui_popup_menu_add_item(format_res.value, "Text: gray", menu_color_gray, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: color gray");
    item_res = yetty_ygui_popup_menu_add_separator(format_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: separator 4");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Highlight: yellow",
                                              menu_highlight_yellow, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: highlight yellow");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Highlight: green",
                                              menu_highlight_green, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: highlight green");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Highlight: pink",
                                              menu_highlight_pink, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: highlight pink");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Highlight: none",
                                              menu_highlight_none, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: highlight none");
    item_res = yetty_ygui_popup_menu_add_separator(format_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: separator 5");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Clear formatting",
                                              menu_clear_format, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: clear formatting");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Copy formatting", menu_copy_format,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: copy formatting");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Paint formatting",
                                              menu_paint_format, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: paint formatting");
    item_res = yetty_ygui_popup_menu_add_separator(format_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: separator 6");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Line spacing: single",
                                              menu_linespacing_single, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: line spacing single");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Line spacing: 1.5",
                                              menu_linespacing_one_half, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: line spacing 1.5");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Line spacing: double",
                                              menu_linespacing_double, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: line spacing double");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Add space before paragraph",
                                              menu_space_before_some, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: space before");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Add space after paragraph",
                                              menu_space_after_some, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: space after");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Remove paragraph spacing",
                                              menu_space_before_none, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: remove space before");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "  (after)", menu_space_after_none,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: remove space after");
    item_res = yetty_ygui_popup_menu_add_separator(format_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: separator 7");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Increase indent",
                                              menu_indent_increase, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: increase indent");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Decrease indent",
                                              menu_indent_decrease, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: decrease indent");
    item_res = yetty_ygui_popup_menu_add_separator(format_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: separator 8");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Bulleted list", menu_list_bullet,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: bulleted list");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Numbered list", menu_list_numbered,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: numbered list");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Checklist", menu_list_checklist,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: checklist");
    item_res = yetty_ygui_popup_menu_add_item(format_res.value, "Toggle checkbox",
                                              menu_check_toggle, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: toggle checkbox");

    struct yetty_yclass_object_ptr_result insert_res = add_menu(out, "Insert");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, insert_res, "ydoc menus: Insert");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Horizontal rule",
                                              menu_insert_hrule, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: horizontal rule");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Page break",
                                              menu_insert_page_break, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: page break");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Link (from selection)",
                                              menu_insert_link, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: link");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Remove link",
                                              menu_insert_remove_link, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: remove link");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Bookmark (from selection)",
                                              menu_insert_bookmark, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: bookmark");
    item_res =
        yetty_ygui_popup_menu_add_item(insert_res.value, "Table 2x2", menu_table_2x2, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: table 2x2");
    item_res =
        yetty_ygui_popup_menu_add_item(insert_res.value, "Table 3x3", menu_table_3x3, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: table 3x3");
    item_res =
        yetty_ygui_popup_menu_add_item(insert_res.value, "Table 3x2", menu_table_3x2, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: table 3x2");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Table: insert row",
                                              menu_table_insert_row, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: table insert row");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Table: insert column",
                                              menu_table_insert_col, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: table insert col");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Table: delete row",
                                              menu_table_delete_row, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: table delete row");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Table: delete column",
                                              menu_table_delete_col, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: table delete col");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Table of contents",
                                              menu_insert_toc, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: toc");
    item_res = yetty_ygui_popup_menu_add_separator(insert_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: insert separator");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Em dash  \xE2\x80\x94",
                                              menu_glyph_emdash, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: em dash");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Arrow  \xE2\x86\x92",
                                              menu_glyph_arrow, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: arrow");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Bullet  \xE2\x80\xA2",
                                              menu_glyph_bullet, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: bullet glyph");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Check  \xE2\x9C\x93",
                                              menu_glyph_check, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: check glyph");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Copyright  \xC2\xA9",
                                              menu_glyph_copyright, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: copyright");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Degree  \xC2\xB0",
                                              menu_glyph_degree, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: degree");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Euro  \xE2\x82\xAC",
                                              menu_glyph_euro, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: euro");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Multiply  \xC3\x97",
                                              menu_glyph_times, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: multiply");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Ellipsis  \xE2\x80\xA6",
                                              menu_glyph_ellipsis, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: ellipsis");
    item_res = yetty_ygui_popup_menu_add_separator(insert_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: insert separator 2");
    item_res = yetty_ygui_popup_menu_add_item(insert_res.value, "Show nonprinting chars",
                                              menu_toggle_nonprinting, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: nonprinting");

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
    item_res =
        yetty_ygui_popup_menu_add_item(styles_res.value, "Heading 4", menu_heading_4, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: h4");
    item_res =
        yetty_ygui_popup_menu_add_item(styles_res.value, "Heading 5", menu_heading_5, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: h5");
    item_res =
        yetty_ygui_popup_menu_add_item(styles_res.value, "Heading 6", menu_heading_6, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: h6");
    item_res = yetty_ygui_popup_menu_add_separator(styles_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: styles separator");
    item_res =
        yetty_ygui_popup_menu_add_item(styles_res.value, "Title", menu_style_title, out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: title");
    item_res = yetty_ygui_popup_menu_add_item(styles_res.value, "Subtitle", menu_style_subtitle,
                                              out->view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "ydoc menus: subtitle");
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
        yetty_yrich_spreadsheet_set_grid_size(dr.value, 20, 8);
    if (YETTY_IS_ERR(grid_res)) {
        struct yetty_ycore_void_result destroy_res = yetty_yrich_document_destroy(dr.value);
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
        struct yetty_ycore_void_result destroy_res = yetty_yrich_document_destroy(dr.value);
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
