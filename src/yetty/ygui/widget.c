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
 * consume are defined directly in this TU. Codegen sees these definitions
 * too, and because the exposed `yetty_ygui_layout_default()` returns
 * `struct yetty_ygui_layout` by value, codegen reproduces the struct (and the
 * flex enums it embeds) into the generated widget.h for downstream modules —
 * no duplicate header-destined block to keep in sync. */
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

/* Per-child cross-axis alignment override. AUTO (the zero default, so a
 * zero-initialized or defaulted layout inherits) means "use the parent's
 * align"; the rest mirror yetty_ygui_flex_align. */
enum yetty_ygui_flex_align_self {
    YETTY_YGUI_ALIGN_SELF_AUTO = 0,
    YETTY_YGUI_ALIGN_SELF_START,
    YETTY_YGUI_ALIGN_SELF_CENTER,
    YETTY_YGUI_ALIGN_SELF_END,
    YETTY_YGUI_ALIGN_SELF_STRETCH,
};

/* Single-line vs multi-line flow. NOWRAP (the zero default) keeps the
 * original single-line algorithm; WRAP breaks children onto new lines when
 * they overflow the container's main axis. */
enum yetty_ygui_flex_wrap {
    YETTY_YGUI_WRAP_NOWRAP = 0,
    YETTY_YGUI_WRAP_WRAP,
};

struct yetty_ygui_layout {
    enum yetty_ygui_flex_direction direction;
    enum yetty_ygui_flex_justify justify;
    enum yetty_ygui_flex_align align;
    enum yetty_ygui_flex_align_self align_self;
    enum yetty_ygui_flex_wrap wrap;
    float gap;
    float padding_top;
    float padding_right;
    float padding_bottom;
    float padding_left;
    float margin_top;
    float margin_right;
    float margin_bottom;
    float margin_left;
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

/* The flex layout pass is defined in layout.c — a non-class TU that produces
 * no header of its own — but its public declaration belongs to the widget API,
 * so expose it from here (codegen emits the prototype into widget.h). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_layout_compute(struct yetty_yclass_object *root,
                                                         struct yetty_ycore_rectangle root_rect);

/* The class accessor is defined in the appended widget.gen.c; forward-
 * declared here because the helpers above the foot include reference it. */
struct yetty_yclass_ptr_result yetty_ygui_widget_class_get(void);

/*===========================================================================
 * Widget per-class data slice.
 *=========================================================================*/

struct YETTY_ANNOTATE("class@ygui:widget") yetty_ygui_widget {
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

    /* ---- Widget tree + per-widget framework state (flat members of the
     * widget base data; reached by other ygui .c through the accessors
     * below, never poked directly). ---- */

    struct yetty_yclass_object *parent;
    /* Sibling links inside parent's first_child list. */
    struct yetty_yclass_object *first_child;
    struct yetty_yclass_object *next_sibling;
    /* Wire id allocated by the framework at construction. 0 = unassigned. */
    uint32_t id;
    /* Figure-boundary marker. When non-zero this widget is emitted as its
     * OWN receiver-side child figure of this kind (a separate yfigure
     * container child) instead of inlining its prims into the shared chrome
     * ygrid. 0 = inline (the default). Set via yetty_ygui_widget_make_figure. */
    uint32_t figure_kind;
    /* Stacking order for this widget's figure (only meaningful when
     * figure_kind != 0). Emitted as SET_CHILD_Z. */
    int32_t figure_z;
    /* Floating overlay (dialog / debug window): a press inside it moves it to
     * the end of its parent's child list (paints last, wins hit-test). */
    int floating;
    /* Dirty flag — content changed without geometry move. */
    int dirty;
    /* Hover state — set by the framework's pointer-tracking pass when this is
     * the deepest hit; cleared when the mouse leaves. */
    int hovered;
    /* framework object that owns this widget tree. Stored only on the root;
     * children resolve via parent walk through yetty_ygui_widget_framework. */
    struct yetty_yclass_object *framework;
    /* Event subscriptions — singly-linked list, freed at object destroy. */
    struct yetty_ygui_event_subscription *subscriptions;
};

/* The codegen accessor/downcast defined in the appended widget.gen.c,
 * declared here (this TU does not include its own generated widget.h) so the
 * helpers + exposed widget API below have it in scope. The generated
 * widget.h publishes the identical declaration for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_widget_ptr, struct yetty_ygui_widget *);
struct yetty_ygui_widget_ptr_result yetty_ygui_widget_from(struct yetty_yclass_object *obj);

/* Result wrapper for the layout getter below. File-local: no shared header
 * carries it (the rectangle / framework / object-pointer / scalar results come
 * from <yetty/ycore/types.h>, <yetty/ygui/framework.h>, <yetty/yclass/class.h>
 * and <yetty/ycore/result.h> respectively, all pulled in via internal.h; the
 * event-subscription result lives in internal.h alongside its struct). */
YETTY_YRESULT_DECLARE(yetty_ygui_layout_const_ptr, const struct yetty_ygui_layout *);

/* The module constructor/destructor slot stubs (generated in the appended
 * widget.gen.c). widget.c calls them in widget_instantiate / _destroy above
 * that foot include, and can't include its own widget.h, so forward-declare
 * them here. */
struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_object *obj);

