/*
 * yrich-app.c — standalone window host for the ygui-decorated yrich
 * editors.
 *
 * Mirrors tools/ycompositor-ygui (headless ygui path) and yaudio: a
 * window via yinit_run + yframework, a texture render target blitting to
 * the GLFW surface, an in-process yfigure container fed by a ygui
 * framework through the yclass slot path (set_container_obj — no PTY, no
 * OSC). The widget tree is the decorated editor shell for the requested
 * document kind; window input is fed straight into the framework.
 */

#include <yetty/yrich/yrich-app.h>

#include <yetty/yconfig/config.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yevent/event.h>
#include <yetty/api/yfigure/figure.h>
#include <yetty/api/yfigure/container.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yfont/font.h>
#include "yetty/gen/impl/ychrome/chrome.h" /* YETTY_YCHROME_FLAG_* + yetty_ychrome_handle_event */
#include <yetty/ychrome/host.h>
#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/yframework/yframework.h>
#include <yetty/api/yscene/scene.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/event.h>
#include "yetty/gen/impl/ygui/mixins/clickable.h"
#include "yetty/gen/impl/ygui/widgets/button.h"
#include "yetty/gen/impl/ygui/widgets/menubar.h"
#include "yetty/gen/impl/ygui/widgets/popup_menu.h"
#include "yetty/gen/impl/ygui/widgets/vbox.h"
#include "yetty/gen/impl/ygui/widgets/yrich_view.h"
#include "yetty/gen/impl/yapp/app.h"
#include <yetty/yclass/class.h>
#include <yetty/yplatform/gpu-context.h>
#include <yetty/yplatform/yclipboard/clipboard.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/yplatform/platform.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yrich/yrich-app.h> /* public entry: yetty_yrich_app_run + kind enum */
#include <yetty/yrich/yrich-shell.h>
#include <yetty/yrich/yrich-types.h>

#include <yetty/ycore/terminal-detect.h>   /* yetty_running_under_yetty (dual mode) */
#include "yetty/gen/impl/yrich/document.h" /* document_undo / document_redo */
#include "yetty/gen/impl/yrich/ydoc.h"
#include <yetty/yrich/yrich-keymap.h> /* semantic commands + remappable modal keymap */
#include <yetty/yrich/yrich-yaml.h>   /* ydoc save */

#ifdef YETTY_YGUI_HAS_UV
#include <yetty/yclass/transport-pty.h>
#include <yetty/ygui/framework-defs.h> /* key_cb + YETTY_YGUI_KEY_* */
#include <yetty/ymgui/wire.h>          /* forwarded input kinds */
#include <yetty/yterminal/client-input.h>
#include <yetty/yterminal/dcs-codes.h> /* OSC client-input codes */
#include <yetty/ywire/channel.h>
#include <yetty/ywire/connection.h>
#include <unistd.h>
#include <uv.h>
#endif
#include <yetty/yetty/yetty.h>
#include <yetty/ytrace/ytrace.h>

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>

static inline void destroy_safe(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

struct YETTY_ANNOTATE("class@yrich:app") YETTY_ANNOTATE("parent@yapp:app") yetty_yrich_app {
    int quit;
    enum yetty_yrich_app_kind kind;
    struct yetty_yclass_object *doc;         /* borrowed until handed to the view */
    struct yetty_yclass_object *editor_view; /* the yrich_view — keyboard target */

    struct yetty_yrich_keymap keymap; /* semantic command bindings, remappable */
    enum yetty_yrich_edit_mode mode;  /* active input mode (default / vi normal/insert) */

    struct yetty_context ctx;
    struct yetty_yframework *yrt;
    struct yetty_ydraw_target *target;
    struct yetty_yclass_object *root_obj;
    struct yetty_yclass_object *container_obj;
    struct yetty_yfigure_registry *registry;
    struct yetty_yclass_object *ygui;
    struct yetty_yclass_object *win; /* framework root widget */
    struct yetty_yfont_font *font;
    /* Styled faces (bold / italic / bold-italic) registered at scene font slots
     * 1/2/3 so ydoc renders real bold + italic glyphs. NULL if a face fails to
     * load (ydoc then falls back to the Regular face for that style). */
    struct yetty_yfont_font *font_bold;
    struct yetty_yfont_font *font_italic;
    struct yetty_yfont_font *font_bold_italic;
    struct yetty_ychrome_host *chrome; /* draggable/resizable titlebar + min/max/close */
    struct yetty_yscene_factory_args figure_args;
    /* Composite figure factory (yimage/yplot) so ydoc inline images render as
     * real decoded textures rather than placeholder boxes. Owned. */
    struct yetty_ydraw_composite_factory *composite_factory;
    void *surface;
    uint32_t surface_w;
    uint32_t surface_h;
    /* HiDPI factor (framebuffer px / logical px). Standalone reads it from the
     * GPU context at startup; the in-terminal client learns it from
     * yetty_client_input_resize.content_scale. ygui and ychrome author in
     * LOGICAL px and the receiving yscene multiplies absolute-coords figures
     * back up by this, so every framebuffer-px input (surface size, pane size,
     * pointer coords) is divided by it on the way in. 1.0 on non-HiDPI. */
    float content_scale;
};

/* HiDPI factor with the 1.0 guard — see yetty_yrich_app::content_scale. */
static float yrich_scale(const struct yetty_yrich_app *app)
{
    return (app && app->content_scale > 0.0f) ? app->content_scale : 1.0f;
}

/* Result wrapper + codegen accessor/downcast forward-decls (this TU does not
 * include its own generated header). */
YETTY_YRESULT_DECLARE(yetty_yrich_app_ptr, struct yetty_yrich_app *);
struct yetty_yclass_ptr_result yetty_yrich_app_class_get(void);
struct yetty_yrich_app_ptr_result yetty_yrich_app_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_app_create(struct yetty_yclass_ctx *ctx);

/* Platform bring-up sequence symbols. yetty_yrich_app_run is the public entry the
 * thin ydoc / ysheet / yslide tools call (it carries the document object), so it
 * drives the platform sequence directly rather than via the shared ymain entry. */
struct yetty_ycore_void_result yetty_yplatform_register(void);
struct yetty_ycore_void_result yetty_yapp_register(void);
/* Proportional-layout metrics font setter (exported from ydoc.c). */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_metrics_font(
    struct yetty_yclass_object *obj, const struct yetty_yfont_font *font);
/* List toggle (1=bullet, 2=numbered), exported from ydoc.c. */
struct yetty_ycore_void_result yetty_yrich_ydoc_set_list(struct yetty_yclass_object *obj,
                                                         uint32_t kind);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app,
                                                            int argc, char **argv);

/* Close button ('x') callback — quits the editor (standalone loop checks
 * app->quit; the terminal host checks it too). */
static struct yetty_ycore_void_result yrich_close_cb(struct yetty_yclass_object *target,
                                                     const struct yetty_ygui_event *event,
                                                     void *userdata)
{
    (void)target;
    (void)event;
    struct yetty_yrich_app *app = (struct yetty_yrich_app *)userdata;
    if (app) {
        app->quit = 1;
    }
    return YETTY_OK_VOID();
}

/* Menubar "Quit" entry — the clickable mixin's on_click signature is
 * (object, userdata), distinct from the widget event_cb. Same quit path. */
static struct yetty_ycore_void_result yrich_close_trigger(struct yetty_yclass_object *obj,
                                                          void *userdata)
{
    (void)obj;
    struct yetty_yrich_app *app = (struct yetty_yrich_app *)userdata;
    if (app) {
        app->quit = 1;
    }
    return YETTY_OK_VOID();
}

/* File > Exit menu item callback — same quit path as the close button. */
static struct yetty_ycore_void_result yrich_exit_menu_cb(struct yetty_yclass_object *menu,
                                                         int item_index, void *userdata)
{
    (void)menu;
    (void)item_index;
    struct yetty_yrich_app *app = (struct yetty_yrich_app *)userdata;
    if (app) {
        app->quit = 1;
    }
    return YETTY_OK_VOID();
}

/* Build the decorated editor tree. The engine root is created and
 * registered FIRST so that figure widgets in the shell (the scrollarea)
 * resolve an engine and get wire ids when added underneath it. */
