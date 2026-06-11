/*
 * ygui-widget.c — base widget class, ported to yclass.
 *
 * Every widget inherits from this class. It owns:
 *   - geometry: rect (absolute target pixels)
 *   - layout struct (flex / CSS authoring values)
 *
 * Public stubs (`yetty_ygui_widget_paint`, on_press, …) stay
 * historic-shaped (`obj, …`) for caller convenience; the slot impls
 * are yclass-shaped (`ctx, obj, …`). The stub looks up the impl via
 * `yetty_ygui_dispatch_lookup(obj->klass, slot)` and invokes it,
 * passing NULL ctx and casting the ygui_object* to yclass_object*
 * (layout-compatible: first member `klass` is the same field).
 */

#include "internal.h"

#include <yetty/yfigure/wire.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>

/* This TU deliberately does NOT include its own generated header
 * `yetty/ygui/widget.h` (internal.h no longer pulls it in either). That
 * header is a downstream artifact for other modules; pulling it here would
 * redefine the YETTY_YRESULT_DECLARE this TU declares manually below. The
 * flex/layout types the widget data slice embeds and the exposed setters
 * consume are therefore defined directly in this TU for the real build,
 * inside the `#ifndef YCLASS_CODEGEN` block below. During codegen those
 * same types are instead taken from the `#ifdef YCLASS_CODEGEN`
 * header-destined block at the foot (which is what codegen copies verbatim
 * into the generated widget.h), so the two never coexist in one parse. */
#ifndef YCLASS_CODEGEN
enum yetty_ygui_flex_direction {
    YETTY_YGUI_FLEX_ROW = 0,
    YETTY_YGUI_FLEX_COLUMN = 1,
};

enum yetty_ygui_flex_justify {
    YETTY_YGUI_JUSTIFY_START = 0,
    YETTY_YGUI_JUSTIFY_CENTER,
    YETTY_YGUI_JUSTIFY_END,
    YETTY_YGUI_JUSTIFY_SPACE_BETWEEN,
};

enum yetty_ygui_flex_align {
    YETTY_YGUI_ALIGN_START = 0,
    YETTY_YGUI_ALIGN_CENTER,
    YETTY_YGUI_ALIGN_END,
    YETTY_YGUI_ALIGN_STRETCH,
};

struct yetty_ygui_layout {
    enum yetty_ygui_flex_direction direction;
    enum yetty_ygui_flex_justify justify;
    enum yetty_ygui_flex_align align;
    float gap;
    float padding_top;
    float padding_right;
    float padding_bottom;
    float padding_left;
    float width;
    float height;
    float flex_grow;
    float flex_shrink;
    float min_width;
    float max_width;
    float min_height;
    float max_height;
    int absolute;
    float pos_x;
    float pos_y;
    int hidden;
};

struct yetty_ygui_layout yetty_ygui_layout_default(void);

/* The class accessor is defined in the appended widget.gen.c; forward-
 * declared here because the helpers above the foot include reference it. */
struct yetty_yclass_ptr_result yetty_ygui_widget_class_get(void);
#endif /* !YCLASS_CODEGEN */

/*===========================================================================
 * Widget per-class data slice.
 *=========================================================================*/

struct [[clang::annotate("class@ygui:widget")]] yetty_ygui_widget {
    /* Widget tree + per-widget framework state. MUST be the first member:
     * widget is the root class of every ygui object, so this slice sits
     * directly after the object header and ygui_tree() reaches `tree` at a
     * fixed offset on any ygui object. */
    struct yetty_ygui_tree tree;
    struct yetty_ycore_rectangle rect;
    struct yetty_ygui_layout layout;
    /* Optional background fill (packed 0xAABBGGRR). 0 = transparent, so
     * widgets that never call set_bg_color are unaffected. Painted by
     * primitive_widget's emit_body before the widget's own paint. */
    uint32_t bg;
    /* Main-axis scroll offset in px. The layout pass shifts this node's
     * in-flow children toward the content-box origin by this amount. Used
     * by scrollarea (a ygrid figure) to slide its content; the figure's
     * GPU scissor clips the overflow. 0 for every other widget. */
    float scroll_main;
};

/* The codegen accessor/downcast defined in the appended widget.gen.c,
 * declared here (this TU does not include its own generated widget.h) so the
 * helpers + exposed widget API below have it in scope. The generated
 * widget.h publishes the identical declaration for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_widget_ptr, struct yetty_ygui_widget *);
struct yetty_ygui_widget_ptr_result yetty_ygui_widget_from(struct yetty_yclass_object *obj);

/* The module constructor/destructor slot stubs (generated in the appended
 * widget.gen.c). widget.c calls them in widget_instantiate / _destroy above
 * that foot include, and can't include its own widget.h, so forward-declare
 * them here. */
struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj);

/* Defined lower in this file (the widget tree/lifecycle block), but the
 * geometry setters above it mark the widget dirty — forward-declare. */
struct yetty_ycore_void_result yetty_ygui_widget_set_dirty(struct yetty_yclass_object *obj);

/* Convenience accessor — every internal helper that needs the widget
 * class pointer goes through this. By the time any widget method runs,
 * the class is already registered (you cannot dispatch a method on an
 * unregistered class), so the accessor's Result is always OK here and
 * .value is safe. Registration can only fail on the first call, which
 * happens at widget-creation time where the Result is checked. */
