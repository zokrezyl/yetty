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
#include <stdlib.h>
#include <string.h>

/*===========================================================================
 * Widget per-class data slice.
 *=========================================================================*/

struct [[clang::annotate("class@ygui:widget")]] yetty_ygui_widget_data {
    struct yetty_ycore_rectangle rect;
    struct yetty_ygui_layout layout;
    /* Optional background fill (packed 0xAABBGGRR). 0 = transparent, so
     * widgets that never call set_bg_color are unaffected. Painted by
     * primitive_widget's emit_body before the widget's own paint. */
    uint32_t bg;
};

/* Convenience accessor — every internal helper that needs the widget
 * class pointer goes through this. The codegen-shaped accessor returns
 * a Result; this delegates to yetty_ygui_class_expect so a registration
 * failure aborts loudly with the chained cause rather than silently
 * casting the error union to a pointer. Registration only happens at
 * the first call and the result is cached — after that this is a
 * one-instruction Result unwrap. */
static const struct yetty_yclass *widget_class(void)
{
    return yetty_ygui_class_expect(yetty_ygui_widget_class_get(), "yetty_ygui_widget_class_get");
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

static struct yetty_ygui_object *self_of(struct yetty_yclass_object *obj)
{
    return (struct yetty_ygui_object *)obj;
}

[[clang::annotate("override@ygui:widget:constructor")]]
static struct yetty_ycore_void_result widget_default_constructor(struct yetty_yclass_ctx *ctx,
                                                                 struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_ygui_widget_data *wd = yetty_ygui_data_get(self_of(obj), widget_class());
    wd->rect.min.x = 0;
    wd->rect.min.y = 0;
    wd->rect.max.x = 0;
    wd->rect.max.y = 0;
    wd->layout = yetty_ygui_layout_default();
    wd->bg = 0;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:widget:destructor")]]
static struct yetty_ycore_void_result widget_default_destructor(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj)
{
    (void)ctx;
    (void)obj;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:widget:widget_on_press")]]
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

[[clang::annotate("override@ygui:widget:widget_on_release")]]
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

[[clang::annotate("override@ygui:widget:widget_on_motion")]]
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

[[clang::annotate("override@ygui:widget:widget_paint")]] [[clang::annotate(
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

[[clang::annotate("override@ygui:widget:widget_emit_container")]] [[clang::annotate(
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

[[clang::annotate("override@ygui:widget:widget_emit_body")]] [[clang::annotate(
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

struct yetty_ycore_void_result yetty_ygui_widget_set_rect(struct yetty_ygui_object *obj,
                                                          struct yetty_ycore_rectangle rect)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_rect: NULL obj");
    }
    struct yetty_ygui_widget_data *wd = yetty_ygui_data_get(obj, widget_class());
    wd->rect = rect;
    obj->dirty = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_rectangle yetty_ygui_widget_rect(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        struct yetty_ycore_rectangle z = {0};
        return z;
    }
    struct yetty_ygui_widget_data *wd =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, widget_class());
    return wd->rect;
}

struct yetty_ycore_void_result yetty_ygui_widget_layout_set(struct yetty_ygui_object *obj,
                                                            const struct yetty_ygui_layout *layout)
{
    if (!obj || !layout) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_layout_set: NULL arg");
    }
    struct yetty_ygui_widget_data *wd = yetty_ygui_data_get(obj, widget_class());
    wd->layout = *layout;
    obj->dirty = 1;
    return YETTY_OK_VOID();
}

const struct yetty_ygui_layout *yetty_ygui_widget_layout_get(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct yetty_ygui_widget_data *wd =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, widget_class());
    return &wd->layout;
}

struct yetty_ycore_void_result yetty_ygui_widget_set_visible(struct yetty_ygui_object *obj,
                                                             int visible)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_visible: NULL obj");
    }
    struct yetty_ygui_widget_data *wd = yetty_ygui_data_get(obj, widget_class());
    wd->layout.hidden = visible ? 0 : 1;
    return yetty_ygui_object_set_dirty(obj);
}

int yetty_ygui_widget_is_visible(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return 0;
    }
    struct yetty_ygui_widget_data *wd =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, widget_class());
    return wd->layout.hidden ? 0 : 1;
}

struct yetty_ycore_void_result yetty_ygui_widget_set_size(struct yetty_ygui_object *obj, float w,
                                                          float h)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_size: NULL obj");
    }
    struct yetty_ygui_widget_data *wd = yetty_ygui_data_get(obj, widget_class());
    wd->layout.width = w;
    wd->layout.height = h;
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_widget_set_position(struct yetty_ygui_object *obj, float x,
                                                              float y)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_position: NULL obj");
    }
    struct yetty_ygui_widget_data *wd = yetty_ygui_data_get(obj, widget_class());
    wd->layout.absolute = 1;
    wd->layout.pos_x = x;
    wd->layout.pos_y = y;
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_widget_make_figure(struct yetty_ygui_object *obj,
                                                             uint32_t kind, int32_t z)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_make_figure: NULL obj");
    }
    obj->figure_kind = kind;
    obj->figure_z = z;
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_widget_set_figure_z(struct yetty_ygui_object *obj,
                                                              int32_t z)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_figure_z: NULL obj");
    }
    if (obj->figure_z != z) {
        obj->figure_z = z;
        return yetty_ygui_object_set_dirty(obj);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_widget_set_floating(struct yetty_ygui_object *obj,
                                                             int floating)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_floating: NULL obj");
    }
    obj->floating = floating ? 1 : 0;
    return YETTY_OK_VOID();
}

int yetty_ygui_widget_is_floating(const struct yetty_ygui_object *obj)
{
    return obj ? obj->floating : 0;
}

uint32_t yetty_ygui_widget_figure_kind(const struct yetty_ygui_object *obj)
{
    return obj ? obj->figure_kind : 0;
}

int32_t yetty_ygui_widget_figure_z(const struct yetty_ygui_object *obj)
{
    return obj ? obj->figure_z : 0;
}

struct yetty_ycore_void_result yetty_ygui_widget_set_bg_color(struct yetty_ygui_object *obj,
                                                              uint32_t color)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_bg_color: NULL obj");
    }
    struct yetty_ygui_widget_data *wd = yetty_ygui_data_get(obj, widget_class());
    wd->bg = color;
    return yetty_ygui_object_set_dirty(obj);
}

uint32_t yetty_ygui_widget_bg(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return 0;
    }
    struct yetty_ygui_widget_data *wd =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, widget_class());
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
        l->direction =
            strcmp(vbuf, "column") == 0 ? YETTY_YGUI_FLEX_COLUMN : YETTY_YGUI_FLEX_ROW;
    }
    /* align-self: per-child cross-align isn't modelled in the new flex
     * pass (the parent's align governs); accepted and ignored so yui's
     * `align-self: stretch;` is a harmless no-op. */
}

struct yetty_ycore_void_result yetty_ygui_widget_apply_css(struct yetty_ygui_object *obj,
                                                           const char *css)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_apply_css: NULL obj");
    }
    if (!css) {
        return YETTY_OK_VOID();
    }
    struct yetty_ygui_widget_data *wd = yetty_ygui_data_get(obj, widget_class());
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
    return yetty_ygui_object_set_dirty(obj);
}

/*===========================================================================
 * Class accessor — yclass-shaped, returns Result. First call registers
 * the class with the yclass runtime; subsequent calls return the
 * cached pointer.
 *=========================================================================*/

#include "widget.gen.c"