static struct yetty_ycore_void_result build_editor(struct yetty_yrich_app *app)
{
    struct yetty_yclass_object_ptr_result rootr = yetty_ygui_widget_new(
        yetty_ygui_class_expect(yetty_ygui_vbox_class_get(), "yetty_ygui_vbox_class_get"));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rootr, "build_editor: root add");
    app->win = rootr.value;
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(app->win);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "build_editor: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.align = YETTY_YGUI_ALIGN_STRETCH;
    destroy_safe(yetty_ygui_widget_layout_set(app->win, &l));
    /* Opaque backdrop (brand near-black, packed 0xAABBGGRR). Without it the root
     * is transparent and, in-terminal, the host terminal's own text shows
     * through the chrome (the shell command echo bled across the menu bar). */
    destroy_safe(yetty_ygui_widget_set_bg_color(app->win, 0xFF14100Bu));
    struct yetty_ycore_void_result sr = yetty_ygui_framework_set_root(app->ygui, app->win);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "build_editor: set_root");

    struct yetty_yrich_editor editor;
    struct yetty_ycore_void_result er;
    switch (app->kind) {
    case YETTY_YRICH_APP_YSHEET:
        er = yetty_yrich_ysheet_editor_create(app->win, &editor);
        break;
    case YETTY_YRICH_APP_YSLIDE:
        er = yetty_yrich_yslide_editor_create(app->win, &editor);
        break;
    case YETTY_YRICH_APP_YDOC:
    default:
        er = yetty_yrich_ydoc_editor_create(app->win, &editor);
        break;
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "build_editor: shell create");

    /* Swap the shell's default document for the caller's (own=1 →
     * destroyed with the view), then drop our borrowed pointer. */
    struct yetty_ycore_void_result dr =
        yetty_ygui_yrich_view_set_document(editor.view, app->doc, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "build_editor: set_document");
    /* Give the document a metrics font so layout uses real proportional glyph
     * advances instead of a fixed monospace approximation. */
    if (app->kind == YETTY_YRICH_APP_YDOC && app->font) {
        struct yetty_ycore_void_result mf = yetty_yrich_ydoc_set_metrics_font(app->doc, app->font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, mf, "build_editor: metrics font");
        /* Tell the document which styled faces are registered on the render
         * scene (slots 1/2/3), so bold/italic runs use the real face. Bits:
         * 1 = bold, 2 = italic, 4 = bold-italic. */
        uint32_t styled_mask = (app->font_bold ? 1u : 0u) | (app->font_italic ? 2u : 0u) |
                               (app->font_bold_italic ? 4u : 0u);
        struct yetty_ycore_void_result sm =
            yetty_yrich_ydoc_set_styled_font_mask(app->doc, styled_mask);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sm, "build_editor: styled font mask");
    }
    app->doc = NULL;
    app->editor_view = editor.view;

    /* Close button on the toolbar — a visible way to quit. In-terminal mode has
     * no OS window chrome; the standalone window keeps its own too. */
    if (editor.toolbar) {
        struct yetty_yclass_object_ptr_result close_btn =
            yetty_ygui_widget_add(editor.toolbar, yetty_ygui_button_class_get().value);
        if (YETTY_IS_OK(close_btn)) {
            destroy_safe(yetty_ygui_button_set_label(close_btn.value, "\xE2\x9C\x95 Close"));
            /* Pin an explicit width AND height — over the wire the button's
             * content-fit size is not resolved and it would otherwise collapse
             * (the toolbar centers rather than stretches its children). */
            struct yetty_ygui_layout_const_ptr_result close_layout =
                yetty_ygui_widget_layout_get(close_btn.value);
            if (YETTY_IS_OK(close_layout)) {
                struct yetty_ygui_layout button_layout = *close_layout.value;
                button_layout.width = 90.0f;
                button_layout.height = 26.0f;
                destroy_safe(yetty_ygui_widget_layout_set(close_btn.value, &button_layout));
            } else {
                yetty_ycore_error_destroy(close_layout.error);
            }
            destroy_safe(yetty_ygui_widget_subscribe(close_btn.value, YETTY_YGUI_EVENT_CLICK,
                                                     yrich_close_cb, app));
        } else {
            yetty_ycore_error_destroy(close_btn.error);
        }
    }

    /* File > Exit menu item — the standard, always-visible way out. */
    if (editor.file_menu) {
        destroy_safe(
            yetty_ygui_popup_menu_add_item(editor.file_menu, "Exit", yrich_exit_menu_cb, app));
    }

    /* A single-click "✕ Quit" entry in the menubar. The menubar reliably
     * paints and hit-tests its child buttons (in-terminal the toolbar's do
     * not), so this is the always-clickable close affordance. Built exactly
     * like a menubar menu entry, but its click quits directly instead of
     * opening a dropdown. */
    if (editor.menubar) {
        struct yetty_yclass_object_ptr_result quit_btn =
            yetty_ygui_widget_add(editor.menubar, yetty_ygui_button_class_get().value);
        if (YETTY_IS_OK(quit_btn)) {
            destroy_safe(yetty_ygui_button_set_label(quit_btn.value, "\xE2\x9C\x95 Quit"));
            struct yetty_ygui_layout_const_ptr_result quit_layout =
                yetty_ygui_widget_layout_get(quit_btn.value);
            if (YETTY_IS_OK(quit_layout)) {
                struct yetty_ygui_layout button_layout = *quit_layout.value;
                button_layout.width = 80.0f;
                destroy_safe(yetty_ygui_widget_layout_set(quit_btn.value, &button_layout));
            } else {
                yetty_ycore_error_destroy(quit_layout.error);
            }
            destroy_safe(
                yetty_ygui_clickable_on_click_set(quit_btn.value, yrich_close_trigger, app));
        } else {
            yetty_ycore_error_destroy(quit_btn.error);
        }
    }

    return yetty_yrich_editor_refresh(&editor);
}

/* Sync the viewport to the surface and ship the scene into the container
 * over the in-process yclass slot path. */
static struct yetty_ycore_void_result push_scene(struct yetty_yrich_app *app)
{
    if (!app->ygui) {
        return YETTY_OK_VOID();
    }
    /* surface_w/h are FRAMEBUFFER px; ygui lays out in LOGICAL px and the
     * receiving yscene scales absolute-coords figures back up. */
    const float scale = yrich_scale(app);
    struct yetty_ycore_void_result vr = yetty_ygui_framework_set_viewport(
        app->ygui, (float)app->surface_w / scale, (float)app->surface_h / scale);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vr, "push_scene: set_viewport");
    yetty_ygui_framework_mark_dirty(app->ygui);
    return yetty_ygui_framework_emit(app->ygui);
}

/* GLFW modifier bits → YETTY_YRICH_MOD_* (the low three bits coincide:
 * shift=1, ctrl=2, alt=4 — GLFW has ctrl=2/alt=4 as well). */
static uint32_t glfw_mods_to_yrich(int glfw_mods)
{
    uint32_t mods = 0;
    if (glfw_mods & 0x1) {
        mods |= YETTY_YRICH_MOD_SHIFT;
    }
    if (glfw_mods & 0x2) {
        mods |= YETTY_YRICH_MOD_CTRL;
    }
    if (glfw_mods & 0x4) {
        mods |= YETTY_YRICH_MOD_ALT;
    }
    return mods;
}

/* GLFW key codes → yrich editing keys. Letters are only mapped when a
 * Ctrl/Alt chord is held — plain letters arrive as CHAR events. */
static uint32_t glfw_key_to_yrich(int glfw_key, uint32_t mods)
{
    switch (glfw_key) {
    case 257: /* GLFW_KEY_ENTER */
        return YETTY_YRICH_KEY_ENTER;
    case 258: /* GLFW_KEY_TAB */
        return YETTY_YRICH_KEY_TAB;
    case 259: /* GLFW_KEY_BACKSPACE */
        return YETTY_YRICH_KEY_BACKSPACE;
    case 261: /* GLFW_KEY_DELETE */
        return YETTY_YRICH_KEY_DELETE;
    case 262: /* GLFW_KEY_RIGHT */
        return YETTY_YRICH_KEY_RIGHT;
    case 263: /* GLFW_KEY_LEFT */
        return YETTY_YRICH_KEY_LEFT;
    case 264: /* GLFW_KEY_DOWN */
        return YETTY_YRICH_KEY_DOWN;
    case 265: /* GLFW_KEY_UP */
        return YETTY_YRICH_KEY_UP;
    case 266: /* GLFW_KEY_PAGE_UP */
        return YETTY_YRICH_KEY_PAGEUP;
    case 267: /* GLFW_KEY_PAGE_DOWN */
        return YETTY_YRICH_KEY_PAGEDOWN;
    case 268: /* GLFW_KEY_HOME */
        return YETTY_YRICH_KEY_HOME;
    case 269: /* GLFW_KEY_END */
        return YETTY_YRICH_KEY_END;
    default:
        break;
    }
    if (glfw_key >= 65 && glfw_key <= 90 && (mods & (YETTY_YRICH_MOD_CTRL | YETTY_YRICH_MOD_ALT))) {
        return YETTY_YRICH_KEY_A + (uint32_t)(glfw_key - 65);
    }
    /* Digit keys (GLFW_KEY_0..9 = 48..57) under Ctrl/Alt drive heading/list
     * shortcuts. */
    if (glfw_key >= 48 && glfw_key <= 57 && (mods & (YETTY_YRICH_MOD_CTRL | YETTY_YRICH_MOD_ALT))) {
        return YETTY_YRICH_KEY_0 + (uint32_t)(glfw_key - 48);
    }
    return YETTY_YRICH_KEY_UNKNOWN;
}

/* Minimal UTF-8 encoder for one codepoint; returns the byte count (0 for
 * out-of-range input). */
static size_t encode_utf8(uint32_t codepoint, char out[4])
{
    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    if (codepoint <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (codepoint >> 18));
        out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;
}

/* Track content growth after an edit, then re-emit the scene. */
static struct yetty_ycore_void_result refit_and_push(struct yetty_yrich_app *app)
{
    if (app->editor_view) {
        struct yetty_ycore_void_result fit_res =
            yetty_ygui_yrich_view_fit_content(app->editor_view);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fit_res, "yrich: fit content");
    }
    return push_scene(app);
}

/* Ctrl+C / Ctrl+X / Ctrl+V — copy/cut go straight to the clipboard
 * manager; paste is an async round trip that returns as a PASTE event. */
static struct yetty_ycore_void_result handle_clipboard_chord(struct yetty_yrich_app *app,
                                                             int glfw_key)
{
    struct yetty_yclass_object *clipboard = app->yrt->clipboard;
    struct yetty_yclass_object *doc = NULL;
    if (app->editor_view) {
        struct yetty_yclass_object_ptr_result doc_res =
            yetty_ygui_yrich_view_document(app->editor_view);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "handle_clipboard_chord: document");
        doc = doc_res.value;
    }
    if (!doc) {
        return YETTY_OK_VOID();
    }
    if (glfw_key == 86) { /* V — request async paste */
        if (clipboard) {
            struct yetty_ycore_void_result paste_res =
                yetty_yplatform_clipboard_request_paste(clipboard);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, paste_res, "yrich: request paste");
        }
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_char_ptr_result selection_res = yetty_yrich_ydoc_selection_text(doc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, selection_res, "handle_clipboard_chord: selection_text");
    char *selection_text = selection_res.value;
    if (!selection_text) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result copy_res = YETTY_OK_VOID();
    if (clipboard) {
        copy_res =
            yetty_yplatform_clipboard_set_text(clipboard, selection_text, strlen(selection_text));
    }
    free(selection_text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, copy_res, "yrich: clipboard set");
    if (glfw_key == 88) { /* X — delete the selection after copying */
        struct yetty_ycore_void_result delete_res =
            yetty_ygui_yrich_view_feed_key(app->editor_view, YETTY_YRICH_KEY_DELETE, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, delete_res, "yrich: cut delete");
        return refit_and_push(app);
    }
    return YETTY_OK_VOID();
}

