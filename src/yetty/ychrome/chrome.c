/*
 * chrome.c — yclass class `ychrome:chrome`: a minimal, widget-free window
 * chrome gesture engine.
 *
 * Desktop yetty apps run in a borderless OS window (GLFW_DECORATED = FALSE) so
 * they can draw their own title bar. That means the move/resize/maximize
 * gestures the OS would normally provide have to be re-implemented in the app.
 * This class is that engine, factored out of yui's tab strip so ANY app —
 * ygreeter, a ygui demo, a bare tool — can get draggable / resizable /
 * maximizable windows without depending on ygui or yui.
 *
 * It owns no rendering and no widgets. The app draws whatever title bar it
 * likes, decides the caption height and resize-border thickness, and feeds raw
 * mouse events in. The engine hit-tests them and drives a
 * yplatform:window_manager yclass object:
 *
 *   - press + drag inside the caption strip  → move the window
 *   - double-click the caption strip         → toggle maximize
 *   - press + drag a right/bottom edge        → resize the window
 *
 * The app is responsible for its own controls (close/min/max buttons, tabs):
 * handle those first and only forward an event to chrome_handle_event when your
 * own UI did not consume it. handle_event returns 1 when it claimed the event.
 *
 * Coordinates are in the same pixel space the app hit-tests its UI in (yetty
 * feeds framebuffer pixels). Call set_size() whenever the window resizes so the
 * right/bottom edge bands track the window.
 *
 * Every slot is `local@` — chrome is an in-process object, never proxied over
 * RPC. window-manager.c is its platform backend (it forwards the gestures over
 * the render→main event pipe).
 *
 * Lifecycle:
 *   yetty_ychrome_register();
 *   obj = yetty_ychrome_chrome_create(NULL);
 *   yetty_ychrome_configure(NULL, obj, window_manager_obj,
 *                           caption_height, edge_size, YETTY_YCHROME_FLAG_ALL);
 *   yetty_ychrome_set_size(NULL, obj, width, height);   // on every resize
 *   consumed = yetty_ychrome_handle_event(NULL, obj, &mouse_event);
 *   shape    = yetty_ychrome_edge_cursor_at(NULL, obj, x, y); // optional hover
 *   yetty_ychrome_destroy(NULL, obj);
 */
#include <yetty/yclass/class.h>
#include <yetty/ychrome/chrome.h>

#include <yetty/yevent/event.h>      /* event types + resize-edge / cursor enums */
#include <yetty/yplatform/methods.h> /* window_manager gesture slots */
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

/* Framework-level draw primitives — the SAME layer ygui paints into (a ygui
 * widget calls these from its paint; see ygui/widgets/button.c). Using them
 * here keeps chrome ygui-free: it draws its own titlebar like any framework
 * primitive would. */
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <string.h>

#include <stdint.h>

#ifdef YCLASS_CODEGEN
/* Feature flags for configure(). OR them together; FLAG_ALL enables the lot.
 * Copied verbatim into the generated chrome.h. */
#define YETTY_YCHROME_FLAG_DRAG 0x1u     /* caption drag moves the window      */
#define YETTY_YCHROME_FLAG_RESIZE 0x2u   /* right/bottom edges resize          */
#define YETTY_YCHROME_FLAG_MAXIMIZE 0x4u /* caption double-click toggles max   */
#define YETTY_YCHROME_FLAG_ALL                                                                     \
    (YETTY_YCHROME_FLAG_DRAG | YETTY_YCHROME_FLAG_RESIZE | YETTY_YCHROME_FLAG_MAXIMIZE)
#endif

enum {
    /* Drag/resize-vs-click slop: below this many pixels of motion the gesture
     * might still be a clean click, so we don't grab or move yet. */
    YCHROME_SLOP_PX = 3,
    /* Default resize-border thickness if configure() is passed 0. A touch wider
     * than common desktop slop (5–6 px) because there's no visible frame. */
    YCHROME_DEFAULT_EDGE_PX = 8,
    /* Width (px) of each window-control button (minimize/maximize/close). The
     * three sit flush against the right edge; render and hit-test share this. */
    YCHROME_BTN_W = 46,
};

