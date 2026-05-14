/*
 * ygui_widgets.c - Widget implementations
 */

#include "ygui_internal.h"
#include <yetty/ytrace/ytrace.h>
#include <stdio.h>

/*=============================================================================
 * Widget Base Functions
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_widget_alloc(struct yetty_ygui_engine *engine,
                                                         ygui_widget_type_t type, const char *id)
{
    struct yetty_ygui_widget *w =
        (struct yetty_ygui_widget *)calloc(1, sizeof(struct yetty_ygui_widget));
    if (!w) {
        yetty_ygui_set_error("Failed to allocate widget");
        return NULL;
    }

    w->id = ygui_strdup(id);
    w->type = type;
    w->engine = engine;
    w->flags = YETTY_YGUI_FLAG_VISIBLE;
    w->bg_color = engine->theme->bg_surface;
    w->fg_color = engine->theme->text_primary;
    w->accent_color = engine->theme->accent;

    return w;
}

void yetty_ygui_widget_init_base(struct yetty_ygui_widget *widget, float x, float y, float w,
                                 float h)
{
    widget->authored_x = x;
    widget->authored_y = y;
    widget->authored_w = w;
    widget->authored_h = h;
    widget->x = x;
    widget->y = y;
    widget->w = w;
    widget->h = h;
}

void yetty_ygui_widget_free(struct yetty_ygui_widget *widget)
{
    if (!widget) {
        return;
    }

    ydebug("widget_free enter id=%s type=%d ptr=%p", widget->id ? widget->id : "?",
           (int)widget->type, (void *)widget);

    /* Free children recursively */
    struct yetty_ygui_widget *child = widget->first_child;
    while (child) {
        struct yetty_ygui_widget *next = child->next_sibling;
        yetty_ygui_widget_free(child);
        child = next;
    }

    /* Call type-specific destroy via the vtable. */
    if (widget->vtable && widget->vtable->destroy) {
        ydebug("widget_free destroy id=%s", widget->id ? widget->id : "?");
        widget->vtable->destroy(widget);
    }

    ydebug("widget_free finalize id=%s ptr=%p", widget->id ? widget->id : "?", (void *)widget);
    free(widget->id);
    free(widget);
}

struct yetty_ycore_void_result yetty_ygui_widget_render_all_default(
    struct yetty_ygui_widget *self, struct yetty_ygui_render_ctx *ctx)
{
    /* Skip invisible widgets globally — was previously a per-container concern. */
    if (!(self->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        return YETTY_OK_VOID();
    }

    /* Layout pass already wrote effective_x/y and live x/y/w/h; nothing to
     * recompute here. */
    self->was_rendered = 1;
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();

    if (self->vtable && self->vtable->render) {
        struct yetty_ycore_void_result r = self->vtable->render(self, ctx);
        if (YETTY_IS_ERR(r)) {
            first_err = r;
        }
    }

    /* Recurse into children with offset = self's absolute origin so each
     * child can keep drawing at its own (relative) self->x / self->y. */
    if (self->first_child) {
        float old_offset_x = ctx->offset_x;
        float old_offset_y = ctx->offset_y;
        ctx->offset_x = self->layout_x;
        ctx->offset_y = self->layout_y;

        for (struct yetty_ygui_widget *child = self->first_child; child;
             child = child->next_sibling) {
            if (!(child->flags & YETTY_YGUI_FLAG_VISIBLE)) {
                continue;
            }
            struct yetty_ycore_void_result r;
            if (child->vtable && child->vtable->render_all) {
                r = child->vtable->render_all(child, ctx);
            } else {
                r = yetty_ygui_widget_render_all_default(child, ctx);
            }
            if (YETTY_IS_ERR(r)) {
                if (YETTY_IS_OK(first_err)) {
                    first_err = r;
                } else {
                    yetty_ycore_error_destroy(r.error);
                }
            }
        }

        ctx->offset_x = old_offset_x;
        ctx->offset_y = old_offset_y;
    }
    return first_err;
}

/*=============================================================================
 * Widget Hierarchy
 *===========================================================================*/

static void add_to_engine(struct yetty_ygui_engine *engine, struct yetty_ygui_widget *widget)
{
    widget->engine = engine;
    if (!engine->first_widget) {
        engine->first_widget = widget;
        engine->last_widget = widget;
    } else {
        engine->last_widget->next_sibling = widget;
        widget->prev_sibling = engine->last_widget;
        engine->last_widget = widget;
    }
    engine->widget_count++;
    engine->dirty = 1;
}

/* Public-to-other-ygui-TUs wrapper around add_to_engine. The static
 * add_to_engine remains in use for every constructor in this file; the
 * new widget files (ygui_rich.c, ygui_tabbar.c) call this so they don't
 * have to redo the linked-list bookkeeping. */
void yetty_ygui_engine_attach_widget(struct yetty_ygui_engine *engine,
                                     struct yetty_ygui_widget *widget)
{
    if (!engine || !widget) {
        return;
    }
    add_to_engine(engine, widget);
}

void yetty_ygui_widget_add_child(struct yetty_ygui_widget *parent, struct yetty_ygui_widget *child)
{
    if (!parent || !child) {
        return;
    }

    /* Remove from engine's top-level list if present */
    struct yetty_ygui_engine *engine = parent->engine;
    if (engine && !child->parent) {
        if (child->prev_sibling) {
            child->prev_sibling->next_sibling = child->next_sibling;
        } else if (engine->first_widget == child) {
            engine->first_widget = child->next_sibling;
        }
        if (child->next_sibling) {
            child->next_sibling->prev_sibling = child->prev_sibling;
        } else if (engine->last_widget == child) {
            engine->last_widget = child->prev_sibling;
        }
        engine->widget_count--;
    }

    child->parent = parent;
    child->prev_sibling = NULL;
    child->next_sibling = NULL;

    if (!parent->first_child) {
        parent->first_child = child;
        parent->last_child = child;
    } else {
        parent->last_child->next_sibling = child;
        child->prev_sibling = parent->last_child;
        parent->last_child = child;
    }

    if (engine) {
        engine->dirty = 1;
    }
}

void yetty_ygui_widget_remove_child(struct yetty_ygui_widget *parent,
                                    struct yetty_ygui_widget *child)
{
    if (!parent || !child || child->parent != parent) {
        return;
    }

    if (child->prev_sibling) {
        child->prev_sibling->next_sibling = child->next_sibling;
    } else {
        parent->first_child = child->next_sibling;
    }

    if (child->next_sibling) {
        child->next_sibling->prev_sibling = child->prev_sibling;
    } else {
        parent->last_child = child->prev_sibling;
    }

    child->parent = NULL;
    child->prev_sibling = NULL;
    child->next_sibling = NULL;