/* Save the active document to its source path (or a default name). */
static struct yetty_ycore_void_result handle_save(struct yetty_yrich_app *app)
{
    if (!app->editor_view) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object_ptr_result doc_res =
        yetty_ygui_yrich_view_document(app->editor_view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "handle_save: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (!doc) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_const_char_ptr_result path_res = yetty_yrich_ydoc_source_path(doc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, path_res, "handle_save: source path");
    const char *path = path_res.value ? path_res.value : "untitled.ydoc.yaml";
    return yetty_yrich_ydoc_save_yaml_file(doc, path);
}

/* Execute a semantic editor command. This is the single sink for every action,
 * whether it originated from a key (via the keymap), a menu, the toolbar, or
 * (later) a script. Keeping behaviour here — never in the key handler — is what
 * lets keys be remapped and modes (vi) be layered on freely. */
static struct yetty_ycore_void_result dispatch_command(struct yetty_yrich_app *app,
                                                       enum yetty_yrich_command_id command)
{
    if (!app->editor_view) {
        return YETTY_OK_VOID();
    }
    /* Mode switches and app-level commands first — they need no document. */
    switch (command) {
    case YETTY_YRICH_CMD_MODE_DEFAULT:
        app->mode = YETTY_YRICH_MODE_DEFAULT;
        return YETTY_OK_VOID();
    case YETTY_YRICH_CMD_MODE_VI_NORMAL:
        app->mode = YETTY_YRICH_MODE_VI_NORMAL;
        return YETTY_OK_VOID();
    case YETTY_YRICH_CMD_MODE_VI_INSERT:
        app->mode = YETTY_YRICH_MODE_VI_INSERT;
        return YETTY_OK_VOID();
    case YETTY_YRICH_CMD_SAVE:
        return handle_save(app);
    case YETTY_YRICH_CMD_QUIT:
        app->quit = 1;
        return YETTY_OK_VOID();
    case YETTY_YRICH_CMD_COPY:
        return handle_clipboard_chord(app, 67 /* C */);
    case YETTY_YRICH_CMD_CUT:
        return handle_clipboard_chord(app, 88 /* X */);
    case YETTY_YRICH_CMD_PASTE:
        return handle_clipboard_chord(app, 86 /* V */);
    default:
        break;
    }

    struct yetty_yclass_object_ptr_result doc_res =
        yetty_ygui_yrich_view_document(app->editor_view);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, doc_res, "dispatch_command: document");
    struct yetty_yclass_object *doc = doc_res.value;
    if (!doc) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result action = YETTY_OK_VOID();
    switch (command) {
    case YETTY_YRICH_CMD_UNDO:
        action = yetty_yrich_document_undo(doc);
        break;
    case YETTY_YRICH_CMD_REDO:
        action = yetty_yrich_document_redo(doc);
        break;
    case YETTY_YRICH_CMD_TOGGLE_BOLD:
        action = yetty_yrich_ydoc_toggle_format(doc, YETTY_YRICH_FMT_BOLD);
        break;
    case YETTY_YRICH_CMD_TOGGLE_ITALIC:
        action = yetty_yrich_ydoc_toggle_format(doc, YETTY_YRICH_FMT_ITALIC);
        break;
    case YETTY_YRICH_CMD_TOGGLE_UNDERLINE:
        action = yetty_yrich_ydoc_toggle_format(doc, YETTY_YRICH_FMT_UNDERLINE);
        break;
    case YETTY_YRICH_CMD_TOGGLE_STRIKE:
        action = yetty_yrich_ydoc_toggle_format(doc, YETTY_YRICH_FMT_STRIKE);
        break;
    case YETTY_YRICH_CMD_ALIGN_LEFT:
        action = yetty_yrich_ydoc_set_alignment(doc, YETTY_YRICH_HALIGN_LEFT);
        break;
    case YETTY_YRICH_CMD_ALIGN_CENTER:
        action = yetty_yrich_ydoc_set_alignment(doc, YETTY_YRICH_HALIGN_CENTER);
        break;
    case YETTY_YRICH_CMD_ALIGN_RIGHT:
        action = yetty_yrich_ydoc_set_alignment(doc, YETTY_YRICH_HALIGN_RIGHT);
        break;
    case YETTY_YRICH_CMD_ALIGN_JUSTIFY:
        action = yetty_yrich_ydoc_set_alignment(doc, YETTY_YRICH_HALIGN_JUSTIFY);
        break;
    case YETTY_YRICH_CMD_HEADING_NORMAL:
        action = yetty_yrich_ydoc_set_heading(doc, 0);
        break;
    case YETTY_YRICH_CMD_HEADING_1:
        action = yetty_yrich_ydoc_set_heading(doc, 1);
        break;
    case YETTY_YRICH_CMD_HEADING_2:
        action = yetty_yrich_ydoc_set_heading(doc, 2);
        break;
    case YETTY_YRICH_CMD_HEADING_3:
        action = yetty_yrich_ydoc_set_heading(doc, 3);
        break;
    case YETTY_YRICH_CMD_FONT_LARGER:
        action = yetty_yrich_ydoc_change_font_size(doc, 2.0f);
        break;
    case YETTY_YRICH_CMD_FONT_SMALLER:
        action = yetty_yrich_ydoc_change_font_size(doc, -2.0f);
        break;
    case YETTY_YRICH_CMD_ADD_PARAGRAPH: {
        struct yetty_yclass_object_ptr_result para = yetty_yrich_ydoc_add_paragraph(doc, "", 0);
        if (YETTY_IS_ERR(para)) {
            action = YETTY_ERR(yetty_ycore_void, "dispatch_command: add_paragraph", para);
        }
        break;
    }
    case YETTY_YRICH_CMD_LIST_BULLET:
        action = yetty_yrich_ydoc_set_list(doc, 1);
        break;
    case YETTY_YRICH_CMD_LIST_NUMBERED:
        action = yetty_yrich_ydoc_set_list(doc, 2);
        break;
    case YETTY_YRICH_CMD_LIST_CHECKLIST:
        action = yetty_yrich_ydoc_set_list(doc, 3);
        break;
    case YETTY_YRICH_CMD_CHECK_TOGGLE:
        action = yetty_yrich_ydoc_toggle_checked(doc);
        break;
    case YETTY_YRICH_CMD_INSERT_HRULE:
        action = yetty_yrich_ydoc_insert_horizontal_rule(doc);
        break;
    case YETTY_YRICH_CMD_SELECT_ALL:
        action = yetty_ygui_yrich_view_feed_key(app->editor_view, YETTY_YRICH_KEY_A,
                                                YETTY_YRICH_MOD_CTRL);
        break;
    case YETTY_YRICH_CMD_CARET_LEFT:
        action = yetty_ygui_yrich_view_feed_key(app->editor_view, YETTY_YRICH_KEY_LEFT, 0);
        break;
    case YETTY_YRICH_CMD_CARET_RIGHT:
        action = yetty_ygui_yrich_view_feed_key(app->editor_view, YETTY_YRICH_KEY_RIGHT, 0);
        break;
    case YETTY_YRICH_CMD_CARET_UP:
        action = yetty_ygui_yrich_view_feed_key(app->editor_view, YETTY_YRICH_KEY_UP, 0);
        break;
    case YETTY_YRICH_CMD_CARET_DOWN:
        action = yetty_ygui_yrich_view_feed_key(app->editor_view, YETTY_YRICH_KEY_DOWN, 0);
        break;
    case YETTY_YRICH_CMD_CARET_LINE_START:
        action = yetty_ygui_yrich_view_feed_key(app->editor_view, YETTY_YRICH_KEY_HOME, 0);
        break;
    case YETTY_YRICH_CMD_CARET_LINE_END:
        action = yetty_ygui_yrich_view_feed_key(app->editor_view, YETTY_YRICH_KEY_END, 0);
        break;
    default:
        return YETTY_OK_VOID();
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, action, "dispatch_command: action failed");
    return refit_and_push(app);
}

