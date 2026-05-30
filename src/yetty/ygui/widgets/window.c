/* window.c — top-level framed window: title bar + hamburger menu button
 * + an auto-allocated body container below.
 *
 * App code
 * adds content to window_body() so the title strip's pixels stay
 * reserved (the constructor sets padding_top = title height and parents
 * a flex-grow body vbox inside).
 */
#include "paint-helpers.h"
#include <yetty/ygui/event.h>
#include <yetty/ygui/widgets/popup_menu.h>
#include <yetty/ygui/widgets/vbox.h>
#include <yetty/ygui/widgets/window.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_TITLE_H 30.0f
#define WINDOW_RADIUS 8.0f
#define WINDOW_BTN 18.0f
#define WINDOW_BTN_PAD 6.0f
#define WINDOW_BTN_GAP 4.0f

#define WIN_BG 0xFF1F1A14u      /* BRAND_BG_LIFTED   */
#define WIN_TITLE_BG 0xFF2C261Eu /* BRAND_BG_ROW     */
#define WIN_BORDER 0xFF474A36u  /* BRAND_BORDER      */
#define WIN_TEXT 0xFFE4E5E0u    /* BRAND_TEXT_PRIMARY */
#define WIN_GRIP 0xFFA8A79Fu    /* BRAND_TEXT_SECONDARY */
#define WIN_CLOSE 0xFFA8A79Fu   /* BRAND_TEXT_SECONDARY */

struct [[clang::annotate("class@ygui:window")]] [[clang::annotate("parent@ygui:vbox")]]
window_data {
    char *title;
    struct yetty_ygui_object *body; /* auto-allocated child, owned by tree */
    struct yetty_ygui_object *menu; /* borrowed, opened by the hamburger */
    /* When set, the title bar paints an "x" at the far right (ImGui's
     * Begin(&p_open) model). A click on it emits YETTY_YGUI_EVENT_CLOSE
     * on the window; the app reacts via yetty_ygui_object_subscribe.
     * The framework owns the button; the app decides what closing
     * means (quit, hide, …). */
    int closable;
    /* Title-bar drag state — a press on the title strip (not on a
     * button) moves the window. Captured by the framework on the
     * consumed press; pos is updated against the press anchor. */
    int dragging;
    float drag_pos_x, drag_pos_y;       /* layout pos at press */
    float drag_cursor_x, drag_cursor_y; /* cursor at press */
};

/* X position of the close-button box (or -1 when the window isn't
 * closable). */
static float window_close_x(const struct window_data *d, struct yetty_ycore_rectangle r)
{
    if (!d->closable) {
        return -1.0f;
    }
    return r.max.x - WINDOW_BTN_PAD - WINDOW_BTN;
}

/* X position of the hamburger box (or -1 when no menu). Sits left of the
 * close button when both are present. */
static float window_hamburger_x(const struct window_data *d, struct yetty_ycore_rectangle r)
{
    if (!d->menu) {
        return -1.0f;
    }
    float x = r.max.x - WINDOW_BTN_PAD - WINDOW_BTN;
    if (d->closable) {
        x -= WINDOW_BTN + WINDOW_BTN_GAP;
    }
    return x;
}

static const struct yetty_yclass *window_class(void)
{
    return yetty_ygui_class_expect(yetty_ygui_window_class_get(), "yetty_ygui_window_class_get");
}

static struct yetty_ycore_void_result win_rounded(struct yetty_ygui_emit_ctx *ctx, float x, float y,
                                                  float w, float h, uint32_t fill, uint32_t stroke,
                                                  float stroke_w, float radius)
{
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    if (radius > w * 0.5f) {
        radius = w * 0.5f;
    }
    if (radius > h * 0.5f) {
        radius = h * 0.5f;
    }
    struct yetty_ysdf_rounded_box g = {.center_x = x + w * 0.5f,
                                       .center_y = y + h * 0.5f,
                                       .half_width = w * 0.5f,
                                       .half_height = h * 0.5f,
                                       .radius_top_right = radius,
                                       .radius_bottom_right = radius,
                                       .radius_top_left = radius,
                                       .radius_bottom_left = radius};
    return yetty_ydraw_draw_list_add_cmd_add_rounded_box(ctx->ygrid_draw_list, 0, 0, fill, stroke,
                                                         stroke_w, &g);
}