    if (parent->engine) {
        parent->engine->dirty = 1;
    }
}

void yetty_ygui_widget_remove(struct yetty_ygui_widget *widget)
{
    if (!widget) {
        return;
    }

    if (widget->parent) {
        yetty_ygui_widget_remove_child(widget->parent, widget);
    } else if (widget->engine) {
        struct yetty_ygui_engine *engine = widget->engine;
        if (widget->prev_sibling) {
            widget->prev_sibling->next_sibling = widget->next_sibling;
        } else {
            engine->first_widget = widget->next_sibling;
        }
        if (widget->next_sibling) {
            widget->next_sibling->prev_sibling = widget->prev_sibling;
        } else {
            engine->last_widget = widget->prev_sibling;
        }
        engine->widget_count--;
        engine->dirty = 1;
    }

    yetty_ygui_widget_free(widget);
}

struct yetty_ygui_widget *yetty_ygui_widget_parent(struct yetty_ygui_widget *widget)
{
    return widget ? widget->parent : NULL;
}

struct yetty_ygui_widget *yetty_ygui_widget_first_child(struct yetty_ygui_widget *widget)
{
    return widget ? widget->first_child : NULL;
}

struct yetty_ygui_widget *yetty_ygui_widget_next_sibling(struct yetty_ygui_widget *widget)
{
    return widget ? widget->next_sibling : NULL;
}

/*=============================================================================
 * Widget Properties (Generic)
 *===========================================================================*/

const char *yetty_ygui_widget_id(const struct yetty_ygui_widget *widget)
{
    return widget ? widget->id : NULL;
}

ygui_widget_type_t yetty_ygui_widget_type(const struct yetty_ygui_widget *widget)
{
    return widget ? widget->type : YETTY_YGUI_WIDGET_CUSTOM;
}

void yetty_ygui_widget_set_position(struct yetty_ygui_widget *widget, float x, float y)
{
    if (!widget) {
        return;
    }
    widget->authored_x = x;
    widget->authored_y = y;
    widget->x = x;
    widget->y = y;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

void yetty_ygui_widget_get_position(const struct yetty_ygui_widget *widget, float *x, float *y)
{
    if (!widget) {
        return;
    }
    if (x) {
        *x = widget->x;
    }
    if (y) {
        *y = widget->y;
    }
}

void yetty_ygui_widget_set_size(struct yetty_ygui_widget *widget, float w, float h)
{
    if (!widget) {
        return;
    }
    widget->authored_w = w;
    widget->authored_h = h;
    widget->w = w;
    widget->h = h;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

void yetty_ygui_widget_get_size(const struct yetty_ygui_widget *widget, float *w, float *h)
{
    if (!widget) {
        return;
    }
    /* Report authored size — the user-visible value, stable across resizes. */
    if (w) {
        *w = widget->authored_w;
    }
    if (h) {
        *h = widget->authored_h;
    }
}

void yetty_ygui_widget_get_layout_box(const struct yetty_ygui_widget *widget, float *x, float *y,
                                      float *w, float *h)
{
    if (!widget) {
        return;
    }
    if (x) {
        *x = widget->layout_x;
    }
    if (y) {
        *y = widget->layout_y;
    }
    if (w) {
        *w = widget->layout_w;
    }
    if (h) {
        *h = widget->layout_h;
    }
}

void yetty_ygui_widget_set_visible(struct yetty_ygui_widget *widget, int visible)
{
    if (!widget) {
        return;
    }
    if (visible) {
        widget->flags |= YETTY_YGUI_FLAG_VISIBLE;
    } else {
        widget->flags &= ~YETTY_YGUI_FLAG_VISIBLE;
    }
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

int yetty_ygui_widget_is_visible(const struct yetty_ygui_widget *widget)
{
    return widget ? (widget->flags & YETTY_YGUI_FLAG_VISIBLE) != 0 : 0;
}

void yetty_ygui_widget_set_enabled(struct yetty_ygui_widget *widget, int enabled)
{
    if (!widget) {
        return;
    }
    if (enabled) {
        widget->flags &= ~YETTY_YGUI_FLAG_DISABLED;
    } else {
        widget->flags |= YETTY_YGUI_FLAG_DISABLED;
    }
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

int yetty_ygui_widget_is_enabled(const struct yetty_ygui_widget *widget)
{
    return widget ? (widget->flags & YETTY_YGUI_FLAG_DISABLED) == 0 : 0;
}

uint32_t yetty_ygui_widget_get_flags(const struct yetty_ygui_widget *widget)
{
    return widget ? widget->flags : 0;
}

void yetty_ygui_widget_set_bg_color(struct yetty_ygui_widget *widget, uint32_t color)
{
    if (!widget) {
        return;
    }
    widget->bg_color = color;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

void yetty_ygui_widget_set_fg_color(struct yetty_ygui_widget *widget, uint32_t color)
{
    if (!widget) {
        return;
    }
    widget->fg_color = color;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

void yetty_ygui_widget_set_accent_color(struct yetty_ygui_widget *widget, uint32_t color)
{
    if (!widget) {
        return;
    }
    widget->accent_color = color;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

/*=============================================================================
 * Layout setters (flexbox)
 *===========================================================================*/

static void layout_widget_dirty(struct yetty_ygui_widget *widget)
{
    if (widget && widget->engine) {
        widget->engine->dirty = 1;
    }
}

void yetty_ygui_widget_set_layout_mode(struct yetty_ygui_widget *widget, ygui_layout_mode_t mode)
{
    if (!widget) {
        return;
    }
    widget->layout.mode = mode;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_flex_direction(struct yetty_ygui_widget *widget,
                                          ygui_flex_direction_t direction)
{
    if (!widget) {
        return;
    }
    widget->layout.direction = direction;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_justify_content(struct yetty_ygui_widget *widget, ygui_justify_t justify)
{
    if (!widget) {
        return;
    }
    widget->layout.justify_content = justify;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_align_items(struct yetty_ygui_widget *widget, ygui_align_t align)
{
    if (!widget) {
        return;
    }
    widget->layout.align_items = align;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_align_self(struct yetty_ygui_widget *widget, ygui_align_t align)
{
    if (!widget) {
        return;
    }
    widget->layout.align_self = align;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_flex(struct yetty_ygui_widget *widget, float grow, float shrink,
                                float basis)
{
    if (!widget) {
        return;
    }
    widget->layout.flex_grow = grow;
    widget->layout.flex_shrink = shrink;
    widget->layout.flex_basis = basis;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_gap(struct yetty_ygui_widget *widget, float gap)
{
    if (!widget) {
        return;
    }
    widget->layout.gap = gap;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_padding(struct yetty_ygui_widget *widget, float top, float right,
                                   float bottom, float left)
{
    if (!widget) {
        return;
    }
    widget->layout.padding_top = top;
    widget->layout.padding_right = right;
    widget->layout.padding_bottom = bottom;
    widget->layout.padding_left = left;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_margin(struct yetty_ygui_widget *widget, float top, float right,
                                  float bottom, float left)
{
    if (!widget) {
        return;
    }
    widget->layout.margin_top = top;
    widget->layout.margin_right = right;
    widget->layout.margin_bottom = bottom;
    widget->layout.margin_left = left;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_min_size(struct yetty_ygui_widget *widget, float min_w, float min_h)
{
    if (!widget) {
        return;
    }
    widget->layout.min_w = min_w;
    widget->layout.min_h = min_h;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_max_size(struct yetty_ygui_widget *widget, float max_w, float max_h)
{
    if (!widget) {
        return;
    }
    widget->layout.max_w = max_w;
    widget->layout.max_h = max_h;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_flex_wrap(struct yetty_ygui_widget *widget, ygui_flex_wrap_t wrap)
{
    if (!widget) {
        return;
    }
    widget->layout.wrap = wrap;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_align_content(struct yetty_ygui_widget *widget, ygui_align_t align)
{
    if (!widget) {
        return;
    }
    widget->layout.align_content = align;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_position_mode(struct yetty_ygui_widget *widget, ygui_position_t pos)
{
    if (!widget) {
        return;
    }
    widget->layout.position = pos;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_flex_basis_percent(struct yetty_ygui_widget *widget, float pct)
{
    if (!widget) {
        return;
    }
    widget->layout.flex_basis_percent = pct;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_size_percent(struct yetty_ygui_widget *widget, float w_pct, float h_pct)
{
    if (!widget) {
        return;
    }
    widget->layout.width_percent = w_pct;
    widget->layout.height_percent = h_pct;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_min_size_percent(struct yetty_ygui_widget *widget, float min_w_pct,
                                            float min_h_pct)
{
    if (!widget) {
        return;
    }
    widget->layout.min_w_percent = min_w_pct;
    widget->layout.min_h_percent = min_h_pct;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_max_size_percent(struct yetty_ygui_widget *widget, float max_w_pct,
                                            float max_h_pct)
{
    if (!widget) {
        return;
    }
    widget->layout.max_w_percent = max_w_pct;
    widget->layout.max_h_percent = max_h_pct;
    layout_widget_dirty(widget);
}

/*=============================================================================
 * Button Widget
 *===========================================================================*/

static struct yetty_ycore_void_result button_render(struct yetty_ygui_widget *self,
                                                    struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    int pressed = (self->flags & YETTY_YGUI_FLAG_PRESSED) != 0;
    int hovered = (self->flags & YETTY_YGUI_FLAG_HOVER) != 0;
    int focused = (self->flags & YETTY_YGUI_FLAG_FOCUSED) != 0;

    /* Surface base color: hover brightens, press goes to accent. */
    uint32_t surface = pressed ? self->accent_color : (hovered ? t->bg_hover : self->bg_color);

    /* Material-style elevation: low when idle, drops to ~0 when pressed
     * so the button looks "depressed" against the page. */
    float elev = pressed ? 0.0f : t->elevation_low;
    /* Press also nudges the surface down 1px to give tactile feedback. */
    float press_offset = pressed ? 1.0f : 0.0f;

    /* Drop shadow first (skipped in pressed state). */
    yetty_ygui_render_ctx_render_box_shadow(ctx, self->x, self->y, self->w, self->h,
                                            t->radius_medium, elev, t->shadow, t->elevation_alpha);

    /* Surface — flat color, or a real linear gradient when the theme
     * opts in. Using the SDF gradient primitive (ported from yetty-poc):
     * top edge is `surface` lightened, bottom edge is `surface` darkened,
     * giving a subtle convex feel without painting an obvious overlay. */
    if (t->enable_gradient && !pressed) {
        uint32_t top = surface;
        uint32_t bot = surface;
        /* Bias top by +10% white, bottom by -10% black, alpha-preserving. */
        uint8_t r = (uint8_t)(surface & 0xFF);
        uint8_t g = (uint8_t)((surface >> 8) & 0xFF);
        uint8_t b = (uint8_t)((surface >> 16) & 0xFF);
        uint8_t a = (uint8_t)((surface >> 24) & 0xFF);
        uint8_t lr = (uint8_t)((r * 230 + 255 * 25) / 255);
        uint8_t lg = (uint8_t)((g * 230 + 255 * 25) / 255);
        uint8_t lb = (uint8_t)((b * 230 + 255 * 25) / 255);
        uint8_t dr = (uint8_t)(r * 230 / 255);
        uint8_t dg = (uint8_t)(g * 230 / 255);
        uint8_t db = (uint8_t)(b * 230 / 255);
        top = (uint32_t)a << 24 | (uint32_t)lb << 16 | (uint32_t)lg << 8 | lr;
        bot = (uint32_t)a << 24 | (uint32_t)db << 16 | (uint32_t)dg << 8 | dr;
        yetty_ygui_render_ctx_render_box_linear_gradient(
            ctx, self->x, self->y + press_offset, self->w, self->h, t->radius_medium,
            /*gx0,gy0=*/self->x, self->y + press_offset,
            /*gx1,gy1=*/self->x, self->y + press_offset + self->h, top, bot);
    } else {
        yetty_ygui_render_ctx_render_box(ctx, self->x, self->y + press_offset, self->w, self->h,
                                         surface, t->radius_medium);
    }

    /* Focus ring: visible offset outline for keyboard navigation. */
    if (focused) {
        float r = t->radius_medium + 2.0f;
        yetty_ygui_render_ctx_render_box_outline(ctx, self->x - 2.0f, self->y - 2.0f + press_offset,
                                                 self->w + 4.0f, self->h + 4.0f, self->accent_color,
                                                 r, 2.0f);
    } else if (hovered && !pressed) {
        /* Soft hover halo — inset, accent-colored. */
        yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y + press_offset, self->w,
                                                 self->h, self->accent_color, t->radius_medium,
                                                 1.5f);
    }

    /* Label. */
    if (self->data.button.label) {
        yetty_ygui_render_ctx_render_text(ctx, self->data.button.label, self->x + t->pad_large,
                                          self->y + t->pad_medium + press_offset, self->fg_color,
                                          t->font_size);
    }
    return YETTY_OK_VOID();
}

static int button_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)lx;
    (void)ly;
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_PRESS;
    return 1;
}

static int button_on_release(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    if (lx >= 0 && lx < self->w && ly >= 0 && ly < self->h) {
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_CLICK;
        return 1;
    }
    return 0;
}

static void button_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.button.label);
}

static float button_baseline(const struct yetty_ygui_widget *self,
                             const struct yetty_ygui_theme *theme)
{
    /* Mirror button_render: text drawn at y = self->y + pad_medium and the
     * helper places it at baseline = top + font_size * 0.8. */
    (void)self;
    return theme->pad_medium + theme->font_size * 0.8f;
}

struct yetty_ygui_widget *yetty_ygui_engine_button(struct yetty_ygui_engine *engine, const char *id,
                                                   float x, float y, float w, float h,
                                                   const char *label)
{
    struct yetty_ygui_widget *btn =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_BUTTON, id);
    if (!btn) {
        return NULL;
    }

    yetty_ygui_widget_init_base(btn, x, y, w, h);
    btn->data.button.label = ygui_strdup(label);
    static const struct yetty_ygui_widget_vtable button_vtable = {
        .render = button_render,
        .on_press = button_on_press,
        .on_release = button_on_release,
        .destroy = button_destroy,
        .baseline_offset = button_baseline,
    };
    btn->vtable = &button_vtable;

    add_to_engine(engine, btn);
    return btn;
}

void yetty_ygui_widget_button_set_label(struct yetty_ygui_widget *widget, const char *label)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_BUTTON) {
        return;
    }
    free(widget->data.button.label);
    widget->data.button.label = ygui_strdup(label);
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

const char *yetty_ygui_widget_button_get_label(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_BUTTON) {
        return NULL;
    }
    return widget->data.button.label;
}

/*=============================================================================
 * Label Widget
 *===========================================================================*/

static struct yetty_ycore_void_result label_render(struct yetty_ygui_widget *self,
                                                   struct yetty_ygui_render_ctx *ctx)
{
    if (self->data.label.text) {
        float font_size =
            self->data.label.font_size > 0 ? self->data.label.font_size : ctx->theme->font_size;
        yetty_ygui_render_ctx_render_text(ctx, self->data.label.text, self->x, self->y,
                                          self->fg_color, font_size);
    }
    return YETTY_OK_VOID();
}

static void label_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.label.text);
}

static float label_baseline(const struct yetty_ygui_widget *self,
                            const struct yetty_ygui_theme *theme)
{
    /* Label draws at (self->x, self->y); render_text places baseline at
     * top + font_size * 0.8. */
    float fs = self->data.label.font_size > 0 ? self->data.label.font_size : theme->font_size;
    return fs * 0.8f;
}

struct yetty_ygui_widget *yetty_ygui_engine_label(struct yetty_ygui_engine *engine, const char *id,
                                                  float x, float y, const char *text)
{
    struct yetty_ygui_widget *lbl =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_LABEL, id);
    if (!lbl) {
        return NULL;
    }

    float h = engine->theme->row_height;
    yetty_ygui_widget_init_base(lbl, x, y, 100, h); /* Width is flexible */
    lbl->data.label.text = ygui_strdup(text);
    lbl->data.label.font_size = 0; /* Use theme default */
    static const struct yetty_ygui_widget_vtable label_vtable = {
        .render = label_render,
        .destroy = label_destroy,
        .baseline_offset = label_baseline,
    };
    lbl->vtable = &label_vtable;

    add_to_engine(engine, lbl);
    return lbl;
}

void yetty_ygui_widget_label_set_text(struct yetty_ygui_widget *widget, const char *text)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_LABEL) {
        return;
    }
    free(widget->data.label.text);
    widget->data.label.text = ygui_strdup(text);
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

const char *yetty_ygui_widget_label_get_text(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_LABEL) {
        return NULL;
    }
    return widget->data.label.text;
}

void yetty_ygui_widget_label_set_font_size(struct yetty_ygui_widget *widget, float size)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_LABEL) {
        return;
    }
    widget->data.label.font_size = size;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

/*=============================================================================
 * Slider Widget
 *===========================================================================*/

static struct yetty_ycore_void_result slider_render(struct yetty_ygui_widget *self,
                                                    struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float track_h = t->pad_medium;
    float track_y = self->y + (self->h - track_h) / 2;

    /* Track background */
    yetty_ygui_render_ctx_render_box(ctx, self->x, track_y, self->w, track_h, self->bg_color,
                                     t->radius_small);

    /* Filled portion */
    float range = self->data.slider.max_val - self->data.slider.min_val;
    float pct = range > 0 ? (self->data.slider.value - self->data.slider.min_val) / range : 0;
    float fill_w = pct * self->w;
    yetty_ygui_render_ctx_render_box(ctx, self->x, track_y, fill_w, track_h, self->accent_color,
                                     t->radius_small);

    /* Handle */
    float handle_w = t->scrollbar_size;
    float handle_x = self->x + fill_w - handle_w / 2;
    yetty_ygui_render_ctx_render_box(ctx, handle_x, self->y, handle_w, self->h, self->accent_color,
                                     handle_w / 2);
    return YETTY_OK_VOID();
}

static void slider_update_value(struct yetty_ygui_widget *self, float local_x)
{
    float pct = ygui_clamp(local_x / self->w, 0.0f, 1.0f);
    float range = self->data.slider.max_val - self->data.slider.min_val;
    self->data.slider.value = self->data.slider.min_val + pct * range;
}

static int slider_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)ly;
    slider_update_value(self, lx);

    /* Call user callback */
    if (self->change_callback) {
        self->change_callback(self, self->data.slider.value, self->change_userdata);
    }

    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.float_value = self->data.slider.value;
    return 1;
}

static int slider_on_drag(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)ly;
    slider_update_value(self, lx);

    /* Call user callback */
    if (self->change_callback) {
        self->change_callback(self, self->data.slider.value, self->change_userdata);
    }

    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.float_value = self->data.slider.value;
    return 1;
}

static int slider_on_scroll(struct yetty_ygui_widget *self, float dx, float dy, ygui_event_t *out)
{
    (void)dx;
    float range = self->data.slider.max_val - self->data.slider.min_val;
    float delta = dy * range * 0.05f;
    self->data.slider.value = ygui_clamp(self->data.slider.value + delta, self->data.slider.min_val,
                                         self->data.slider.max_val);
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.float_value = self->data.slider.value;
    return 1;
}

struct yetty_ygui_widget *yetty_ygui_engine_slider(struct yetty_ygui_engine *engine, const char *id,
                                                   float x, float y, float w, float h,
                                                   float min_val, float max_val, float value)
{
    struct yetty_ygui_widget *sld =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_SLIDER, id);
    if (!sld) {
        return NULL;
    }

    yetty_ygui_widget_init_base(sld, x, y, w, h);
    sld->data.slider.min_val = min_val;
    sld->data.slider.max_val = max_val;
    sld->data.slider.value = ygui_clamp(value, min_val, max_val);
    static const struct yetty_ygui_widget_vtable slider_vtable = {
        .render = slider_render,
        .on_press = slider_on_press,
        .on_drag = slider_on_drag,
        .on_scroll = slider_on_scroll,
    };
    sld->vtable = &slider_vtable;

    add_to_engine(engine, sld);
    return sld;
}

void yetty_ygui_widget_slider_set_value(struct yetty_ygui_widget *widget, float value)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SLIDER) {
        return;
    }
    widget->data.slider.value =
        ygui_clamp(value, widget->data.slider.min_val, widget->data.slider.max_val);
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

float yetty_ygui_widget_slider_get_value(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SLIDER) {
        return 0;
    }
    return widget->data.slider.value;
}

void yetty_ygui_widget_slider_set_range(struct yetty_ygui_widget *widget, float min_val,
                                        float max_val)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SLIDER) {
        return;
    }
    widget->data.slider.min_val = min_val;
    widget->data.slider.max_val = max_val;
    widget->data.slider.value = ygui_clamp(widget->data.slider.value, min_val, max_val);
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

/*=============================================================================
 * Checkbox Widget
 *===========================================================================*/

static struct yetty_ycore_void_result checkbox_render(struct yetty_ygui_widget *self,
                                                      struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float box_size = self->h - t->pad_small * 2;
    float box_y = self->y + t->pad_small;

    /* Box background */
    uint32_t box_color = self->data.checkbox.checked ? self->accent_color : self->bg_color;
    yetty_ygui_render_ctx_render_box(ctx, self->x, box_y, box_size, box_size, box_color,
                                     t->radius_small);
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, box_y, box_size, box_size, t->border,
                                             t->radius_small, 1.5f);

    /* Checkmark (simple cross for now) */
    if (self->data.checkbox.checked) {
        float cx = self->x + box_size / 2;
        float cy = box_y + box_size / 2;
        float s = box_size * 0.3f;
        /* Draw a simple checkmark using triangles */
        yetty_ygui_render_ctx_render_box(ctx, cx - s, cy - 1, s * 2, 3, self->fg_color, 1);
    }

    /* Label */
    if (self->data.checkbox.label) {
        float text_x = self->x + box_size + t->pad_medium;
        yetty_ygui_render_ctx_render_text(ctx, self->data.checkbox.label, text_x,
                                          self->y + t->pad_medium, self->fg_color, t->font_size);
    }
    return YETTY_OK_VOID();
}

static int checkbox_on_release(struct yetty_ygui_widget *self, float lx, float ly,
                               ygui_event_t *out)
{
    if (lx >= 0 && lx < self->w && ly >= 0 && ly < self->h) {
        self->data.checkbox.checked = !self->data.checkbox.checked;

        /* Call user callback */
        if (self->check_callback) {
            self->check_callback(self, self->data.checkbox.checked, self->check_userdata);
        }

        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_CHANGE;
        out->data.bool_value = self->data.checkbox.checked;
        return 1;
    }
    return 0;
}

static void checkbox_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.checkbox.label);
}

struct yetty_ygui_widget *yetty_ygui_engine_checkbox(struct yetty_ygui_engine *engine,
                                                     const char *id, float x, float y, float w,
                                                     float h, const char *label, int checked)
{
    struct yetty_ygui_widget *chk =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_CHECKBOX, id);
    if (!chk) {
        return NULL;
    }