/* Caption fill + glyph colours, packed 0xAABBGGRR (the encoding ydraw uses).
 * Match yetty's titlebar: BRAND_BG_ROW strip, off-white glyphs. */
#define YCHROME_STRIP_BG 0xFF2C261Eu   /* #1E262C */
#define YCHROME_GLYPH_COLOR 0xFFE4E5E0u /* #E0E5E4 */

/* Window-control glyphs, in left→right order: minimize, maximize, close. Same
 * codepoints yui's titlebar uses (in the bundled font): − □ × */
#define YCHROME_GLYPH_MINIMIZE "\xE2\x88\x92"
#define YCHROME_GLYPH_MAXIMIZE "\xE2\x96\xA1"
#define YCHROME_GLYPH_CLOSE "\xC3\x97"

struct [[clang::annotate("class@ychrome:chrome")]] chrome_data {
    /* Borrowed yplatform:window_manager yclass object — set by configure(). */
    struct yetty_yclass_object *window_manager;

    float caption_height; /* top strip that drags / double-click-maximizes (px) */
    float edge_size;      /* right/bottom resize border thickness (px)          */
    float width;          /* current window size (px); set via set_size()       */
    float height;
    uint32_t flags; /* YETTY_YCHROME_FLAG_*                               */

    /* Drag (move) gesture state. */
    int dragging;
    int drag_grab_sent; /* begin_interactive_move fired once for this drag */
    float drag_anchor_x;
    float drag_anchor_y;

    /* Resize gesture state. */
    int resizing;
    int resize_grab_sent; /* begin_interactive_resize fired once for this gesture */
    int resize_edge;      /* yetty_ycore_resize_edge bitmask (xdg-shell)          */
    int resize_left;      /* which edges the gesture grabbed */
    int resize_right;
    int resize_top;
    int resize_bottom;
    /* Right/bottom edges keep the top-left fixed → track incrementally from
     * resize_last. Left/top edges move the origin → the window-relative cursor
     * resets after each move, so they measure from a fixed anchor instead. */
    float resize_last_x;
    float resize_last_y;
    float resize_anchor_x;
    float resize_anchor_y;
};

/* Resolve the object's chrome_data slice, preserving the class_get /
 * object_data error chain. */
static struct yetty_yclass_void_ptr_result chrome_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ychrome_chrome_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "chrome_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_r, "chrome_from_obj: object_data");
    return slice_r;
}

/* The window-manager slots are fire-and-forget (they post a request onto the
 * render→main pipe). A failure can only be an object-resolve error, not
 * recoverable here — absorb the chain so it isn't leaked. */
static void wm_absorb(struct yetty_ycore_void_result window_manager_result)
{
    if (YETTY_IS_ERR(window_manager_result)) {
        yetty_ycore_error_destroy(window_manager_result.error);
    }
}

/* Pull the pointer position out of a mouse event. Returns 0 for events that
 * carry no usable cursor position. */
static int chrome_event_xy(const struct yetty_yui_event *event, float *out_x, float *out_y)
{
    switch (event->type) {
    case YETTY_YCORE_MOUSE_DOWN:
    case YETTY_YCORE_MOUSE_UP:
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG:
    case YETTY_YCORE_MOUSE_DOUBLE_CLICK:
        *out_x = event->mouse.x;
        *out_y = event->mouse.y;
        return 1;
    default:
        return 0;
    }
}

/*=============================================================================
 * Lifecycle / configuration
 *===========================================================================*/

/* Bind the engine to a window_manager and set the caption/edge geometry. Call
 * once after create(), before feeding events. window_manager is borrowed. */
[[clang::annotate("override@ychrome:chrome:configure")]] [[clang::annotate(
    "local@ychrome:configure")]]