YETTY_EXTERNAL_CALLBACK
static const struct yetty_yclass *widget_class(void)
{
    return yetty_ygui_widget_class_get().value;
}

/*===========================================================================
 * Public stubs `yetty_ygui_widget_paint` / `_emit_container` / `_emit_body`
 * / `_on_press` / `_on_release` / `_on_motion` are emitted by yclass
 * codegen from the override annotations on `widget_default_*` below.
 * The generated stubs live in methods.gen.c; they have the canonical
 * yclass slot signature `(struct yetty_yclass_ctx *, struct
 * yetty_yclass_object *, …)`.
 *=========================================================================*/

/*===========================================================================
 * Default implementations registered on the base widget class. Each
 * impl is yclass-shaped (ctx, obj, …) — `ctx` is unused (local
 * dispatch only); `obj` is the ygui_object cast as yclass_object,
 * recovered here as the original type via the layout-compatible first
 * member.
 *=========================================================================*/

static struct yetty_yclass_object *self_of(struct yetty_yclass_object *obj)
{
    return (struct yetty_yclass_object *)obj;
}

[[clang::annotate("virtual@ygui:widget:constructor")]]
static struct yetty_ycore_void_result widget_default_constructor(struct yetty_yclass_ctx *ctx,
                                                                 struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_ygui_widget_ptr_result wd_dr = yetty_ygui_widget_from(self_of(obj));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_dr, "widget_default_constructor: data_get");
    struct yetty_ygui_widget *wd = wd_dr.value;
    wd->rect.min.x = 0;
    wd->rect.min.y = 0;
    wd->rect.max.x = 0;
    wd->rect.max.y = 0;
    wd->layout = yetty_ygui_layout_default();
    wd->bg = 0;
    return YETTY_OK_VOID();
}

[[clang::annotate("virtual@ygui:widget:destructor")]]
static struct yetty_ycore_void_result widget_default_destructor(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj)
{
    (void)ctx;
    (void)obj;
    return YETTY_OK_VOID();
}

[[clang::annotate("virtual@ygui:widget:widget_on_press")]]
static struct yetty_ycore_int_result widget_default_on_press(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj,
                                                             float x, float y, int button)
{
    (void)ctx;
    (void)obj;
    (void)x;
    (void)y;
    (void)button;
    return YETTY_OK(yetty_ycore_int, 0);
}

[[clang::annotate("virtual@ygui:widget:widget_on_release")]]
static struct yetty_ycore_int_result widget_default_on_release(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj,
                                                               float x, float y, int button)
{
    (void)ctx;
    (void)obj;
    (void)x;
    (void)y;
    (void)button;
    return YETTY_OK(yetty_ycore_int, 0);
}

[[clang::annotate("virtual@ygui:widget:widget_on_motion")]]
static struct yetty_ycore_int_result widget_default_on_motion(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              float x, float y)
{
    (void)ctx;
    (void)obj;
    (void)x;
    (void)y;
    return YETTY_OK(yetty_ycore_int, 0);
}

/* Wheel / trackpad scroll. (dx, dy) are the deltas at (x, y). Default:
 * not handled (0), so the framework keeps bubbling to an ancestor that
 * scrolls. Scrollable widgets (scrollarea, filepicker) override this. */
[[clang::annotate("virtual@ygui:widget:widget_on_scroll")]]
static struct yetty_ycore_int_result widget_default_on_scroll(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              float x, float y, float dx, float dy)
{
    (void)ctx;
    (void)obj;
    (void)x;
    (void)y;
    (void)dx;
    (void)dy;
    return YETTY_OK(yetty_ycore_int, 0);
}

[[clang::annotate("virtual@ygui:widget:widget_paint")]] [[clang::annotate(
    "local@ygui:widget_paint")]]
static struct yetty_ycore_void_result widget_default_paint(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj,
                                                           struct yetty_ygui_emit_ctx *emit_ctx)
{
    (void)ctx;
    (void)obj;
    (void)emit_ctx;
    return YETTY_OK_VOID();
}

[[clang::annotate("virtual@ygui:widget:widget_emit_container")]] [[clang::annotate(
    "local@ygui:widget_emit_container")]]
static struct yetty_ycore_void_result widget_default_emit_container(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_ygui_emit_ctx *emit_ctx)
{
    (void)ctx;
    (void)obj;
    (void)emit_ctx;
    return YETTY_OK_VOID();
}

[[clang::annotate("virtual@ygui:widget:widget_emit_body")]] [[clang::annotate(
    "local@ygui:widget_emit_body")]]