    yetty_ygui_widget_init_base(chk, x, y, w, h);
    chk->data.checkbox.label = ygui_strdup(label);
    chk->data.checkbox.checked = checked;
    static const struct yetty_ygui_widget_vtable checkbox_vtable = {
        .render = checkbox_render,
        .on_release = checkbox_on_release,
        .destroy = checkbox_destroy,
    };
    chk->vtable = &checkbox_vtable;

    add_to_engine(engine, chk);
    return chk;
}

void yetty_ygui_widget_checkbox_set_checked(struct yetty_ygui_widget *widget, int checked)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_CHECKBOX) {
        return;
    }
    widget->data.checkbox.checked = checked;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

int yetty_ygui_widget_checkbox_get_checked(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_CHECKBOX) {
        return 0;
    }
    return widget->data.checkbox.checked;
}

void yetty_ygui_widget_checkbox_set_label(struct yetty_ygui_widget *widget, const char *label)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_CHECKBOX) {
        return;
    }
    free(widget->data.checkbox.label);
    widget->data.checkbox.label = ygui_strdup(label);
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

/*=============================================================================
 * Panel Widget
 *===========================================================================*/

static struct yetty_ycore_void_result panel_render(struct yetty_ygui_widget *self,
                                                   struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float radius =
        self->data.panel.corner_radius > 0 ? self->data.panel.corner_radius : t->radius_large;

    /* Soft elevation underneath. Panels use medium elevation by default. */
    yetty_ygui_render_ctx_render_box_shadow(ctx, self->x, self->y, self->w, self->h, radius,
                                            t->elevation_medium, t->shadow, t->elevation_alpha);

    /* Background */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, self->bg_color,
                                     radius);

    /* Scrollbar if needed */
    float scrollable_h = self->h - self->data.panel.header_h;
    float content_h = self->data.panel.content_h;
    if (content_h > scrollable_h && scrollable_h > 0) {
        float sb_w = t->scrollbar_size;
        float track_x = self->x + self->w - sb_w;
        float track_y = self->y + self->data.panel.header_h;
        float track_h = scrollable_h;

        /* Track */
        yetty_ygui_render_ctx_render_box(ctx, track_x, track_y, sb_w, track_h, t->bg_secondary,
                                         sb_w / 2);

        /* Thumb */
        float max_scroll = content_h - scrollable_h;
        float thumb_h = ygui_max(20.0f, track_h * scrollable_h / content_h);
        float thumb_range = track_h - thumb_h;
        float thumb_y =
            track_y + (max_scroll > 0 ? (self->data.panel.scroll_y / max_scroll) * thumb_range : 0);
        yetty_ygui_render_ctx_render_box(ctx, track_x + t->pad_small, thumb_y, sb_w - t->pad_medium,
                                         thumb_h, t->thumb_normal, (sb_w - t->pad_medium) / 2);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result panel_render_all(struct yetty_ygui_widget *self,
                                                       struct yetty_ygui_render_ctx *ctx)
{
    self->effective_x = self->x + ctx->offset_x;
    self->effective_y = self->y + ctx->offset_y;
    self->was_rendered = 1;

    /* Render panel background and scrollbar */
    panel_render(self, ctx);

    const struct yetty_ygui_theme *t = ctx->theme;
    float header_h = self->data.panel.header_h;
    float scrollable_h = self->h - header_h;
    float content_h = self->data.panel.content_h;
    float sb_w = (content_h > scrollable_h && scrollable_h > 0) ? t->scrollbar_size : 0;

    /* Save context state */
    float old_offset_x = ctx->offset_x;
    float old_offset_y = ctx->offset_y;

    /* Render header children (no scrolling) */
    ctx->offset_x = old_offset_x + self->x;
    ctx->offset_y = old_offset_y + self->y;
    for (struct yetty_ygui_widget *child = self->first_child; child; child = child->next_sibling) {
        if (child->y < header_h) {
            if (child->vtable && child->vtable->render_all) {
                child->vtable->render_all(child, ctx);
            } else {
                yetty_ygui_widget_render_all_default(child, ctx);
            }
        }
    }

    /* Render scrollable children */
    ctx->offset_x = old_offset_x + self->x - self->data.panel.scroll_x;
    ctx->offset_y = old_offset_y + self->y - self->data.panel.scroll_y;
    for (struct yetty_ygui_widget *child = self->first_child; child; child = child->next_sibling) {
        if (child->y >= header_h) {
            /* TODO: proper clipping */
            if (child->vtable && child->vtable->render_all) {
                child->vtable->render_all(child, ctx);
            } else {
                yetty_ygui_widget_render_all_default(child, ctx);
            }
        }
    }

    /* Restore context */
    ctx->offset_x = old_offset_x;
    ctx->offset_y = old_offset_y;
    return YETTY_OK_VOID();
}

static int panel_on_scroll(struct yetty_ygui_widget *self, float dx, float dy, ygui_event_t *out)
{
    (void)dx;
    float scrollable_h = self->h - self->data.panel.header_h;
    float max_scroll = ygui_max(0, self->data.panel.content_h - scrollable_h);
    float speed = 20.0f; /* TODO: get from theme */

    self->data.panel.scroll_y = ygui_clamp(self->data.panel.scroll_y - dy * speed, 0, max_scroll);

    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_SCROLL;
    out->data.scroll.x = self->data.panel.scroll_x;
    out->data.scroll.y = self->data.panel.scroll_y;
    return 1;
}

struct yetty_ygui_widget *yetty_ygui_engine_panel(struct yetty_ygui_engine *engine, const char *id,
                                                  float x, float y, float w, float h)
{
    struct yetty_ygui_widget *pnl =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_PANEL, id);
    if (!pnl) {
        return NULL;
    }

    yetty_ygui_widget_init_base(pnl, x, y, w, h);
    pnl->data.panel.scroll_x = 0;
    pnl->data.panel.scroll_y = 0;
    pnl->data.panel.content_w = w;
    pnl->data.panel.content_h = h;
    pnl->data.panel.header_h = 0;
    pnl->data.panel.corner_radius = 0;
    static const struct yetty_ygui_widget_vtable panel_vtable = {
        .render = panel_render,
        .render_all = panel_render_all,
        .on_scroll = panel_on_scroll,
    };
    pnl->vtable = &panel_vtable;

    add_to_engine(engine, pnl);
    return pnl;
}

void yetty_ygui_widget_panel_set_scroll(struct yetty_ygui_widget *widget, float x, float y)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_PANEL) {
        return;
    }
    widget->data.panel.scroll_x = x;
    widget->data.panel.scroll_y = y;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

void yetty_ygui_widget_panel_get_scroll(const struct yetty_ygui_widget *widget, float *x, float *y)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_PANEL) {
        return;
    }
    if (x) {
        *x = widget->data.panel.scroll_x;
    }
    if (y) {
        *y = widget->data.panel.scroll_y;
    }
}

void yetty_ygui_widget_panel_set_content_size(struct yetty_ygui_widget *widget, float w, float h)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_PANEL) {
        return;
    }
    widget->data.panel.content_w = w;
    widget->data.panel.content_h = h;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

void yetty_ygui_widget_panel_set_header_height(struct yetty_ygui_widget *widget, float h)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_PANEL) {
        return;
    }
    widget->data.panel.header_h = h;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

/*=============================================================================
 * Progress Widget
 *===========================================================================*/

static struct yetty_ycore_void_result progress_render(struct yetty_ygui_widget *self,
                                                      struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;

    /* Background track */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, self->bg_color,
                                     t->radius_small);

    /* Filled portion */
    float pct = ygui_clamp(self->data.progress.value, 0, 1);
    float fill_w = pct * self->w;
    if (fill_w > 0) {
        yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, fill_w, self->h, self->accent_color,
                                         t->radius_small);
    }
    return YETTY_OK_VOID();
}

struct yetty_ygui_widget *yetty_ygui_engine_progress(struct yetty_ygui_engine *engine,
                                                     const char *id, float x, float y, float w,
                                                     float h, float value)
{
    struct yetty_ygui_widget *prg =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_PROGRESS, id);
    if (!prg) {
        return NULL;
    }

    yetty_ygui_widget_init_base(prg, x, y, w, h);
    prg->data.progress.value = ygui_clamp(value, 0, 1);
    static const struct yetty_ygui_widget_vtable progress_vtable = {
        .render = progress_render,
    };
    prg->vtable = &progress_vtable;

    add_to_engine(engine, prg);
    return prg;
}

void yetty_ygui_widget_progress_set_value(struct yetty_ygui_widget *widget, float value)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_PROGRESS) {
        return;
    }
    widget->data.progress.value = ygui_clamp(value, 0, 1);
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

float yetty_ygui_widget_progress_get_value(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_PROGRESS) {
        return 0;
    }
    return widget->data.progress.value;
}

/*=============================================================================
 * Separator Widget
 *===========================================================================*/

static struct yetty_ycore_void_result separator_render(struct yetty_ygui_widget *self,
                                                       struct yetty_ygui_render_ctx *ctx)
{
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, ctx->theme->border,
                                     0);
    return YETTY_OK_VOID();
}

struct yetty_ygui_widget *yetty_ygui_engine_separator(struct yetty_ygui_engine *engine,
                                                      const char *id, float x, float y, float w,
                                                      float h)
{
    struct yetty_ygui_widget *sep =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_SEPARATOR, id);
    if (!sep) {
        return NULL;
    }

    yetty_ygui_widget_init_base(sep, x, y, w, h);
    static const struct yetty_ygui_widget_vtable separator_vtable = {
        .render = separator_render,
    };
    sep->vtable = &separator_vtable;

    add_to_engine(engine, sep);
    return sep;
}

/*=============================================================================
 * Stub implementations for remaining widgets
 * TODO: Implement fully
 *===========================================================================*/

/*=============================================================================
 * TextInput Widget
 *===========================================================================*/

static struct yetty_ycore_void_result textinput_render(struct yetty_ygui_widget *self,
                                                       struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;

    /* Background */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, self->bg_color,
                                     t->radius_small);

    /* Border - accent if focused, normal otherwise */
    uint32_t border_color =
        (self->flags & YETTY_YGUI_FLAG_FOCUSED) ? self->accent_color : t->border;
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h, border_color,
                                             t->radius_small, 1.5f);

    /* Text or placeholder */
    const char *display_text = self->data.textinput.text;
    uint32_t text_color = self->fg_color;

    if (!display_text || display_text[0] == '\0') {
        display_text = self->data.textinput.placeholder;
        text_color = t->text_muted;
    }

    if (display_text) {
        yetty_ygui_render_ctx_render_text(ctx, display_text, self->x + t->pad_large,
                                          self->y + t->pad_medium, text_color, t->font_size);
    }

    /* Cursor if focused */
    if (self->flags & YETTY_YGUI_FLAG_FOCUSED) {
        float cursor_x = self->x + t->pad_large;
        if (self->data.textinput.text) {
            /* Approximate cursor position based on character count */
            cursor_x += self->data.textinput.cursor_pos * (t->font_size * 0.6f);
        }
        float cursor_y = self->y + t->pad_small;
        float cursor_h = self->h - t->pad_small * 2;
        yetty_ygui_render_ctx_render_box(ctx, cursor_x, cursor_y, 2.0f, cursor_h,
                                         self->accent_color, 0);
    }
    return YETTY_OK_VOID();
}

/* Modifier bits — match yetty's GLFW-derived layout (see yetty/yetty.c,
 * tabbar.c). We only key off CTRL for emacs bindings; SHIFT is already
 * applied at the platform layer when emitting CHAR events. */
#define YGUI_MOD_CTRL  0x0002

/* GLFW key codes for the special keys the textinput needs to react to.
 * Inlining the constants avoids pulling GLFW headers into ygui. */
enum {
    YGUI_KEY_ESCAPE    = 256,
    YGUI_KEY_ENTER     = 257,
    YGUI_KEY_TAB       = 258,
    YGUI_KEY_BACKSPACE = 259,
    YGUI_KEY_DELETE    = 261,
    YGUI_KEY_RIGHT     = 262,
    YGUI_KEY_LEFT      = 263,
    YGUI_KEY_HOME      = 268,
    YGUI_KEY_END       = 269,
    YGUI_KEY_A         = 65,
    YGUI_KEY_B         = 66,
    YGUI_KEY_D         = 68,
    YGUI_KEY_E         = 69,
    YGUI_KEY_F         = 70,
    YGUI_KEY_H         = 72,
    YGUI_KEY_K         = 75,
    YGUI_KEY_U         = 85,
    YGUI_KEY_W         = 87,
};

/* Find the start of the word boundary to the left of `pos`. Used by
 * Ctrl+W (kill-previous-word). Skips trailing whitespace, then runs of
 * non-whitespace — same shape as readline's behaviour. */
static int textinput_word_start(const char *text, int pos)
{
    while (pos > 0 && text[pos - 1] == ' ') {
        pos--;
    }
    while (pos > 0 && text[pos - 1] != ' ') {
        pos--;
    }
    return pos;
}

/* Delete the half-open range [from, to) from `text`, in place. Returns
 * the new length; caller must update cursor afterwards. */
static int textinput_delete_range(char *text, int len, int from, int to)
{
    if (from < 0) from = 0;
    if (to > len) to = len;
    if (from >= to) return len;
    memmove(text + from, text + to, len - to + 1 /* NUL */);
    return len - (to - from);
}