/* The pointer-event slot stubs (generated in the appended widget.gen.c). The
 * typed super/mixin helpers below need their addresses as the slot method ids
 * and their exact signatures for the dispatch cast; widget.c can't include its
 * own widget.h, so forward-declare them here (identical to the generated
 * declarations). */
struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_object *obj, float x,
                                                         float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_object *obj, float x,
                                                           float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_object *obj, float x,
                                                          float y);
struct yetty_ycore_int_result yetty_ygui_widget_on_scroll(struct yetty_yclass_object *obj, float x,
                                                          float y, float dx, float dy);

/* Defined lower in this file (the widget tree/lifecycle block), but the
 * geometry setters above it mark the widget dirty — forward-declare. */
struct yetty_ycore_void_result yetty_ygui_widget_set_dirty(struct yetty_yclass_object *obj);

/* The widget base-data slice (the flat tree/framework fields) is reached
 * through yetty_ygui_widget_from(obj), whose Result is checked and propagated
 * at every call site — there is no error-absorbing funnel. widget.c owns the
 * struct so it touches fields directly through the unwrapped pointer; other
 * ygui .c go through the exposed accessor functions below. */

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
 * yclass slot signature `(struct yetty_yclass_object *, …)` — methods take
 * no ctx; the RPC session is read from the object (obj->session).
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