static struct yetty_ycore_void_result chrome_configure(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       struct yetty_yclass_object *window_manager,
                                                       float caption_height, float edge_size,
                                                       uint32_t flags)
{
    (void)ctx;
    if (!window_manager) {
        return YETTY_ERR(yetty_ycore_void, "chrome configure: window_manager required");
    }
    struct yetty_yclass_void_ptr_result data_r = chrome_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "chrome configure: object");
    struct chrome_data *chrome = data_r.value;
    chrome->window_manager = window_manager;
    chrome->caption_height = caption_height > 0.0f ? caption_height : 0.0f;
    chrome->edge_size = edge_size > 0.0f ? edge_size : (float)YCHROME_DEFAULT_EDGE_PX;
    chrome->flags = flags;
    return YETTY_OK_VOID();
}

/* Update the window size so the right/bottom edge bands track it. Call on every
 * resize. */
[[clang::annotate("override@ychrome:chrome:set_size")]] [[clang::annotate(
    "local@ychrome:set_size")]]
static struct yetty_ycore_void_result chrome_set_size(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj, float width,
                                                      float height)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result data_r = chrome_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "chrome set_size: object");
    struct chrome_data *chrome = data_r.value;
    chrome->width = width;
    chrome->height = height;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ychrome:chrome:destroy")]] [[clang::annotate("local@ychrome:destroy")]]
static struct yetty_ycore_void_result chrome_destroy(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj)
{
    (void)ctx;
    /* No owned resources (window_manager is borrowed) — just free the object. */
    return yetty_yclass_object_free(obj);
}

/*=============================================================================
 * Hit-testing
 *===========================================================================*/

/* Cursor shape to show for a hover at (x, y): a resize arrow over an active
 * edge, else the default. Lets the app give resize-edge feedback without
 * duplicating the hit-test. */
/* Which resize edges (if any) the point (x, y) is over. Any combination →
 * corners are two edges at once. The bands hug all four window margins. */
static void chrome_edges_at(const struct chrome_data *chrome, float x, float y, int *left,
                            int *right, int *top, int *bottom)
{
    float edge = chrome->edge_size;
    *left = x >= 0.0f && x < edge;
    *right = x <= chrome->width && x > chrome->width - edge;
    *top = y >= 0.0f && y < edge;
    *bottom = y <= chrome->height && y > chrome->height - edge;
}

[[clang::annotate("override@ychrome:chrome:edge_cursor_at")]] [[clang::annotate(
    "local@ychrome:edge_cursor_at")]]
static struct yetty_ycore_int_result chrome_edge_cursor_at(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj, float x,
                                                           float y)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result data_r = chrome_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_r, "chrome edge_cursor_at: object");
    struct chrome_data *chrome = data_r.value;
    if (!(chrome->flags & YETTY_YCHROME_FLAG_RESIZE) || chrome->width <= 0.0f ||
        chrome->height <= 0.0f) {
        return YETTY_OK(yetty_ycore_int, YETTY_YCORE_CURSOR_DEFAULT);
    }
    int left, right, top, bottom;
    chrome_edges_at(chrome, x, y, &left, &right, &top, &bottom);
    /* No diagonal cursor in the enum yet — corners fall back to HRESIZE. */
    if (left || right) {
        return YETTY_OK(yetty_ycore_int, YETTY_YCORE_CURSOR_HRESIZE);
    }
    if (top || bottom) {
        return YETTY_OK(yetty_ycore_int, YETTY_YCORE_CURSOR_VRESIZE);
    }
    return YETTY_OK(yetty_ycore_int, YETTY_YCORE_CURSOR_DEFAULT);
}

/*=============================================================================
 * Self-rendering — draw the caption bar via ydraw (no ygui)
 *===========================================================================*/

/* Which window-control button is at (x, y): 1=minimize, 2=maximize, 3=close,
 * or 0 for none. The three are flush against the right edge, each YCHROME_BTN_W
 * wide, within the caption strip. Shared by render() (placement) and
 * handle_event() (hit-test). */