static int textinput_on_key(struct yetty_ygui_widget *self, uint32_t key, int mods,
                            ygui_event_t *out)
{
    char *text = self->data.textinput.text;
    int len = text ? (int)strlen(text) : 0;
    int cursor = self->data.textinput.cursor_pos;
    if (cursor < 0) cursor = 0;
    if (cursor > len) cursor = len;
    int handled = 0;

    int ctrl = (mods & YGUI_MOD_CTRL) ? 1 : 0;

    /* Printable text DOES NOT come through here — the platform emits a
     * separate CHAR event (yetty_ygui_engine_text_input). KEY_DOWN is
     * for navigation / editing commands only. Mixing both was the
     * "every letter appears twice, once upper, once lower" bug.
     *
     * We treat Ctrl+letter as an emacs-style chord. ASCII letter keys
     * land here as GLFW codes 65..90 (always uppercase) with mods set;
     * the chord is unambiguous regardless of the shift state. */
    if (ctrl) {
        switch (key) {
        case YGUI_KEY_A:                /* go to start of line */
            cursor = 0; handled = 1; break;
        case YGUI_KEY_E:                /* go to end of line */
            cursor = len; handled = 1; break;
        case YGUI_KEY_B:                /* back one char */
            if (cursor > 0) cursor--;
            handled = 1; break;
        case YGUI_KEY_F:                /* forward one char */
            if (cursor < len) cursor++;
            handled = 1; break;
        case YGUI_KEY_H:                /* backspace */
            if (text && cursor > 0) {
                len = textinput_delete_range(text, len, cursor - 1, cursor);
                cursor--;
            }
            handled = 1; break;
        case YGUI_KEY_D:                /* delete-char-forward */
            if (text && cursor < len) {
                len = textinput_delete_range(text, len, cursor, cursor + 1);
            }
            handled = 1; break;
        case YGUI_KEY_K:                /* kill to end-of-line */
            if (text && cursor < len) {
                len = textinput_delete_range(text, len, cursor, len);
            }
            handled = 1; break;
        case YGUI_KEY_U:                /* kill to start-of-line */
            if (text && cursor > 0) {
                len = textinput_delete_range(text, len, 0, cursor);
                cursor = 0;
            }
            handled = 1; break;
        case YGUI_KEY_W: {              /* kill previous word */
            if (text && cursor > 0) {
                int ws = textinput_word_start(text, cursor);
                len = textinput_delete_range(text, len, ws, cursor);
                cursor = ws;
            }
            handled = 1; break;
        }
        }
    } else {
        switch (key) {
        case YGUI_KEY_BACKSPACE:
        case 8:                          /* ASCII BS (some platforms) */
        case 127:                        /* ASCII DEL (some platforms) */
            if (text && cursor > 0) {
                len = textinput_delete_range(text, len, cursor - 1, cursor);
                cursor--;
            }
            handled = 1; break;
        case YGUI_KEY_DELETE:
            if (text && cursor < len) {
                len = textinput_delete_range(text, len, cursor, cursor + 1);
            }
            handled = 1; break;
        case YGUI_KEY_LEFT:
            if (cursor > 0) cursor--;
            handled = 1; break;
        case YGUI_KEY_RIGHT:
            if (cursor < len) cursor++;
            handled = 1; break;
        case YGUI_KEY_HOME:
            cursor = 0; handled = 1; break;
        case YGUI_KEY_END:
            cursor = len; handled = 1; break;
        }
    }

    if (!handled) {
        return 0;
    }

    self->data.textinput.cursor_pos = cursor;

    if (self->text_callback) {
        self->text_callback(self, text ? text : "", self->text_userdata);
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.string_value = text ? text : "";
    return 1;
}

static void textinput_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.textinput.text);
    free(self->data.textinput.placeholder);
}

struct yetty_ygui_widget *yetty_ygui_engine_textinput(struct yetty_ygui_engine *engine,
                                                      const char *id, float x, float y, float w,
                                                      float h, const char *placeholder)
{
    struct yetty_ygui_widget *txt =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_TEXTINPUT, id);
    if (!txt) {
        return NULL;
    }

    yetty_ygui_widget_init_base(txt, x, y, w, h);
    txt->data.textinput.text = ygui_strdup("");
    txt->data.textinput.placeholder = ygui_strdup(placeholder);
    txt->data.textinput.cursor_pos = 0;
    static const struct yetty_ygui_widget_vtable textinput_vtable = {
        .render = textinput_render,
        .on_key = textinput_on_key,
        .destroy = textinput_destroy,
    };
    txt->vtable = &textinput_vtable;

    add_to_engine(engine, txt);
    return txt;
}

void yetty_ygui_widget_textinput_set_text(struct yetty_ygui_widget *widget, const char *text)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TEXTINPUT) {
        return;
    }
    free(widget->data.textinput.text);
    widget->data.textinput.text = ygui_strdup(text);
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

const char *yetty_ygui_widget_textinput_get_text(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TEXTINPUT) {
        return NULL;
    }
    return widget->data.textinput.text;
}

void yetty_ygui_widget_textinput_set_placeholder(struct yetty_ygui_widget *widget, const char *text)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TEXTINPUT) {
        return;
    }
    free(widget->data.textinput.placeholder);
    widget->data.textinput.placeholder = ygui_strdup(text);
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

/*=============================================================================
 * HBox / VBox — flex containers (row / column).
 *
 * Layout is computed in ygui_layout.c; rendering reuses the default
 * render_all. Theme padding/gap are applied at construction time so
 * existing callers see the same visual behavior they did before.
 *===========================================================================*/

static void box_apply_theme_layout(struct yetty_ygui_widget *box,
                                   const struct yetty_ygui_theme *theme,
                                   ygui_flex_direction_t direction)
{
    box->layout.mode = YETTY_YGUI_LAYOUT_FLEX;
    box->layout.direction = direction;
    box->layout.gap = theme->pad_medium;
    box->layout.padding_top = theme->pad_medium;
    box->layout.padding_right = theme->pad_medium;
    box->layout.padding_bottom = theme->pad_medium;
    box->layout.padding_left = theme->pad_medium;
}

struct yetty_ygui_widget *yetty_ygui_engine_hbox(struct yetty_ygui_engine *engine, const char *id,
                                                 float x, float y, float w, float h)
{
    struct yetty_ygui_widget *hbox =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_HBOX, id);
    if (!hbox) {
        return NULL;
    }
    yetty_ygui_widget_init_base(hbox, x, y, w, h);
    box_apply_theme_layout(hbox, engine->theme, YETTY_YGUI_FLEX_ROW);
    add_to_engine(engine, hbox);
    return hbox;
}

struct yetty_ygui_widget *yetty_ygui_engine_vbox(struct yetty_ygui_engine *engine, const char *id,
                                                 float x, float y, float w, float h)
{
    struct yetty_ygui_widget *vbox =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_VBOX, id);
    if (!vbox) {
        return NULL;
    }
    yetty_ygui_widget_init_base(vbox, x, y, w, h);
    box_apply_theme_layout(vbox, engine->theme, YETTY_YGUI_FLEX_COLUMN);
    add_to_engine(engine, vbox);
    return vbox;
}

/*=============================================================================
 * Dropdown Widget
 *===========================================================================*/

static struct yetty_ycore_void_result dropdown_render(struct yetty_ygui_widget *self,
                                                      struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    int is_open = self->data.dropdown.open;

    /* Low elevation when closed; the open list itself takes medium below. */
    yetty_ygui_render_ctx_render_box_shadow(ctx, self->x, self->y, self->w, self->h,
                                            t->radius_medium, t->elevation_low, t->shadow,
                                            t->elevation_alpha);

    /* Main button area */
    uint32_t bg = (self->flags & YETTY_YGUI_FLAG_HOVER) ? t->bg_hover : self->bg_color;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, bg, t->radius_medium);
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h, t->border,
                                             t->radius_medium, 1.0f);

    /* Selected text */
    const char *selected_text = NULL;
    if (self->data.dropdown.options && self->data.dropdown.selected >= 0 &&
        self->data.dropdown.selected < self->data.dropdown.option_count) {
        selected_text = self->data.dropdown.options[self->data.dropdown.selected];
    }
    if (selected_text) {
        yetty_ygui_render_ctx_render_text(ctx, selected_text, self->x + t->pad_large,
                                          self->y + t->pad_medium, self->fg_color, t->font_size);
    }

    /* Arrow indicator */
    float arrow_x = self->x + self->w - t->pad_large - 8;
    float arrow_y = self->y + self->h / 2;
    if (is_open) {
        /* Up arrow */
        yetty_ygui_render_ctx_render_triangle(ctx, arrow_x, arrow_y + 3, arrow_x + 8, arrow_y + 3,
                                              arrow_x + 4, arrow_y - 3, self->fg_color);
    } else {
        /* Down arrow */
        yetty_ygui_render_ctx_render_triangle(ctx, arrow_x, arrow_y - 3, arrow_x + 8, arrow_y - 3,
                                              arrow_x + 4, arrow_y + 3, self->fg_color);
    }

    /* Dropdown list when open */
    if (is_open && self->data.dropdown.options) {
        float list_y = self->y + self->h + 2;
        float item_h = t->row_height;
        float list_h = self->data.dropdown.option_count * item_h;

        /* List background */
        yetty_ygui_render_ctx_render_box(ctx, self->x, list_y, self->w, list_h, t->bg_surface,
                                         t->radius_medium);
        yetty_ygui_render_ctx_render_box_outline(ctx, self->x, list_y, self->w, list_h, t->border,
                                                 t->radius_medium, 1.0f);

        /* Options */
        for (int i = 0; i < self->data.dropdown.option_count; i++) {
            float opt_y = list_y + i * item_h;
            if (i == self->data.dropdown.selected) {
                yetty_ygui_render_ctx_render_box(ctx, self->x + 2, opt_y + 2, self->w - 4,
                                                 item_h - 4, self->accent_color, t->radius_small);
            }
            yetty_ygui_render_ctx_render_text(ctx, self->data.dropdown.options[i],
                                              self->x + t->pad_large, opt_y + t->pad_small,
                                              self->fg_color, t->font_size);
        }
    }
    return YETTY_OK_VOID();
}

static int dropdown_on_release(struct yetty_ygui_widget *self, float lx, float ly,
                               ygui_event_t *out)
{
    const struct yetty_ygui_theme *t = self->engine->theme;

    if (self->data.dropdown.open) {
        /* Check if clicked on an option */
        float list_y_start = self->h + 2;
        float item_h = t->row_height;

        if (ly >= list_y_start) {
            int idx = (int)((ly - list_y_start) / item_h);
            if (idx >= 0 && idx < self->data.dropdown.option_count) {
                self->data.dropdown.selected = idx;
                out->widget_id = self->id;
                out->type = YETTY_YGUI_EVENT_CHANGE;
                out->data.int_value = idx;
            }
        }
        self->data.dropdown.open = 0;
    } else {
        /* Toggle open */
        if (lx >= 0 && lx < self->w && ly >= 0 && ly < self->h) {
            self->data.dropdown.open = 1;
        }
    }
    return 1;
}

static void dropdown_free_options(struct yetty_ygui_widget *self)
{
    if (self->data.dropdown.options) {
        for (int i = 0; i < self->data.dropdown.option_count; i++) {
            free(self->data.dropdown.options[i]);
        }
        free(self->data.dropdown.options);
        self->data.dropdown.options = NULL;
    }
}

static void dropdown_copy_options(struct yetty_ygui_widget *self, const char **options, int count)
{
    dropdown_free_options(self);
    if (!options || count <= 0) {
        self->data.dropdown.option_count = 0;
        return;
    }
    self->data.dropdown.options = (char **)malloc(count * sizeof(char *));
    if (!self->data.dropdown.options) {
        self->data.dropdown.option_count = 0;
        return;
    }
    for (int i = 0; i < count; i++) {
        self->data.dropdown.options[i] = ygui_strdup(options[i]);
    }
    self->data.dropdown.option_count = count;
}

static void dropdown_destroy(struct yetty_ygui_widget *self)
{
    dropdown_free_options(self);
}

struct yetty_ygui_widget *yetty_ygui_engine_dropdown(struct yetty_ygui_engine *engine,
                                                     const char *id, float x, float y, float w,
                                                     float h, const char **options,
                                                     int option_count)
{
    struct yetty_ygui_widget *dd =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_DROPDOWN, id);
    if (!dd) {
        return NULL;
    }
    yetty_ygui_widget_init_base(dd, x, y, w, h);
    dd->data.dropdown.options = NULL;
    dd->data.dropdown.option_count = 0;
    dd->data.dropdown.selected = 0;
    dd->data.dropdown.open = 0;
    dropdown_copy_options(dd, options, option_count);
    static const struct yetty_ygui_widget_vtable dropdown_vtable = {
        .render = dropdown_render,
        .on_release = dropdown_on_release,
        .destroy = dropdown_destroy,
    };
    dd->vtable = &dropdown_vtable;
    add_to_engine(engine, dd);
    return dd;
}

void yetty_ygui_widget_dropdown_set_options(struct yetty_ygui_widget *widget, const char **options,
                                            int count)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_DROPDOWN) {
        return;
    }
    dropdown_copy_options(widget, options, count);
    if (widget->data.dropdown.selected >= count) {
        widget->data.dropdown.selected = count > 0 ? 0 : -1;
    }
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

void yetty_ygui_widget_dropdown_set_selected(struct yetty_ygui_widget *widget, int index)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_DROPDOWN) {
        return;
    }
    widget->data.dropdown.selected = index;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

int yetty_ygui_widget_dropdown_get_selected(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_DROPDOWN) {
        return 0;
    }
    return widget->data.dropdown.selected;
}

/*=============================================================================
 * ColorPicker Widget
 *===========================================================================*/

/* HSV to RGB conversion */
static void hsv_to_rgb(float h, float s, float v, float *r, float *g, float *b)
{
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }
    h = h - (int)h; /* Wrap to 0-1 */
    if (h < 0) {
        h += 1;
    }
    h *= 6.0f;
    int i = (int)h;
    float f = h - i;
    float p = v * (1 - s);
    float q = v * (1 - s * f);
    float t = v * (1 - s * (1 - f));
    switch (i % 6) {
    case 0:
        *r = v;
        *g = t;
        *b = p;
        break;
    case 1:
        *r = q;
        *g = v;
        *b = p;
        break;
    case 2:
        *r = p;
        *g = v;
        *b = t;
        break;
    case 3:
        *r = p;
        *g = q;
        *b = v;
        break;
    case 4:
        *r = t;
        *g = p;
        *b = v;
        break;
    case 5:
        *r = v;
        *g = p;
        *b = q;
        break;
    }
}

/* RGB to HSV conversion */
static void rgb_to_hsv(float r, float g, float b, float *h, float *s, float *v)
{
    float max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d = max - min;
    *v = max;
    *s = (max == 0) ? 0 : d / max;
    if (d == 0) {
        *h = 0;
    } else if (max == r) {
        *h = (g - b) / d / 6.0f;
        if (*h < 0) {
            *h += 1;
        }
    } else if (max == g) {
        *h = ((b - r) / d + 2) / 6.0f;
    } else {
        *h = ((r - g) / d + 4) / 6.0f;
    }
}

static uint32_t make_color_abgr(float r, float g, float b, float a)
{
    uint8_t ri = (uint8_t)(r * 255);
    uint8_t gi = (uint8_t)(g * 255);
    uint8_t bi = (uint8_t)(b * 255);
    uint8_t ai = (uint8_t)(a * 255);
    return ((uint32_t)ai << 24) | ((uint32_t)bi << 16) | ((uint32_t)gi << 8) | ri;
}