static struct yetty_ycore_void_result widget_default_emit_body(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj,
                                                               struct yetty_ygui_emit_ctx *emit_ctx)
{
    (void)ctx;
    (void)obj;
    (void)emit_ctx;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Layout setters / getters.
 *=========================================================================*/

struct yetty_ygui_layout yetty_ygui_layout_default(void)
{
    struct yetty_ygui_layout l = {0};
    l.direction = YETTY_YGUI_FLEX_ROW;
    l.justify = YETTY_YGUI_JUSTIFY_START;
    l.align = YETTY_YGUI_ALIGN_START;
    l.gap = 0;
    l.width = -1.0f;
    l.height = -1.0f;
    l.flex_grow = 0;
    l.flex_shrink = 0;
    l.min_width = -1.0f;
    l.max_width = -1.0f;
    l.min_height = -1.0f;
    l.max_height = -1.0f;
    l.absolute = 0;
    l.pos_x = 0.0f;
    l.pos_y = 0.0f;
    l.hidden = 0;
    return l;
}

struct yetty_ycore_void_result yetty_ygui_widget_set_rect(struct yetty_yclass_object *obj,
                                                          struct yetty_ycore_rectangle rect)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_rect: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_dr = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_dr, "yetty_ygui_widget_set_rect: data_get");
    struct yetty_ygui_widget *wd = wd_dr.value;
    /* Only dirty on an actual change. The layout pass calls set_rect for
     * every widget on every emit; marking dirty unconditionally would keep
     * the whole tree perpetually dirty and defeat incremental emit. */
    if (wd->rect.min.x != rect.min.x || wd->rect.min.y != rect.min.y ||
        wd->rect.max.x != rect.max.x || wd->rect.max.y != rect.max.y) {
        wd->rect = rect;
        ygui_tree(obj)->dirty = 1;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_rectangle yetty_ygui_widget_rect(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        struct yetty_ycore_rectangle z = {0};
        return z;
    }
    struct yetty_ygui_widget *wd = yetty_ygui_widget_from((struct yetty_yclass_object *)obj).value;
    return wd->rect;
}

/* Main-axis scroll offset (internal — read by the layout pass to slide a
 * scrolling container's children). Sets ygui_tree(obj)->dirty but not engine dirty;
 * the scroller requests the repaint via yetty_ygui_widget_set_dirty. */
struct yetty_ycore_void_result yetty_ygui_widget_scroll_main_set(struct yetty_yclass_object *obj,
                                                                 float offset)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_scroll_main_set: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_dr = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_dr, "yetty_ygui_widget_scroll_main_set: data_get");
    struct yetty_ygui_widget *wd = wd_dr.value;
    if (wd->scroll_main != offset) {
        wd->scroll_main = offset;
        ygui_tree(obj)->dirty = 1;
    }
    return YETTY_OK_VOID();
}

float yetty_ygui_widget_scroll_main_get(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return 0.0f;
    }
    struct yetty_ygui_widget *wd = yetty_ygui_widget_from((struct yetty_yclass_object *)obj).value;
    return wd->scroll_main;
}

struct yetty_ycore_void_result yetty_ygui_widget_layout_set(struct yetty_yclass_object *obj,
                                                            const struct yetty_ygui_layout *layout)
{
    if (!obj || !layout) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_layout_set: NULL arg");
    }
    struct yetty_ygui_widget_ptr_result wd_dr = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_dr, "yetty_ygui_widget_layout_set: data_get");
    struct yetty_ygui_widget *wd = wd_dr.value;
    wd->layout = *layout;
    ygui_tree(obj)->dirty = 1;
    return YETTY_OK_VOID();
}

const struct yetty_ygui_layout *yetty_ygui_widget_layout_get(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct yetty_ygui_widget *wd = yetty_ygui_widget_from((struct yetty_yclass_object *)obj).value;
    return &wd->layout;
}

struct yetty_ycore_void_result yetty_ygui_widget_set_visible(struct yetty_yclass_object *obj,
                                                             int visible)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_visible: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_dr = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_dr, "yetty_ygui_widget_set_visible: data_get");
    struct yetty_ygui_widget *wd = wd_dr.value;
    wd->layout.hidden = visible ? 0 : 1;
    return yetty_ygui_widget_set_dirty(obj);
}

int yetty_ygui_widget_is_visible(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return 0;
    }
    struct yetty_ygui_widget *wd = yetty_ygui_widget_from((struct yetty_yclass_object *)obj).value;
    return wd->layout.hidden ? 0 : 1;
}

struct yetty_ycore_void_result yetty_ygui_widget_set_size(struct yetty_yclass_object *obj, float w,
                                                          float h)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_size: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_dr = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_dr, "yetty_ygui_widget_set_size: data_get");
    struct yetty_ygui_widget *wd = wd_dr.value;
    wd->layout.width = w;
    wd->layout.height = h;
    return yetty_ygui_widget_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_widget_set_position(struct yetty_yclass_object *obj,
                                                              float x, float y)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_position: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_dr = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_dr, "yetty_ygui_widget_set_position: data_get");
    struct yetty_ygui_widget *wd = wd_dr.value;
    wd->layout.absolute = 1;
    wd->layout.pos_x = x;
    wd->layout.pos_y = y;
    return yetty_ygui_widget_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_widget_make_figure(struct yetty_yclass_object *obj,
                                                             uint32_t kind, int32_t z)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_make_figure: NULL obj");
    }
    ygui_tree(obj)->figure_kind = kind;
    ygui_tree(obj)->figure_z = z;
    return yetty_ygui_widget_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_widget_set_figure_z(struct yetty_yclass_object *obj,
                                                              int32_t z)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_figure_z: NULL obj");
    }
    if (ygui_tree(obj)->figure_z != z) {
        ygui_tree(obj)->figure_z = z;
        return yetty_ygui_widget_set_dirty(obj);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_widget_set_floating(struct yetty_yclass_object *obj,
                                                              int floating)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_floating: NULL obj");
    }
    ygui_tree(obj)->floating = floating ? 1 : 0;
    return YETTY_OK_VOID();
}

int yetty_ygui_widget_is_floating(const struct yetty_yclass_object *obj)
{
    return obj ? ygui_tree(obj)->floating : 0;
}

uint32_t yetty_ygui_widget_figure_kind(const struct yetty_yclass_object *obj)
{
    return obj ? ygui_tree(obj)->figure_kind : 0;
}

