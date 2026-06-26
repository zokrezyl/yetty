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
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/container.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yfont/font.h>
#include <yetty/ychrome/chrome.h> /* YETTY_YCHROME_FLAG_* + yetty_ychrome_handle_event */
#include <yetty/ychrome/host.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/yframework/yframework.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/widgets/vbox.h>
#include <yetty/ygui/widgets/yrich_view.h>
#include <yetty/yapp/app.h>
#include <yetty/yclass/class.h>
#include <yetty/yplatform/gpu-context.h>
#include <yetty/yplatform/yclipboard/clipboard.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/yplatform/platform.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yrich/yrich-app.h> /* public entry: yetty_yrich_app_run + kind enum */
#include <yetty/yrich/yrich-shell.h>
#include <yetty/yrich/yrich-types.h>

#include <yetty/yrich/ydoc.h>
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

    struct yetty_context ctx;
    struct yetty_yframework *yrt;
    struct yetty_ydraw_target *target;
    struct yetty_yclass_object *root_obj;
    struct yetty_yclass_object *container_obj;
    struct yetty_yfigure_registry *registry;
    struct yetty_ygui_framework *ygui;
    struct yetty_yclass_object *win; /* framework root widget */
    struct yetty_yfont_font *font;
    struct yetty_ychrome_host *chrome; /* draggable/resizable titlebar + min/max/close */
    struct yetty_ygrid_factory_args figure_args;
    void *surface;
    uint32_t surface_w;
    uint32_t surface_h;
};

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
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app,
                                                            int argc, char **argv);

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
    app->doc = NULL;
    app->editor_view = editor.view;
    return yetty_yrich_editor_refresh(&editor);
}

/* Sync the viewport to the surface and ship the scene into the container
 * over the in-process yclass slot path. */