static struct yetty_ycore_void_result colorpicker_render(struct yetty_ygui_widget *self,
                                                         struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float hue = self->data.colorpicker.hue;
    float sat = self->data.colorpicker.sat;
    float val = self->data.colorpicker.val;

    /* Layout: SV gradient on top, hue slider below, preview box on right */
    float hue_bar_h = 20.0f;
    float preview_w = 40.0f;
    float sv_w = self->w - preview_w - t->pad_medium;
    float sv_h = self->h - hue_bar_h - t->pad_medium;

    /* SV gradient area - use color wheel primitive */
    float r, g, b;
    hsv_to_rgb(hue, 1.0f, 1.0f, &r, &g, &b);
    uint32_t hue_color = make_color_abgr(r, g, b, 1.0f);

    /* Background with current hue */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, sv_w, sv_h, hue_color, t->radius_small);

    /* SV indicator */
    float ind_x = self->x + sat * sv_w;
    float ind_y = self->y + (1 - val) * sv_h;
    yetty_ygui_render_ctx_render_circle(ctx, ind_x, ind_y, 6.0f, 0xFFFFFFFF);
    yetty_ygui_render_ctx_render_circle(ctx, ind_x, ind_y, 4.0f, 0xFF000000);

    /* Hue slider bar */
    float hue_y = self->y + sv_h + t->pad_medium;
    yetty_ygui_render_ctx_render_box(ctx, self->x, hue_y, sv_w, hue_bar_h, t->bg_surface,
                                     t->radius_small);

    /* Hue indicator */
    float hue_ind_x = self->x + hue * sv_w;
    yetty_ygui_render_ctx_render_box(ctx, hue_ind_x - 3, hue_y, 6, hue_bar_h, 0xFFFFFFFF,
                                     t->radius_small);

    /* Color preview */
    float preview_x = self->x + sv_w + t->pad_medium;
    hsv_to_rgb(hue, sat, val, &r, &g, &b);
    uint32_t preview_color = make_color_abgr(r, g, b, self->data.colorpicker.alpha);
    yetty_ygui_render_ctx_render_box(ctx, preview_x, self->y, preview_w, self->h, preview_color,
                                     t->radius_medium);
    yetty_ygui_render_ctx_render_box_outline(ctx, preview_x, self->y, preview_w, self->h, t->border,
                                             t->radius_medium, 1.5f);
    return YETTY_OK_VOID();
}

static int colorpicker_on_press(struct yetty_ygui_widget *self, float lx, float ly,
                                ygui_event_t *out)
{
    const struct yetty_ygui_theme *t = self->engine->theme;
    float hue_bar_h = 20.0f;
    float preview_w = 40.0f;
    float sv_w = self->w - preview_w - t->pad_medium;
    float sv_h = self->h - hue_bar_h - t->pad_medium;

    if (ly < sv_h && lx < sv_w) {
        /* Clicked in SV area */
        self->data.colorpicker.sat = ygui_clamp(lx / sv_w, 0, 1);
        self->data.colorpicker.val = ygui_clamp(1 - ly / sv_h, 0, 1);
    } else if (ly >= sv_h + t->pad_medium && lx < sv_w) {
        /* Clicked in hue bar */
        self->data.colorpicker.hue = ygui_clamp(lx / sv_w, 0, 1);
    }

    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    return 1;
}

static int colorpicker_on_drag(struct yetty_ygui_widget *self, float lx, float ly,
                               ygui_event_t *out)
{
    return colorpicker_on_press(self, lx, ly, out);
}

struct yetty_ygui_widget *yetty_ygui_engine_colorpicker(struct yetty_ygui_engine *engine,
                                                        const char *id, float x, float y, float w,
                                                        float h)
{
    struct yetty_ygui_widget *cp =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_COLORPICKER, id);
    if (!cp) {
        return NULL;
    }
    yetty_ygui_widget_init_base(cp, x, y, w, h);
    cp->data.colorpicker.hue = 0;
    cp->data.colorpicker.sat = 1;
    cp->data.colorpicker.val = 1;
    cp->data.colorpicker.alpha = 1;
    static const struct yetty_ygui_widget_vtable colorpicker_vtable = {
        .render = colorpicker_render,
        .on_press = colorpicker_on_press,
        .on_drag = colorpicker_on_drag,
    };
    cp->vtable = &colorpicker_vtable;
    add_to_engine(engine, cp);
    return cp;
}

void yetty_ygui_widget_colorpicker_set_color(struct yetty_ygui_widget *widget, float r, float g,
                                             float b, float a)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_COLORPICKER) {
        return;
    }
    rgb_to_hsv(r, g, b, &widget->data.colorpicker.hue, &widget->data.colorpicker.sat,
               &widget->data.colorpicker.val);
    widget->data.colorpicker.alpha = a;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

void yetty_ygui_widget_colorpicker_get_color(const struct yetty_ygui_widget *widget, float *r,
                                             float *g, float *b, float *a)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_COLORPICKER) {
        if (r) {
            *r = 1;
        }
        if (g) {
            *g = 1;
        }
        if (b) {
            *b = 1;
        }
        if (a) {
            *a = 1;
        }
        return;
    }
    float ri, gi, bi;
    hsv_to_rgb(widget->data.colorpicker.hue, widget->data.colorpicker.sat,
               widget->data.colorpicker.val, &ri, &gi, &bi);
    if (r) {
        *r = ri;
    }
    if (g) {
        *g = gi;
    }
    if (b) {
        *b = bi;
    }
    if (a) {
        *a = widget->data.colorpicker.alpha;
    }
}

/*=============================================================================
 * Popup Widget
 *
 * Modal/non-modal popup window with optional header. Children only render
 * when the popup is open. Press toggles open state.
 *===========================================================================*/

static struct yetty_ycore_void_result popup_render(struct yetty_ygui_widget *self,
                                                   struct yetty_ygui_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_FLAG_OPEN)) {
        return YETTY_OK_VOID();
    }
    const struct yetty_ygui_theme *t = ctx->theme;

    if (self->data.popup.modal) {
        yetty_ygui_render_ctx_render_box(ctx, 0, 0, self->data.popup.scene_w,
                                         self->data.popup.scene_h, t->overlay_modal, 0);
    }

    /* Soft drop shadow (high elevation — popups float above everything). */
    yetty_ygui_render_ctx_render_box_shadow(ctx, self->x, self->y, self->w, self->h,
                                            t->radius_large, t->elevation_high, t->shadow,
                                            t->elevation_alpha);

    /* Body + outline */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, self->bg_color,
                                     t->radius_large);
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h,
                                             self->accent_color, t->radius_large, 2.0f);

    /* Header */
    const char *label = self->data.popup.label;
    if (label && label[0]) {
        uint32_t hdr = self->data.popup.header_color ? self->data.popup.header_color : t->bg_header;
        float hdr_h = t->row_height + t->pad_medium;
        yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, hdr_h, hdr,
                                         t->radius_large);
        yetty_ygui_render_ctx_render_text(ctx, label, self->x + t->pad_large,
                                          self->y + t->pad_large - 2, self->fg_color, t->font_size);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result popup_render_all(struct yetty_ygui_widget *self,
                                                       struct yetty_ygui_render_ctx *ctx)
{
    /* When closed: bail out completely. Earlier this function still
     * set was_rendered = 1 even with OPEN=0, which kept the popup
     * registered in the engine's spatial grid at its rect. The result
     * was "ghost clicks": after the popup closed, any click in its
     * former area routed back to popup_on_press, which toggled OPEN
     * to 1 and made the dialog reappear. Returning early here keeps
     * the popup out of the grid entirely when it's closed. */
    if (!(self->flags & YETTY_YGUI_FLAG_OPEN)) {
        return YETTY_OK_VOID();
    }

    self->effective_x = self->x + ctx->offset_x;
    self->effective_y = self->y + ctx->offset_y;
    self->was_rendered = 1;

    popup_render(self, ctx);

    for (struct yetty_ygui_widget *child = self->first_child; child;
         child = child->next_sibling) {
        if (child->vtable && child->vtable->render_all) {
            child->vtable->render_all(child, ctx);
        } else {
            yetty_ygui_widget_render_all_default(child, ctx);
        }
    }
    return YETTY_OK_VOID();
}

static int popup_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)lx;
    (void)ly;
    /* Modal popups stay open until an explicit close button (or
     * code) clears the OPEN flag. Earlier this toggled OPEN on any
     * body click, including clicks in the margin around children —
     * which dismissed the dialog mid-interaction (you click Close,
     * the click registers on the popup body instead, frame goes
     * away but child widgets — which are usually top-level siblings
     * for positioning reasons — stay visible). We still consume the
     * press so it doesn't leak through to whatever is rendered
     * behind the popup. */
    if (self->data.popup.modal) {
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_PRESS;
        return 1;
    }
    /* Non-modal popups keep the legacy click-anywhere-to-close
     * shorthand — handy for tooltips / dropdown-style transient UI. */
    self->flags ^= YETTY_YGUI_FLAG_OPEN;
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CLICK;
    out->data.bool_value = (self->flags & YETTY_YGUI_FLAG_OPEN) ? 1 : 0;
    return 1;
}

static void popup_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.popup.label);
}

struct yetty_ygui_widget *yetty_ygui_engine_popup(struct yetty_ygui_engine *engine, const char *id,
                                                  float x, float y, float w, float h,
                                                  const char *label)
{
    struct yetty_ygui_widget *p =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_POPUP, id);
    if (!p) {
        return NULL;
    }
    yetty_ygui_widget_init_base(p, x, y, w, h);
    p->data.popup.label = ygui_strdup(label);
    p->data.popup.modal = 0;
    p->data.popup.header_color = 0;
    p->data.popup.scene_w = engine->width;
    p->data.popup.scene_h = engine->height;
    static const struct yetty_ygui_widget_vtable popup_vtable = {
        .render = popup_render,
        .render_all = popup_render_all,
        .on_press = popup_on_press,
        .destroy = popup_destroy,
    };
    p->vtable = &popup_vtable;
    add_to_engine(engine, p);
    return p;
}

void yetty_ygui_widget_popup_set_label(struct yetty_ygui_widget *widget, const char *label)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return;
    }
    free(widget->data.popup.label);
    widget->data.popup.label = ygui_strdup(label);
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

const char *yetty_ygui_widget_popup_get_label(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return NULL;
    }
    return widget->data.popup.label;
}

void yetty_ygui_widget_popup_set_modal(struct yetty_ygui_widget *widget, int modal)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return;
    }
    widget->data.popup.modal = modal ? 1 : 0;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

int yetty_ygui_widget_popup_is_modal(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return 0;
    }
    return widget->data.popup.modal;
}

void yetty_ygui_widget_popup_set_open(struct yetty_ygui_widget *widget, int open)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return;
    }
    if (open) {
        widget->flags |= YETTY_YGUI_FLAG_OPEN;
    } else {
        widget->flags &= ~YETTY_YGUI_FLAG_OPEN;
    }
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

int yetty_ygui_widget_popup_is_open(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return 0;
    }
    return (widget->flags & YETTY_YGUI_FLAG_OPEN) ? 1 : 0;
}

void yetty_ygui_widget_popup_set_scene_size(struct yetty_ygui_widget *widget, float w, float h)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return;
    }
    widget->data.popup.scene_w = w;
    widget->data.popup.scene_h = h;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

void yetty_ygui_widget_popup_set_header_color(struct yetty_ygui_widget *widget, uint32_t color)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return;
    }
    widget->data.popup.header_color = color;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

/*=============================================================================
 * CollapsingHeader Widget
 *
 * Header bar with arrow + label; toggles open on press; when open, lays its
 * children out vertically below the header.
 *===========================================================================*/

static struct yetty_ycore_void_result collapsing_header_render(struct yetty_ygui_widget *self,
                                                               struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, self->bg_color,
                                     t->radius_medium);

    float arrow_size = t->pad_large;
    float arrow_x = self->x + t->pad_large + 2;
    float arrow_y = self->y + self->h * 0.5f;
    if (self->flags & YETTY_YGUI_FLAG_OPEN) {
        /* Down-pointing triangle */
        yetty_ygui_render_ctx_render_triangle(ctx, arrow_x, arrow_y - arrow_size / 3.0f,
                                              arrow_x + arrow_size, arrow_y - arrow_size / 3.0f,
                                              arrow_x + arrow_size / 2, arrow_y + arrow_size / 3.0f,
                                              self->fg_color);
    } else {
        /* Right-pointing triangle */
        yetty_ygui_render_ctx_render_triangle(ctx, arrow_x, arrow_y - arrow_size / 2.0f, arrow_x,
                                              arrow_y + arrow_size / 2.0f,
                                              arrow_x + arrow_size * 0.7f, arrow_y, self->fg_color);
    }

    if (self->data.collapsing_header.label) {
        yetty_ygui_render_ctx_render_text(ctx, self->data.collapsing_header.label,
                                          self->x + arrow_size + t->pad_large * 2 + 2,
                                          self->y + t->pad_medium, self->fg_color, t->font_size);
    }
    if (self->flags & YETTY_YGUI_FLAG_HOVER) {
        yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h,
                                                 self->accent_color, t->radius_medium, 1.5f);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result collapsing_header_render_all(
    struct yetty_ygui_widget *self, struct yetty_ygui_render_ctx *ctx)
{
    self->effective_x = self->x + ctx->offset_x;
    self->effective_y = self->y + ctx->offset_y;
    self->was_rendered = 1;

    collapsing_header_render(self, ctx);

    if (!(self->flags & YETTY_YGUI_FLAG_OPEN)) {
        return YETTY_OK_VOID();
    }

    /* Lay children out vertically below the header */
    float old_offset_x = ctx->offset_x;
    float old_offset_y = ctx->offset_y;
    float y_accum = 0;
    for (struct yetty_ygui_widget *child = self->first_child; child; child = child->next_sibling) {
        if (!(child->flags & YETTY_YGUI_FLAG_VISIBLE)) {
            continue;
        }
        ctx->offset_x = old_offset_x + self->x;
        ctx->offset_y = old_offset_y + self->y + self->h + y_accum;
        if (child->vtable && child->vtable->render_all) {
            child->vtable->render_all(child, ctx);
        } else {
            yetty_ygui_widget_render_all_default(child, ctx);
        }
        y_accum += child->h;
    }
    ctx->offset_x = old_offset_x;
    ctx->offset_y = old_offset_y;
    return YETTY_OK_VOID();
}

static int collapsing_header_on_press(struct yetty_ygui_widget *self, float lx, float ly,
                                      ygui_event_t *out)
{
    (void)lx;
    (void)ly;
    self->flags ^= YETTY_YGUI_FLAG_OPEN;
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CLICK;
    out->data.bool_value = (self->flags & YETTY_YGUI_FLAG_OPEN) ? 1 : 0;
    return 1;
}

static void collapsing_header_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.collapsing_header.label);
}