YETTY_ANNOTATE("virtual@ygui:widget:constructor")
static struct yetty_ycore_void_result widget_default_constructor(struct yetty_yclass_object *obj)
{
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

YETTY_ANNOTATE("virtual@ygui:widget:destructor")
static struct yetty_ycore_void_result widget_default_destructor(struct yetty_yclass_object *obj)
{
    (void)obj;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@ygui:widget:widget_on_press")
static struct yetty_ycore_int_result widget_default_on_press(struct yetty_yclass_object *obj,
                                                             float x, float y, int button)
{
    (void)obj;
    (void)x;
    (void)y;
    (void)button;
    return YETTY_OK(yetty_ycore_int, 0);
}

YETTY_ANNOTATE("virtual@ygui:widget:widget_on_release")
static struct yetty_ycore_int_result widget_default_on_release(struct yetty_yclass_object *obj,
                                                               float x, float y, int button)
{
    (void)obj;
    (void)x;
    (void)y;
    (void)button;
    return YETTY_OK(yetty_ycore_int, 0);
}

YETTY_ANNOTATE("virtual@ygui:widget:widget_on_motion")
static struct yetty_ycore_int_result widget_default_on_motion(struct yetty_yclass_object *obj,
                                                              float x, float y)
{
    (void)obj;
    (void)x;
    (void)y;
    return YETTY_OK(yetty_ycore_int, 0);
}

/* Wheel / trackpad scroll. (dx, dy) are the deltas at (x, y). Default:
 * not handled (0), so the framework keeps bubbling to an ancestor that
 * scrolls. Scrollable widgets (scrollarea, filepicker) override this. */
YETTY_ANNOTATE("virtual@ygui:widget:widget_on_scroll")
static struct yetty_ycore_int_result widget_default_on_scroll(struct yetty_yclass_object *obj,
                                                              float x, float y, float dx, float dy)
{
    (void)obj;
    (void)x;
    (void)y;
    (void)dx;
    (void)dy;
    return YETTY_OK(yetty_ycore_int, 0);
}

YETTY_ANNOTATE("virtual@ygui:widget:widget_paint")
YETTY_ANNOTATE("local@ygui:widget_paint")
static struct yetty_ycore_void_result widget_default_paint(struct yetty_yclass_object *obj,
                                                           struct yetty_ygui_emit_ctx *emit_ctx)
{
    (void)obj;
    (void)emit_ctx;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@ygui:widget:widget_emit_container")
YETTY_ANNOTATE("local@ygui:widget_emit_container")
static struct yetty_ycore_void_result widget_default_emit_container(
    struct yetty_yclass_object *obj, struct yetty_ygui_emit_ctx *emit_ctx)
{
    (void)obj;
    (void)emit_ctx;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@ygui:widget:widget_emit_body")
YETTY_ANNOTATE("local@ygui:widget_emit_body")
static struct yetty_ycore_void_result widget_default_emit_body(struct yetty_yclass_object *obj,
                                                               struct yetty_ygui_emit_ctx *emit_ctx)
{
    (void)obj;
    (void)emit_ctx;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Layout setters / getters.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_ygui_layout yetty_ygui_layout_default(void)
{
    struct yetty_ygui_layout l = {0};
    l.direction = YETTY_YGUI_FLEX_ROW;
    l.justify = YETTY_YGUI_JUSTIFY_START;
    l.align = YETTY_YGUI_ALIGN_START;
    l.align_self = YETTY_YGUI_ALIGN_SELF_AUTO;
    l.wrap = YETTY_YGUI_WRAP_NOWRAP;
    l.gap = 0;
    l.margin_top = 0;
    l.margin_right = 0;
    l.margin_bottom = 0;
    l.margin_left = 0;
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

YETTY_ANNOTATE("expose")
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
        wd->dirty = 1;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_rectangle_result yetty_ygui_widget_rect(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_rectangle, "yetty_ygui_widget_rect: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res =
        yetty_ygui_widget_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_rectangle, wd_res, "yetty_ygui_widget_rect: data_get");
    return YETTY_OK(yetty_ycore_rectangle, wd_res.value->rect);
}

/* Main-axis scroll offset (internal — read by the layout pass to slide a
 * scrolling container's children). Sets the widget's dirty flag but not engine dirty;
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
        wd->dirty = 1;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_float_result yetty_ygui_widget_scroll_main_get(
    const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_float, "yetty_ygui_widget_scroll_main_get: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res =
        yetty_ygui_widget_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, wd_res, "yetty_ygui_widget_scroll_main_get: data_get");
    return YETTY_OK(yetty_ycore_float, wd_res.value->scroll_main);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_widget_layout_set(struct yetty_yclass_object *obj,
                                                            const struct yetty_ygui_layout *layout)
{
    if (!obj || !layout) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_layout_set: NULL arg");
    }
    struct yetty_ygui_widget_ptr_result wd_dr = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_dr, "yetty_ygui_widget_layout_set: data_get");
    struct yetty_ygui_widget *wd = wd_dr.value;
    /* No-op when the layout is byte-identical, so a caller re-pushing an
     * unchanged layout in a hot path does not force an avoidable emit. The
     * byte compare is a valid equality test only while the struct stays
     * padding-free; the assert enforces that (every field is a 4-byte
     * enum/float/int, so the member sizes sum to sizeof the struct). A future
     * field reorder or a smaller field that introduces padding — which would
     * make byte equality diverge from value equality — breaks the build here
     * instead of silently weakening the guard, prompting a switch to a
     * field-by-field compare. */
    _Static_assert(
        sizeof *layout ==
            sizeof(enum yetty_ygui_flex_direction) + sizeof(enum yetty_ygui_flex_justify) +
                sizeof(enum yetty_ygui_flex_align) + sizeof(enum yetty_ygui_flex_align_self) +
                sizeof(enum yetty_ygui_flex_wrap) + 19 * sizeof(float) + 2 * sizeof(int),
        "struct yetty_ygui_layout must stay padding-free for the layout_set() "
        "memcmp no-op guard; add the new field here or compare fields semantically");
    if (memcmp(&wd->layout, layout, sizeof *layout) == 0) {
        return YETTY_OK_VOID();
    }
    wd->layout = *layout;
    /* Route through set_dirty (not a bare wd->dirty = 1) so the owning
     * framework is also marked dirty and the next frame re-emits. Mirrors
     * set_visible above — a public setter that mutates layout must trigger
     * a repaint on its own; callers should not have to mark dirty by hand. */
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ygui_layout_const_ptr_result yetty_ygui_widget_layout_get(
    const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ygui_layout_const_ptr, "yetty_ygui_widget_layout_get: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res =
        yetty_ygui_widget_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ygui_layout_const_ptr, wd_res,
                        "yetty_ygui_widget_layout_get: data_get");
    return YETTY_OK(yetty_ygui_layout_const_ptr, &wd_res.value->layout);
}

YETTY_ANNOTATE("expose")
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

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_widget_is_visible(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_is_visible: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res =
        yetty_ygui_widget_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, wd_res, "yetty_ygui_widget_is_visible: data_get");
    return YETTY_OK(yetty_ycore_int, wd_res.value->layout.hidden ? 0 : 1);
}

YETTY_ANNOTATE("expose")
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

YETTY_ANNOTATE("expose")
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

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_widget_make_figure(struct yetty_yclass_object *obj,
                                                             uint32_t kind, int32_t z)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_make_figure: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_res, "yetty_ygui_widget_make_figure: data_get");
    wd_res.value->figure_kind = kind;
    wd_res.value->figure_z = z;
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_widget_set_figure_z(struct yetty_yclass_object *obj,
                                                              int32_t z)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_figure_z: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_res, "yetty_ygui_widget_set_figure_z: data_get");
    if (wd_res.value->figure_z != z) {
        wd_res.value->figure_z = z;
        return yetty_ygui_widget_set_dirty(obj);
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_widget_set_floating(struct yetty_yclass_object *obj,
                                                              int floating)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_floating: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_res, "yetty_ygui_widget_set_floating: data_get");
    wd_res.value->floating = floating ? 1 : 0;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_widget_is_floating(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_is_floating: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res =
        yetty_ygui_widget_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, wd_res, "yetty_ygui_widget_is_floating: data_get");
    return YETTY_OK(yetty_ycore_int, wd_res.value->floating);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ygui_widget_figure_kind(
    const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_uint32, "yetty_ygui_widget_figure_kind: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res =
        yetty_ygui_widget_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, wd_res, "yetty_ygui_widget_figure_kind: data_get");
    return YETTY_OK(yetty_ycore_uint32, wd_res.value->figure_kind);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_widget_figure_z(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_figure_z: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res =
        yetty_ygui_widget_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, wd_res, "yetty_ygui_widget_figure_z: data_get");
    return YETTY_OK(yetty_ycore_int, (int)wd_res.value->figure_z);
}