static struct yetty_ycore_void_result push_scene(struct yetty_yrich_app *app)
{
    if (!app->ygui) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result vr =
        yetty_ygui_framework_set_viewport(app->ygui, (float)app->surface_w, (float)app->surface_h);
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

static struct yetty_ycore_void_result handle_event(struct yetty_yrich_app *app,
                                                   const struct yetty_yui_event *ev)
{
    /* Window chrome gets first dibs on pointer events; anything it doesn't
     * claim (caption drag / edge resize / its buttons) falls through. */
    if (app->chrome && (ev->type == YETTY_YCORE_MOUSE_DOWN || ev->type == YETTY_YCORE_MOUSE_UP ||
                        ev->type == YETTY_YCORE_MOUSE_MOVE || ev->type == YETTY_YCORE_MOUSE_DRAG ||
                        ev->type == YETTY_YCORE_MOUSE_DOUBLE_CLICK)) {
        struct yetty_ycore_int_result chrome_r = yetty_ychrome_host_handle_event(app->chrome, ev);
        int chrome_consumed = YETTY_IS_OK(chrome_r) && chrome_r.value;
        if (YETTY_IS_ERR(chrome_r)) {
            yetty_ycore_error_destroy(chrome_r.error);
        }
        if (chrome_consumed) {
            return YETTY_OK_VOID();
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
        /* Esc quits; everything else feeds the editor view (the window
         * close button is the regular exit path). */
        if (ev->key.key == 256) {
            app->quit = 1;
            return YETTY_OK_VOID();
        }
        if (!app->editor_view) {
            return YETTY_OK_VOID();
        }
        uint32_t mods = glfw_mods_to_yrich(ev->key.mods);
        /* Clipboard chords — handled at the app level, where the platform
         * clipboard manager lives. C=67 X=88 V=86 (GLFW codes). */
        if ((mods & YETTY_YRICH_MOD_CTRL) &&
            (ev->key.key == 67 || ev->key.key == 88 || ev->key.key == 86)) {
            return handle_clipboard_chord(app, ev->key.key);
        }
        uint32_t key = glfw_key_to_yrich(ev->key.key, mods);
        if (key == YETTY_YRICH_KEY_UNKNOWN) {
            return YETTY_OK_VOID();
        }
        struct yetty_ycore_void_result key_res =
            yetty_ygui_yrich_view_feed_key(app->editor_view, key, mods);
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
        struct yetty_ycore_void_result scroll_res = yetty_ygui_framework_feed_mouse_scroll(
            app->ygui, ev->mouse_scroll.x, ev->mouse_scroll.y, ev->mouse_scroll.dx,
            ev->mouse_scroll.dy);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scroll_res, "yrich: scroll");
        struct yetty_ycore_void_result scene_r = push_scene(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_r, "yrich: push scene");
        return YETTY_OK_VOID();
    }
    case YETTY_YCORE_CHAR: {
        if (!app->editor_view) {
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
    case YETTY_YCORE_MOUSE_DOWN: {
        struct yetty_ycore_int_result feed_r = yetty_ygui_framework_feed_mouse_button(
            app->ygui, ev->mouse.x, ev->mouse.y, ev->mouse.button, 1, ev->mouse.mods);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, feed_r, "yrich: mouse down");
        struct yetty_ycore_void_result scene_r = push_scene(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_r, "yrich: push scene");
        return YETTY_OK_VOID();
    }
    case YETTY_YCORE_MOUSE_UP: {
        struct yetty_ycore_int_result feed_r = yetty_ygui_framework_feed_mouse_button(
            app->ygui, ev->mouse.x, ev->mouse.y, ev->mouse.button, 0, ev->mouse.mods);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, feed_r, "yrich: mouse up");
        struct yetty_ycore_void_result scene_r = push_scene(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_r, "yrich: push scene");
        return YETTY_OK_VOID();
    }
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG: {
        struct yetty_ycore_int_result feed_r =
            yetty_ygui_framework_feed_mouse_motion(app->ygui, ev->mouse.x, ev->mouse.y);
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

    const struct yetty_yplatform_gpu_context *gpu = yetty_yplatform_platform_gpu_context(platform);
    struct yetty_ycore_xthread_event_pipe *input_pipe =
        yetty_yplatform_platform_input_pipe(platform);
    if (!gpu || !input_pipe) {
        return YETTY_ERR(yetty_ycore_void, "yrich:app:run: platform state not populated");
    }

    struct yetty_yframework_ptr_result yr = yetty_yframework_create(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yr, "yframework_create failed");
    app->yrt = yr.value;

    app->ctx.runtime = app->yrt;
    app->ctx.pty_factory = NULL;
    app->ctx.event_loop = app->yrt->event_loop;

    app->surface = gpu->surface;
    app->surface_w = gpu->surface_width;
    app->surface_h = gpu->surface_height;

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

    /* Font for the ygrid factory (text spans → glyphs). */
    {
        struct yetty_yconfig_config *config = app->yrt->config;
        const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
        const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
        const char *font_family = "DejaVuSansMNerdFontMono";
        char cdb_path[768];
        char shader_path[768];
        snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", fonts_dir,
                 font_family);
        snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl", shaders_dir);
        struct yetty_font_font_result font_result =
            yetty_yfont_msdf_font_create(cdb_path, shader_path, "yrich_app");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, font_result, "msdf_font_create failed");
        app->font = font_result.value;
        struct yetty_ycore_void_result result_224 = app->font->ops->load_basic_latin(app->font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_224, "font load_basic_latin failed");
    }

    /* Registry + ygrid factory + in-process root container. */
    struct yetty_yfigure_registry_ptr_result reg_r = yetty_yfigure_registry_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, reg_r, "yfigure_registry_create failed");
    app->registry = reg_r.value;
    app->figure_args.default_font = app->font;
    app->figure_args.composite_factory = NULL;
    struct yetty_ycore_void_result result_234 =
        yetty_ygrid_register_factory(app->registry, &app->figure_args);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_234, "ygrid_register_factory failed");

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
    struct yetty_ygui_framework_ptr_result eng_r = yetty_ygui_framework_create(NULL);
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
            (float)app->surface_h, 34.0f, 8.0f, YETTY_YCHROME_FLAG_ALL);
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
    destroy_safe(yetty_yframework_destroy(app->yrt));
    app->yrt = NULL;
    return YETTY_OK_VOID();
}

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

#include "app.gen.c"