struct yetty_ygui_widget *yetty_ygui_engine_collapsing_header(struct yetty_ygui_engine *engine,
                                                              const char *id, float x, float y,
                                                              float w, float h, const char *label)
{
    struct yetty_ygui_widget *c =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_COLLAPSING_HEADER, id);
    if (!c) {
        return NULL;
    }
    yetty_ygui_widget_init_base(c, x, y, w, h);
    c->data.collapsing_header.label = ygui_strdup(label);
    static const struct yetty_ygui_widget_vtable collapsing_header_vtable = {
        .render = collapsing_header_render,
        .render_all = collapsing_header_render_all,
        .on_press = collapsing_header_on_press,
        .destroy = collapsing_header_destroy,
    };
    c->vtable = &collapsing_header_vtable;
    add_to_engine(engine, c);
    return c;
}

void yetty_ygui_widget_collapsing_header_set_label(struct yetty_ygui_widget *widget,
                                                   const char *label)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_COLLAPSING_HEADER) {
        return;
    }
    free(widget->data.collapsing_header.label);
    widget->data.collapsing_header.label = ygui_strdup(label);
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

const char *yetty_ygui_widget_collapsing_header_get_label(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_COLLAPSING_HEADER) {
        return NULL;
    }
    return widget->data.collapsing_header.label;
}

void yetty_ygui_widget_collapsing_header_set_open(struct yetty_ygui_widget *widget, int open)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_COLLAPSING_HEADER) {
        return;
    }
    if (open) {
        widget->flags |= YETTY_YGUI_FLAG_OPEN;
    } else {
        widget->flags &= ~YETTY_YGUI_FLAG_OPEN;
    }
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

int yetty_ygui_widget_collapsing_header_is_open(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_COLLAPSING_HEADER) {
        return 0;
    }
    return (widget->flags & YETTY_YGUI_FLAG_OPEN) ? 1 : 0;
}

/*=============================================================================
 * Tooltip Widget
 *===========================================================================*/

static struct yetty_ycore_void_result tooltip_render(struct yetty_ygui_widget *self,
                                                     struct yetty_ygui_render_ctx *ctx)
{
    if (!self->data.tooltip.label || !self->data.tooltip.label[0]) {
        return YETTY_OK_VOID();
    }
    const struct yetty_ygui_theme *t = ctx->theme;
    yetty_ygui_render_ctx_render_box_shadow(ctx, self->x, self->y, self->w, self->h,
                                            t->radius_medium, t->elevation_medium, t->shadow,
                                            t->elevation_alpha);
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, t->tooltip_bg,
                                     t->radius_medium);
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h,
                                             t->border_muted, t->radius_medium, 1.0f);
    yetty_ygui_render_ctx_render_text(ctx, self->data.tooltip.label, self->x + t->pad_large - 2,
                                      self->y + t->pad_medium, self->fg_color, t->font_size);
    return YETTY_OK_VOID();
}

static void tooltip_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.tooltip.label);
}

struct yetty_ygui_widget *yetty_ygui_engine_tooltip(struct yetty_ygui_engine *engine,
                                                    const char *id, float x, float y, float w,
                                                    float h, const char *label)
{
    struct yetty_ygui_widget *tt =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_TOOLTIP, id);
    if (!tt) {
        return NULL;
    }
    yetty_ygui_widget_init_base(tt, x, y, w, h);
    tt->data.tooltip.label = ygui_strdup(label);
    static const struct yetty_ygui_widget_vtable tooltip_vtable = {
        .render = tooltip_render,
        .destroy = tooltip_destroy,
    };
    tt->vtable = &tooltip_vtable;
    add_to_engine(engine, tt);
    return tt;
}

void yetty_ygui_widget_tooltip_set_label(struct yetty_ygui_widget *widget, const char *label)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TOOLTIP) {
        return;
    }
    free(widget->data.tooltip.label);
    widget->data.tooltip.label = ygui_strdup(label);
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

const char *yetty_ygui_widget_tooltip_get_label(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TOOLTIP) {
        return NULL;
    }
    return widget->data.tooltip.label;
}

/*=============================================================================
 * Selectable Widget
 *
 * List item that toggles its checked state on press.
 *===========================================================================*/

static struct yetty_ycore_void_result selectable_render(struct yetty_ygui_widget *self,
                                                        struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    if (self->flags & YETTY_YGUI_FLAG_CHECKED) {
        yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h,
                                         self->accent_color, t->radius_small);
    } else if (self->flags & YETTY_YGUI_FLAG_HOVER) {
        yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, t->bg_hover,
                                         t->radius_small);
    }
    if (self->data.selectable.label) {
        yetty_ygui_render_ctx_render_text(ctx, self->data.selectable.label, self->x + t->pad_large,
                                          self->y + t->pad_medium, self->fg_color, t->font_size);
    }
    return YETTY_OK_VOID();
}

static int selectable_on_press(struct yetty_ygui_widget *self, float lx, float ly,
                               ygui_event_t *out)
{
    (void)lx;
    (void)ly;
    self->flags ^= YETTY_YGUI_FLAG_CHECKED;
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CLICK;
    out->data.bool_value = (self->flags & YETTY_YGUI_FLAG_CHECKED) ? 1 : 0;
    return 1;
}

static void selectable_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.selectable.label);
}

struct yetty_ygui_widget *yetty_ygui_engine_selectable(struct yetty_ygui_engine *engine,
                                                       const char *id, float x, float y, float w,
                                                       float h, const char *label)
{
    struct yetty_ygui_widget *s =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_SELECTABLE, id);
    if (!s) {
        return NULL;
    }
    yetty_ygui_widget_init_base(s, x, y, w, h);
    s->data.selectable.label = ygui_strdup(label);
    static const struct yetty_ygui_widget_vtable selectable_vtable = {
        .render = selectable_render,
        .on_press = selectable_on_press,
        .destroy = selectable_destroy,
    };
    s->vtable = &selectable_vtable;
    add_to_engine(engine, s);
    return s;
}

void yetty_ygui_widget_selectable_set_label(struct yetty_ygui_widget *widget, const char *label)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SELECTABLE) {
        return;
    }
    free(widget->data.selectable.label);
    widget->data.selectable.label = ygui_strdup(label);
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

const char *yetty_ygui_widget_selectable_get_label(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SELECTABLE) {
        return NULL;
    }
    return widget->data.selectable.label;
}

void yetty_ygui_widget_selectable_set_checked(struct yetty_ygui_widget *widget, int checked)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SELECTABLE) {
        return;
    }
    if (checked) {
        widget->flags |= YETTY_YGUI_FLAG_CHECKED;
    } else {
        widget->flags &= ~YETTY_YGUI_FLAG_CHECKED;
    }
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

int yetty_ygui_widget_selectable_is_checked(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SELECTABLE) {
        return 0;
    }
    return (widget->flags & YETTY_YGUI_FLAG_CHECKED) ? 1 : 0;
}

/*=============================================================================
 * ChoiceBox Widget
 *
 * Vertical radio-button list. Press maps localY → option index.
 *===========================================================================*/

static void choicebox_free_options(struct yetty_ygui_widget *self)
{
    if (self->data.choicebox.options) {
        for (int i = 0; i < self->data.choicebox.option_count; i++) {
            free(self->data.choicebox.options[i]);
        }
        free(self->data.choicebox.options);
        self->data.choicebox.options = NULL;
    }
    self->data.choicebox.option_count = 0;
}

static void choicebox_copy_options(struct yetty_ygui_widget *self, const char **options, int count)
{
    choicebox_free_options(self);
    if (!options || count <= 0) {
        return;
    }
    self->data.choicebox.options = (char **)malloc(count * sizeof(char *));
    if (!self->data.choicebox.options) {
        return;
    }
    for (int i = 0; i < count; i++) {
        self->data.choicebox.options[i] = ygui_strdup(options[i]);
    }
    self->data.choicebox.option_count = count;
}

static struct yetty_ycore_void_result choicebox_render(struct yetty_ygui_widget *self,
                                                       struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float opt_h = t->row_height;
    float radio_size = 14.0f;
    float cy = self->y;
    for (int i = 0; i < self->data.choicebox.option_count; i++) {
        int is_selected = (i == self->data.choicebox.selected);
        int is_hovered = (i == self->data.choicebox.hover_index);
        float center_x = self->x + radio_size * 0.5f;
        float center_y = cy + t->pad_medium + radio_size * 0.5f;

        yetty_ygui_render_ctx_render_circle_outline(
            ctx, center_x, center_y, radio_size * 0.5f,
            is_hovered ? self->accent_color : t->border_muted, 1.5f);
        if (is_selected) {
            yetty_ygui_render_ctx_render_circle(ctx, center_x, center_y, radio_size * 0.25f,
                                                self->accent_color);
        } else if (is_hovered) {
            yetty_ygui_render_ctx_render_circle(ctx, center_x, center_y, radio_size / 6.0f,
                                                t->thumb_hover);
        }

        if (self->data.choicebox.options[i]) {
            yetty_ygui_render_ctx_render_text(ctx, self->data.choicebox.options[i],
                                              self->x + radio_size + t->pad_large,
                                              cy + t->pad_medium, self->fg_color, t->font_size);
        }
        cy += opt_h;
    }
    return YETTY_OK_VOID();
}

static int choicebox_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)lx;
    const struct yetty_ygui_theme *t = self->engine->theme;
    float opt_h = t->row_height;
    int idx = (int)(ly / opt_h);
    if (idx < 0 || idx >= self->data.choicebox.option_count) {
        return 0;
    }
    self->data.choicebox.selected = idx;
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.int_value = idx;
    return 1;
}

static void choicebox_destroy(struct yetty_ygui_widget *self)
{
    choicebox_free_options(self);
}

struct yetty_ygui_widget *yetty_ygui_engine_choicebox(struct yetty_ygui_engine *engine,
                                                      const char *id, float x, float y, float w,
                                                      float h, const char **options,
                                                      int option_count)
{
    struct yetty_ygui_widget *c =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_CHOICEBOX, id);
    if (!c) {
        return NULL;
    }
    yetty_ygui_widget_init_base(c, x, y, w, h);
    c->data.choicebox.options = NULL;
    c->data.choicebox.option_count = 0;
    c->data.choicebox.selected = 0;
    c->data.choicebox.hover_index = -1;
    choicebox_copy_options(c, options, option_count);
    static const struct yetty_ygui_widget_vtable choicebox_vtable = {
        .render = choicebox_render,
        .on_press = choicebox_on_press,
        .destroy = choicebox_destroy,
    };
    c->vtable = &choicebox_vtable;
    add_to_engine(engine, c);
    return c;
}

void yetty_ygui_widget_choicebox_set_options(struct yetty_ygui_widget *widget, const char **options,
                                             int count)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_CHOICEBOX) {
        return;
    }
    choicebox_copy_options(widget, options, count);
    if (widget->data.choicebox.selected >= count) {
        widget->data.choicebox.selected = count > 0 ? 0 : -1;
    }
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

void yetty_ygui_widget_choicebox_set_selected(struct yetty_ygui_widget *widget, int index)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_CHOICEBOX) {
        return;
    }
    widget->data.choicebox.selected = index;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

int yetty_ygui_widget_choicebox_get_selected(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_CHOICEBOX) {
        return 0;
    }
    return widget->data.choicebox.selected;
}

/*=============================================================================
 * Scrollbar Widgets (vertical + horizontal)
 *
 * Standalone scrollbar; drag thumb to set value in [0..1].
 *===========================================================================*/

static struct yetty_ycore_void_result vscrollbar_render(struct yetty_ygui_widget *self,
                                                        struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float track_w = self->w > 0 ? self->w : t->scrollbar_size;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, track_w, self->h, t->bg_secondary,
                                     track_w * 0.5f);

    float thumb_h = ygui_max(20.0f, self->h * 0.2f);
    float track_range = self->h - thumb_h;
    float thumb_y = self->y + self->data.scrollbar.value * track_range;
    uint32_t thumb_color =
        (self->flags & YETTY_YGUI_FLAG_PRESSED)
            ? self->accent_color
            : (self->flags & YETTY_YGUI_FLAG_HOVER ? t->thumb_hover : t->thumb_normal);
    yetty_ygui_render_ctx_render_box(ctx, self->x + t->pad_small, thumb_y, track_w - t->pad_medium,
                                     thumb_h, thumb_color, (track_w - t->pad_medium) * 0.5f);
    return YETTY_OK_VOID();
}

static int vscrollbar_update(struct yetty_ygui_widget *self, float ly, ygui_event_t *out)
{
    float thumb_h = ygui_max(20.0f, self->h * 0.2f);
    float track_range = self->h - thumb_h;
    if (track_range <= 0) {
        return 0;
    }
    float pct = (ly - thumb_h * 0.5f) / track_range;
    self->data.scrollbar.value = ygui_clamp(pct, 0.0f, 1.0f);
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.float_value = self->data.scrollbar.value;
    return 1;
}

static int vscrollbar_on_press(struct yetty_ygui_widget *self, float lx, float ly,
                               ygui_event_t *out)
{
    (void)lx;
    return vscrollbar_update(self, ly, out);
}

static int vscrollbar_on_drag(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)lx;
    return vscrollbar_update(self, ly, out);
}

struct yetty_ygui_widget *yetty_ygui_engine_vscrollbar(struct yetty_ygui_engine *engine,
                                                       const char *id, float x, float y, float w,
                                                       float h)
{
    struct yetty_ygui_widget *sb =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_VSCROLLBAR, id);
    if (!sb) {
        return NULL;
    }
    yetty_ygui_widget_init_base(sb, x, y, w, h);
    sb->data.scrollbar.value = 0;
    static const struct yetty_ygui_widget_vtable vscrollbar_vtable = {
        .render = vscrollbar_render,
        .on_press = vscrollbar_on_press,
        .on_drag = vscrollbar_on_drag,
    };
    sb->vtable = &vscrollbar_vtable;
    add_to_engine(engine, sb);
    return sb;
}

static struct yetty_ycore_void_result hscrollbar_render(struct yetty_ygui_widget *self,
                                                        struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float track_h = self->h > 0 ? self->h : t->scrollbar_size;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, track_h, t->bg_secondary,
                                     track_h * 0.5f);

    float thumb_w = ygui_max(20.0f, self->w * 0.2f);
    float track_range = self->w - thumb_w;
    float thumb_x = self->x + self->data.scrollbar.value * track_range;
    uint32_t thumb_color =
        (self->flags & YETTY_YGUI_FLAG_PRESSED)
            ? self->accent_color
            : (self->flags & YETTY_YGUI_FLAG_HOVER ? t->thumb_hover : t->thumb_normal);
    yetty_ygui_render_ctx_render_box(ctx, thumb_x, self->y + t->pad_small, thumb_w,
                                     track_h - t->pad_medium, thumb_color,
                                     (track_h - t->pad_medium) * 0.5f);
    return YETTY_OK_VOID();
}