static struct yetty_ycore_void_result handle_event(struct yetty_yrich_app *app,
                                                   const struct yetty_yui_event *ev)
{
    /* Pointer routing follows the ychrome contract: the app's OWN UI comes
     * first, chrome gets only what ygui declined. The caption strip overlaps
     * the menubar row, so a chrome-first press would eat every menu click.
     * Chrome still precedes the UI for (a) motion — it claims nothing when
     * idle but needs the stream for the window-button hover highlight and a
     * live drag — and (b) any event while a caption-drag / edge-resize
     * gesture is live, so widgets the pointer crosses cannot steal it. */
    int chrome_offered = 0;
    if (app->chrome && (ev->type == YETTY_YCORE_MOUSE_DOWN || ev->type == YETTY_YCORE_MOUSE_UP ||
                        ev->type == YETTY_YCORE_MOUSE_MOVE || ev->type == YETTY_YCORE_MOUSE_DRAG ||
                        ev->type == YETTY_YCORE_MOUSE_DOUBLE_CLICK)) {
        int in_gesture = 0;
        struct yetty_ycore_int_result gesture_r = yetty_ychrome_host_in_gesture(app->chrome);
        if (YETTY_IS_OK(gesture_r)) {
            in_gesture = gesture_r.value;
        } else {
            yetty_ycore_error_destroy(gesture_r.error);
        }
        if (in_gesture || ev->type == YETTY_YCORE_MOUSE_MOVE ||
            ev->type == YETTY_YCORE_MOUSE_DRAG) {
            chrome_offered = 1;
            struct yetty_ycore_int_result chrome_r =
                yetty_ychrome_host_handle_event(app->chrome, ev);
            int chrome_consumed = YETTY_IS_OK(chrome_r) && chrome_r.value;
            if (YETTY_IS_ERR(chrome_r)) {
                yetty_ycore_error_destroy(chrome_r.error);
            }
            if (chrome_consumed) {
                return YETTY_OK_VOID();
            }
        }
    }
    switch (ev->type) {
    case YETTY_YCORE_SHUTDOWN:
    case YETTY_YCORE_WINDOW_CLOSE:
        app->quit = 1;
        return YETTY_OK_VOID();
    case YETTY_YCORE_RESIZE: {
        uint32_t w = (uint32_t)ev->resize.width;
        uint32_t h = (uint32_t)ev->resize.height;
        if (w == 0 || h == 0) {
            return YETTY_OK_VOID();
        }
        app->surface_w = w;
        app->surface_h = h;
        struct yetty_ycore_void_result reconf_r =
            yetty_yframework_reconfigure_surface(app->yrt, w, h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, reconf_r, "yrich: reconfigure surface");
        struct yetty_yrender_viewport vp = {.x = 0, .y = 0, .w = (float)w, .h = (float)h};
        struct yetty_ycore_void_result resize_r = app->target->ops->resize(app->target, vp);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, resize_r, "yrich: target resize");
        struct yetty_yfigure_figure_ptr_result rf_res =
            yetty_yfigure_container_as_figure(app->root_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rf_res, "yrich: container_as_figure");
        struct yetty_yfigure_figure *rf = rf_res.value;
        struct yetty_ycore_void_result rect_r = yetty_yfigure_figure_rect_set(
            (struct yetty_yclass_object *)(rf)-1, (struct yetty_ycore_rectangle){
                                                      .min = {.x = 0.0f, .y = 0.0f},
                                                      .max = {.x = (float)w, .y = (float)h},
                                                  });
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_r, "yrich: root rect");
        struct yetty_ycore_void_result dirty_r =
            yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(rf)-1, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dirty_r, "yrich: root dirty");
        struct yetty_ycore_void_result scene_r = push_scene(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_r, "yrich: push scene");
        if (app->chrome) {
            struct yetty_ycore_void_result chrome_rz =
                yetty_ychrome_host_resized(app->chrome, (float)w, (float)h);
            if (YETTY_IS_ERR(chrome_rz)) {
                yetty_ycore_error_destroy(chrome_rz.error);
            }
        }
        return YETTY_OK_VOID();
    }
    case YETTY_YCORE_KEY_DOWN: {
        if (!app->editor_view) {
            if (ev->key.key == 256) { /* Esc still exits when there's no editor */
                app->quit = 1;
            }
            return YETTY_OK_VOID();
        }
        uint32_t mods = glfw_mods_to_yrich(ev->key.mods);
        uint32_t key = glfw_key_to_yrich(ev->key.key, mods);
        if (ev->key.key == 256) { /* GLFW_KEY_ESCAPE */
            key = YETTY_YRICH_KEY_ESCAPE;
        }
        /* In vi modes, bare (unmodified) letters are commands — motions, mode
         * switches — but glfw_key_to_yrich only maps letters under Ctrl/Alt, so
         * map them here for the keymap lookup. */
        if (key == YETTY_YRICH_KEY_UNKNOWN && app->mode != YETTY_YRICH_MODE_DEFAULT &&
            ev->key.key >= 65 && ev->key.key <= 90) {
            key = YETTY_YRICH_KEY_A + (uint32_t)(ev->key.key - 65);
        }

        /* Keymap indirection: resolve (mode, key, mods) to a semantic command
         * and dispatch it. No shortcut is ever hardwired to an action here. */
        enum yetty_yrich_command_id command =
            yetty_yrich_keymap_lookup(&app->keymap, app->mode, (enum yetty_yrich_key)key, mods);
        if (command != YETTY_YRICH_CMD_NONE) {
            return dispatch_command(app, command);
        }

        /* Unbound Escape keeps the legacy "quit" behaviour. */
        if (key == YETTY_YRICH_KEY_ESCAPE) {
            app->quit = 1;
            return YETTY_OK_VOID();
        }
        /* No binding: vi-normal swallows the key (no raw editing); text-entry
         * modes feed it to the editor for caret/edit handling. */
        if (app->mode == YETTY_YRICH_MODE_VI_NORMAL || key == YETTY_YRICH_KEY_UNKNOWN) {
            return YETTY_OK_VOID();
        }
        struct yetty_ycore_void_result key_res =
            yetty_ygui_yrich_view_feed_key(app->editor_view, (enum yetty_yrich_key)key, mods);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, key_res, "yrich: feed key");
        return refit_and_push(app);
    }
    case YETTY_YCORE_PASTE: {
        /* Async clipboard fetch response — payload is a malloc'd string. */
        char *paste_text = ev->payload;
        if (!paste_text) {
            return YETTY_OK_VOID();
        }
        struct yetty_ycore_void_result text_res = YETTY_OK_VOID();
        if (app->editor_view) {
            text_res =
                yetty_ygui_yrich_view_feed_text(app->editor_view, paste_text, strlen(paste_text));
        }
        free(paste_text);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "yrich: paste");
        return refit_and_push(app);
    }
    case YETTY_YCORE_MOUSE_SCROLL: {
        /* Positions are framebuffer px and scale like the viewport; the wheel
         * deltas are notches and do not. */
        struct yetty_ycore_int_result scroll_res = yetty_ygui_framework_feed_mouse_scroll(
            app->ygui, ev->mouse_scroll.x / yrich_scale(app), ev->mouse_scroll.y / yrich_scale(app),
            ev->mouse_scroll.dx, ev->mouse_scroll.dy);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scroll_res, "yrich: scroll");
        struct yetty_ycore_void_result scene_r = push_scene(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_r, "yrich: push scene");
        return YETTY_OK_VOID();
    }
    case YETTY_YCORE_CHAR: {
        if (!app->editor_view) {
            return YETTY_OK_VOID();
        }
        /* In vi-normal mode, characters are commands (dispatched from KEY_DOWN),
         * never inserted as text. */
        if (app->mode == YETTY_YRICH_MODE_VI_NORMAL) {
            return YETTY_OK_VOID();
        }
        /* Characters arriving with Ctrl/Alt are shortcut echoes, not text. */
        if (ev->chr.mods & (2 /* GLFW_MOD_CONTROL */ | 4 /* GLFW_MOD_ALT */)) {
            return YETTY_OK_VOID();
        }
        char utf8[4];
        size_t utf8_len = encode_utf8(ev->chr.codepoint, utf8);
        if (utf8_len == 0) {
            return YETTY_OK_VOID();
        }
        struct yetty_ycore_void_result text_res =
            yetty_ygui_yrich_view_feed_text(app->editor_view, utf8, utf8_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "yrich: feed text");
        return refit_and_push(app);
    }
    case YETTY_YCORE_MOUSE_DOWN:
    case YETTY_YCORE_MOUSE_UP: {
        int is_down = ev->type == YETTY_YCORE_MOUSE_DOWN;
        struct yetty_ycore_int_result feed_r = yetty_ygui_framework_feed_mouse_button(
            app->ygui, ev->mouse.x / yrich_scale(app), ev->mouse.y / yrich_scale(app),
            ev->mouse.button, is_down, ev->mouse.mods);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, feed_r, "yrich: mouse button");
        if (!feed_r.value && app->chrome && !chrome_offered) {
            /* The UI declined — offer chrome its caption-drag / edge-resize /
             * window-button behavior. */
            struct yetty_ycore_int_result chrome_r =
                yetty_ychrome_host_handle_event(app->chrome, ev);
            if (YETTY_IS_ERR(chrome_r)) {
                yetty_ycore_error_destroy(chrome_r.error);
            }
        }
        struct yetty_ycore_void_result scene_r = push_scene(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_r, "yrich: push scene");
        return YETTY_OK_VOID();
    }
    case YETTY_YCORE_MOUSE_DOUBLE_CLICK: {
        /* The document view gets first crack — a double-click inside it
         * selects the word and arms word-granularity dragging. The raw
         * DOWN/UP pair already went through the widget tree for caret
         * placement; this adds the word behavior on top. If the click was
         * outside the view, chrome's caption double-click-maximize applies. */
        int consumed = 0;
        if (app->editor_view) {
            struct yetty_ycore_int_result view_r = yetty_ygui_yrich_view_feed_double_click(
                app->editor_view, ev->mouse.x, ev->mouse.y, ev->mouse.button);
            if (YETTY_IS_OK(view_r)) {
                consumed = view_r.value;
            } else {
                yetty_ycore_error_destroy(view_r.error);
            }
            if (consumed) {
                struct yetty_ycore_void_result scene_r = push_scene(app);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_r, "yrich: push scene");
            }
        }
        if (!consumed && app->chrome && !chrome_offered) {
            struct yetty_ycore_int_result chrome_r =
                yetty_ychrome_host_handle_event(app->chrome, ev);
            if (YETTY_IS_ERR(chrome_r)) {
                yetty_ycore_error_destroy(chrome_r.error);
            }
        }
        return YETTY_OK_VOID();
    }
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG: {
        struct yetty_ycore_int_result feed_r = yetty_ygui_framework_feed_mouse_motion(
            app->ygui, ev->mouse.x / yrich_scale(app), ev->mouse.y / yrich_scale(app));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, feed_r, "yrich: mouse move");
        struct yetty_ycore_void_result scene_r = push_scene(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_r, "yrich: push scene");
        return YETTY_OK_VOID();
    }
    default:
        return YETTY_OK_VOID();
    }
}

YETTY_ANNOTATE("override@yapp:app:init")
static struct yetty_ycore_void_result yrich_app_init(struct yetty_yclass_object *obj,
                                                     struct yetty_yclass_object *platform)
{
    (void)obj;
    (void)platform;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yapp:app:run")
static struct yetty_ycore_void_result yrich_app_run(struct yetty_yclass_object *obj,
                                                    struct yetty_yclass_object *platform)
{
    struct yetty_yrich_app_ptr_result app_res = yetty_yrich_app_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, app_res, "yrich:app:run: app_from");
    struct yetty_yrich_app *app = app_res.value;

    struct yetty_yplatform_gpu_context_const_ptr_result gpu_res =
        yetty_yplatform_platform_gpu_context(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gpu_res, "yrich:app:run: platform gpu context");
    const struct yetty_yplatform_gpu_context *gpu = gpu_res.value;

    struct yetty_ycore_xthread_event_pipe_ptr_result input_pipe_res =
        yetty_yplatform_platform_input_pipe(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, input_pipe_res, "yrich:app:run: platform input pipe");
    struct yetty_ycore_xthread_event_pipe *input_pipe = input_pipe_res.value;