YETTY_ANNOTATE("expose")
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

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ygui_widget_bg(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_uint32, "yetty_ygui_widget_bg: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res =
        yetty_ygui_widget_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, wd_res, "yetty_ygui_widget_bg: data_get");
    return YETTY_OK(yetty_ycore_uint32, wd_res.value->bg);
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

YETTY_ANNOTATE("expose")
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
 * reached through the checked yetty_ygui_widget_from downcast).
 *=========================================================================*/

/*
 * Generic "call the parent's implementation of this slot" helpers.
 *
 * ABI CONTRACT: read before adding a new caller. These two generic helpers
 * cast the resolved parent impl to a function pointer that takes only:
 *
 *     (struct yetty_yclass_object *)
 *
 * They are therefore valid only for slots whose implementation signature is
 * exactly that. In the current ygui widget slot set, that means constructor
 * and destructor for super_void.
 *
 * They must not be used for slots that carry extra parameters. In particular,
 * these widget slots have wider signatures:
 *
 *     widget_on_press(obj, float x, float y, int button)
 *     widget_on_release(obj, float x, float y, int button)
 *     widget_on_motion(obj, float x, float y)
 *     widget_on_scroll(obj, float x, float y, float dx, float dy)
 *     widget_paint(obj, struct yetty_ygui_emit_ctx *)
 *     widget_emit_container(obj, struct yetty_ygui_emit_ctx *)
 *     widget_emit_body(obj, struct yetty_ygui_emit_ctx *)
 *
 * Routing one of those through super_void/super_int casts away the extra
 * arguments and calls with the wrong ABI (undefined behavior). If a subclass
 * needs to chain to a parent implementation for a wider slot, add a typed
 * per-slot super helper with the matching signature rather than widening
 * these generic helpers.
 *
 * This contract is ENFORCED at runtime, not merely documented: super_void
 * rejects any method_id other than constructor/destructor, and super_int —
 * for which no valid (obj)-only int virtual exists and which has no callers —
 * rejects unconditionally. Both return a Result error rather than performing
 * the unsafe cast, so misuse fails loudly at the call site.
 */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_super_void(struct yetty_yclass_object *obj,
                                                     const struct yetty_yclass *self_class,
                                                     yetty_yclass_method_id_t method_id)
{
    if (!obj || !self_class || !method_id) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_super_void: NULL arg");
    }
    /* Enforce, don't just document, the ABI contract above: the (obj)-only
     * cast is safe only for the parameterless void virtuals — constructor and
     * destructor. Reject any other slot loudly instead of calling a wider one
     * (on_press / paint / emit_container / …) with the wrong ABI. A wider slot
     * must get a typed per-slot super helper. */
    if (method_id != (yetty_yclass_method_id_t)yetty_ygui_constructor &&
        method_id != (yetty_yclass_method_id_t)yetty_ygui_destructor) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ygui_super_void: generic super is only valid for "
                         "constructor/destructor; a parameter-carrying slot needs a typed helper");
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
    typedef struct yetty_ycore_void_result (*fn_t)(struct yetty_yclass_object *);
    return ((fn_t)impl)((struct yetty_yclass_object *)obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_super_int(struct yetty_yclass_object *obj,
                                                   const struct yetty_yclass *self_class,
                                                   yetty_yclass_method_id_t method_id)
{
    (void)obj;
    (void)self_class;
    (void)method_id;
    /* Every int widget virtual is a pointer-event handler (on_press /
     * on_release / on_motion / on_scroll) that carries x/y/button/… args, so
     * the (obj)-only cast this helper would use is ABI-unsafe for all of them,
     * and there is no (obj)-only int virtual it could serve. It has no callers.
     * Reject unconditionally rather than expose an unsafe dispatch path; add a
     * typed per-slot super if a genuine (obj)-only int virtual ever appears. */
    return YETTY_ERR(yetty_ycore_int,
                     "yetty_ygui_super_int: no (obj)-only int virtual exists; a pointer-event "
                     "slot needs a typed per-slot super helper");
}

/*===========================================================================
 * Typed per-slot chaining for the pointer-event virtuals (on_press /
 * on_release / on_motion / on_scroll). Unlike the generic super_void/super_int
 * these carry the slot's real signature, so a leaf override can safely call:
 *
 *   - the PARENT's implementation via yetty_ygui_super_on_<slot>() — walks the
 *     parent chain (yetty_yclass_parent), exactly like super_void but ABI-safe
 *     for the extra x/y/button/… arguments; and
 *   - a specific MIXIN's implementation via yetty_ygui_mixin_on_<slot>() —
 *     looks the slot up directly on the named mixin class. Because dispatch is
 *     a flat overwrite (a leaf override replaces a mixin's slot; see the yclass
 *     README), this is how a widget that both `uses@` a behavior mixin AND
 *     overrides the slot itself runs the mixin's part explicitly — the missing
 *     composition path that a flat vtable cannot provide automatically.
 *
 * A slot the target class doesn't implement resolves to NULL and no-ops
 * (returns 0 = not-consumed), matching the historic "un-overridden slot
 * silently does nothing" contract.
 *
 * yetty_ygui_mixin_on_* is a DIRECT slot lookup on the class you pass — it does
 * not verify that class is actually a mixin the object `uses@`. Passing an
 * unrelated class simply dispatches that class's slot impl (or no-ops if it has
 * none); the intent is that callers pass a mixin their own class composes.
 *=========================================================================*/

static struct yetty_yclass_impl_t_result ygui_super_impl(const struct yetty_yclass *self_class,
                                                         yetty_yclass_method_id_t method_id)
{
    struct yetty_yclass_method_slot_result slot_result = yetty_ygui_method_slot_get(method_id);
    YETTY_RETURN_IF_ERR(yetty_yclass_impl_t, slot_result, "ygui_super_impl: slot lookup");
    if (slot_result.value == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        return YETTY_OK(yetty_yclass_impl_t, NULL);
    }
    return yetty_ygui_dispatch_lookup_super(self_class, slot_result.value);
}

static struct yetty_yclass_impl_t_result ygui_mixin_impl(const struct yetty_yclass *mixin_class,
                                                         yetty_yclass_method_id_t method_id)
{
    struct yetty_yclass_method_slot_result slot_result = yetty_ygui_method_slot_get(method_id);
    YETTY_RETURN_IF_ERR(yetty_yclass_impl_t, slot_result, "ygui_mixin_impl: slot lookup");
    if (slot_result.value == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        return YETTY_OK(yetty_yclass_impl_t, NULL);
    }
    return yetty_ygui_dispatch_lookup(mixin_class, slot_result.value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_super_on_press(struct yetty_yclass_object *obj,
                                                        const struct yetty_yclass *self_class,
                                                        float x, float y, int button)
{
    if (!obj || !self_class) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_super_on_press: NULL arg");
    }
    struct yetty_yclass_impl_t_result impl_result =
        ygui_super_impl(self_class, (yetty_yclass_method_id_t)yetty_ygui_widget_on_press);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, impl_result, "yetty_ygui_super_on_press: resolve");
    if (!impl_result.value) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    typedef struct yetty_ycore_int_result (*fn_t)(struct yetty_yclass_object *, float, float, int);
    return ((fn_t)impl_result.value)(obj, x, y, button);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_super_on_release(struct yetty_yclass_object *obj,
                                                          const struct yetty_yclass *self_class,
                                                          float x, float y, int button)
{
    if (!obj || !self_class) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_super_on_release: NULL arg");
    }
    struct yetty_yclass_impl_t_result impl_result =
        ygui_super_impl(self_class, (yetty_yclass_method_id_t)yetty_ygui_widget_on_release);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, impl_result, "yetty_ygui_super_on_release: resolve");
    if (!impl_result.value) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    typedef struct yetty_ycore_int_result (*fn_t)(struct yetty_yclass_object *, float, float, int);
    return ((fn_t)impl_result.value)(obj, x, y, button);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_super_on_motion(struct yetty_yclass_object *obj,
                                                         const struct yetty_yclass *self_class,
                                                         float x, float y)
{
    if (!obj || !self_class) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_super_on_motion: NULL arg");
    }
    struct yetty_yclass_impl_t_result impl_result =
        ygui_super_impl(self_class, (yetty_yclass_method_id_t)yetty_ygui_widget_on_motion);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, impl_result, "yetty_ygui_super_on_motion: resolve");
    if (!impl_result.value) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    typedef struct yetty_ycore_int_result (*fn_t)(struct yetty_yclass_object *, float, float);
    return ((fn_t)impl_result.value)(obj, x, y);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_super_on_scroll(struct yetty_yclass_object *obj,
                                                         const struct yetty_yclass *self_class,
                                                         float x, float y, float dx, float dy)
{
    if (!obj || !self_class) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_super_on_scroll: NULL arg");
    }
    struct yetty_yclass_impl_t_result impl_result =
        ygui_super_impl(self_class, (yetty_yclass_method_id_t)yetty_ygui_widget_on_scroll);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, impl_result, "yetty_ygui_super_on_scroll: resolve");
    if (!impl_result.value) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    typedef struct yetty_ycore_int_result (*fn_t)(struct yetty_yclass_object *, float, float, float,
                                                  float);
    return ((fn_t)impl_result.value)(obj, x, y, dx, dy);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_mixin_on_press(struct yetty_yclass_object *obj,
                                                        const struct yetty_yclass *mixin_class,
                                                        float x, float y, int button)
{
    if (!obj || !mixin_class) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_mixin_on_press: NULL arg");
    }
    struct yetty_yclass_impl_t_result impl_result =
        ygui_mixin_impl(mixin_class, (yetty_yclass_method_id_t)yetty_ygui_widget_on_press);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, impl_result, "yetty_ygui_mixin_on_press: resolve");
    if (!impl_result.value) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    typedef struct yetty_ycore_int_result (*fn_t)(struct yetty_yclass_object *, float, float, int);
    return ((fn_t)impl_result.value)(obj, x, y, button);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_mixin_on_release(struct yetty_yclass_object *obj,
                                                          const struct yetty_yclass *mixin_class,
                                                          float x, float y, int button)
{
    if (!obj || !mixin_class) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_mixin_on_release: NULL arg");
    }
    struct yetty_yclass_impl_t_result impl_result =
        ygui_mixin_impl(mixin_class, (yetty_yclass_method_id_t)yetty_ygui_widget_on_release);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, impl_result, "yetty_ygui_mixin_on_release: resolve");
    if (!impl_result.value) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    typedef struct yetty_ycore_int_result (*fn_t)(struct yetty_yclass_object *, float, float, int);
    return ((fn_t)impl_result.value)(obj, x, y, button);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_mixin_on_motion(struct yetty_yclass_object *obj,
                                                         const struct yetty_yclass *mixin_class,
                                                         float x, float y)
{
    if (!obj || !mixin_class) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_mixin_on_motion: NULL arg");
    }
    struct yetty_yclass_impl_t_result impl_result =
        ygui_mixin_impl(mixin_class, (yetty_yclass_method_id_t)yetty_ygui_widget_on_motion);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, impl_result, "yetty_ygui_mixin_on_motion: resolve");
    if (!impl_result.value) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    typedef struct yetty_ycore_int_result (*fn_t)(struct yetty_yclass_object *, float, float);
    return ((fn_t)impl_result.value)(obj, x, y);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_mixin_on_scroll(struct yetty_yclass_object *obj,
                                                         const struct yetty_yclass *mixin_class,
                                                         float x, float y, float dx, float dy)
{
    if (!obj || !mixin_class) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_mixin_on_scroll: NULL arg");
    }
    struct yetty_yclass_impl_t_result impl_result =
        ygui_mixin_impl(mixin_class, (yetty_yclass_method_id_t)yetty_ygui_widget_on_scroll);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, impl_result, "yetty_ygui_mixin_on_scroll: resolve");
    if (!impl_result.value) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    typedef struct yetty_ycore_int_result (*fn_t)(struct yetty_yclass_object *, float, float, float,
                                                  float);
    return ((fn_t)impl_result.value)(obj, x, y, dx, dy);
}