[[clang::annotate("override@ygui:window:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, window_class(), (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "window: super");
    struct window_data *d = yetty_ygui_data_get(obj, window_class());
    d->title = NULL;
    d->body = NULL;
    d->menu = NULL;
    d->closable = 0;
    d->dragging = 0;
    /* Reserve the title strip; stretch the body across the width. */
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.padding_top = WINDOW_TITLE_H;
    l.padding_left = 1.0f;
    l.padding_right = 1.0f;
    l.padding_bottom = 1.0f;
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "window: layout");

    /* Auto-allocate the body — a flex-grow vbox filling the content box. */
    struct yetty_ygui_object_ptr_result br = yetty_ygui_add(yetty_ygui_vbox_class_get().value, obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "window: body add");
    d->body = br.value;
    struct yetty_ygui_layout bl = *yetty_ygui_widget_layout_get(d->body);
    bl.flex_grow = 1.0f;
    return yetty_ygui_widget_layout_set(d->body, &bl);
}

[[clang::annotate("override@ygui:window:destructor")]]
static struct yetty_ycore_void_result dtor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct window_data *d = yetty_ygui_data_get(obj, window_class());
    free(d->title);
    return yetty_ygui_super_void(obj, window_class(),
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:window:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "window paint: NULL ctx");
    }
    struct window_data *d = yetty_ygui_data_get(obj, window_class());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    /* Frame + title strip. */
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        win_rounded(ctx, r.min.x, r.min.y, w, h, WIN_BG, WIN_BORDER, 1.0f,
                                    WINDOW_RADIUS),
                        "window: frame");
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        win_rounded(ctx, r.min.x, r.min.y, w, WINDOW_TITLE_H, WIN_TITLE_BG, 0, 0.0f,
                                    WINDOW_RADIUS),
                        "window: titlebar");
    if (d->title) {
        float fs = 14.0f;
        float ty = r.min.y + (WINDOW_TITLE_H + fs) * 0.5f - 3.0f;
        YETTY_RETURN_IF_ERR(yetty_ycore_void, yguix_text(ctx, d->title, r.min.x + 10.0f, ty, fs, WIN_TEXT),
                            "window: title");
    }
    float by = r.min.y + (WINDOW_TITLE_H - WINDOW_BTN) * 0.5f;

    /* Close "x" at the far right (only when the window is closable). */
    float close_bx = window_close_x(d, r);
    if (close_bx >= 0.0f) {
        float fs = 16.0f;
        float ty = r.min.y + (WINDOW_TITLE_H + fs) * 0.5f - 3.0f;
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_text(ctx, "\xC3\x97", close_bx + 4.0f, ty, fs, WIN_CLOSE),
                            "window: close x");
    }

    /* Hamburger — three lines, left of the close button (if any). */
    float ham_bx = window_hamburger_x(d, r);
    if (ham_bx >= 0.0f) {
        for (int i = 0; i < 3; i++) {
            YETTY_RETURN_IF_ERR(yetty_ycore_void,
                                yguix_box(ctx, ham_bx + 3.0f, by + 4.0f + (float)i * 5.0f,
                                          WINDOW_BTN - 6.0f, 2.0f, WIN_GRIP, 0),
                                "window: hamburger");
        }
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:window:widget_on_press")]]
static struct yetty_ycore_int_result on_press(struct yetty_yclass_ctx *yclass_ctx,
                                              struct yetty_yclass_object *yclass_obj, float x,
                                              float y, int btn)
{
    (void)yclass_ctx;
    (void)btn;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct window_data *d = yetty_ygui_data_get(obj, window_class());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float by = r.min.y + (WINDOW_TITLE_H - WINDOW_BTN) * 0.5f;

    /* Close "x" — emit a CLOSE event the app subscribes to. */
    float close_bx = window_close_x(d, r);
    if (close_bx >= 0.0f && x >= close_bx && x <= close_bx + WINDOW_BTN && y >= by &&
        y <= by + WINDOW_BTN) {
        struct yetty_ygui_event ev = {.type = YETTY_YGUI_EVENT_CLOSE, .source = obj};
        struct yetty_ycore_void_result er = yetty_ygui_object_emit(obj, &ev);
        if (YETTY_IS_ERR(er)) {
            return YETTY_ERR(yetty_ycore_int, "window: emit close", er);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Hamburger toggles the attached popup menu. */
    float ham_bx = window_hamburger_x(d, r);
    if (ham_bx >= 0.0f && x >= ham_bx && x <= ham_bx + WINDOW_BTN && y >= by &&
        y <= by + WINDOW_BTN) {
        struct yetty_ycore_void_result mr =
            yetty_ygui_popup_menu_toggle_at(d->menu, ham_bx, r.min.y + WINDOW_TITLE_H + 2.0f);
        if (YETTY_IS_ERR(mr)) {
            return YETTY_ERR(yetty_ycore_int, "window: menu toggle", mr);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* A press elsewhere on the title strip starts a window drag. Below
     * the title strip the press falls through to the body children. */
    if (y - r.min.y <= WINDOW_TITLE_H) {
        const struct yetty_ygui_layout *l = yetty_ygui_widget_layout_get(obj);
        d->dragging = 1;
        d->drag_pos_x = l->pos_x;
        d->drag_pos_y = l->pos_y;
        d->drag_cursor_x = x;
        d->drag_cursor_y = y;
        return YETTY_OK(yetty_ycore_int, 1);
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

[[clang::annotate("override@ygui:window:widget_on_motion")]]
static struct yetty_ycore_int_result on_motion(struct yetty_yclass_ctx *yclass_ctx,
                                               struct yetty_yclass_object *yclass_obj, float x,
                                               float y)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct window_data *d = yetty_ygui_data_get(obj, window_class());
    if (!d->dragging) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    /* Move the window by the cursor delta since the press. pos is
     * relative to the parent content box, and the cursor delta is in the
     * same units, so adding it tracks the pointer regardless of parent
     * offset. Make sure absolute positioning stays on. */
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.absolute = 1;
    l.pos_x = d->drag_pos_x + (x - d->drag_cursor_x);
    l.pos_y = d->drag_pos_y + (y - d->drag_cursor_y);
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
    if (YETTY_IS_ERR(lr)) {
        return YETTY_ERR(yetty_ycore_int, "window: drag move", lr);
    }
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "window: drag dirty", dr);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:window:widget_on_release")]]
static struct yetty_ycore_int_result on_release(struct yetty_yclass_ctx *yclass_ctx,
                                                struct yetty_yclass_object *yclass_obj, float x,
                                                float y, int btn)
{
    (void)yclass_ctx;
    (void)x;
    (void)y;
    (void)btn;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct window_data *d = yetty_ygui_data_get(obj, window_class());
    d->dragging = 0;
    return YETTY_OK(yetty_ycore_int, 1);
}

struct yetty_ygui_object *yetty_ygui_window_body(struct yetty_ygui_object *obj)
{
    if (!obj) {
        return NULL;
    }
    return ((struct window_data *)yetty_ygui_data_get(obj, window_class()))->body;
}

struct yetty_ycore_void_result yetty_ygui_window_set_title(struct yetty_ygui_object *obj,
                                                           const char *title)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "window_set_title: NULL");
    }
    struct window_data *d = yetty_ygui_data_get(obj, window_class());
    free(d->title);
    d->title = NULL;
    if (title) {
        size_t n = strlen(title);
        d->title = malloc(n + 1);
        if (!d->title) {
            return YETTY_ERR(yetty_ycore_void, "window_set_title: malloc");
        }
        memcpy(d->title, title, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_window_set_menu(struct yetty_ygui_object *obj,
                                                          struct yetty_ygui_object *menu)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "window_set_menu: NULL");
    }
    struct window_data *d = yetty_ygui_data_get(obj, window_class());
    d->menu = menu;
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_window_set_closable(struct yetty_ygui_object *obj,
                                                              int closable)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "window_set_closable: NULL");
    }
    struct window_data *d = yetty_ygui_data_get(obj, window_class());
    d->closable = closable ? 1 : 0;
    return yetty_ygui_object_set_dirty(obj);
}

#include "window.gen.c"