static int hscrollbar_update(struct yetty_ygui_widget *self, float lx, ygui_event_t *out)
{
    float thumb_w = ygui_max(20.0f, self->w * 0.2f);
    float track_range = self->w - thumb_w;
    if (track_range <= 0) {
        return 0;
    }
    float pct = (lx - thumb_w * 0.5f) / track_range;
    self->data.scrollbar.value = ygui_clamp(pct, 0.0f, 1.0f);
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.float_value = self->data.scrollbar.value;
    return 1;
}

static int hscrollbar_on_press(struct yetty_ygui_widget *self, float lx, float ly,
                               ygui_event_t *out)
{
    (void)ly;
    return hscrollbar_update(self, lx, out);
}

static int hscrollbar_on_drag(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)ly;
    return hscrollbar_update(self, lx, out);
}

struct yetty_ygui_widget *yetty_ygui_engine_hscrollbar(struct yetty_ygui_engine *engine,
                                                       const char *id, float x, float y, float w,
                                                       float h)
{
    struct yetty_ygui_widget *sb =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_HSCROLLBAR, id);
    if (!sb) {
        return NULL;
    }
    yetty_ygui_widget_init_base(sb, x, y, w, h);
    sb->data.scrollbar.value = 0;
    static const struct yetty_ygui_widget_vtable hscrollbar_vtable = {
        .render = hscrollbar_render,
        .on_press = hscrollbar_on_press,
        .on_drag = hscrollbar_on_drag,
    };
    sb->vtable = &hscrollbar_vtable;
    add_to_engine(engine, sb);
    return sb;
}

void yetty_ygui_widget_scrollbar_set_value(struct yetty_ygui_widget *widget, float value)
{
    if (!widget) {
        return;
    }
    if (widget->type != YETTY_YGUI_WIDGET_VSCROLLBAR &&
        widget->type != YETTY_YGUI_WIDGET_HSCROLLBAR) {
        return;
    }
    widget->data.scrollbar.value = ygui_clamp(value, 0.0f, 1.0f);
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

float yetty_ygui_widget_scrollbar_get_value(const struct yetty_ygui_widget *widget)
{
    if (!widget) {
        return 0;
    }
    if (widget->type != YETTY_YGUI_WIDGET_VSCROLLBAR &&
        widget->type != YETTY_YGUI_WIDGET_HSCROLLBAR) {
        return 0;
    }
    return widget->data.scrollbar.value;
}

/*=============================================================================
 * List Widget — generic row-aware vertical container with selection.
 *===========================================================================*/

/* Custom render_all so the selection background lands at the correct
 * absolute position. The default render_all_default calls render() while
 * ctx->offset still points at the *parent's* origin — wrong frame for
 * drawing a box at a *child's* relative coords. We instead push offset
 * to self->layout_x/y first, paint the selection rect, then recurse
 * normally. The list draws nothing else decorative; children render
 * their own surfaces. */
static struct yetty_ycore_void_result list_render_all(struct yetty_ygui_widget *self,
                                                      struct yetty_ygui_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        return YETTY_OK_VOID();
    }
    self->was_rendered = 1;
    const struct yetty_ygui_theme *t = ctx->theme;

    float old_offset_x = ctx->offset_x;
    float old_offset_y = ctx->offset_y;
    ctx->offset_x = self->layout_x;
    ctx->offset_y = self->layout_y;

    /* Selection background — drawn before children so they sit on top. */
    struct yetty_ygui_widget *sel = self->data.list.selected;
    if (sel && (sel->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        yetty_ygui_render_ctx_render_box(ctx, sel->x, sel->y, sel->w, sel->h, t->selection_bg,
                                         t->radius_small);
    }

    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    for (struct yetty_ygui_widget *child = self->first_child; child; child = child->next_sibling) {
        if (!(child->flags & YETTY_YGUI_FLAG_VISIBLE)) {
            continue;
        }
        struct yetty_ycore_void_result r;
        if (child->vtable && child->vtable->render_all) {
            r = child->vtable->render_all(child, ctx);
        } else {
            r = yetty_ygui_widget_render_all_default(child, ctx);
        }
        if (YETTY_IS_ERR(r) && YETTY_IS_OK(first_err)) {
            first_err = r;
        } else if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
    }

    ctx->offset_x = old_offset_x;
    ctx->offset_y = old_offset_y;
    return first_err;
}

/* Find the nearest child that contains (lx, ly) in this widget's local
 * coordinate space. Returns NULL if no hit. */
static struct yetty_ygui_widget *list_child_at(struct yetty_ygui_widget *self, float lx, float ly)
{
    /* Children's x/y are relative to parent (self). */
    for (struct yetty_ygui_widget *c = self->first_child; c; c = c->next_sibling) {
        if (!(c->flags & YETTY_YGUI_FLAG_VISIBLE)) {
            continue;
        }
        if (lx >= c->x && lx < c->x + c->w && ly >= c->y && ly < c->y + c->h) {
            return c;
        }
    }
    return NULL;
}

static int list_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    struct yetty_ygui_widget *child = list_child_at(self, lx, ly);
    if (!child) {
        return 0;
    }
    if (self->data.list.selected != child) {
        self->data.list.selected = child;
        if (self->engine) {
            self->engine->dirty = 1;
        }
    }
    if (self->data.list.on_select) {
        self->data.list.on_select(child, self->data.list.on_select_userdata);
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    return 1;
}

struct yetty_ygui_widget *yetty_ygui_engine_list(struct yetty_ygui_engine *engine, const char *id,
                                                 float x, float y, float w, float h)
{
    struct yetty_ygui_widget *lst =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_LIST, id);
    if (!lst) {
        return NULL;
    }
    yetty_ygui_widget_init_base(lst, x, y, w, h);
    /* Default layout: flex column with theme gap; children stretched on
     * the cross axis so each row spans the list width. */
    lst->layout.mode = YETTY_YGUI_LAYOUT_FLEX;
    lst->layout.direction = YETTY_YGUI_FLEX_COLUMN;
    lst->layout.align_items = YETTY_YGUI_ALIGN_STRETCH;
    lst->layout.gap = engine->theme->pad_small;
    static const struct yetty_ygui_widget_vtable list_vtable = {
        .render_all = list_render_all,
        .on_press = list_on_press,
    };
    lst->vtable = &list_vtable;
    add_to_engine(engine, lst);
    return lst;
}

void yetty_ygui_widget_list_set_selected(struct yetty_ygui_widget *list,
                                         struct yetty_ygui_widget *child)
{
    if (!list || list->type != YETTY_YGUI_WIDGET_LIST) {
        return;
    }
    list->data.list.selected = child;
    if (list->engine) {
        list->engine->dirty = 1;
    }
}

struct yetty_ygui_widget *yetty_ygui_widget_list_get_selected(const struct yetty_ygui_widget *list)
{
    if (!list || list->type != YETTY_YGUI_WIDGET_LIST) {
        return NULL;
    }
    return list->data.list.selected;
}

void yetty_ygui_widget_list_on_select(struct yetty_ygui_widget *list, ygui_click_callback_t cb,
                                      void *userdata)
{
    if (!list || list->type != YETTY_YGUI_WIDGET_LIST) {
        return;
    }
    list->data.list.on_select = cb;
    list->data.list.on_select_userdata = userdata;
}

/*=============================================================================
 * Table Widget — header row + N×M cell grid of plain strings.
 *
 * Self-rendered (no child widgets). Cells own their string copies and are
 * freed on destroy / clear_rows. Click on a data row fires on_select(row).
 *===========================================================================*/

static char *strdup_or_null(const char *s)
{
    if (!s) {
        return NULL;
    }
    size_t n = strlen(s);
    char *r = malloc(n + 1);
    if (!r) {
        return NULL;
    }
    memcpy(r, s, n + 1);
    return r;
}

static void table_free_row(char **row, int n_cells)
{
    if (!row) {
        return;
    }
    for (int c = 0; c < n_cells; c++) {
        free(row[c]);
    }
    free(row);
}

static char **table_dup_row(const char *const *cells, int n_cells)
{
    char **row = calloc((size_t)n_cells, sizeof(char *));
    if (!row) {
        return NULL;
    }
    for (int c = 0; c < n_cells; c++) {
        row[c] = strdup_or_null(cells[c] ? cells[c] : "");
    }
    return row;
}

/* Resolved per-column widths in pixels. Stretch columns share the leftover
 * space evenly. Caller provides an out array sized n_columns. */
static void table_resolve_widths(const struct yetty_ygui_widget *self, float *out)
{
    int n = self->data.table.n_columns;
    if (n <= 0) {
        return;
    }
    float total_fixed = 0.0f;
    int stretch_count = 0;
    for (int c = 0; c < n; c++) {
        float w = self->data.table.column_widths[c];
        if (w > 0.0f) {
            total_fixed += w;
        } else {
            stretch_count++;
        }
    }
    float stretch_w = 0.0f;
    if (stretch_count > 0) {
        float leftover = self->w - total_fixed;
        if (leftover < 0.0f) {
            leftover = 0.0f;
        }
        stretch_w = leftover / (float)stretch_count;
    }
    for (int c = 0; c < n; c++) {
        float w = self->data.table.column_widths[c];
        out[c] = (w > 0.0f) ? w : stretch_w;
    }
}

static struct yetty_ycore_void_result table_render(struct yetty_ygui_widget *self,
                                                   struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    int n = self->data.table.n_columns;
    if (n <= 0) {
        /* No columns set yet — just paint the surface and bail. */
        yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, t->bg_surface,
                                         t->radius_small);
        return YETTY_OK_VOID();
    }
    float row_h = self->data.table.row_height > 0.0f ? self->data.table.row_height : t->row_height;
    if (row_h <= 0.0f) {
        row_h = 24.0f;
    }
    float font_size = t->font_size > 0.0f ? t->font_size : 12.0f;
    float text_pad_x = 6.0f;
    float text_y_off = (row_h - font_size) * 0.5f;

    float widths[64]; /* practical cap; heroic tables can grow this */
    if (n > (int)(sizeof(widths) / sizeof(widths[0]))) {
        n = (int)(sizeof(widths) / sizeof(widths[0]));
    }
    table_resolve_widths(self, widths);

    /* Surface. */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, t->bg_surface,
                                     t->radius_small);

    /* Header row. */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, row_h, t->bg_header,
                                     t->radius_small);
    {
        float cx = self->x;
        for (int c = 0; c < n; c++) {
            const char *name =
                self->data.table.column_names[c] ? self->data.table.column_names[c] : "";
            yetty_ygui_render_ctx_render_text(ctx, name, cx + text_pad_x, self->y + text_y_off,
                                              t->text_primary, font_size);
            cx += widths[c];
            /* Vertical separator after every column except the last. */
            if (c < n - 1) {
                yetty_ygui_render_ctx_render_box(ctx, cx, self->y, 1.0f, self->h, t->border_muted,
                                                 0.0f);
            }
        }
    }
    /* Header / data divider. */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y + row_h, self->w, 1.0f, t->border, 0.0f);

    /* Data rows. */
    int rows = self->data.table.n_rows;
    int sel = self->data.table.selected_row;
    for (int r = 0; r < rows; r++) {
        float row_y = self->y + row_h + (float)r * row_h;
        if (row_y + row_h > self->y + self->h) {
            /* Clip — table is too short for this row. Stop drawing. */
            break;
        }
        if (r == sel) {
            yetty_ygui_render_ctx_render_box(ctx, self->x, row_y, self->w, row_h, t->selection_bg,
                                             0.0f);
        } else if ((r & 1) == 1 && t->bg_secondary != 0u) {
            /* Zebra striping for odd-indexed rows when the theme provides
             * a distinct secondary surface. */
            yetty_ygui_render_ctx_render_box(ctx, self->x, row_y, self->w, row_h, t->bg_secondary,
                                             0.0f);
        }
        float cx = self->x;
        char **row = self->data.table.rows[r];
        for (int c = 0; c < n; c++) {
            const char *txt = row ? row[c] : NULL;
            if (txt) {
                yetty_ygui_render_ctx_render_text(ctx, txt, cx + text_pad_x, row_y + text_y_off,
                                                  t->text_primary, font_size);
            }
            cx += widths[c];
        }
    }

    /* Outer border last so it sits above the rows. */
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h, t->border,
                                             t->radius_small, 1.0f);
    return YETTY_OK_VOID();
}

static int table_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    const struct yetty_ygui_theme *t = self->engine ? self->engine->theme : NULL;
    if (!t) {
        return 0;
    }
    float row_h = self->data.table.row_height > 0.0f ? self->data.table.row_height : t->row_height;
    if (row_h <= 0.0f) {
        row_h = 24.0f;
    }
    /* Header row swallows clicks but doesn't change selection. Future:
     * sortable headers. */
    if (ly < row_h) {
        return 0;
    }
    int row = (int)((ly - row_h) / row_h);
    if (row < 0 || row >= self->data.table.n_rows) {
        return 0;
    }
    if (self->data.table.selected_row != row) {
        self->data.table.selected_row = row;
        if (self->engine) {
            self->engine->dirty = 1;
        }
    }
    if (self->data.table.on_select) {
        self->data.table.on_select(self, row, self->data.table.on_select_userdata);
    }
    if (out) {
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_CHANGE;
    }
    return 1;
}

static void table_destroy(struct yetty_ygui_widget *self)
{
    int n_cols = self->data.table.n_columns;
    for (int r = 0; r < self->data.table.n_rows; r++) {
        table_free_row(self->data.table.rows[r], n_cols);
    }
    free(self->data.table.rows);
    for (int c = 0; c < n_cols; c++) {
        free(self->data.table.column_names[c]);
    }
    free(self->data.table.column_names);
    free(self->data.table.column_widths);
    self->data.table.rows = NULL;
    self->data.table.column_names = NULL;
    self->data.table.column_widths = NULL;
    self->data.table.n_columns = 0;
    self->data.table.n_rows = 0;
    self->data.table.row_capacity = 0;
}

struct yetty_ygui_widget *yetty_ygui_engine_table(struct yetty_ygui_engine *engine, const char *id,
                                                  float x, float y, float w, float h)
{
    struct yetty_ygui_widget *tbl =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_TABLE, id);
    if (!tbl) {
        return NULL;
    }
    yetty_ygui_widget_init_base(tbl, x, y, w, h);
    tbl->data.table.selected_row = -1;
    static const struct yetty_ygui_widget_vtable table_vtable = {
        .render = table_render,
        .on_press = table_on_press,
        .destroy = table_destroy,
    };
    tbl->vtable = &table_vtable;
    add_to_engine(engine, tbl);
    return tbl;
}