int32_t yetty_ygui_widget_figure_z(const struct yetty_yclass_object *obj)
{
    return obj ? ygui_tree(obj)->figure_z : 0;
}

struct yetty_ycore_void_result yetty_ygui_widget_set_bg_color(struct yetty_yclass_object *obj,
                                                              uint32_t color)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_bg_color: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_dr = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_dr, "yetty_ygui_widget_set_bg_color: data_get");
    struct yetty_ygui_widget *wd = wd_dr.value;
    wd->bg = color;
    return yetty_ygui_widget_set_dirty(obj);
}

uint32_t yetty_ygui_widget_bg(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return 0;
    }
    struct yetty_ygui_widget *wd = yetty_ygui_widget_from((struct yetty_yclass_object *)obj).value;
    return wd->bg;
}

/* --- minimal CSS shim for the yui port --- */

static int css_token_is(const char *s, size_t n, const char *kw)
{
    return strlen(kw) == n && strncmp(s, kw, n) == 0;
}

/* Trim ASCII spaces/tabs from both ends, returning the new [start,len). */
static const char *css_trim(const char *s, size_t *len)
{
    while (*len > 0 && (s[0] == ' ' || s[0] == '\t')) {
        s++;
        (*len)--;
    }
    while (*len > 0 && (s[*len - 1] == ' ' || s[*len - 1] == '\t')) {
        (*len)--;
    }
    return s;
}

/* Parse a leading float from a NUL-terminated value slice (ignores a
 * trailing "px" / units). */
static float css_num(const char *v)
{
    return (float)strtod(v, NULL);
}

static void css_apply_decl(struct yetty_ygui_layout *l, const char *prop, size_t plen,
                           const char *val, size_t vlen)
{
    prop = css_trim(prop, &plen);
    val = css_trim(val, &vlen);
    if (plen == 0 || vlen == 0) {
        return;
    }
    char vbuf[64];
    if (vlen >= sizeof(vbuf)) {
        vlen = sizeof(vbuf) - 1;
    }
    memcpy(vbuf, val, vlen);
    vbuf[vlen] = '\0';

    if (css_token_is(prop, plen, "width")) {
        l->width = css_num(vbuf);
    } else if (css_token_is(prop, plen, "height")) {
        l->height = css_num(vbuf);
    } else if (css_token_is(prop, plen, "gap")) {
        l->gap = css_num(vbuf);
    } else if (css_token_is(prop, plen, "flex-grow")) {
        l->flex_grow = css_num(vbuf);
    } else if (css_token_is(prop, plen, "flex-shrink")) {
        l->flex_shrink = css_num(vbuf);
    } else if (css_token_is(prop, plen, "flex")) {
        /* "<grow> [<shrink> [<basis>]]" */
        char *end = NULL;
        l->flex_grow = (float)strtod(vbuf, &end);
        if (end && *end) {
            l->flex_shrink = (float)strtod(end, &end);
        }
    } else if (css_token_is(prop, plen, "padding")) {
        float p = css_num(vbuf);
        l->padding_top = l->padding_right = l->padding_bottom = l->padding_left = p;
    } else if (css_token_is(prop, plen, "padding-top")) {
        l->padding_top = css_num(vbuf);
    } else if (css_token_is(prop, plen, "padding-bottom")) {
        l->padding_bottom = css_num(vbuf);
    } else if (css_token_is(prop, plen, "padding-left")) {
        l->padding_left = css_num(vbuf);
    } else if (css_token_is(prop, plen, "padding-right")) {
        l->padding_right = css_num(vbuf);
    } else if (css_token_is(prop, plen, "align-items")) {
        if (strcmp(vbuf, "center") == 0) {
            l->align = YETTY_YGUI_ALIGN_CENTER;
        } else if (strcmp(vbuf, "end") == 0 || strcmp(vbuf, "flex-end") == 0) {
            l->align = YETTY_YGUI_ALIGN_END;
        } else if (strcmp(vbuf, "stretch") == 0) {
            l->align = YETTY_YGUI_ALIGN_STRETCH;
        } else {
            l->align = YETTY_YGUI_ALIGN_START;
        }
    } else if (css_token_is(prop, plen, "justify-content")) {
        if (strcmp(vbuf, "center") == 0) {
            l->justify = YETTY_YGUI_JUSTIFY_CENTER;
        } else if (strcmp(vbuf, "end") == 0 || strcmp(vbuf, "flex-end") == 0) {
            l->justify = YETTY_YGUI_JUSTIFY_END;
        } else if (strcmp(vbuf, "space-between") == 0) {
            l->justify = YETTY_YGUI_JUSTIFY_SPACE_BETWEEN;
        } else {
            l->justify = YETTY_YGUI_JUSTIFY_START;
        }
    } else if (css_token_is(prop, plen, "direction") ||
               css_token_is(prop, plen, "flex-direction")) {
        l->direction = strcmp(vbuf, "column") == 0 ? YETTY_YGUI_FLEX_COLUMN : YETTY_YGUI_FLEX_ROW;
    }
    /* align-self: per-child cross-align isn't modelled in the new flex
     * pass (the parent's align governs); accepted and ignored so yui's
     * `align-self: stretch;` is a harmless no-op. */
}