YETTY_ANNOTATE("expose")
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

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui_widget_parent(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_ygui_widget_ptr_result wd_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, wd_res, "yetty_ygui_widget_parent: data_get");
    return YETTY_OK(yetty_yclass_object_ptr, wd_res.value->parent);
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui_widget_first_child(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_ygui_widget_ptr_result wd_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, wd_res, "yetty_ygui_widget_first_child: data_get");
    return YETTY_OK(yetty_yclass_object_ptr, wd_res.value->first_child);
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui_widget_next_sibling(
    struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_ygui_widget_ptr_result wd_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, wd_res,
                        "yetty_ygui_widget_next_sibling: data_get");
    return YETTY_OK(yetty_yclass_object_ptr, wd_res.value->next_sibling);
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui_widget_framework(struct yetty_yclass_object *obj)
{
    while (obj) {
        struct yetty_ygui_widget_ptr_result wd_res = yetty_ygui_widget_from(obj);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, wd_res,
                            "yetty_ygui_widget_framework: data_get");
        if (wd_res.value->framework) {
            return YETTY_OK(yetty_yclass_object_ptr, wd_res.value->framework);
        }
        obj = wd_res.value->parent;
    }
    return YETTY_OK(yetty_yclass_object_ptr, NULL);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ygui_widget_id(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK(yetty_ycore_uint32, 0);
    }
    struct yetty_ygui_widget_ptr_result wd_res =
        yetty_ygui_widget_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, wd_res, "yetty_ygui_widget_id: data_get");
    return YETTY_OK(yetty_ycore_uint32, wd_res.value->id);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_widget_set_dirty(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_dirty: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_res, "yetty_ygui_widget_set_dirty: data_get");
    wd_res.value->dirty = 1;
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui_widget_framework(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res, "yetty_ygui_widget_set_dirty: framework");
    if (framework_res.value) {
        yetty_ygui_framework_mark_dirty(framework_res.value);
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_widget_is_dirty(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ygui_widget_ptr_result wd_res =
        yetty_ygui_widget_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, wd_res, "yetty_ygui_widget_is_dirty: data_get");
    return YETTY_OK(yetty_ycore_int, wd_res.value->dirty ? 1 : 0);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_widget_is_hovered(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ygui_widget_ptr_result wd_res =
        yetty_ygui_widget_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, wd_res, "yetty_ygui_widget_is_hovered: data_get");
    return YETTY_OK(yetty_ycore_int, wd_res.value->hovered ? 1 : 0);
}