    if (!gpu || !input_pipe) {
        return YETTY_ERR(yetty_ycore_void, "yrich:app:run: platform state not populated");
    }

    struct yetty_yframework_ptr_result yr = yetty_yframework_create(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yr, "yframework_create failed");
    app->yrt = yr.value;

    app->ctx.runtime = app->yrt;
    app->ctx.pty_factory = NULL;
    app->ctx.event_loop = app->yrt->event_loop;

    /* Semantic command bindings — the default (desktop) + vi keymaps. Keys are
     * resolved to commands through this, never hardwired. */
    yetty_yrich_keymap_init(&app->keymap);
    app->mode = YETTY_YRICH_MODE_DEFAULT;
    struct yetty_ycore_void_result keymap_res = yetty_yrich_keymap_load_defaults(&app->keymap);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, keymap_res, "yrich_app_init: keymap defaults");

    app->surface = gpu->surface;
    app->surface_w = gpu->surface_width;
    app->surface_h = gpu->surface_height;
    /* Standalone reads the HiDPI factor straight off the GPU context; the
     * in-terminal path overwrites it from the RESIZE envelope. */
    app->content_scale = gpu->content_scale > 0.0f ? gpu->content_scale : 1.0f;

    /* Texture target that blits to the GLFW surface on present. */
    app->yrt->render_target->ops->destroy(app->yrt->render_target);
    app->yrt->render_target = NULL;
    struct yetty_yrender_viewport vp = {
        .x = 0, .y = 0, .w = (float)app->surface_w, .h = (float)app->surface_h};
    struct yetty_yrender_target_ptr_result tr = yetty_yrender_target_texture_create(
        app->yrt->gpu.device, app->yrt->gpu.queue, app->yrt->gpu.surface_format,
        app->yrt->gpu.allocator, (WGPUSurface)app->surface, vp);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "texture target create failed");
    app->target = tr.value;

    /* Font for the scene factory (text spans → glyphs). The Regular face is
     * required; the styled faces (Bold/Oblique/BoldOblique) are best-effort so
     * ydoc can render real bold + italic glyphs — a face that fails to load
     * just leaves ydoc falling back to Regular for that style. */
    char shader_path[768];
    {
        struct yetty_yconfig_config *config = app->yrt->config;
        const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
        const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
        const char *cache_dir = config->ops->get_string(config, "paths/cache", "");
        const char *font_family = "DejaVuSansMNerdFontMono";
        struct yetty_ymsdf_generator *generator = app->yrt->gpu.msdf_generator;
        char cdb_path[768];
        struct yetty_ycore_void_result cdb_res = yetty_yfont_msdf_resolve_cdb(
            generator, fonts_dir, cache_dir, font_family, "-Regular", cdb_path, sizeof(cdb_path));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cdb_res, "resolve regular msdf cdb failed");
        snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl", shaders_dir);
        struct yetty_font_font_result font_result =
            yetty_yfont_msdf_font_create(cdb_path, shader_path, "yrich_app");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, font_result, "msdf_font_create failed");
        app->font = font_result.value;
        struct yetty_ycore_void_result result_224 = app->font->ops->load_basic_latin(app->font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_224, "font load_basic_latin failed");

        /* Best-effort styled faces. Each is an independent MSDF font over its
         * own .cdb; slot assignment (1/2/3) happens in the scene factory. */
        struct {
            const char *suffix; /* face style suffix, e.g. "-Bold" */
            const char *name;
            struct yetty_yfont_font **out;
        } styled[3] = {
            {"-Bold", "yrich_app_bold", &app->font_bold},
            {"-Oblique", "yrich_app_italic", &app->font_italic},
            {"-BoldOblique", "yrich_app_bold_italic", &app->font_bold_italic},
        };
        for (size_t style_index = 0; style_index < 3; style_index++) {
            char styled_cdb[768];
            /* Best-effort: resolve (and GPU-generate on first run) the styled
             * face. A style whose TTF isn't shipped resolves to an error and is
             * skipped silently — ydoc falls back to Regular for that style. */
            struct yetty_ycore_void_result styled_cdb_res = yetty_yfont_msdf_resolve_cdb(
                generator, fonts_dir, cache_dir, font_family, styled[style_index].suffix,
                styled_cdb, sizeof(styled_cdb));
            if (YETTY_IS_ERR(styled_cdb_res)) {
                yetty_ycore_error_destroy(styled_cdb_res.error);
                continue;
            }
            struct yetty_font_font_result styled_result =
                yetty_yfont_msdf_font_create(styled_cdb, shader_path, styled[style_index].name);
            if (YETTY_IS_ERR(styled_result)) {
                yetty_ycore_error_destroy(styled_result.error);
                continue;
            }
            struct yetty_ycore_void_result latin_res =
                styled_result.value->ops->load_basic_latin(styled_result.value);
            if (YETTY_IS_ERR(latin_res)) {
                yetty_ycore_error_destroy(latin_res.error);
                styled_result.value->ops->destroy(styled_result.value);
                continue;
            }
            *styled[style_index].out = styled_result.value;
        }
    }

    /* Registry + yscene factory + in-process root container. */
    struct yetty_yfigure_registry_ptr_result reg_r = yetty_yfigure_registry_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, reg_r, "yfigure_registry_create failed");
    app->registry = reg_r.value;
    /* Composite factory so ydoc inline images (and plots) render as real
     * decoded figures through the scene's composite pass. */
    {
        struct yetty_ydraw_composite_factory_ptr_result factory_res =
            yetty_ydraw_composite_factory_create(app->yrt->gpu.device, app->yrt->gpu.queue,
                                                 app->yrt->gpu.surface_format,
                                                 app->yrt->gpu.allocator, app->yrt->event_loop);
        if (YETTY_IS_ERR(factory_res)) {
            yetty_ycore_error_destroy(factory_res.error);
        } else {
            app->composite_factory = factory_res.value;
            struct yetty_ydraw_concrete_factory *yimage_factory = yetty_yimage_factory_create();
            if (yimage_factory) {
                struct yetty_ycore_void_result reg =
                    yetty_ydraw_composite_factory_register(app->composite_factory, yimage_factory);
                if (YETTY_IS_ERR(reg)) {
                    yetty_ycore_error_destroy(reg.error);
                }
            }
            struct yetty_ydraw_concrete_factory *yplot_factory = yetty_yplot_factory_create();
            if (yplot_factory) {
                struct yetty_ycore_void_result reg =
                    yetty_ydraw_composite_factory_register(app->composite_factory, yplot_factory);
                if (YETTY_IS_ERR(reg)) {
                    yetty_ycore_error_destroy(reg.error);
                }
            }
        }
    }

    app->figure_args.default_font = app->font;
    app->figure_args.bold_font = app->font_bold;
    app->figure_args.italic_font = app->font_italic;
    app->figure_args.bold_italic_font = app->font_bold_italic;
    app->figure_args.composite_factory = app->composite_factory;
    /* The legacy "ygrid" kind token renders through the retained yscene
     * engine. Absolute (logical-pane) coordinates — the same mode the
     * ygrid factory forced for this kind. */
    app->figure_args.absolute_coords = 1;
    struct yetty_ycore_void_result result_234 = yetty_yscene_register_factory_for_kind(
        app->registry, yetty_yfigure_kind_token("ygrid"), &app->figure_args);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_234, "yscene_register_factory_for_kind failed");

    struct yetty_ycore_rectangle root_rect = {
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = (float)app->surface_w, .y = (float)app->surface_h},
    };
    struct yetty_yclass_ctx yclass_ctx = {0};
    struct yetty_yclass_object_ptr_result obj_res = yetty_yfigure_container_create(&yclass_ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_res, "root_container create failed");
    app->container_obj = obj_res.value;
    app->root_obj = obj_res.value;
    yetty_yfigure_container_set_context(app->root_obj, &app->ctx);
    yetty_yfigure_container_set_registry(app->root_obj, app->registry);
    yetty_yfigure_container_set_rect(app->root_obj, root_rect);

    /* ygui framework → container over the in-process yclass slot path. */
    struct yetty_yclass_object_ptr_result eng_r = yetty_ygui_framework_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, eng_r, "ygui framework alloc failed");
    app->ygui = eng_r.value;
    struct yetty_ycore_void_result result_255 =
        yetty_ygui_framework_set_container_obj(app->ygui, app->container_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_255, "framework set_container_obj failed");

    struct yetty_ycore_void_result result_259 = build_editor(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_259, "build_editor failed");
    struct yetty_ycore_void_result result_260 = push_scene(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_260, "initial push_scene failed");

    /* Window chrome: draggable/resizable titlebar + min/max/close (SDF, no
     * font). Composited as a pinned figure over the document. */
    {
        struct yetty_ychrome_host_ptr_result chrome_r = yetty_ychrome_host_create(
            app->root_obj, app->font, &app->ctx, app->yrt->window_chrome, (float)app->surface_w,
            (float)app->surface_h, app->yrt->gpu.app_gpu_context.content_scale, 34.0f, 8.0f,
            YETTY_YCHROME_FLAG_ALL);
        if (YETTY_IS_OK(chrome_r)) {
            app->chrome = chrome_r.value;
        } else {
            ywarn("yrich app: chrome host create failed: %s", chrome_r.error.msg);
            yetty_ycore_error_destroy(chrome_r.error);
        }
    }

    struct yetty_ycore_int_result fdr = input_pipe->ops->read_fd(input_pipe);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fdr, "pipe read_fd failed");
    int pipe_fd = fdr.value;

    int needs_render = 1;
    while (!app->quit) {
        struct pollfd pfd = {.fd = pipe_fd, .events = POLLIN, .revents = 0};
        int pr = poll(&pfd, 1, needs_render ? 0 : -1);
        int had_events = 0;
        if (pr > 0 && (pfd.revents & POLLIN)) {
            for (;;) {
                struct yetty_yui_event ev = {0};
                struct yetty_ycore_size_result rr =
                    input_pipe->ops->read(input_pipe, &ev, sizeof(ev));
                if (YETTY_IS_ERR(rr) || rr.value != sizeof(ev)) {
                    break;
                }
                struct yetty_ycore_void_result ev_r = handle_event(app, &ev);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_r, "yrich worker: handle_event");
                had_events = 1;
            }
        }
        if (gpu->instance) {
            wgpuInstanceProcessEvents(gpu->instance);
        }
        struct yetty_yfigure_figure_ptr_result rrf_res =
            yetty_yfigure_container_as_figure(app->root_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rrf_res, "yrich worker: container_as_figure");
        struct yetty_yfigure_figure *rrf = rrf_res.value;
        if (!(needs_render || had_events ||
              yetty_yfigure_figure_dirty_get((struct yetty_yclass_object *)(rrf)-1).value)) {
            continue;
        }
        destroy_safe(app->target->ops->clear(app->target));
        struct yetty_ycore_void_result rr =
            yetty_yfigure_render((struct yetty_yclass_object *)rrf - 1, app->target);
        if (YETTY_IS_ERR(rr)) {
            yerror("yrich-app: root render failed: %s", rr.error.msg);
            yetty_ycore_error_destroy(rr.error);
        } else {
            {
                struct yetty_ycore_void_result drop_r =
                    yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(rrf)-1, 0);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r,
                                    "drop: yetty_yfigure_figure_dirty_set");
            }
        }
        destroy_safe(app->target->ops->present(app->target));
        needs_render = 0;
    }

    /* Chrome engine (its caption figure is owned by the container below). */
    if (app->chrome) {
        destroy_safe(yetty_ychrome_host_destroy(app->chrome));
        app->chrome = NULL;
    }

    /* Teardown — container first so pending GPU work flushes. */
    {
        struct yetty_yfigure_figure_ptr_result rrf_res =
            yetty_yfigure_container_as_figure(app->root_obj);
        if (YETTY_IS_ERR(rrf_res)) {
            yetty_ycore_error_destroy(rrf_res.error);
        } else {
            struct yetty_yfigure_figure *rrf = rrf_res.value;
            destroy_safe(yetty_yfigure_destroy((struct yetty_yclass_object *)rrf - 1));
        }
    }
    app->root_obj = NULL;
    if (app->registry) {
        destroy_safe(yetty_yfigure_registry_destroy(app->registry));
        app->registry = NULL;
    }
    if (app->composite_factory) {
        yetty_ydraw_composite_factory_destroy(app->composite_factory);
        app->composite_factory = NULL;
    }
    app->target->ops->destroy(app->target);
    app->target = NULL;
    if (app->ygui) {
        destroy_safe(yetty_ygui_framework_destroy(app->ygui));
        app->ygui = NULL;
    }
    if (app->font) {
        app->font->ops->destroy(app->font);
        app->font = NULL;
    }
    if (app->font_bold) {
        app->font_bold->ops->destroy(app->font_bold);
        app->font_bold = NULL;
    }
    if (app->font_italic) {
        app->font_italic->ops->destroy(app->font_italic);
        app->font_italic = NULL;
    }
    if (app->font_bold_italic) {
        app->font_bold_italic->ops->destroy(app->font_bold_italic);
        app->font_bold_italic = NULL;
    }
    yetty_yrich_keymap_clear(&app->keymap);
    destroy_safe(yetty_yframework_destroy(app->yrt));
    app->yrt = NULL;
    return YETTY_OK_VOID();
}

