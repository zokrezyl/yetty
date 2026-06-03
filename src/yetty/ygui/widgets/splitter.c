/* ygui-splitter.c — draggable divider strip.
 *
 * Sits between two siblings in a flex row/column. Dragging it resizes the
 * preceding sibling (the following one absorbs the change via flex_grow).
 * Relies on the framework's pointer capture so the drag tracks the cursor
 * even when it leaves the thin strip.
 */
#include "../internal.h"
#include "paint-helpers.h"
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/splitter.h>

#define COLOR_TRACK 0xFF2C261Eu
#define COLOR_GRIP 0xFF92A86Bu
#define SPLITTER_MIN 40.0f

struct [[clang::annotate("class@ygui:splitter")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] splitter_data {
    /* axis_plus1: 0 = auto (derive from parent flex direction); 1 =
     * column-bar (horizontal bar between stacked siblings → vertical
     * resize); 2 = row-bar (vertical bar between side-by-side siblings
     * → horizontal resize). Encoded +1 so the zeroed default reads as
     * "auto" without needing a constructor override. */
    int axis_plus1;
    /* Minimum size of the resized sibling in flex mode (0 = use the
     * SPLITTER_MIN default). Ignored in external-drive mode — the host
     * clamps. */
    float min_size;
    /* External-drive mode: when set, the splitter does NOT resize its
     * flex siblings; on drag it reports the cursor's offset from its
     * own centre along the resize axis and lets the host reposition it
     * next frame. Used by yui's absolutely-positioned pane dividers. */
    yetty_ygui_splitter_change_cb change_cb;
    void *change_userdata;
};

static const struct yetty_yclass *splitter_class(void)
{
    return yetty_ygui_splitter_class_get().value;
}

/* Effective axis: 1 = row-bar (horizontal resize), 0 = column-bar
 * (vertical resize). Honours an explicit override, else derives from the
 * parent's flex direction. */
static int splitter_axis_row(struct yetty_ygui_object *obj, const struct splitter_data *d)
{
    if (d->axis_plus1 == 1) {
        return 0;
    }
    if (d->axis_plus1 == 2) {
        return 1;
    }
    struct yetty_ygui_object *parent = yetty_ygui_object_parent(obj);
    if (parent) {
        const struct yetty_ygui_layout *pl = yetty_ygui_widget_layout_get(parent);
        return pl->direction == YETTY_YGUI_FLEX_ROW ? 1 : 0;
    }
    return 1;
}

[[clang::annotate("override@ygui:splitter:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "splitter paint: NULL ctx");
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result result_76 = yguix_box(ctx, r.min.x, r.min.y, w, h, COLOR_TRACK, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_76, "splitter: track");
    /* Grip dots — three small marks centred. */
    float cx = r.min.x + w * 0.5f;
    float cy = r.min.y + h * 0.5f;
    for (int i = -1; i <= 1; i++) {
        struct yetty_ycore_void_result result_82 = yguix_circle(ctx, cx, cy + i * 6, 2, COLOR_GRIP);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_82, "splitter: dot");
    }
    return YETTY_OK_VOID();
}

/* The immediately preceding in-flow sibling (skips absolute / hidden). */
static struct yetty_ygui_object *splitter_prev_sibling(struct yetty_ygui_object *obj)
{
    struct yetty_ygui_object *parent = yetty_ygui_object_parent(obj);
    if (!parent) {
        return NULL;
    }
    struct yetty_ygui_object *prev = NULL;
    for (struct yetty_ygui_object *c = yetty_ygui_object_first_child(parent); c && c != obj;
         c = yetty_ygui_object_next_sibling(c)) {
        const struct yetty_ygui_layout *cl = yetty_ygui_widget_layout_get(c);
        if (cl->absolute || cl->hidden) {
            continue;
        }
        prev = c;
    }
    return prev;
}

[[clang::annotate("override@ygui:splitter:widget_on_press")]]
static struct yetty_ycore_int_result on_press(struct yetty_yclass_ctx *yclass_ctx,
                                              struct yetty_yclass_object *yclass_obj, float x,
                                              float y, int btn)
{
    (void)yclass_ctx;
    (void)yclass_obj;
    (void)x;
    (void)y;
    (void)btn;
    /* Consume so the framework captures the pointer for the drag. */
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:splitter:widget_on_motion")]]
static struct yetty_ycore_int_result on_motion(struct yetty_yclass_ctx *yclass_ctx,
                                               struct yetty_yclass_object *yclass_obj, float x,
                                               float y)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ygui_void_ptr_result sd_dr = yetty_ygui_data_get_result(obj, splitter_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_int, sd_dr, "on_motion: data_get");
    struct splitter_data *sd = sd_dr.value;

    /* Only resize while this splitter is the actively-dragged widget.
     * The framework also delivers on_motion to whatever the pointer is
     * merely hovering over; without this guard a plain mouse-move across
     * the divider (no button held) would drag the panes. The framework
     * captures the pointer on a consumed press, so pressed_obj == this
     * splitter exactly during a real drag. */
    struct yetty_ygui_framework *eng = yetty_ygui_object_framework(obj);
    if (!eng || eng->pressed_obj != obj) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    /* External-drive mode — report the cursor's offset from this bar's
     * centre along the resize axis and let the host reposition us. */
    if (sd->change_cb) {
        int row = splitter_axis_row(obj, sd);
        struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
        float cx = (r.min.x + r.max.x) * 0.5f;
        float cy = (r.min.y + r.max.y) * 0.5f;
        float delta = row ? (x - cx) : (y - cy);
        sd->change_cb(obj, delta, sd->change_userdata);
        struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
        if (YETTY_IS_ERR(dr)) {
            return YETTY_ERR(yetty_ycore_int, "splitter: external dirty", dr);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    struct yetty_ygui_object *parent = yetty_ygui_object_parent(obj);
    struct yetty_ygui_object *prev = splitter_prev_sibling(obj);
    if (!parent || !prev) {
        return YETTY_OK(yetty_ycore_int, 1);
    }
    const struct yetty_ygui_layout *pl = yetty_ygui_widget_layout_get(parent);
    struct yetty_ycore_rectangle prect = yetty_ygui_widget_rect(prev);
    struct yetty_ycore_rectangle parect = yetty_ygui_widget_rect(parent);
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(prev);

    if (pl->direction == YETTY_YGUI_FLEX_COLUMN) {
        float nh = y - prect.min.y;
        float max_h = parect.max.y - prect.min.y - SPLITTER_MIN;
        if (nh < SPLITTER_MIN) {
            nh = SPLITTER_MIN;
        }
        if (max_h > SPLITTER_MIN && nh > max_h) {
            nh = max_h;
        }
        l.height = nh;
    } else {
        float nw = x - prect.min.x;
        float max_w = parect.max.x - prect.min.x - SPLITTER_MIN;
        if (nw < SPLITTER_MIN) {
            nw = SPLITTER_MIN;
        }
        if (max_w > SPLITTER_MIN && nw > max_w) {
            nw = max_w;
        }
        l.width = nw;
    }
    struct yetty_ycore_void_result sr = yetty_ygui_widget_layout_set(prev, &l);
    if (YETTY_IS_ERR(sr)) {
        return YETTY_ERR(yetty_ycore_int, "splitter: resize", sr);
    }
    /* Mark dirty so the next frame re-runs the layout pass + repaints. */
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(prev);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "splitter: dirty", dr);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

/*-----------------------------------------------------------------------------
 * Public API — axis / min / external-drive change callback. Added for the
 * yui port, which positions splitters absolutely over its pane tree and
 * drives the resize itself off the reported delta.
 *---------------------------------------------------------------------------*/

[[clang::annotate("expose")]]
void yetty_ygui_splitter_set_axis(struct yetty_ygui_object *obj, int row)
{
    if (!obj) {
        return;
    }
    struct yetty_yclass_ptr_result cr = yetty_ygui_splitter_class_get();
    if (YETTY_IS_ERR(cr)) {
        yetty_ycore_error_destroy(cr.error);
        return;
    }
    if (obj->klass != cr.value) {
        return;
    }
    struct splitter_data *d = yetty_ygui_data_get(obj, cr.value);
    d->axis_plus1 = (row ? 2 : 1);
}

[[clang::annotate("expose")]]
int yetty_ygui_splitter_get_axis(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return -1;
    }
    struct yetty_yclass_ptr_result cr = yetty_ygui_splitter_class_get();
    if (YETTY_IS_ERR(cr)) {
        yetty_ycore_error_destroy(cr.error);
        return -1;
    }
    if (obj->klass != cr.value) {
        return -1;
    }
    const struct splitter_data *d =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, cr.value);
    return d->axis_plus1 == 0 ? -1 : d->axis_plus1 - 1;
}

[[clang::annotate("expose")]]
void yetty_ygui_splitter_set_min(struct yetty_ygui_object *obj, float min_size)
{
    if (!obj) {
        return;
    }
    struct yetty_yclass_ptr_result cr = yetty_ygui_splitter_class_get();
    if (YETTY_IS_ERR(cr)) {
        yetty_ycore_error_destroy(cr.error);
        return;
    }
    if (obj->klass != cr.value) {
        return;
    }
    struct splitter_data *d = yetty_ygui_data_get(obj, cr.value);
    d->min_size = min_size;
}

[[clang::annotate("expose")]]
void yetty_ygui_splitter_on_change(struct yetty_ygui_object *obj,
                                   yetty_ygui_splitter_change_cb cb, void *userdata)
{
    if (!obj) {
        return;
    }
    struct yetty_yclass_ptr_result cr = yetty_ygui_splitter_class_get();
    if (YETTY_IS_ERR(cr)) {
        yetty_ycore_error_destroy(cr.error);
        return;
    }
    if (obj->klass != cr.value) {
        return;
    }
    struct splitter_data *d = yetty_ygui_data_get(obj, cr.value);
    d->change_cb = cb;
    d->change_userdata = userdata;
}

#include "splitter.gen.c"

#ifdef YCLASS_CODEGEN
struct yetty_ygui_object;
typedef void (*yetty_ygui_splitter_change_cb)(struct yetty_ygui_object *splitter, float delta,
                                              void *userdata);
#endif