/*---------------------------------------------------------------------------
 * Framework-internal mutators of the widget base slice. These are NOT part
 * of the public widget API (no expose annotation) — they are declared in
 * internal.h and used only by framework.c / event.c, which own the tree and
 * dirty/hover bookkeeping. The public mutator yetty_ygui_widget_set_dirty
 * (above) additionally marks the framework dirty; the raw setter here does
 * not, so framework.c can manage the framework dirty bit itself.
 *-------------------------------------------------------------------------*/

struct yetty_ycore_void_result yetty_ygui_widget_set_framework(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *framework)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_framework: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_res, "yetty_ygui_widget_set_framework: data_get");
    wd_res.value->framework = framework;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_widget_set_id(struct yetty_yclass_object *obj,
                                                        uint32_t id)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_id: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_res, "yetty_ygui_widget_set_id: data_get");
    wd_res.value->id = id;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_widget_set_hovered(struct yetty_yclass_object *obj,
                                                             int hovered)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_hovered: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_res, "yetty_ygui_widget_set_hovered: data_get");
    wd_res.value->hovered = hovered ? 1 : 0;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_widget_set_dirty_flag(struct yetty_yclass_object *obj,
                                                                int dirty)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_dirty_flag: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_res, "yetty_ygui_widget_set_dirty_flag: data_get");
    wd_res.value->dirty = dirty ? 1 : 0;
    return YETTY_OK_VOID();
}