#ifdef YETTY_YGUI_HAS_UV
/*===========================================================================
 * Dual mode: in-terminal (hosted-by-yetty) host. Renders the editor into the
 * hosting yetty over ywire instead of a local GPU window. Reuses build_editor
 * (the framework in wire mode emits the same widget tree); the transport/ywire/
 * libuv scaffolding mirrors yguiapp/run.c's client host.
 *=========================================================================*/
struct yrich_terminal_host {
    uv_loop_t loop;
    uv_poll_t stdin_poll;
    uv_prepare_t prep;
    uv_timer_t frame_timer;
    struct yetty_yclass_transport_pty *transport;
    struct yetty_ywire_connection *conn;
    struct yetty_yrich_app *app;
    int running;
};

/* Raw keystrokes from the controlling terminal → the framework input decoder. */
static void yrich_terminal_raw_sink(void *user, const uint8_t *bytes, size_t n)
{
    struct yrich_terminal_host *host = (struct yrich_terminal_host *)user;
    struct yetty_ycore_void_result fr =
        yetty_ygui_framework_feed_input(host->app->ygui, (const char *)bytes, n);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
    }
}

static void yrich_terminal_resize_cb(void *user, int width_px, int height_px, int cols, int rows)
{
    (void)cols;
    (void)rows;
    struct yrich_terminal_host *host = (struct yrich_terminal_host *)user;
    if (width_px > 0 && height_px > 0) {
        /* Record the pane size: push_scene() (shared with the standalone path)
         * re-applies the viewport from surface_w/h after every edit. In terminal
         * mode these are otherwise 0, so a command would reset the viewport to
         * 0x0 and collapse the whole editor to nothing. */
        host->app->surface_w = (uint32_t)width_px;
        host->app->surface_h = (uint32_t)height_px;
        /* TIOCGWINSZ pixels are FRAMEBUFFER px; ygui wants LOGICAL. The scale
         * arrives on the RESIZE envelope (1.0 until then). */
        const float scale = yrich_scale(host->app);
        struct yetty_ycore_void_result r = yetty_ygui_framework_set_viewport(
            host->app->ygui, (float)width_px / scale, (float)height_px / scale);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        /* The wire path has no container rect to stretch the root to the pane
         * (the standalone path gets that from the yfigure container). Pin the
         * root's size to the pane so the editor fills it instead of shrinking to
         * content. */
        if (host->app->win) {
            struct yetty_ycore_void_result sr =
                yetty_ygui_widget_set_size(host->app->win, (float)width_px, (float)height_px);
            if (YETTY_IS_ERR(sr)) {
                yetty_ycore_error_destroy(sr.error);
            }
        }
    }
}

static void yrich_terminal_stdin_cb(uv_poll_t *handle, int status, int events)
{
    struct yrich_terminal_host *host = (struct yrich_terminal_host *)handle->data;
    if (status < 0) {
        /* Poll error / fd disconnect — the host terminal is gone. Stop so ydoc
         * exits with it instead of lingering as an orphan. */
        host->running = 0;
        return;
    }
    if (!(events & UV_READABLE)) {
        return;
    }
    struct yetty_ycore_size_result r = yetty_ywire_connection_pump_readable(host->conn);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
    struct yetty_ycore_size_result w = yetty_ywire_connection_pump_writable(host->conn);
    if (YETTY_IS_ERR(w)) {
        yetty_ycore_error_destroy(w.error);
    }
    if (yetty_ywire_connection_is_eof(host->conn)) {
        host->running = 0;
    }
}

static void yrich_terminal_prep_cb(uv_prepare_t *handle)
{
    struct yrich_terminal_host *host = (struct yrich_terminal_host *)handle->data;
    if (yetty_ygui_framework_is_dirty(host->app->ygui)) {
        struct yetty_ycore_void_result r = yetty_ygui_framework_emit(host->app->ygui);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
    }
    struct yetty_ycore_size_result w = yetty_ywire_connection_pump_writable(host->conn);
    if (YETTY_IS_ERR(w)) {
        yetty_ycore_error_destroy(w.error);
    }
    if (!host->running || host->app->quit) {
        uv_stop(handle->loop);
    }
}

/* Keeps the loop ticking so the prepare hook drains output promptly. */
static void yrich_terminal_frame_cb(uv_timer_t *handle)
{
    (void)handle;
}

/* Route a resolved (yrich key, mods) through the keymap → a command, else feed
 * the key to the view. Escape quits. Shared by the raw-decoder key_cb and the
 * forwarded figure-key path so both honour the same bindings. */
static struct yetty_ycore_void_result yrich_route_key(struct yetty_yrich_app *app,
                                                      enum yetty_yrich_key ykey, uint32_t ymods)
{
    if (!app->editor_view || ykey == YETTY_YRICH_KEY_UNKNOWN) {
        return YETTY_OK_VOID();
    }
    enum yetty_yrich_command_id command =
        yetty_yrich_keymap_lookup(&app->keymap, app->mode, ykey, ymods);
    if (command != YETTY_YRICH_CMD_NONE) {
        return dispatch_command(app, command);
    }
    if (ykey == YETTY_YRICH_KEY_ESCAPE) {
        app->quit = 1; /* Esc closes the editor */
        return YETTY_OK_VOID();
    }
    if (app->mode == YETTY_YRICH_MODE_VI_NORMAL) {
        return YETTY_OK_VOID();
    }
    return yetty_ygui_yrich_view_feed_key(app->editor_view, ykey, ymods);
}

/* Decoded keyboard from the framework input decoder → the editor. Mirrors the
 * standalone KEY_DOWN path: resolve (mode,key,mods) to a command, else feed the
 * key/text to the view. This is what makes editing work in terminal mode — the
 * yrich_view is driven by feed_key/feed_text, not the framework's widget focus. */