struct yetty_ycore_void_result yetty_ygui_widget_apply_css(struct yetty_yclass_object *obj,
                                                           const char *css)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_apply_css: NULL obj");
    }
    if (!css) {
        return YETTY_OK_VOID();
    }
    struct yetty_ygui_widget_ptr_result wd_dr = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_dr, "yetty_ygui_widget_apply_css: data_get");
    struct yetty_ygui_widget *wd = wd_dr.value;
    struct yetty_ygui_layout l = wd->layout;

    const char *p = css;
    while (*p) {
        const char *semi = strchr(p, ';');
        size_t decl_len = semi ? (size_t)(semi - p) : strlen(p);
        const char *colon = memchr(p, ':', decl_len);
        if (colon) {
            size_t plen = (size_t)(colon - p);
            const char *val = colon + 1;
            size_t vlen = decl_len - plen - 1;
            css_apply_decl(&l, p, plen, val, vlen);
        }
        if (!semi) {
            break;
        }
        p = semi + 1;
    }
    wd->layout = l;
    return yetty_ygui_widget_set_dirty(obj);
}

/*===========================================================================
 * Class accessor — yclass-shaped, returns Result. First call registers
 * the class with the yclass runtime; subsequent calls return the
 * cached pointer.
 *=========================================================================*/

/*===========================================================================
 * Widget lifecycle, tree navigation, framework/dirty/hover state and super
 * invokers. In ygui every object IS a widget (widget is the root class of
 * every instantiable class), so these are widget operations — exposed via
 * codegen into the generated widget.h. There is no separate "object" layer:
 * the yclass core owns identity/dispatch/allocation; this owns the widget
 * tree + per-widget framework state (in the yetty_ygui_tree base slice,
 * reached with ygui_tree()).
 *=========================================================================*/

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_super_void(struct yetty_yclass_object *obj,
                                                     const struct yetty_yclass *self_class,
                                                     yetty_yclass_method_id_t method_id)
{
    if (!obj || !self_class || !method_id) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_super_void: NULL arg");
    }
    struct yetty_yclass_method_slot_result slot_result = yetty_ygui_method_slot_get(method_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_result, "yetty_ygui_super_void: slot lookup");
    yetty_yclass_method_slot slot = slot_result.value;
    if (slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_super_void: slot lookup failed");
    }
    struct yetty_yclass_impl_t_result impl_result =
        yetty_ygui_dispatch_lookup_super(self_class, slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, impl_result, "yetty_ygui_super_void: dispatch lookup");
    yetty_yclass_impl_t impl = impl_result.value;
    if (!impl) {
        return YETTY_OK_VOID();
    }
    typedef struct yetty_ycore_void_result (*fn_t)(struct yetty_yclass_ctx *,
                                                   struct yetty_yclass_object *);
    return ((fn_t)impl)(NULL, (struct yetty_yclass_object *)obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_int_result yetty_ygui_super_int(struct yetty_yclass_object *obj,
                                                   const struct yetty_yclass *self_class,
                                                   yetty_yclass_method_id_t method_id)
{
    if (!obj || !self_class || !method_id) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_super_int: NULL arg");
    }
    struct yetty_yclass_method_slot_result slot_result = yetty_ygui_method_slot_get(method_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, slot_result, "yetty_ygui_super_int: slot lookup");
    yetty_yclass_method_slot slot = slot_result.value;
    if (slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_super_int: slot lookup failed");
    }
    struct yetty_yclass_impl_t_result impl_result =
        yetty_ygui_dispatch_lookup_super(self_class, slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, impl_result, "yetty_ygui_super_int: dispatch lookup");
    yetty_yclass_impl_t impl = impl_result.value;
    if (!impl) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    typedef struct yetty_ycore_int_result (*fn_t)(struct yetty_yclass_ctx *,
                                                  struct yetty_yclass_object *);
    return ((fn_t)impl)(NULL, (struct yetty_yclass_object *)obj);
}

[[clang::annotate("expose")]]
const struct yetty_yclass *yetty_ygui_class_expect(struct yetty_yclass_ptr_result class_result,
                                                   const char *name)
{
    if (YETTY_IS_ERR(class_result)) {
        yerror("yetty_ygui_class_expect: %s failed: %s", name ? name : "(class)",
               class_result.error.msg);
        yetty_ycore_error_destroy(class_result.error);
        return NULL;
    }
    return class_result.value;
}

/*---------------------------------------------------------------------------
 * Widget tree navigation + per-widget framework / dirty / hover state.
 *-------------------------------------------------------------------------*/

[[clang::annotate("expose")]]
struct yetty_yclass_object *yetty_ygui_widget_parent(struct yetty_yclass_object *obj)
{
    return obj ? ygui_tree(obj)->parent : NULL;
}

[[clang::annotate("expose")]]
struct yetty_yclass_object *yetty_ygui_widget_first_child(struct yetty_yclass_object *obj)
{
    return obj ? ygui_tree(obj)->first_child : NULL;
}

[[clang::annotate("expose")]]
struct yetty_yclass_object *yetty_ygui_widget_next_sibling(struct yetty_yclass_object *obj)
{
    return obj ? ygui_tree(obj)->next_sibling : NULL;
}

[[clang::annotate("expose")]]
struct yetty_ygui_framework *yetty_ygui_widget_framework(struct yetty_yclass_object *obj)
{
    while (obj) {
        if (ygui_tree(obj)->framework) {
            return ygui_tree(obj)->framework;
        }
        obj = ygui_tree(obj)->parent;
    }
    return NULL;
}