static int chrome_button_at(const struct chrome_data *chrome, float x, float y)
{
    if (y < 0.0f || y >= chrome->caption_height) {
        return 0;
    }
    float close_left = chrome->width - (float)YCHROME_BTN_W;
    float max_left = close_left - (float)YCHROME_BTN_W;
    float min_left = max_left - (float)YCHROME_BTN_W;
    if (x >= close_left && x <= chrome->width) {
        return 3;
    }
    if (x >= max_left && x < close_left) {
        return 2;
    }
    if (x >= min_left && x < max_left) {
        return 1;
    }
    return 0;
}

/* Paint the caption strip into a fresh drawable list and return it: a filled
 * background spanning (width × caption_height) plus the minimize/maximize/close
 * glyphs flush right. The caller composites the list as a pinned top figure and
 * destroys it. Uses font_id=-1 (the default font), so no font dependency. */
[[clang::annotate("override@ychrome:chrome:render")]]
[[clang::annotate("local@ychrome:render")]]
static struct yetty_ydraw_drawable_list_result chrome_render(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result data_r = chrome_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, data_r, "chrome render: object");
    struct chrome_data *chrome = data_r.value;
    float width = chrome->width;
    float height = chrome->caption_height;
    if (width <= 0.0f || height <= 0.0f) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "chrome render: zero caption size");
    }

    struct yetty_ydraw_drawable_list_config config = {
        .scene_min_x = 0.0f, .scene_min_y = 0.0f, .scene_max_x = width, .scene_max_y = height};
    struct yetty_ydraw_drawable_list_result list_r =
        yetty_ydraw_drawable_list_config_buffer_create(&config);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, list_r, "chrome render: list create");
    struct yetty_ydraw_drawable_list *list = list_r.value;

    /* Background strip. */
    struct yetty_ysdf_box bg = {.center_x = width * 0.5f,
                                .center_y = height * 0.5f,
                                .half_width = width * 0.5f,
                                .half_height = height * 0.5f,
                                .corner_radius = 0.0f};
    struct yetty_ycore_void_result bg_r =
        yetty_ydraw_drawable_list_add_cmd_add_box(list, /*id=*/0, /*z_order=*/0, YCHROME_STRIP_BG,
                                                  /*stroke=*/0u, /*stroke_width=*/0.0f, &bg);
    if (YETTY_IS_ERR(bg_r)) {
        yetty_ydraw_drawable_list_destroy(list);
        return YETTY_ERR(yetty_ydraw_drawable_list, "chrome render: bg box", bg_r);
    }

    /* Window-control glyphs, flush right. Glyph metrics aren't measured (no font
     * dep), so each glyph is placed at a fixed inset inside its slot — close
     * enough for single symbols. */
    const char *glyphs[3] = {YCHROME_GLYPH_MINIMIZE, YCHROME_GLYPH_MAXIMIZE, YCHROME_GLYPH_CLOSE};
    float font_size = height * 0.5f;
    float baseline = height * 0.5f + font_size * 0.35f;
    for (int i = 0; i < 3; i++) {
        float slot_left = width - (float)((3 - i) * YCHROME_BTN_W);
        struct yetty_ycore_buffer glyph = {.data = (uint8_t *)(uintptr_t)glyphs[i],
                                           .capacity = strlen(glyphs[i]),
                                           .size = strlen(glyphs[i])};
        struct yetty_ycore_void_result glyph_r = yetty_ydraw_drawable_list_add_text(
            list, slot_left + (float)YCHROME_BTN_W * 0.34f, baseline, &glyph, font_size,
            YCHROME_GLYPH_COLOR, /*layer=*/1, /*font_id=*/-1, /*rotation=*/0.0f);
        if (YETTY_IS_ERR(glyph_r)) {
            yetty_ydraw_drawable_list_destroy(list);
            return YETTY_ERR(yetty_ydraw_drawable_list, "chrome render: glyph", glyph_r);
        }
    }
    return YETTY_OK(yetty_ydraw_drawable_list, list);
}

/*=============================================================================
 * Event handling — the gesture state machine
 *===========================================================================*/

/* Feed one mouse event. Returns 1 if chrome claimed it (the app should stop
 * processing it), 0 if it's not a chrome gesture. Forward events here only
 * after your own controls (buttons, tabs) had their chance. */