static int yrich_terminal_key_cb(struct yetty_yclass_object *framework, uint32_t key, int mods,
                                 void *userdata)
{
    (void)framework;
    struct yrich_terminal_host *host = (struct yrich_terminal_host *)userdata;
    struct yetty_yrich_app *app = host->app;
    if (!app->editor_view) {
        return 0;
    }
    uint32_t ymods = 0;
    if (mods & 0x1) {
        ymods |= YETTY_YRICH_MOD_SHIFT;
    }
    if (mods & 0x2) {
        ymods |= YETTY_YRICH_MOD_ALT;
    }
    if (mods & 0x4) {
        ymods |= YETTY_YRICH_MOD_CTRL;
    }

    enum yetty_yrich_key ykey = YETTY_YRICH_KEY_UNKNOWN;
    switch (key) {
    case YETTY_YGUI_KEY_ARROW_LEFT:
        ykey = YETTY_YRICH_KEY_LEFT;
        break;
    case YETTY_YGUI_KEY_ARROW_RIGHT:
        ykey = YETTY_YRICH_KEY_RIGHT;
        break;
    case YETTY_YGUI_KEY_ARROW_UP:
        ykey = YETTY_YRICH_KEY_UP;
        break;
    case YETTY_YGUI_KEY_ARROW_DOWN:
        ykey = YETTY_YRICH_KEY_DOWN;
        break;
    case YETTY_YGUI_KEY_HOME:
        ykey = YETTY_YRICH_KEY_HOME;
        break;
    case YETTY_YGUI_KEY_END:
        ykey = YETTY_YRICH_KEY_END;
        break;
    case YETTY_YGUI_KEY_PAGE_UP:
        ykey = YETTY_YRICH_KEY_PAGEUP;
        break;
    case YETTY_YGUI_KEY_PAGE_DOWN:
        ykey = YETTY_YRICH_KEY_PAGEDOWN;
        break;
    case YETTY_YGUI_KEY_DELETE:
        ykey = YETTY_YRICH_KEY_DELETE;
        break;
    case 0x0D:
    case 0x0A:
        ykey = YETTY_YRICH_KEY_ENTER;
        break;
    case 0x09:
        ykey = YETTY_YRICH_KEY_TAB;
        break;
    case 0x7F:
    case 0x08:
        ykey = YETTY_YRICH_KEY_BACKSPACE;
        break;
    case 0x1B:
        ykey = YETTY_YRICH_KEY_ESCAPE;
        break;
    default:
        break;
    }
    /* Ctrl+letter arrives as a control byte (0x01..0x1A) with no CSI mods. */
    if (ykey == YETTY_YRICH_KEY_UNKNOWN && key >= 0x01 && key <= 0x1A) {
        ykey = (enum yetty_yrich_key)(YETTY_YRICH_KEY_A + (int)(key - 1));
        ymods |= YETTY_YRICH_MOD_CTRL;
    }

    if (ykey != YETTY_YRICH_KEY_UNKNOWN) {
        destroy_safe(yrich_route_key(app, ykey, ymods));
        return 1;
    }

    /* Printable codepoint → text. */
    if (key >= 0x20 && key <= 0x10FFFF && !(ymods & YETTY_YRICH_MOD_CTRL)) {
        if (app->mode == YETTY_YRICH_MODE_VI_NORMAL) {
            return 1;
        }
        char utf8[4];
        size_t len = encode_utf8(key, utf8);
        if (len > 0) {
            destroy_safe(yetty_ygui_yrich_view_feed_text(app->editor_view, utf8, len));
        }
        return 1;
    }
    return 0;
}

/* Forwarded pointer events from the host yetty (once the figure is click-
 * focused) → the framework, which routes them to the yrich_view's on_press/
 * on_motion (caret placement + drag-select). */