[[clang::annotate("expose")]]
uint32_t yetty_ygui_widget_id(const struct yetty_yclass_object *obj)
{
    return obj ? ygui_tree(obj)->id : 0;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_widget_set_dirty(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_dirty: NULL obj");
    }
    ygui_tree(obj)->dirty = 1;
    struct yetty_ygui_framework *framework = yetty_ygui_widget_framework(obj);
    if (framework) {
        yetty_ygui_framework_mark_dirty(framework);
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
int yetty_ygui_widget_is_dirty(const struct yetty_yclass_object *obj)
{
    return obj && ygui_tree(obj)->dirty;
}

[[clang::annotate("expose")]]
int yetty_ygui_widget_is_hovered(const struct yetty_yclass_object *obj)
{
    return obj && ygui_tree(obj)->hovered;
}

/*---------------------------------------------------------------------------
 * Widget tree linking (internal) + create / add-child / destroy / raise.
 *-------------------------------------------------------------------------*/

static void widget_unlink_from_parent(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_object *p = ygui_tree(obj)->parent;
    if (!p) {
        return;
    }
    struct yetty_yclass_object **slot = &ygui_tree(p)->first_child;
    while (*slot && *slot != obj) {
        slot = &ygui_tree(*slot)->next_sibling;
    }
    if (*slot == obj) {
        *slot = ygui_tree(obj)->next_sibling;
    }
    ygui_tree(obj)->next_sibling = NULL;
    ygui_tree(obj)->parent = NULL;
}

static void widget_link_to_parent(struct yetty_yclass_object *obj,
                                  struct yetty_yclass_object *parent)
{
    ygui_tree(obj)->parent = parent;
    if (!parent) {
        return;
    }
    if (!ygui_tree(parent)->first_child) {
        ygui_tree(parent)->first_child = obj;
        return;
    }
    struct yetty_yclass_object *t = ygui_tree(parent)->first_child;
    while (ygui_tree(t)->next_sibling) {
        t = ygui_tree(t)->next_sibling;
    }
    ygui_tree(t)->next_sibling = obj;
}

/* Instantiate a widget of `cls` and, if `parent` is non-NULL, link it as
 * `parent`'s last child. Allocates via the yclass runtime, assigns a wire
 * id from the owning framework, and runs the constructor chain. Shared by
 * yetty_ygui_widget_create (root) and yetty_ygui_widget_add (child). */
static struct yetty_yclass_object_ptr_result widget_instantiate(const struct yetty_yclass *cls,
                                                                struct yetty_yclass_object *parent)
{
    if (!cls) {
        return YETTY_ERR(yetty_yclass_object_ptr, "widget_instantiate: NULL class");
    }
    /* Reject mixin direct instantiation — mixins contribute data via
     * `uses@`, they're not concrete classes you can instantiate. */
    struct yetty_yclass_const_char_ptr_result tr = yetty_yclass_type_str(cls);
    if (YETTY_IS_OK(tr) && strcmp(tr.value, "mixin") == 0) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "widget_instantiate: cannot instantiate a mixin class directly");
    }
    if (YETTY_IS_ERR(tr)) {
        yetty_ycore_error_destroy(tr.error);
    }

    /* The yclass runtime owns instance layout + sizing: the object is a
     * plain yetty_yclass_object whose data slices (widget tree first, then
     * each subclass / mixin slice) follow the header. */
    struct yetty_yclass_object_ptr_result objr = yetty_yclass_object_alloc(cls);
    if (YETTY_IS_ERR(objr)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "widget_instantiate: object_alloc failed", objr);
    }
    struct yetty_yclass_object *obj = objr.value;
    widget_link_to_parent(obj, parent);

    struct yetty_ygui_framework *framework = yetty_ygui_widget_framework(obj);
    if (framework) {
        struct uint32_result idr = yetty_ygui_framework_alloc_id(framework);
        if (YETTY_IS_ERR(idr)) {
            widget_unlink_from_parent(obj);
            free(obj);
            return YETTY_ERR(yetty_yclass_object_ptr, "widget_instantiate: id alloc failed", idr);
        }
        ygui_tree(obj)->id = idr.value;
    }

    struct yetty_ycore_void_result cr =
        yetty_ygui_constructor(NULL, (struct yetty_yclass_object *)obj);
    if (YETTY_IS_ERR(cr)) {
        if (framework && ygui_tree(obj)->id) {
            struct yetty_ycore_void_result fr =
                yetty_ygui_framework_free_id(framework, ygui_tree(obj)->id);
            if (YETTY_IS_ERR(fr)) {
                yetty_ycore_error_destroy(fr.error);
            }
        }
        widget_unlink_from_parent(obj);
        free(obj);
        return YETTY_ERR(yetty_yclass_object_ptr, "widget_instantiate: constructor failed", cr);
    }

    return YETTY_OK(yetty_yclass_object_ptr, obj);
}

/* Create a parentless (root / top-level) widget of `cls`. */
[[clang::annotate("expose")]]
struct yetty_yclass_object_ptr_result yetty_ygui_widget_new(const struct yetty_yclass *cls)
{
    return widget_instantiate(cls, NULL);
}