void yetty_ygui_widget_table_set_columns(struct yetty_ygui_widget *table, const char *const *names,
                                         const float *widths, int n_columns)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE || n_columns <= 0) {
        return;
    }
    /* Wipe existing column metadata + any rows (cell counts must match). */
    int old_cols = table->data.table.n_columns;
    for (int r = 0; r < table->data.table.n_rows; r++) {
        table_free_row(table->data.table.rows[r], old_cols);
    }
    free(table->data.table.rows);
    table->data.table.rows = NULL;
    table->data.table.n_rows = 0;
    table->data.table.row_capacity = 0;
    for (int c = 0; c < old_cols; c++) {
        free(table->data.table.column_names[c]);
    }
    free(table->data.table.column_names);
    free(table->data.table.column_widths);

    table->data.table.column_names = calloc((size_t)n_columns, sizeof(char *));
    table->data.table.column_widths = calloc((size_t)n_columns, sizeof(float));
    if (!table->data.table.column_names || !table->data.table.column_widths) {
        free(table->data.table.column_names);
        free(table->data.table.column_widths);
        table->data.table.column_names = NULL;
        table->data.table.column_widths = NULL;
        table->data.table.n_columns = 0;
        return;
    }
    for (int c = 0; c < n_columns; c++) {
        table->data.table.column_names[c] = strdup_or_null(names && names[c] ? names[c] : "");
        table->data.table.column_widths[c] = widths ? widths[c] : 0.0f;
    }
    table->data.table.n_columns = n_columns;
    if (table->engine) {
        table->engine->dirty = 1;
    }
}

void yetty_ygui_widget_table_clear_rows(struct yetty_ygui_widget *table)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return;
    }
    for (int r = 0; r < table->data.table.n_rows; r++) {
        table_free_row(table->data.table.rows[r], table->data.table.n_columns);
        table->data.table.rows[r] = NULL;
    }
    table->data.table.n_rows = 0;
    table->data.table.selected_row = -1;
    if (table->engine) {
        table->engine->dirty = 1;
    }
}

void yetty_ygui_widget_table_add_row(struct yetty_ygui_widget *table, const char *const *cells,
                                     int n_cells)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return;
    }
    if (n_cells != table->data.table.n_columns) {
        return; /* row arity must match column count */
    }
    /* Grow the rows array if needed. */
    if (table->data.table.n_rows >= table->data.table.row_capacity) {
        int new_cap = table->data.table.row_capacity > 0 ? table->data.table.row_capacity * 2 : 8;
        char ***bigger = realloc(table->data.table.rows, (size_t)new_cap * sizeof(char **));
        if (!bigger) {
            return;
        }
        for (int i = table->data.table.row_capacity; i < new_cap; i++) {
            bigger[i] = NULL;
        }
        table->data.table.rows = bigger;
        table->data.table.row_capacity = new_cap;
    }
    char **row = table_dup_row(cells, n_cells);
    if (!row) {
        return;
    }
    table->data.table.rows[table->data.table.n_rows++] = row;
    if (table->engine) {
        table->engine->dirty = 1;
    }
}

void yetty_ygui_widget_table_set_row(struct yetty_ygui_widget *table, int row,
                                     const char *const *cells, int n_cells)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return;
    }
    if (row < 0 || row >= table->data.table.n_rows) {
        return;
    }
    if (n_cells != table->data.table.n_columns) {
        return;
    }
    char **new_row = table_dup_row(cells, n_cells);
    if (!new_row) {
        return;
    }
    table_free_row(table->data.table.rows[row], n_cells);
    table->data.table.rows[row] = new_row;
    if (table->engine) {
        table->engine->dirty = 1;
    }
}

int yetty_ygui_widget_table_row_count(const struct yetty_ygui_widget *table)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return 0;
    }
    return table->data.table.n_rows;
}

void yetty_ygui_widget_table_set_selected(struct yetty_ygui_widget *table, int row)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return;
    }
    if (row < -1 || row >= table->data.table.n_rows) {
        return;
    }
    table->data.table.selected_row = row;
    if (table->engine) {
        table->engine->dirty = 1;
    }
}

int yetty_ygui_widget_table_get_selected(const struct yetty_ygui_widget *table)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return -1;
    }
    return table->data.table.selected_row;
}

void yetty_ygui_widget_table_on_select(struct yetty_ygui_widget *table,
                                       yetty_ygui_table_select_fn cb, void *userdata)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return;
    }
    table->data.table.on_select = cb;
    table->data.table.on_select_userdata = userdata;
}

void yetty_ygui_widget_table_set_row_height(struct yetty_ygui_widget *table, float h)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return;
    }
    table->data.table.row_height = h;
    if (table->engine) {
        table->engine->dirty = 1;
    }
}

/*=============================================================================
 * Tree Node Widget — chevron + label header + auto-allocated children list.
 *===========================================================================*/

#define TREE_CHEVRON_W 16.0f
#define TREE_CHEVRON_PAD 4.0f
#define TREE_INDENT_DEFAULT 20.0f

/* Header height scales with the theme's row_height. The pre-flight in
 * ygui_layout.c also queries this, so it must work without a render
 * context — caller supplies the theme. */
static float tree_node_header_h(const struct yetty_ygui_widget *self,
                                const struct yetty_ygui_theme *theme)
{
    (void)self;
    return theme ? theme->row_height : 24.0f;
}

static struct yetty_ycore_void_result tree_node_render(struct yetty_ygui_widget *self,
                                                       struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    int expanded = self->data.tree_node.expanded;
    int hovered = (self->flags & YETTY_YGUI_FLAG_HOVER) != 0;
    int pressed = (self->flags & YETTY_YGUI_FLAG_PRESSED) != 0;

    float header_h = tree_node_header_h(self, t);

    if (hovered || pressed) {
        uint32_t bg = pressed ? t->bg_header : t->bg_hover;
        yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, header_h, bg,
                                         t->radius_small);
    }

    /* Chevron — always rendered. tree_node represents a folder; whether
     * it currently has children loaded is irrelevant (lazy loading is
     * common). The triangle scales gently with header height. */
    float cx = self->x + TREE_CHEVRON_PAD;
    float cy = self->y + header_h * 0.5f;
    float r = header_h * 0.18f;
    if (r < 4.0f) {
        r = 4.0f;
    }
    if (expanded) {
        yetty_ygui_render_ctx_render_triangle(ctx, cx, cy - r * 0.6f, cx + r * 2.0f, cy - r * 0.6f,
                                              cx + r, cy + r * 0.8f, t->text_primary);
    } else {
        yetty_ygui_render_ctx_render_triangle(ctx, cx, cy - r, cx, cy + r, cx + r * 1.2f, cy,
                                              t->text_primary);
    }

    /* Label */
    if (self->data.tree_node.label) {
        float label_x = self->x + TREE_CHEVRON_W + TREE_CHEVRON_PAD;
        float label_y = self->y + (header_h - t->font_size) * 0.5f;
        yetty_ygui_render_ctx_render_text(ctx, self->data.tree_node.label, label_x, label_y,
                                          self->fg_color, t->font_size);
    }
    return YETTY_OK_VOID();
}

/* tree_node has two layout sections: a fixed-height header row plus the
 * children list. Easiest way to express that with our flex engine is to
 * make tree_node itself a flex column where:
 *   - the header is "implicit" (rendered by tree_node_render at y=0 with
 *     a fixed height TREE_HEADER_H_DEFAULT — the layout pass sees the
 *     header as the widget's own first content_h slot)
 *   - the children list lives at y = header_h
 *
 * To get the children list to sit below the header, we lay it out
 * manually here in a custom render_all (similar to panel_render_all).
 * The children list is the only child widget; we render the header
 * background/chevron/label first, then recurse into the list. */
static struct yetty_ycore_void_result tree_node_render_all(struct yetty_ygui_widget *self,
                                                           struct yetty_ygui_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        return YETTY_OK_VOID();
    }
    self->was_rendered = 1;

    /* Always render the header. */
    struct yetty_ycore_void_result first_err = tree_node_render(self, ctx);
    if (YETTY_IS_ERR(first_err)) {
        return first_err;
    }

    /* Render the children list below the header when expanded. The
     * layout pass already placed children_list at the right position
     * because we set its authored x/y in the constructor. */
    struct yetty_ygui_widget *kids = self->data.tree_node.children_list;
    if (!kids || !self->data.tree_node.expanded) {
        return YETTY_OK_VOID();
    }
    if (!(kids->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        return YETTY_OK_VOID();
    }

    /* Push offset so kids->x/y are interpreted relative to self. */
    float old_offset_x = ctx->offset_x;
    float old_offset_y = ctx->offset_y;
    ctx->offset_x = self->layout_x;
    ctx->offset_y = self->layout_y;
    struct yetty_ycore_void_result r;
    if (kids->vtable && kids->vtable->render_all) {
        r = kids->vtable->render_all(kids, ctx);
    } else {
        r = yetty_ygui_widget_render_all_default(kids, ctx);
    }
    ctx->offset_x = old_offset_x;
    ctx->offset_y = old_offset_y;
    return r;
}

static int tree_node_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    const struct yetty_ygui_theme *theme = self->engine ? self->engine->theme : NULL;
    float header_h = tree_node_header_h(self, theme);
    int on_chevron = (lx <= TREE_CHEVRON_W + TREE_CHEVRON_PAD) && (ly <= header_h);
    int on_header = (ly <= header_h);

    /* tree_node represents a folder. The chevron always toggles, even
     * if children haven't been loaded yet (lazy expansion: on_toggle
     * fires and the user populates inside the callback). */
    if (on_chevron) {
        self->data.tree_node.expanded = !self->data.tree_node.expanded;
        if (self->data.tree_node.children_list) {
            yetty_ygui_widget_set_visible(self->data.tree_node.children_list,
                                          self->data.tree_node.expanded);
        }
        if (self->data.tree_node.on_toggle) {
            self->data.tree_node.on_toggle(self, self->data.tree_node.expanded,
                                           self->data.tree_node.on_toggle_userdata);
        }
        if (self->engine) {
            self->engine->dirty = 1;
        }
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_CHANGE;
        return 1;
    }
    if (on_header) {
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_PRESS;
        return 1;
    }
    return 0;
}

static void tree_node_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.tree_node.label);
    /* children_list is in the regular child widget hierarchy; the
     * engine's recursive destroy handles it. Nothing to do here. */
}

struct yetty_ygui_widget *yetty_ygui_engine_tree_node(struct yetty_ygui_engine *engine,
                                                      const char *id, const char *label)
{
    struct yetty_ygui_widget *node =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_TREE_NODE, id);
    if (!node) {
        return NULL;
    }
    /* Authored size: full width is filled by parent flex (align: stretch).
     * Height: just the header at construction; the pre-flight in
     * ygui_layout.c grows authored_h on every layout pass to fit the
     * currently-visible children. We use FLEX/COLUMN so the layout
     * places the children list directly below the header. */
    float header_h = engine && engine->theme ? engine->theme->row_height : 24.0f;
    yetty_ygui_widget_init_base(node, 0, 0, 200.0f, header_h);
    node->data.tree_node.label = ygui_strdup(label);
    node->data.tree_node.expanded = 0;
    node->data.tree_node.children_list = NULL;

    /* Layout: flex column. The header occupies the top `header_h`
     * pixels (rendered by tree_node_render at y=0). The children list
     * lives in the content box thanks to padding_top = header_h. */
    node->layout.mode = YETTY_YGUI_LAYOUT_FLEX;
    node->layout.direction = YETTY_YGUI_FLEX_COLUMN;
    node->layout.align_items = YETTY_YGUI_ALIGN_STRETCH;
    node->layout.padding_top = header_h;

    static const struct yetty_ygui_widget_vtable tree_node_vtable = {
        .render = tree_node_render,
        .render_all = tree_node_render_all,
        .on_press = tree_node_on_press,
        .destroy = tree_node_destroy,
    };
    node->vtable = &tree_node_vtable;

    /* Auto-allocate the children list. It's added as a normal child
     * widget — the layout pass places it inside the content box (below
     * the header thanks to padding_top), and it's hidden by default
     * (expanded = 0). */
    char child_id[256];
    snprintf(child_id, sizeof(child_id), "%s.children", id ? id : "tree_node");
    struct yetty_ygui_widget *kids = yetty_ygui_engine_list(engine, child_id, 0, 0, 0, 0);
    if (kids) {
        /* Indent: CSS padding-left on the children list. Users can
         * override with apply_css. */
        kids->layout.padding_left = TREE_INDENT_DEFAULT;
        /* Grow to fill whatever the layout pre-flight reserves for us
         * inside the tree_node's content box (everything below the
         * padding_top header strip). */
        kids->layout.flex_grow = 1.0f;
        yetty_ygui_widget_set_visible(kids, 0);
        yetty_ygui_widget_add_child(node, kids);
        node->data.tree_node.children_list = kids;
    }

    add_to_engine(engine, node);
    return node;
}

void yetty_ygui_widget_tree_node_set_label(struct yetty_ygui_widget *node, const char *label)
{
    if (!node || node->type != YETTY_YGUI_WIDGET_TREE_NODE) {
        return;
    }
    free(node->data.tree_node.label);
    node->data.tree_node.label = ygui_strdup(label);
    if (node->engine) {
        node->engine->dirty = 1;
    }
}

const char *yetty_ygui_widget_tree_node_get_label(const struct yetty_ygui_widget *node)
{
    if (!node || node->type != YETTY_YGUI_WIDGET_TREE_NODE) {
        return NULL;
    }
    return node->data.tree_node.label;
}

void yetty_ygui_widget_tree_node_set_expanded(struct yetty_ygui_widget *node, int expanded)
{
    if (!node || node->type != YETTY_YGUI_WIDGET_TREE_NODE) {
        return;
    }
    node->data.tree_node.expanded = expanded ? 1 : 0;
    if (node->data.tree_node.children_list) {
        yetty_ygui_widget_set_visible(node->data.tree_node.children_list, expanded);
    }
    if (node->engine) {
        node->engine->dirty = 1;
    }
}

int yetty_ygui_widget_tree_node_is_expanded(const struct yetty_ygui_widget *node)
{
    if (!node || node->type != YETTY_YGUI_WIDGET_TREE_NODE) {
        return 0;
    }
    return node->data.tree_node.expanded;
}

struct yetty_ygui_widget *yetty_ygui_widget_tree_node_children(struct yetty_ygui_widget *node)
{
    if (!node || node->type != YETTY_YGUI_WIDGET_TREE_NODE) {
        return NULL;
    }
    return node->data.tree_node.children_list;
}

void yetty_ygui_widget_tree_node_on_toggle(struct yetty_ygui_widget *node, ygui_check_callback_t cb,
                                           void *userdata)
{
    if (!node || node->type != YETTY_YGUI_WIDGET_TREE_NODE) {
        return;
    }
    node->data.tree_node.on_toggle = cb;
    node->data.tree_node.on_toggle_userdata = userdata;
}