static void yrich_terminal_input_sink(void *user, int wire_code, const uint8_t *args,
                                      size_t args_len, const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    struct yrich_terminal_host *host = (struct yrich_terminal_host *)user;
    struct yetty_yrich_app *app = host->app;

    /* Forwarded keyboard: once a figure is click-focused, yetty delivers keys as
     * client-input key envelopes (not raw bytes). Route them through the same
     * keymap — so Ctrl+Q / Esc / formatting chords work after clicking in. */
    if (wire_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY ||
        wire_code == YETTY_OSC_SC_CLIENT_INPUT_KEY) {
        if (payload_len < sizeof(struct yetty_client_input_key)) {
            return;
        }
        const struct yetty_client_input_key *key_msg =
            (const struct yetty_client_input_key *)payload;
        if (key_msg->magic != YETTY_CLIENT_INPUT_KEY_MAGIC) {
            return;
        }
        uint32_t ymods = 0;
        if (key_msg->mods & 0x1) {
            ymods |= YETTY_YRICH_MOD_SHIFT;
        }
        if (key_msg->mods & 0x2) {
            ymods |= YETTY_YRICH_MOD_CTRL;
        }
        if (key_msg->mods & 0x4) {
            ymods |= YETTY_YRICH_MOD_ALT;
        }
        if (key_msg->kind == YETTY_YMGUI_INPUT_KEY_CHAR) {
            uint32_t codepoint = key_msg->codepoint;
            if (ymods & YETTY_YRICH_MOD_CTRL) {
                uint32_t upper =
                    (codepoint >= 'a' && codepoint <= 'z') ? codepoint - 32u : codepoint;
                if (upper >= 'A' && upper <= 'Z') {
                    destroy_safe(yrich_route_key(
                        app, (enum yetty_yrich_key)(YETTY_YRICH_KEY_A + (int)(upper - 'A')),
                        ymods));
                } else if (codepoint >= 1 && codepoint <= 26) {
                    destroy_safe(yrich_route_key(
                        app, (enum yetty_yrich_key)(YETTY_YRICH_KEY_A + (int)(codepoint - 1)),
                        ymods));
                }
            } else if (codepoint >= 0x20 && codepoint <= 0x10FFFF &&
                       app->mode != YETTY_YRICH_MODE_VI_NORMAL && app->editor_view) {
                char utf8[4];
                size_t len = encode_utf8(codepoint, utf8);
                if (len > 0) {
                    destroy_safe(yetty_ygui_yrich_view_feed_text(app->editor_view, utf8, len));
                }
            }
        } else if (key_msg->kind == YETTY_YMGUI_INPUT_KEY_DOWN) {
            uint32_t ykey = glfw_key_to_yrich(key_msg->key, ymods);
            if (key_msg->key == 256) { /* GLFW_KEY_ESCAPE */
                ykey = YETTY_YRICH_KEY_ESCAPE;
            }
            destroy_safe(yrich_route_key(app, (enum yetty_yrich_key)ykey, ymods));
        }
        return;
    }

    if (wire_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE ||
        wire_code == YETTY_OSC_SC_CLIENT_INPUT_RESIZE) {
        if (payload_len < sizeof(struct yetty_client_input_resize)) {
            return;
        }
        const struct yetty_client_input_resize *rz =
            (const struct yetty_client_input_resize *)payload;
        if (rz->magic != YETTY_CLIENT_INPUT_RESIZE_MAGIC || rz->width <= 0.0f ||
            rz->height <= 0.0f) {
            return;
        }
        /* Learn the host's HiDPI factor. surface_w/h stay FRAMEBUFFER px —
         * push_scene() divides when it applies the viewport. */
        if (rz->content_scale > 0.0f) {
            app->content_scale = rz->content_scale;
        }
        app->surface_w = (uint32_t)rz->width;
        app->surface_h = (uint32_t)rz->height;
        destroy_safe(push_scene(app));
        return;
    }

    if (wire_code != YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE &&
        wire_code != YETTY_OSC_SC_CLIENT_INPUT_MOUSE) {
        return;
    }
    if (payload_len < sizeof(struct yetty_client_input_mouse)) {
        return;
    }
    const struct yetty_client_input_mouse *msg = (const struct yetty_client_input_mouse *)payload;
    if (msg->magic != YETTY_CLIENT_INPUT_MOUSE_MAGIC) {
        return;
    }
    /* Host forwards pointer coords in FRAMEBUFFER px; ygui hit-tests logical. */
    const float mscale = yrich_scale(app);
    struct yetty_ycore_int_result r = YETTY_OK(yetty_ycore_int, 0);
    switch (msg->kind) {
    case YETTY_YMGUI_INPUT_MOUSE_BUTTON:
        r = yetty_ygui_framework_feed_mouse_button(host->app->ygui, msg->x / mscale,
                                                   msg->y / mscale, msg->button, msg->pressed, 0);
        break;
    case YETTY_YMGUI_INPUT_MOUSE_POS:
        r = yetty_ygui_framework_feed_mouse_motion(host->app->ygui, msg->x / mscale,
                                                   msg->y / mscale);
        break;
    case YETTY_YMGUI_INPUT_MOUSE_WHEEL:
        r = yetty_ygui_framework_feed_mouse_scroll(host->app->ygui, msg->x / mscale,
                                                   msg->y / mscale, 0.0f, msg->wheel_dy);
        break;
    default:
        break;
    }
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* Ask the host yetty to forward mouse (?1500/?1501) + keyboard (?1502). */
static void yrich_terminal_enable_input(struct yrich_terminal_host *host)
{
    struct yetty_ywire_channel *raw =
        yetty_ywire_connection_channel(host->conn, YETTY_YWIRE_CHANNEL_RAW);
    if (!raw) {
        return;
    }
    static const char enable[] = "\033[?1500h\033[?1501h\033[?1502h";
    struct yetty_ycore_size_result wr = yetty_ywire_channel_write(raw, enable, sizeof(enable) - 1);
    if (YETTY_IS_ERR(wr)) {
        yetty_ycore_error_destroy(wr.error);
        return;
    }
    destroy_safe(yetty_ywire_channel_flush(raw));
}

/* Undo yrich_terminal_enable_input on graceful exit: turn the host terminal's
 * mouse/keyboard forwarding back off so the shell owns wheel/keys again. Runs
 * after the uv loop has stopped, so pump the bytes out synchronously. */
static void yrich_terminal_disable_input(struct yrich_terminal_host *host)
{
    struct yetty_ywire_channel *raw =
        yetty_ywire_connection_channel(host->conn, YETTY_YWIRE_CHANNEL_RAW);
    if (!raw) {
        return;
    }
    static const char disable[] = "\033[?1500l\033[?1501l\033[?1502l";
    struct yetty_ycore_size_result wr =
        yetty_ywire_channel_write(raw, disable, sizeof(disable) - 1);
    if (YETTY_IS_ERR(wr)) {
        yetty_ycore_error_destroy(wr.error);
        return;
    }
    destroy_safe(yetty_ywire_channel_flush(raw));
    struct yetty_ycore_size_result pump_res = yetty_ywire_connection_pump_writable(host->conn);
    if (YETTY_IS_ERR(pump_res)) {
        yetty_ycore_error_destroy(pump_res.error);
    }
}

static struct yetty_ycore_void_result yrich_run_terminal(struct yetty_yrich_app *app)
{
    struct yrich_terminal_host host = {0};
    host.app = app;
    host.running = 1;

    /* Load the command bindings — the standalone run() override does this, but
     * terminal mode bypasses it, so without this no command chord (Ctrl+S/B/Q,
     * vi) resolves and only raw text/caret editing works. */
    yetty_yrich_keymap_init(&app->keymap);
    app->mode = YETTY_YRICH_MODE_DEFAULT;
    struct yetty_ycore_void_result keymap_res = yetty_yrich_keymap_load_defaults(&app->keymap);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, keymap_res, "yrich terminal: keymap defaults");

    struct yetty_yclass_transport_pty_ptr_result tr =
        yetty_yclass_transport_pty_create_from_env(STDIN_FILENO, STDOUT_FILENO);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "yrich terminal: transport create");
    host.transport = tr.value;

    struct yetty_ycore_void_result raw = yetty_yclass_transport_pty_enable_raw_mode(host.transport);
    if (YETTY_IS_ERR(raw)) {
        destroy_safe(yetty_yclass_transport_pty_destroy(host.transport));
        return YETTY_ERR(yetty_ycore_void, "yrich terminal: enable_raw_mode", raw);
    }

    struct yetty_ywire_connection_ptr_result cr =
        yetty_ywire_connection_create(yetty_yclass_transport_pty_reactor(host.transport), 1);
    if (YETTY_IS_ERR(cr)) {
        destroy_safe(yetty_yclass_transport_pty_destroy(host.transport));
        return YETTY_ERR(yetty_ycore_void, "yrich terminal: connection create", cr);
    }
    host.conn = cr.value;

    if (uv_loop_init(&host.loop) != 0) {
        destroy_safe(yetty_yclass_transport_pty_destroy(host.transport));
        return YETTY_ERR(yetty_ycore_void, "yrich terminal: uv_loop_init");
    }

    struct yetty_yclass_object_ptr_result fr = yetty_ygui_framework_create(NULL);
    if (YETTY_IS_ERR(fr)) {
        uv_loop_close(&host.loop);
        destroy_safe(yetty_yclass_transport_pty_destroy(host.transport));
        return YETTY_ERR(yetty_ycore_void, "yrich terminal: framework create", fr);
    }
    app->ygui = fr.value;

    /* Bind the framework's figure output to its OWN dynamic RPC channel on the
     * connection (the SSH model): the framework opens the channel, the host
     * serves it via its accept callback. */
    struct yetty_ycore_void_result attach =
        yetty_ygui_framework_attach_connection(app->ygui, host.conn);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, attach, "yrich terminal: attach connection");

    /* Build the same editor tree the standalone path builds. */
    struct yetty_ycore_void_result be = build_editor(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, be, "yrich terminal: build_editor");

    /* Decoded keys from framework_feed_input drive the editor. */
    yetty_ygui_framework_set_key_cb(app->ygui, yrich_terminal_key_cb, &host);

    /* Route inbound lanes: raw keystrokes → framework decoder; forwarded mouse →
     * framework (→ yrich_view on_press); resize → viewport. */
    struct yetty_ywire_channel *raw_ch =
        yetty_ywire_connection_channel(host.conn, YETTY_YWIRE_CHANNEL_RAW);
    destroy_safe(yetty_ywire_channel_set_raw_sink(raw_ch, yrich_terminal_raw_sink, &host));
    struct yetty_ywire_channel *input_ch =
        yetty_ywire_connection_channel(host.conn, YETTY_YWIRE_CHANNEL_INPUT);
    destroy_safe(yetty_ywire_channel_set_envelope_sink(input_ch, yrich_terminal_input_sink, &host));
    destroy_safe(yetty_ywire_connection_set_resize_cb(host.conn, yrich_terminal_resize_cb, &host));
    destroy_safe(yetty_ywire_connection_pickup_winsize(host.conn));
    yrich_terminal_enable_input(&host);

    uv_poll_init(&host.loop, &host.stdin_poll, yetty_ywire_connection_fd(host.conn));
    host.stdin_poll.data = &host;
    uv_poll_start(&host.stdin_poll, UV_READABLE, yrich_terminal_stdin_cb);
    uv_prepare_init(&host.loop, &host.prep);
    host.prep.data = &host;
    uv_prepare_start(&host.prep, yrich_terminal_prep_cb);
    uv_timer_init(&host.loop, &host.frame_timer);
    host.frame_timer.data = &host;
    uv_timer_start(&host.frame_timer, yrich_terminal_frame_cb, 33, 33);

    uv_run(&host.loop, UV_RUN_DEFAULT);

    /* Graceful teardown — leave the host terminal exactly as we found it so the
     * shell prompt returns clean (this is what a full-screen program owes its
     * host, and what ygreeter / the canonical yguiapp client do):
     *   1. turn mouse/keyboard forwarding back off,
     *   2. stop + close the uv handles and drain their close callbacks,
     *   3. destroy the framework — that QUEUES the figure-clear records that
     *      remove ydoc's figure from the terminal,
     *   4. push the connection's queued bytes to the transport, then block-flush
     *      the transport so those clears actually reach the terminal BEFORE it
     *      goes away,
     *   5. destroy the connection, then the transport (restores raw mode),
     *   6. close the loop. */
    yrich_terminal_disable_input(&host);

    /* Tell the host to destroy our remote figures — otherwise yetty keeps our
     * last frame frozen on the pane after we exit, and the shell that reclaims
     * the pane draws under a stale ydoc image. This drives yfigure_clear_all on
     * the wired host container (attach_transport gave the framework one). Must
     * run while the framework + session are still alive, i.e. before destroy. */
    if (app->ygui) {
        struct yetty_ycore_void_result clear_res = yetty_ygui_framework_clear(app->ygui);
        if (YETTY_IS_ERR(clear_res)) {
            yetty_ycore_error_destroy(clear_res.error);
        }
    }
    /* Force the disable sequence + the queued figure deletes onto the wire
     * before the transport goes away. */
    struct yetty_ywire_channel *rpc_ch =
        yetty_ywire_connection_channel(host.conn, YETTY_YWIRE_CHANNEL_RPC);
    if (rpc_ch) {
        destroy_safe(yetty_ywire_channel_flush(rpc_ch));
    }
    struct yetty_ycore_size_result flush_pump = yetty_ywire_connection_pump_writable(host.conn);
    if (YETTY_IS_ERR(flush_pump)) {
        yetty_ycore_error_destroy(flush_pump.error);
    }
    destroy_safe(yetty_yclass_transport_pty_flush_blocking(host.transport));

    uv_poll_stop(&host.stdin_poll);
    uv_close((uv_handle_t *)&host.stdin_poll, NULL);
    uv_prepare_stop(&host.prep);
    uv_close((uv_handle_t *)&host.prep, NULL);
    uv_timer_stop(&host.frame_timer);
    uv_close((uv_handle_t *)&host.frame_timer, NULL);
    uv_run(&host.loop, UV_RUN_DEFAULT); /* drain the close callbacks */

    yetty_yrich_keymap_clear(&app->keymap);
    if (app->ygui) {
        destroy_safe(yetty_ygui_framework_destroy(app->ygui));
        app->ygui = NULL;
    }
    destroy_safe(yetty_ywire_connection_destroy(host.conn));
    destroy_safe(yetty_yclass_transport_pty_destroy(host.transport));
    uv_loop_close(&host.loop);
    return YETTY_OK_VOID();
}
#endif /* YETTY_YGUI_HAS_UV */

struct yetty_ycore_int_result yetty_yrich_app_run(int argc, char **argv,
                                                  struct yetty_yclass_object *doc_obj,
                                                  enum yetty_yrich_app_kind kind)
{
    if (!doc_obj) {
        return YETTY_OK(yetty_ycore_int, 2);
    }

    struct yetty_ycore_void_result platform_reg = yetty_yplatform_register();
    if (YETTY_IS_ERR(platform_reg)) {
        return YETTY_ERR(yetty_ycore_int, "yrich:app: platform register", platform_reg);
    }
    struct yetty_ycore_void_result yapp_reg = yetty_yapp_register();
    if (YETTY_IS_ERR(yapp_reg)) {
        return YETTY_ERR(yetty_ycore_int, "yrich:app: yapp register", yapp_reg);
    }

    struct yetty_yclass_object_ptr_result app_res = yetty_yrich_app_create(NULL);
    if (YETTY_IS_ERR(app_res)) {
        return YETTY_ERR(yetty_ycore_int, "yrich:app: app create", app_res);
    }
    struct yetty_yrich_app_ptr_result app_data = yetty_yrich_app_from(app_res.value);
    if (YETTY_IS_ERR(app_data)) {
        return YETTY_ERR(yetty_ycore_int, "yrich:app: app data", app_data);
    }
    app_data.value->doc = doc_obj;
    app_data.value->kind = kind;

    /* Dual mode: inside a hosting yetty (TERM_PROGRAM=yetty) render into the
     * terminal over ywire; otherwise open a standalone GPU window. */
    if (yetty_running_under_yetty()) {
#ifdef YETTY_YGUI_HAS_UV
        struct yetty_ycore_void_result term_res = yrich_run_terminal(app_data.value);
        if (YETTY_IS_ERR(term_res)) {
            return YETTY_ERR(yetty_ycore_int, "yrich:app: terminal run", term_res);
        }
        return YETTY_OK(yetty_ycore_int, 0);
#else
        return YETTY_ERR(yetty_ycore_int,
                         "yrich:app: in-terminal mode requires libuv (YETTY_YGUI_HAS_UV)");
#endif
    }

    struct yetty_yclass_object_ptr_result platform_res = yetty_yplatform_glfw_platform_create(NULL);
    if (YETTY_IS_ERR(platform_res)) {
        return YETTY_ERR(yetty_ycore_int, "yrich:app: platform create", platform_res);
    }

    struct yetty_ycore_void_result run_res =
        yetty_yplatform_platform_run(platform_res.value, app_res.value, argc, argv);
    if (YETTY_IS_ERR(run_res)) {
        return YETTY_ERR(yetty_ycore_int, "yrich:app: run", run_res);
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

#include "yetty/gen/impl/yrich/app.c"