/* Create a widget of `cls` and add it as the last child of `parent`. */
[[clang::annotate("expose")]]
struct yetty_yclass_object_ptr_result yetty_ygui_widget_add(struct yetty_yclass_object *parent,
                                                            const struct yetty_yclass *cls)
{
    if (!parent) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_widget_add: NULL parent (use yetty_ygui_widget_create)");
    }
    return widget_instantiate(cls, parent);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_widget_destroy(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK_VOID();
    }
    while (ygui_tree(obj)->first_child) {
        struct yetty_ycore_void_result cr = yetty_ygui_widget_destroy(ygui_tree(obj)->first_child);
        if (YETTY_IS_ERR(cr)) {
            yetty_ycore_error_destroy(cr.error);
        }
    }
    struct yetty_ygui_event_subscription *sub = ygui_tree(obj)->subscriptions;
    while (sub) {
        struct yetty_ygui_event_subscription *next = sub->next;
        free(sub);
        sub = next;
    }
    ygui_tree(obj)->subscriptions = NULL;
    struct yetty_ycore_void_result dr =
        yetty_ygui_destructor(NULL, (struct yetty_yclass_object *)obj);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    struct yetty_ygui_framework *framework = yetty_ygui_widget_framework(obj);
    if (framework && ygui_tree(obj)->id != 0) {
        struct yetty_ycore_void_result fr =
            yetty_ygui_framework_free_id(framework, ygui_tree(obj)->id);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
    }
    if (framework && framework->hovered_obj == obj) {
        framework->hovered_obj = NULL;
    }
    if (framework && framework->pressed_obj == obj) {
        framework->pressed_obj = NULL;
    }
    widget_unlink_from_parent(obj);
    free(obj);
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_widget_raise(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_raise: NULL obj");
    }
    if (!ygui_tree(obj)->parent) {
        /* No parent: nothing to reorder. Not an error. */
        return YETTY_OK_VOID();
    }
    /* Move to the end of the sibling list so the framework's widget
     * hit-test (last-match-wins) prefers this widget over earlier
     * siblings it overlaps. The render side is ordered by figure z
     * separately; raising bumps both so they agree. */
    struct yetty_yclass_object *parent = ygui_tree(obj)->parent;
    widget_unlink_from_parent(obj);
    widget_link_to_parent(obj, parent);
    return YETTY_OK_VOID();
}

#include "widget.gen.c"

#ifdef YCLASS_CODEGEN
/* Header-destined content for the generated widget.h (skipped by the real build, which takes it from that header). */
/*-----------------------------------------------------------------------------
 * Flex / CSS layout — the field set the flex layout pass in layout.c
 * consumes. Apps author these values directly or via apply_css.
 *---------------------------------------------------------------------------*/
enum yetty_ygui_flex_direction {
    YETTY_YGUI_FLEX_ROW = 0,
    YETTY_YGUI_FLEX_COLUMN = 1,
};

enum yetty_ygui_flex_justify {
    YETTY_YGUI_JUSTIFY_START = 0,
    YETTY_YGUI_JUSTIFY_CENTER,
    YETTY_YGUI_JUSTIFY_END,
    YETTY_YGUI_JUSTIFY_SPACE_BETWEEN,
};

enum yetty_ygui_flex_align {
    YETTY_YGUI_ALIGN_START = 0,
    YETTY_YGUI_ALIGN_CENTER,
    YETTY_YGUI_ALIGN_END,
    YETTY_YGUI_ALIGN_STRETCH,
};

struct yetty_ygui_layout {
    enum yetty_ygui_flex_direction direction;
    enum yetty_ygui_flex_justify justify;
    enum yetty_ygui_flex_align align;
    float gap;
    float padding_top;
    float padding_right;
    float padding_bottom;
    float padding_left;
    /* Authored intrinsic size — used as the starting point for the main-
     * axis distribution and as the cross-axis size unless overridden by
     * align=stretch. -1.0f means "unset" (the layout treats as 0 on the
     * main axis, parent-cross-content on the cross axis). */
    float width;
    float height;
    /* flex factors — main-axis space distribution. */
    float flex_grow;
    float flex_shrink;
    /* min / max constraints; -1.0f means "unset". */
    float min_width;
    float max_width;
    float min_height;
    float max_height;
    /* Absolute positioning. When `absolute` is non-zero, the layout
     * pass skips this widget in its parent's flex flow and instead
     * places it at (parent_content_min.x + pos_x, parent_content_min.y +
     * pos_y) with size (width, height). Used by overlays (popup_menu,
     * dialog) that need to float over their siblings. */
    int absolute;
    float pos_x;
    float pos_y;
    /* When non-zero, this widget and its subtree are excluded from both
     * the layout pass (consumes no main-axis space, contributes no flex
     * sum) and the emit walk (no container record, no body prims). Used
     * by collapsible containers (collapsing_header, tree_node) to fold
     * away their children when closed without disturbing the children's
     * authored sizes — restoring visibility is a single flag flip. */
    int hidden;
};

/* Initial layout — all enums zeroed (row / start / start), no gap, no
 * padding, no flex grow/shrink, unbounded min/max. */
struct yetty_ygui_layout yetty_ygui_layout_default(void);

/*-----------------------------------------------------------------------------
 * Widget geometry / layout setters. Setters take effect on the next
 * engine emit; the engine flags the widget dirty.
 *---------------------------------------------------------------------------*/
struct yetty_ycore_void_result yetty_ygui_widget_set_rect(struct yetty_yclass_object *obj,
                                                          struct yetty_ycore_rectangle rect);

struct yetty_ycore_rectangle yetty_ygui_widget_rect(const struct yetty_yclass_object *obj);