[[clang::annotate("override@ychrome:chrome:handle_event")]] [[clang::annotate(
    "local@ychrome:handle_event")]]
static struct yetty_ycore_int_result chrome_handle_event(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj,
                                                         const struct yetty_yui_event *event)
{
    (void)ctx;
    if (!event) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_yclass_void_ptr_result data_r = chrome_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_r, "chrome handle_event: object");
    struct chrome_data *chrome = data_r.value;
    struct yetty_yclass_object *wm = chrome->window_manager;
    if (!wm) {
        ydebug("CHROMETRACE: handle_event type=%d but window_manager is NULL — ignoring",
               (int)event->type);
        return YETTY_OK(yetty_ycore_int, 0);
    }

    float x = 0.0f, y = 0.0f;
    int have_xy = chrome_event_xy(event, &x, &y);
    ydebug("CHROMETRACE: type=%d xy=(%.1f,%.1f) have_xy=%d caption_h=%.1f wh=(%.1f,%.1f) "
           "dragging=%d resizing=%d flags=0x%x",
           (int)event->type, x, y, have_xy, chrome->caption_height, chrome->width, chrome->height,
           chrome->dragging, chrome->resizing, chrome->flags);

    /* --- continue an in-progress resize -------------------------------- */
    if (chrome->resizing) {
        if (have_xy &&
            (event->type == YETTY_YCORE_MOUSE_MOVE || event->type == YETTY_YCORE_MOUSE_DRAG)) {
            float ax = x - chrome->resize_anchor_x;
            float ay = y - chrome->resize_anchor_y;
            if (ax * ax + ay * ay < (float)(YCHROME_SLOP_PX * YCHROME_SLOP_PX)) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
            if (!chrome->resize_grab_sent && chrome->resize_edge != 0) {
                /* Threshold crossed: on Wayland the compositor grabs the
                 * pointer here; on X11 it's a no-op and the per-pixel resize_by
                 * below keeps driving the size. */
                chrome->resize_grab_sent = 1;
                wm_absorb(yetty_yplatform_window_manager_begin_interactive_resize(
                    NULL, wm, chrome->resize_edge));
            }
            /* Per-axis delta. Right/bottom keep the top-left fixed → track
             * incrementally (update resize_last). Left/top move the origin →
             * after each move the window-relative cursor snaps back to the
             * press anchor, so measure from that fixed anchor (don't update) to
             * avoid a feedback loop. window_manager applies the per-edge origin
             * shift from fresh geometry. */
            int step_dx = 0, step_dy = 0;
            if (chrome->resize_right) {
                step_dx = (int)(x - chrome->resize_last_x);
                chrome->resize_last_x = x;
            } else if (chrome->resize_left) {
                step_dx = (int)(x - chrome->resize_anchor_x);
            }
            if (chrome->resize_bottom) {
                step_dy = (int)(y - chrome->resize_last_y);
                chrome->resize_last_y = y;
            } else if (chrome->resize_top) {
                step_dy = (int)(y - chrome->resize_anchor_y);
            }
            if (step_dx != 0 || step_dy != 0) {
                wm_absorb(yetty_yplatform_window_manager_resize_by(NULL, wm, step_dx, step_dy,
                                                                   chrome->resize_edge));
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (event->type == YETTY_YCORE_MOUSE_UP) {
            chrome->resizing = 0;
            chrome->resize_grab_sent = 0;
            chrome->resize_edge = 0;
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }

    /* --- continue an in-progress drag ---------------------------------- */
    if (chrome->dragging) {
        if (have_xy &&
            (event->type == YETTY_YCORE_MOUSE_MOVE || event->type == YETTY_YCORE_MOUSE_DRAG)) {
            int dx = (int)(x - chrome->drag_anchor_x);
            int dy = (int)(y - chrome->drag_anchor_y);
            if (dx * dx + dy * dy < YCHROME_SLOP_PX * YCHROME_SLOP_PX) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
            if (!chrome->drag_grab_sent) {
                chrome->drag_grab_sent = 1;
                wm_absorb(yetty_yplatform_window_manager_begin_interactive_move(NULL, wm));
            }
            if (dx != 0 || dy != 0) {
                /* Don't update the anchor: glfwSetWindowPos repositions
                 * absolutely, leaving the cursor at the anchor in the moved
                 * frame; resetting would accumulate rounding error. */
                wm_absorb(yetty_yplatform_window_manager_drag_by(NULL, wm, dx, dy));
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (event->type == YETTY_YCORE_MOUSE_UP) {
            chrome->dragging = 0;
            chrome->drag_grab_sent = 0;
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }

    if (!have_xy) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    int in_caption = y < chrome->caption_height;

    /* --- window-control buttons (minimize / maximize / close) ---------- *
     * Checked before the drag arm so a press on a button doesn't start a
     * window move. Acts on press; the matching release falls through (chrome
     * isn't dragging, so it's a no-op). */
    if (in_caption && event->type == YETTY_YCORE_MOUSE_DOWN) {
        int button = chrome_button_at(chrome, x, y);
        if (button == 1) {
            wm_absorb(yetty_yplatform_window_manager_iconify(NULL, wm));
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (button == 2) {
            wm_absorb(yetty_yplatform_window_manager_toggle_maximize(NULL, wm));
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (button == 3) {
            wm_absorb(yetty_yplatform_window_manager_request_close(NULL, wm));
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }

    /* --- edge / corner press → start a resize -------------------------- *
     * Checked before the caption drag so the resize bands win where they run
     * through the caption (the top band, and the left/right margins along the
     * caption row). Buttons were already handled above, so they keep priority
     * in the top-right. All four margins + the four corners are live. */
    if ((chrome->flags & YETTY_YCHROME_FLAG_RESIZE) && event->type == YETTY_YCORE_MOUSE_DOWN &&
        chrome->width > 0.0f && chrome->height > 0.0f) {
        int left, right, top, bottom;
        chrome_edges_at(chrome, x, y, &left, &right, &top, &bottom);
        if (left || right || top || bottom) {
            chrome->resizing = 1;
            chrome->resize_left = left;
            chrome->resize_right = right;
            chrome->resize_top = top;
            chrome->resize_bottom = bottom;
            chrome->resize_last_x = x;
            chrome->resize_last_y = y;
            chrome->resize_anchor_x = x;
            chrome->resize_anchor_y = y;
            chrome->resize_grab_sent = 0;
            chrome->resize_edge = (left ? YETTY_YCORE_RESIZE_EDGE_LEFT : 0) |
                                  (right ? YETTY_YCORE_RESIZE_EDGE_RIGHT : 0) |
                                  (top ? YETTY_YCORE_RESIZE_EDGE_TOP : 0) |
                                  (bottom ? YETTY_YCORE_RESIZE_EDGE_BOTTOM : 0);
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }

    /* --- caption double-click → toggle maximize ------------------------ */
    if ((chrome->flags & YETTY_YCHROME_FLAG_MAXIMIZE) && in_caption &&
        event->type == YETTY_YCORE_MOUSE_DOUBLE_CLICK && event->mouse.button == 0) {
        wm_absorb(yetty_yplatform_window_manager_toggle_maximize(NULL, wm));
        chrome->dragging = 0; /* cancel any drag the preceding press armed */
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* --- caption press → start a window drag --------------------------- */
    if ((chrome->flags & YETTY_YCHROME_FLAG_DRAG) && in_caption &&
        event->type == YETTY_YCORE_MOUSE_DOWN) {
        chrome->dragging = 1;
        chrome->drag_grab_sent = 0;
        chrome->drag_anchor_x = x;
        chrome->drag_anchor_y = y;
        /* Don't fire begin_interactive_move yet — wait for motion past the
         * slop, so a clean click (and the first half of a double-click) isn't
         * eaten by a zero-distance compositor grab. */
        return YETTY_OK(yetty_ycore_int, 1);
    }

    return YETTY_OK(yetty_ycore_int, 0);
}

#include "chrome.gen.c"