struct yetty_ygui_event_subscription_ptr_result yetty_ygui_widget_subscriptions(
    struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK(yetty_ygui_event_subscription_ptr, NULL);
    }
    struct yetty_ygui_widget_ptr_result wd_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ygui_event_subscription_ptr, wd_res,
                        "yetty_ygui_widget_subscriptions: data_get");
    return YETTY_OK(yetty_ygui_event_subscription_ptr, wd_res.value->subscriptions);
}

struct yetty_ycore_void_result yetty_ygui_widget_set_subscriptions(
    struct yetty_yclass_object *obj, struct yetty_ygui_event_subscription *subscriptions)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_set_subscriptions: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_res, "yetty_ygui_widget_set_subscriptions: data_get");
    wd_res.value->subscriptions = subscriptions;
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Widget tree linking (internal) + create / add-child / destroy / raise.
 *-------------------------------------------------------------------------*/

static struct yetty_ycore_void_result widget_unlink_from_parent(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_widget_ptr_result obj_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_res, "widget_unlink_from_parent: obj data_get");
    struct yetty_ygui_widget *obj_widget = obj_res.value;
    struct yetty_yclass_object *parent = obj_widget->parent;
    if (!parent) {
        return YETTY_OK_VOID();
    }
    struct yetty_ygui_widget_ptr_result parent_res = yetty_ygui_widget_from(parent);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, parent_res, "widget_unlink_from_parent: parent data_get");
    struct yetty_yclass_object **slot = &parent_res.value->first_child;
    while (*slot && *slot != obj) {
        struct yetty_ygui_widget_ptr_result sib_res = yetty_ygui_widget_from(*slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sib_res,
                            "widget_unlink_from_parent: sibling data_get");
        slot = &sib_res.value->next_sibling;
    }
    if (*slot == obj) {
        *slot = obj_widget->next_sibling;
    }
    obj_widget->next_sibling = NULL;
    obj_widget->parent = NULL;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result widget_link_to_parent(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *parent)
{
    struct yetty_ygui_widget_ptr_result obj_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_res, "widget_link_to_parent: obj data_get");
    obj_res.value->parent = parent;
    if (!parent) {
        return YETTY_OK_VOID();
    }
    struct yetty_ygui_widget_ptr_result parent_res = yetty_ygui_widget_from(parent);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, parent_res, "widget_link_to_parent: parent data_get");
    if (!parent_res.value->first_child) {
        parent_res.value->first_child = obj;
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object *tail = parent_res.value->first_child;
    for (;;) {
        struct yetty_ygui_widget_ptr_result tail_res = yetty_ygui_widget_from(tail);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tail_res, "widget_link_to_parent: tail data_get");
        if (!tail_res.value->next_sibling) {
            tail_res.value->next_sibling = obj;
            return YETTY_OK_VOID();
        }
        tail = tail_res.value->next_sibling;
    }
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
    struct yetty_ycore_void_result link_res = widget_link_to_parent(obj, parent);
    if (YETTY_IS_ERR(link_res)) {
        free(obj);
        return YETTY_ERR(yetty_yclass_object_ptr, "widget_instantiate: link failed", link_res);
    }

    struct yetty_ygui_widget_ptr_result obj_widget_res = yetty_ygui_widget_from(obj);
    if (YETTY_IS_ERR(obj_widget_res)) {
        struct yetty_ycore_void_result unlink_res = widget_unlink_from_parent(obj);
        if (YETTY_IS_ERR(unlink_res)) {
            yetty_ycore_error_destroy(unlink_res.error);
        }
        free(obj);
        return YETTY_ERR(yetty_yclass_object_ptr, "widget_instantiate: obj data_get",
                         obj_widget_res);
    }
    struct yetty_ygui_widget *obj_widget = obj_widget_res.value;

    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui_widget_framework(obj);
    if (YETTY_IS_ERR(framework_res)) {
        struct yetty_ycore_void_result unlink_res = widget_unlink_from_parent(obj);
        if (YETTY_IS_ERR(unlink_res)) {
            yetty_ycore_error_destroy(unlink_res.error);
        }
        free(obj);
        return YETTY_ERR(yetty_yclass_object_ptr, "widget_instantiate: framework", framework_res);
    }
    struct yetty_yclass_object *framework = framework_res.value;
    if (framework) {
        struct uint32_result idr = yetty_ygui_framework_alloc_id(framework);
        if (YETTY_IS_ERR(idr)) {
            struct yetty_ycore_void_result unlink_res = widget_unlink_from_parent(obj);
            if (YETTY_IS_ERR(unlink_res)) {
                yetty_ycore_error_destroy(unlink_res.error);
            }
            free(obj);
            return YETTY_ERR(yetty_yclass_object_ptr, "widget_instantiate: id alloc failed", idr);
        }
        obj_widget->id = idr.value;
    }

    struct yetty_ycore_void_result cr = yetty_ygui_constructor((struct yetty_yclass_object *)obj);
    if (YETTY_IS_ERR(cr)) {
        if (framework && obj_widget->id) {
            struct yetty_ycore_void_result fr =
                yetty_ygui_framework_free_id(framework, obj_widget->id);
            if (YETTY_IS_ERR(fr)) {
                yetty_ycore_error_destroy(fr.error);
            }
        }
        struct yetty_ycore_void_result unlink_res = widget_unlink_from_parent(obj);
        if (YETTY_IS_ERR(unlink_res)) {
            yetty_ycore_error_destroy(unlink_res.error);
        }
        free(obj);
        return YETTY_ERR(yetty_yclass_object_ptr, "widget_instantiate: constructor failed", cr);
    }

    return YETTY_OK(yetty_yclass_object_ptr, obj);
}