struct yetty_ycore_void_result yetty_ygui_widget_layout_set(struct yetty_yclass_object *obj,
                                                            const struct yetty_ygui_layout *layout);

const struct yetty_ygui_layout *yetty_ygui_widget_layout_get(const struct yetty_yclass_object *obj);

/*-----------------------------------------------------------------------------
 * Convenience geometry / visibility setters — thin wrappers over the
 * layout struct, exposing geometry/visibility as first-class widget
 * calls. Each marks the widget dirty.
 *---------------------------------------------------------------------------*/
/* Toggle the widget (and its subtree) in/out of layout + paint via the
 * layout `hidden` flag. */
struct yetty_ycore_void_result yetty_ygui_widget_set_visible(struct yetty_yclass_object *obj,
                                                             int visible);
int yetty_ygui_widget_is_visible(const struct yetty_yclass_object *obj);

/* Author the widget's main/cross size (width, height). */
struct yetty_ycore_void_result yetty_ygui_widget_set_size(struct yetty_yclass_object *obj, float w,
                                                          float h);

/* Place the widget at an absolute (pos_x, pos_y) inside its parent's
 * content box (sets layout.absolute). */
struct yetty_ycore_void_result yetty_ygui_widget_set_position(struct yetty_yclass_object *obj,
                                                              float x, float y);

/* Promote this widget to its own receiver-side child figure of `kind`
 * (e.g. YETTY_YFIGURE_KIND_YGRID) stacked at `z`. The whole subtree
 * then paints into that figure's own draw list instead of the shared
 * chrome ygrid — giving floating windows / menus an independent z and
 * damage region. kind=0 reverts to inline. */
struct yetty_ycore_void_result yetty_ygui_widget_make_figure(struct yetty_yclass_object *obj,
                                                             uint32_t kind, int32_t z);

/* Update only the figure z (for raise-on-click). No-op if unchanged. */
struct yetty_ycore_void_result yetty_ygui_widget_set_figure_z(struct yetty_yclass_object *obj,
                                                              int32_t z);

uint32_t yetty_ygui_widget_figure_kind(const struct yetty_yclass_object *obj);
int32_t yetty_ygui_widget_figure_z(const struct yetty_yclass_object *obj);

/* Mark this widget a floating overlay (dialog / debug window): a press
 * anywhere inside it moves it to the end of its parent's child list, so
 * it paints last (front) and wins the hit-test — click-to-front, no
 * figures involved. */
struct yetty_ycore_void_result yetty_ygui_widget_set_floating(struct yetty_yclass_object *obj,
                                                              int floating);
int yetty_ygui_widget_is_floating(const struct yetty_yclass_object *obj);

/* Apply a small CSS-like declaration string to the widget's layout.
 * Supported properties (others ignored): width, height, flex,
 * flex-grow, flex-shrink, gap, padding, align-items, justify-content,
 * (flex-)direction, align-self. Values may carry a trailing "px".
 * Eases the yui port, which authored layout via CSS strings. */
struct yetty_ycore_void_result yetty_ygui_widget_apply_css(struct yetty_yclass_object *obj,
                                                           const char *css);

/* Background fill (packed 0xAABBGGRR; 0 = transparent). Painted by the
 * primitive_widget base before the widget's own paint. */
struct yetty_ycore_void_result yetty_ygui_widget_set_bg_color(struct yetty_yclass_object *obj,
                                                              uint32_t color);

/* Current background fill (0 = transparent). Used by primitive_widget. */
uint32_t yetty_ygui_widget_bg(const struct yetty_yclass_object *obj);

/*-----------------------------------------------------------------------------
 * Flex layout pass.
 *
 * Walks the widget tree top-down assigning rects according to each
 * widget's layout struct + the parent's content box. Call this before
 * yetty_ygui_framework_emit when the tree's geometry might have changed
 * (set_rect, set_layout, add / del). The engine's emit pass uses the
 * rects assigned here.
 *
 * `root_rect` is the absolute pixel rect the root widget should fit
 * into — typically the engine's viewport.
 *---------------------------------------------------------------------------*/
struct yetty_ycore_void_result yetty_ygui_layout_compute(struct yetty_yclass_object *root,
                                                         struct yetty_ycore_rectangle root_rect);

/*-----------------------------------------------------------------------------
 * Wire emission — two explicit passes. The engine invokes these via
 * dispatch as it walks the widget tree.
 *
 * Defaults (provided by the base widget class):
 *
 *   emit_container — if figure_kind != 0, writes CREATE_CHILD /
 *     SET_CHILD_RECT / DELETE_CHILD admin records into the engine's
 *     container record stream using obj->id as the figure id. Otherwise
 *     no-op. Subclasses with figure_kind==0 do not override.
 *
 *   emit_body — if figure_kind == 0, opens CMD_GROUP(obj->id, ...) in
 *     the engine's ygrid, calls the widget_paint hook to write prims,
 *     closes the group. If figure_kind != 0, the widget's own override
 *     writes figure-specific body bytes into the per-figure stream.
 *---------------------------------------------------------------------------*/
/* Method public-stub declarations — emit_container, emit_body,
 * widget_paint, widget_on_press, widget_on_release, widget_on_motion
 * — come from the codegen-emitted module-wide methods.h pulled
 * in by class.h's include chain. The yclass slot signature is
 * `(struct yetty_yclass_ctx *, struct yetty_yclass_object *, …)`. */
#endif