/* Create a parentless (root / top-level) widget of `cls`. */
YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui_widget_new(const struct yetty_yclass *cls)
{
    return widget_instantiate(cls, NULL);
}

/* Create a widget of `cls` and add it as the last child of `parent`. */
YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui_widget_add(struct yetty_yclass_object *parent,
                                                            const struct yetty_yclass *cls)
{
    if (!parent) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_widget_add: NULL parent (use yetty_ygui_widget_create)");
    }
    return widget_instantiate(cls, parent);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_widget_destroy(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK_VOID();
    }
    struct yetty_ygui_widget_ptr_result obj_widget_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_widget_res, "yetty_ygui_widget_destroy: data_get");
    struct yetty_ygui_widget *obj_widget = obj_widget_res.value;

    /* Best-effort teardown: every step runs; per-step errors are dropped so a
     * later step still frees its resource. */
    while (obj_widget->first_child) {
        struct yetty_ycore_void_result cr = yetty_ygui_widget_destroy(obj_widget->first_child);
        if (YETTY_IS_ERR(cr)) {
            yetty_ycore_error_destroy(cr.error);
        }
    }
    struct yetty_ygui_event_subscription *sub = obj_widget->subscriptions;
    while (sub) {
        struct yetty_ygui_event_subscription *next = sub->next;
        free(sub);
        sub = next;
    }
    obj_widget->subscriptions = NULL;
    struct yetty_ycore_void_result dr = yetty_ygui_destructor((struct yetty_yclass_object *)obj);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui_widget_framework(obj);
    struct yetty_yclass_object *framework = NULL;
    if (YETTY_IS_ERR(framework_res)) {
        yetty_ycore_error_destroy(framework_res.error);
    } else {
        framework = framework_res.value;
    }
    if (framework && obj_widget->id != 0) {
        struct yetty_ycore_void_result fr = yetty_ygui_framework_free_id(framework, obj_widget->id);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
    }
    /* Drop any hover/press capture this framework still holds on the widget
     * being destroyed. The framework data slice is opaque here, so go through
     * the accessor instead of poking the fields directly. */
    if (framework) {
        struct yetty_ycore_void_result forget_res =
            yetty_ygui_framework_forget_widget(framework, obj);
        if (YETTY_IS_ERR(forget_res)) {
            yetty_ycore_error_destroy(forget_res.error);
        }
    }
    struct yetty_ycore_void_result unlink_res = widget_unlink_from_parent(obj);
    if (YETTY_IS_ERR(unlink_res)) {
        yetty_ycore_error_destroy(unlink_res.error);
    }
    free(obj);
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_widget_raise(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_raise: NULL obj");
    }
    struct yetty_ygui_widget_ptr_result wd_res = yetty_ygui_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wd_res, "yetty_ygui_widget_raise: data_get");
    if (!wd_res.value->parent) {
        /* No parent: nothing to reorder. Not an error. */
        return YETTY_OK_VOID();
    }
    /* Move to the end of the sibling list so the framework's widget
     * hit-test (last-match-wins) prefers this widget over earlier
     * siblings it overlaps. The render side is ordered by figure z
     * separately; raising bumps both so they agree. */
    struct yetty_yclass_object *parent = wd_res.value->parent;
    struct yetty_ycore_void_result unlink_res = widget_unlink_from_parent(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, unlink_res, "yetty_ygui_widget_raise: unlink");
    struct yetty_ycore_void_result link_res = widget_link_to_parent(obj, parent);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, link_res, "yetty_ygui_widget_raise: link");
    return YETTY_OK_VOID();
}

#include "widget.gen.c"
