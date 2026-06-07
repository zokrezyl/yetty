/*
 * ygui-ynodes.c — node-graph editor canvas.
 *
 * Hosts ynode children (absolutely positioned via their graph rect +
 * this editor's pan/zoom view), paints a grid background plus every
 * connection link as a bezier-tessellated polyline, and drives canvas
 * navigation (drag empty canvas = pan, wheel = zoom). Drag-to-connect is
 * started by a ynode when a pin is pressed (begin_link) and completed
 * here on release (end_link), which hit-tests the pin under the cursor.
 *
 * The editor promotes itself to its own YGRID figure so the renderer
 * GPU-scissors its content to its rect — nodes panned past the edge are
 * clipped rather than bleeding over sibling widgets. Coordinates stay
 * absolute, so layout / hit-test / paint need no special-casing.
 *
 * Zoom scales geometry (node rects, pins, grid, link curvature). Embedded
 * child-widget content (text, glyphs) is NOT rescaled — ygui paint has no
 * transform matrix — so at zoom != 1 a node's contents keep their native
 * pixel size. True content zoom would need a render-target transform.
 */

#include "../internal.h"
#include "paint-helpers.h"

#include <yetty/yfigure/wire.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/popup_menu.h>
#include <yetty/ygui/widgets/ynode.h>
#include <yetty/ygui/widgets/ynodes.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define YNODES_CANVAS_BG 0xFF14100Bu    /* BRAND_BG        #0B1014 */
#define YNODES_GRID 0xFF1F1A14u         /* BRAND_BG_LIFTED #141A1F */
#define YNODES_LINK 0xFF92A86Bu         /* BRAND_ACCENT    #6BA892 */
#define YNODES_LINK_PENDING 0xFFA5C574u /* BRAND_ACCENT_BRIGHT #74C5A5 */

#define YNODES_GRID_SIZE 24.0f
#define YNODES_GRID_MIN_STEP 8.0f
#define YNODES_LINK_WIDTH 2.0f
#define YNODES_BEZIER_SEGMENTS 20
#define YNODES_ZOOM_MIN 0.5f
#define YNODES_ZOOM_MAX 2.0f

struct ynodes_link {
    struct yetty_ygui_object *from; /* output-side node */
    int from_pin;
    struct yetty_ygui_object *to; /* input-side node */
    int to_pin;
    uint32_t color;
};

/* One insertable widget kind offered by the node context menu. */
struct ynodes_palette_entry {
    char *label;
    const struct yetty_yclass *cls; /* borrowed */
};

struct [[clang::annotate("class@ygui:ynodes")]] [[clang::annotate("parent@ygui:primitive_widget")]]
nodes_data {
    float pan_x, pan_y;
    float zoom;

    struct ynodes_link *links;
    size_t link_count, link_cap;

    /* Canvas pan drag. */
    int panning;
    float last_x, last_y;

    /* Pending link drag (a ynode owns the pointer capture; it forwards
     * motion/release here). */
    int linking;
    struct yetty_ygui_object *link_from;
    int link_from_pin;
    int link_from_output;
    float link_cur_x, link_cur_y;

    yetty_ygui_ynodes_link_cb link_cb;
    void *link_userdata;

    /* Lazily-created right-click context menu (a popup_menu child),
     * repopulated per open. menu_graph_{x,y} records the graph-space
     * point where the canvas menu opened, so "Add node" lands there;
     * menu_node is the node a node-menu currently targets. */
    struct yetty_ygui_object *menu;
    float menu_graph_x, menu_graph_y;
    struct yetty_ygui_object *menu_node;

    /* Insertable-widget palette (see register_widget). */
    struct ynodes_palette_entry *palette;
    size_t palette_count, palette_cap;
};

YETTY_EXTERNAL_CALLBACK
static const struct yetty_yclass *ynodes_class(void)
{
    return yetty_ygui_ynodes_class_get().value;
}

static struct nodes_data *ynodes_data(struct yetty_ygui_object *editor)
{
    return yetty_ygui_data_get(editor, ynodes_class());
}

YETTY_EXTERNAL_CALLBACK
static int ynodes_child_is_node(struct yetty_ygui_object *child)
{
    struct yetty_yclass_ptr_result cr = yetty_ygui_ynode_class_get();
    if (YETTY_IS_ERR(cr)) {
        yetty_ycore_error_destroy(cr.error);
        return 0;
    }
    return child->klass == cr.value;
}

/*-----------------------------------------------------------------------------
 * Lifecycle.
 *---------------------------------------------------------------------------*/
[[clang::annotate("override@ygui:ynodes:constructor")]]
static struct yetty_ycore_void_result ynodes_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                         struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, ynodes_class(), (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ynodes_constructor: super");

    struct nodes_data *d = ynodes_data(obj);
    d->pan_x = d->pan_y = 0.0f;
    d->zoom = 1.0f;
    d->links = NULL;
    d->link_count = d->link_cap = 0;
    d->panning = 0;
    d->last_x = d->last_y = 0.0f;
    d->linking = 0;
    d->link_from = NULL;
    d->link_from_pin = 0;
    d->link_from_output = 0;
    d->link_cur_x = d->link_cur_y = 0.0f;
    d->link_cb = NULL;
    d->link_userdata = NULL;
    d->menu = NULL;
    d->menu_graph_x = d->menu_graph_y = 0.0f;
    d->menu_node = NULL;
    d->palette = NULL;
    d->palette_count = d->palette_cap = 0;

    /* Own YGRID figure → GPU scissor clips nodes/links to our rect. */
    struct yetty_ycore_void_result fr =
        yetty_ygui_widget_make_figure(obj, YETTY_YFIGURE_KIND_YGRID, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "ynodes_constructor: make_figure");
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("override@ygui:ynodes:destructor")]]
static struct yetty_ycore_void_result ynodes_destructor(struct yetty_yclass_ctx *yclass_ctx,
                                                        struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct nodes_data *d = ynodes_data(obj);
    free(d->links);
    d->links = NULL;
    d->link_count = d->link_cap = 0;
    for (size_t i = 0; i < d->palette_count; i++) {
        free(d->palette[i].label);
    }
    free(d->palette);
    d->palette = NULL;
    d->palette_count = d->palette_cap = 0;
    return yetty_ygui_super_void(obj, ynodes_class(),
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

/*-----------------------------------------------------------------------------
 * View.
 *---------------------------------------------------------------------------*/
[[clang::annotate("expose")]]
void yetty_ygui_ynodes_view(const struct yetty_ygui_object *editor, float *pan_x, float *pan_y,
                            float *zoom)
{
    float px = 0.0f, py = 0.0f, z = 1.0f;
    if (editor) {
        struct nodes_data *d = ynodes_data((struct yetty_ygui_object *)editor);
        px = d->pan_x;
        py = d->pan_y;
        z = d->zoom;
    }
    if (pan_x) {
        *pan_x = px;
    }
    if (pan_y) {
        *pan_y = py;
    }
    if (zoom) {
        *zoom = z;
    }
}

[[clang::annotate("expose")]]
float yetty_ygui_ynodes_zoom(const struct yetty_ygui_object *editor)
{
    if (!editor) {
        return 1.0f;
    }
    return ynodes_data((struct yetty_ygui_object *)editor)->zoom;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ynodes_reflow(struct yetty_ygui_object *editor)
{
    if (!editor) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynodes_reflow: NULL editor");
    }
    for (struct yetty_ygui_object *c = yetty_ygui_object_first_child(editor); c;
         c = yetty_ygui_object_next_sibling(c)) {
        if (!ynodes_child_is_node(c)) {
            continue;
        }
        struct yetty_ycore_void_result rr = yetty_ygui_ynode_reflow(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "yetty_ygui_ynodes_reflow: node");
    }
    return yetty_ygui_object_set_dirty(editor);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ynodes_set_view(struct yetty_ygui_object *editor,
                                                          float pan_x, float pan_y, float zoom)
{
    if (!editor) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynodes_set_view: NULL editor");
    }
    struct nodes_data *d = ynodes_data(editor);
    if (zoom < YNODES_ZOOM_MIN) {
        zoom = YNODES_ZOOM_MIN;
    }
    if (zoom > YNODES_ZOOM_MAX) {
        zoom = YNODES_ZOOM_MAX;
    }
    d->pan_x = pan_x;
    d->pan_y = pan_y;
    d->zoom = zoom;
    return yetty_ygui_ynodes_reflow(editor);
}

/*-----------------------------------------------------------------------------
 * Links.
 *---------------------------------------------------------------------------*/
static int ynodes_has_link(struct nodes_data *d, struct yetty_ygui_object *from, int from_pin,
                           struct yetty_ygui_object *to, int to_pin)
{
    for (size_t i = 0; i < d->link_count; i++) {
        struct ynodes_link *l = &d->links[i];
        if (l->from == from && l->from_pin == from_pin && l->to == to && l->to_pin == to_pin) {
            return 1;
        }
    }
    return 0;
}

/* Append a normalized (output node → input node) link. Returns 1 if a new
 * link was stored, 0 if it already existed; the result carries OOM. */
static struct yetty_ycore_int_result ynodes_add_link(struct yetty_ygui_object *editor,
                                                     struct yetty_ygui_object *from, int from_pin,
                                                     struct yetty_ygui_object *to, int to_pin)
{
    struct nodes_data *d = ynodes_data(editor);
    if (ynodes_has_link(d, from, from_pin, to, to_pin)) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    if (d->link_count == d->link_cap) {
        size_t new_cap = d->link_cap ? d->link_cap * 2 : 8;
        struct ynodes_link *grown = realloc(d->links, new_cap * sizeof(struct ynodes_link));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_int, "ynodes_add_link: oom");
        }
        d->links = grown;
        d->link_cap = new_cap;
    }
    d->links[d->link_count] = (struct ynodes_link){
        .from = from, .from_pin = from_pin, .to = to, .to_pin = to_pin, .color = YNODES_LINK};
    d->link_count++;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(editor);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "ynodes_add_link: dirty", dr);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ynodes_link(struct yetty_ygui_object *editor,
                                                      struct yetty_ygui_object *from, int out_idx,
                                                      struct yetty_ygui_object *to, int in_idx)
{
    if (!editor || !from || !to) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynodes_link: NULL arg");
    }
    struct yetty_ycore_int_result ar = ynodes_add_link(editor, from, out_idx, to, in_idx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "yetty_ygui_ynodes_link");
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ynodes_drop_links_for(struct yetty_ygui_object *editor,
                                                                struct yetty_ygui_object *node)
{
    if (!editor || !node) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynodes_drop_links_for: NULL arg");
    }
    struct nodes_data *d = ynodes_data(editor);
    size_t kept = 0;
    for (size_t i = 0; i < d->link_count; i++) {
        if (d->links[i].from == node || d->links[i].to == node) {
            continue;
        }
        d->links[kept++] = d->links[i];
    }
    if (kept != d->link_count) {
        d->link_count = kept;
        return yetty_ygui_object_set_dirty(editor);
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ynodes_on_link_set(struct yetty_ygui_object *editor,
                                                             yetty_ygui_ynodes_link_cb cb,
                                                             void *userdata)
{
    if (!editor) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynodes_on_link_set: NULL editor");
    }
    struct nodes_data *d = ynodes_data(editor);
    d->link_cb = cb;
    d->link_userdata = userdata;
    return YETTY_OK_VOID();
}

/*-----------------------------------------------------------------------------
 * Pending-link drag (driven by a ynode that captured the pointer on a pin
 * press).
 *---------------------------------------------------------------------------*/
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ynodes_begin_link(struct yetty_ygui_object *editor,
                                                            struct yetty_ygui_object *from, int pin,
                                                            int output, float x, float y)
{
    if (!editor || !from) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynodes_begin_link: NULL arg");
    }
    struct nodes_data *d = ynodes_data(editor);
    d->linking = 1;
    d->link_from = from;
    d->link_from_pin = pin;
    d->link_from_output = output;
    d->link_cur_x = x;
    d->link_cur_y = y;
    return yetty_ygui_object_set_dirty(editor);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ynodes_update_link(struct yetty_ygui_object *editor,
                                                             float x, float y)
{
    if (!editor) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynodes_update_link: NULL editor");
    }
    struct nodes_data *d = ynodes_data(editor);
    if (!d->linking) {
        return YETTY_OK_VOID();
    }
    d->link_cur_x = x;
    d->link_cur_y = y;
    return yetty_ygui_object_set_dirty(editor);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ynodes_end_link(struct yetty_ygui_object *editor, float x,
                                                          float y)
{
    if (!editor) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynodes_end_link: NULL editor");
    }
    struct nodes_data *d = ynodes_data(editor);
    if (!d->linking) {
        return YETTY_OK_VOID();
    }
    struct yetty_ygui_object *src = d->link_from;
    int src_pin = d->link_from_pin;
    int src_output = d->link_from_output;
    d->linking = 0;
    d->link_from = NULL;

    /* Find a pin of the opposite kind under the release point. */
    for (struct yetty_ygui_object *c = yetty_ygui_object_first_child(editor); c;
         c = yetty_ygui_object_next_sibling(c)) {
        if (c == src || !ynodes_child_is_node(c)) {
            continue;
        }
        int tside = 0, tindex = 0;
        if (!yetty_ygui_ynode_pin_at(c, x, y, &tside, &tindex)) {
            continue;
        }
        struct yetty_ygui_object *from = NULL, *to = NULL;
        int from_pin = 0, to_pin = 0;
        if (src_output && tside == 0) {
            from = src;
            from_pin = src_pin;
            to = c;
            to_pin = tindex;
        } else if (!src_output && tside == 1) {
            from = c;
            from_pin = tindex;
            to = src;
            to_pin = src_pin;
        } else {
            continue; /* same kind — not connectable */
        }
        struct yetty_ycore_int_result ar = ynodes_add_link(editor, from, from_pin, to, to_pin);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "yetty_ygui_ynodes_end_link: add");
        if (ar.value && d->link_cb) {
            struct yetty_ycore_void_result cr =
                d->link_cb(editor, from, from_pin, to, to_pin, d->link_userdata);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "yetty_ygui_ynodes_end_link: callback");
        }
        break;
    }
    return yetty_ygui_object_set_dirty(editor);
}

/*-----------------------------------------------------------------------------
 * Add-node convenience.
 *---------------------------------------------------------------------------*/
[[clang::annotate("expose")]]
struct yetty_ygui_object_ptr_result yetty_ygui_ynodes_add_node(struct yetty_ygui_object *editor,
                                                               float gx, float gy)
{
    if (!editor) {
        return YETTY_ERR(yetty_ygui_object_ptr, "yetty_ygui_ynodes_add_node: NULL editor");
    }
    struct yetty_yclass_ptr_result cr = yetty_ygui_ynode_class_get();
    if (YETTY_IS_ERR(cr)) {
        return YETTY_ERR(yetty_ygui_object_ptr, "yetty_ygui_ynodes_add_node: class", cr);
    }
    struct yetty_ygui_object_ptr_result nr = yetty_ygui_add(cr.value, editor);
    YETTY_RETURN_IF_ERR(yetty_ygui_object_ptr, nr, "yetty_ygui_ynodes_add_node: add");
    struct yetty_ycore_void_result pr = yetty_ygui_ynode_set_graph_pos(nr.value, gx, gy);
    if (YETTY_IS_ERR(pr)) {
        struct yetty_ycore_void_result dr = yetty_ygui_del(nr.value);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        return YETTY_ERR(yetty_ygui_object_ptr, "yetty_ygui_ynodes_add_node: set_pos", pr);
    }
    return YETTY_OK(yetty_ygui_object_ptr, nr.value);
}

/*-----------------------------------------------------------------------------
 * Right-click context menus.
 *---------------------------------------------------------------------------*/
/* "Add node" (canvas menu): create a node at the recorded graph point,
 * with a default input + output pin. userdata = the editor. */
static struct yetty_ycore_void_result menu_add_node_cb(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *menu,
                                                       int item_index, void *userdata)
{
    (void)ctx;
    (void)menu;
    (void)item_index;
    struct yetty_ygui_object *editor = userdata;
    struct nodes_data *d = ynodes_data(editor);
    struct yetty_ygui_object_ptr_result nr =
        yetty_ygui_ynodes_add_node(editor, d->menu_graph_x, d->menu_graph_y);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, nr, "menu_add_node_cb: add_node");
    struct yetty_ycore_void_result tr = yetty_ygui_ynode_set_title(nr.value, "Node");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "menu_add_node_cb: title");
    struct uint32_result ir = yetty_ygui_ynode_add_input(nr.value, "in");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ir, "menu_add_node_cb: input");
    struct uint32_result orr = yetty_ygui_ynode_add_output(nr.value, "out");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, orr, "menu_add_node_cb: output");
    return YETTY_OK_VOID();
}

/* "Reset view" (canvas menu): pan to origin, zoom to 1. */
static struct yetty_ycore_void_result menu_reset_view_cb(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *menu,
                                                         int item_index, void *userdata)
{
    (void)ctx;
    (void)menu;
    (void)item_index;
    return yetty_ygui_ynodes_set_view(userdata, 0.0f, 0.0f, 1.0f);
}

/* Node menu rows operate on the node passed as userdata. */
static struct yetty_ycore_void_result menu_node_add_input_cb(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *menu,
                                                             int item_index, void *userdata)
{
    (void)ctx;
    (void)menu;
    (void)item_index;
    struct uint32_result r = yetty_ygui_ynode_add_input(userdata, "in");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "menu_node_add_input_cb");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result menu_node_add_output_cb(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *menu,
                                                              int item_index, void *userdata)
{
    (void)ctx;
    (void)menu;
    (void)item_index;
    struct uint32_result r = yetty_ygui_ynode_add_output(userdata, "out");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "menu_node_add_output_cb");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result menu_node_delete_cb(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *menu,
                                                          int item_index, void *userdata)
{
    (void)ctx;
    (void)menu;
    (void)item_index;
    /* del runs the node's destructor, which drops its links, then frees. */
    return yetty_ygui_del(userdata);
}

/* "Add <widget>" (node menu): instantiate the palette entry `userdata`
 * (its index) as a child of the menu's target node, then grow the node. */
static struct yetty_ycore_void_result insert_widget_cb(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *menu,
                                                       int item_index, void *userdata)
{
    (void)ctx;
    (void)item_index;
    struct yetty_ygui_object *editor = yetty_ygui_object_parent((struct yetty_ygui_object *)menu);
    if (!editor) {
        return YETTY_OK_VOID();
    }
    struct nodes_data *d = ynodes_data(editor);
    struct yetty_ygui_object *node = d->menu_node;
    int idx = (int)(intptr_t)userdata;
    if (!node || idx < 0 || (size_t)idx >= d->palette_count) {
        return YETTY_OK_VOID();
    }
    struct yetty_ygui_object_ptr_result wr = yetty_ygui_add(d->palette[idx].cls, node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "insert_widget_cb: add");
    /* Give the child a visible default height (its width stretches in the
     * node's vbox), then grow the node so the new row has room. */
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(wr.value);
    l.height = 26.0f;
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(wr.value, &l);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "insert_widget_cb: size");
    float gw = 0.0f, gh = 0.0f;
    yetty_ygui_ynode_graph_size(node, &gw, &gh);
    return yetty_ygui_ynode_set_graph_size(node, gw, gh + 32.0f);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ynodes_register_widget(struct yetty_ygui_object *editor,
                                                                 const char *label,
                                                                 const struct yetty_yclass *cls)
{
    if (!editor || !label || !cls) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynodes_register_widget: NULL arg");
    }
    struct nodes_data *d = ynodes_data(editor);
    if (d->palette_count == d->palette_cap) {
        size_t new_cap = d->palette_cap ? d->palette_cap * 2 : 8;
        struct ynodes_palette_entry *grown =
            realloc(d->palette, new_cap * sizeof(struct ynodes_palette_entry));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynodes_register_widget: oom");
        }
        d->palette = grown;
        d->palette_cap = new_cap;
    }
    char *copy = strdup(label);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynodes_register_widget: oom (label)");
    }
    d->palette[d->palette_count].label = copy;
    d->palette[d->palette_count].cls = cls;
    d->palette_count++;
    return YETTY_OK_VOID();
}

static struct yetty_ygui_object_ptr_result ynodes_ensure_menu(struct yetty_ygui_object *editor,
                                                              struct nodes_data *d)
{
    if (d->menu) {
        return YETTY_OK(yetty_ygui_object_ptr, d->menu);
    }
    struct yetty_yclass_ptr_result cr = yetty_ygui_popup_menu_class_get();
    if (YETTY_IS_ERR(cr)) {
        return YETTY_ERR(yetty_ygui_object_ptr, "ynodes_ensure_menu: class", cr);
    }
    struct yetty_ygui_object_ptr_result mr = yetty_ygui_add(cr.value, editor);
    YETTY_RETURN_IF_ERR(yetty_ygui_object_ptr, mr, "ynodes_ensure_menu: add");
    d->menu = mr.value;
    return YETTY_OK(yetty_ygui_object_ptr, d->menu);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ynodes_open_canvas_menu(struct yetty_ygui_object *editor,
                                                                  float x, float y)
{
    if (!editor) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynodes_open_canvas_menu: NULL editor");
    }
    struct nodes_data *d = ynodes_data(editor);
    struct yetty_ygui_object_ptr_result mr = ynodes_ensure_menu(editor, d);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mr, "open_canvas_menu: ensure");
    struct yetty_ygui_object *menu = mr.value;

    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(editor);
    float zoom = d->zoom > 0.0f ? d->zoom : 1.0f;
    d->menu_graph_x = (x - r.min.x - d->pan_x) / zoom;
    d->menu_graph_y = (y - r.min.y - d->pan_y) / zoom;

    struct yetty_ycore_void_result result_628 = yetty_ygui_popup_menu_clear(menu);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_628, "open_canvas_menu: clear");
    struct yetty_ycore_void_result result_630 =
        yetty_ygui_popup_menu_add_item(menu, "Add node", menu_add_node_cb, editor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_630, "open_canvas_menu: add-node item");
    struct yetty_ycore_void_result result_633 = yetty_ygui_popup_menu_add_separator(menu);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_633, "open_canvas_menu: sep");
    struct yetty_ycore_void_result result_635 =
        yetty_ygui_popup_menu_add_item(menu, "Reset view", menu_reset_view_cb, editor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_635, "open_canvas_menu: reset item");
    yetty_ygui_object_raise(menu);
    return yetty_ygui_popup_menu_open_at(menu, x - r.min.x, y - r.min.y);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ynodes_open_node_menu(struct yetty_ygui_object *editor,
                                                                struct yetty_ygui_object *node,
                                                                float x, float y)
{
    if (!editor || !node) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynodes_open_node_menu: NULL arg");
    }
    struct nodes_data *d = ynodes_data(editor);
    struct yetty_ygui_object_ptr_result mr = ynodes_ensure_menu(editor, d);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mr, "open_node_menu: ensure");
    struct yetty_ygui_object *menu = mr.value;
    d->menu_node = node;

    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(editor);
    struct yetty_ycore_void_result result_658 = yetty_ygui_popup_menu_clear(menu);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_658, "open_node_menu: clear");
    struct yetty_ycore_void_result result_659 =
        yetty_ygui_popup_menu_add_item(menu, "Add input", menu_node_add_input_cb, node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_659, "open_node_menu: add-input item");
    struct yetty_ycore_void_result result_663 =
        yetty_ygui_popup_menu_add_item(menu, "Add output", menu_node_add_output_cb, node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_663, "open_node_menu: add-output item");
    /* Insertable-widget palette: one "Add <label>" row per registered kind. */
    if (d->palette_count > 0) {
        struct yetty_ycore_void_result result_669 = yetty_ygui_popup_menu_add_separator(menu);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_669, "open_node_menu: palette sep");
        for (size_t i = 0; i < d->palette_count; i++) {
            char label[80];
            snprintf(label, sizeof(label), "Add %s", d->palette[i].label);
            struct yetty_ycore_void_result result_674 =
                yetty_ygui_popup_menu_add_item(menu, label, insert_widget_cb, (void *)(intptr_t)i);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, result_674, "open_node_menu: palette item");
        }
    }
    struct yetty_ycore_void_result result_681 = yetty_ygui_popup_menu_add_separator(menu);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_681, "open_node_menu: sep");
    struct yetty_ycore_void_result result_683 =
        yetty_ygui_popup_menu_add_item(menu, "Delete node", menu_node_delete_cb, node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_683, "open_node_menu: delete item");
    yetty_ygui_object_raise(menu);
    return yetty_ygui_popup_menu_open_at(menu, x - r.min.x, y - r.min.y);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ynodes_close_menu(struct yetty_ygui_object *editor)
{
    if (!editor) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynodes_close_menu: NULL editor");
    }
    struct nodes_data *d = ynodes_data(editor);
    if (d->menu) {
        return yetty_ygui_popup_menu_close(d->menu);
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_int_result yetty_ygui_ynodes_menu_is_open(const struct yetty_ygui_object *editor)
{
    if (!editor) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_ynodes_menu_is_open: NULL editor");
    }
    struct nodes_data *d = ynodes_data((struct yetty_ygui_object *)editor);
    if (!d->menu) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    return yetty_ygui_popup_menu_is_open(d->menu);
}

/*-----------------------------------------------------------------------------
 * Paint — bg, grid, links, pending link.
 *---------------------------------------------------------------------------*/
static float cubic_at(float p0, float p1, float p2, float p3, float t)
{
    float u = 1.0f - t;
    return u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t * p3;
}

/* Bezier link from (x0,y0) to (x1,y1) with horizontal tangents. `out0`
 * controls the source tangent direction: 1 = leaving to the right (an
 * output pin), 0 = leaving to the left (an input pin). The far end takes
 * the opposite tangent. */
static struct yetty_ycore_void_result ynodes_draw_link(struct yetty_ygui_emit_ctx *ctx, float x0,
                                                       float y0, float x1, float y1, int out0,
                                                       uint32_t color, float width)
{
    float reach = fabsf(x1 - x0) * 0.5f;
    if (reach < 40.0f) {
        reach = 40.0f;
    }
    float c0x = out0 ? x0 + reach : x0 - reach;
    float c1x = out0 ? x1 - reach : x1 + reach;
    float prev_x = x0, prev_y = y0;
    for (int i = 1; i <= YNODES_BEZIER_SEGMENTS; i++) {
        float t = (float)i / (float)YNODES_BEZIER_SEGMENTS;
        float px = cubic_at(x0, c0x, c1x, x1, t);
        float py = cubic_at(y0, y0, y1, y1, t);
        struct yetty_ysdf_segment seg = {
            .start_x = prev_x, .start_y = prev_y, .end_x = px, .end_y = py};
        struct yetty_ycore_void_result result_744 = yetty_ydraw_drawable_list_add_cmd_add_segment(
            ctx->ygrid_drawable_list, 0, 0, 0u, color, width, &seg);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_744, "ynodes_draw_link: segment");
        prev_x = px;
        prev_y = py;
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:ynodes:widget_paint")]]
static struct yetty_ycore_void_result ynodes_paint(struct yetty_yclass_ctx *yclass_ctx,
                                                   struct yetty_yclass_object *yclass_obj,
                                                   struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "ynodes_paint: NULL ctx");
    }
    struct nodes_data *d = ynodes_data(obj);
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }

    /* Canvas background. */
    struct yetty_ycore_void_result result_772 =
        yguix_box(ctx, r.min.x, r.min.y, w, h, YNODES_CANVAS_BG, 0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_772, "ynodes_paint: bg");

    /* Grid. */
    float step = YNODES_GRID_SIZE * d->zoom;
    if (step < YNODES_GRID_MIN_STEP) {
        step = YNODES_GRID_MIN_STEP;
    }
    float offx = fmodf(d->pan_x, step);
    if (offx < 0.0f) {
        offx += step;
    }
    for (float sx = r.min.x + offx; sx <= r.max.x; sx += step) {
        struct yetty_ysdf_segment seg = {
            .start_x = sx, .start_y = r.min.y, .end_x = sx, .end_y = r.max.y};
        struct yetty_ycore_void_result result_788 = yetty_ydraw_drawable_list_add_cmd_add_segment(
            ctx->ygrid_drawable_list, 0, 0, 0u, YNODES_GRID, 1.0f, &seg);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_788, "ynodes_paint: grid vline");
    }
    float offy = fmodf(d->pan_y, step);
    if (offy < 0.0f) {
        offy += step;
    }
    for (float sy = r.min.y + offy; sy <= r.max.y; sy += step) {
        struct yetty_ysdf_segment seg = {
            .start_x = r.min.x, .start_y = sy, .end_x = r.max.x, .end_y = sy};
        struct yetty_ycore_void_result result_800 = yetty_ydraw_drawable_list_add_cmd_add_segment(
            ctx->ygrid_drawable_list, 0, 0, 0u, YNODES_GRID, 1.0f, &seg);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_800, "ynodes_paint: grid hline");
    }

    /* Committed links: output pin → input pin. */
    float link_w = YNODES_LINK_WIDTH * d->zoom;
    if (link_w < 1.5f) {
        link_w = 1.5f;
    }
    for (size_t i = 0; i < d->link_count; i++) {
        struct ynodes_link *l = &d->links[i];
        float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        if (!yetty_ygui_ynode_pin_pos(l->from, 1, l->from_pin, &x0, &y0) ||
            !yetty_ygui_ynode_pin_pos(l->to, 0, l->to_pin, &x1, &y1)) {
            continue;
        }
        struct yetty_ycore_void_result result_818 =
            ynodes_draw_link(ctx, x0, y0, x1, y1, 1, l->color, link_w);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_818, "ynodes_paint: link");
    }

    /* Pending link following the cursor. */
    if (d->linking && d->link_from) {
        float x0 = 0.0f, y0 = 0.0f;
        if (yetty_ygui_ynode_pin_pos(d->link_from, d->link_from_output, d->link_from_pin, &x0,
                                     &y0)) {
            struct yetty_ycore_void_result result_828 =
                ynodes_draw_link(ctx, x0, y0, d->link_cur_x, d->link_cur_y, d->link_from_output,
                                 YNODES_LINK_PENDING, link_w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, result_828, "ynodes_paint: pending link");
        }
    }
    return YETTY_OK_VOID();
}

/*-----------------------------------------------------------------------------
 * Pointer — pan the canvas.
 *---------------------------------------------------------------------------*/
[[clang::annotate("override@ygui:ynodes:widget_on_press")]]
static struct yetty_ycore_int_result ynodes_on_press(struct yetty_yclass_ctx *yclass_ctx,
                                                     struct yetty_yclass_object *yclass_obj,
                                                     float x, float y, int button)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct nodes_data *d = ynodes_data(obj);

    /* A press anywhere while the menu is open dismisses it (clicks inside
     * the menu are consumed by the menu itself, raised above us). */
    if (d->menu) {
        struct yetty_ycore_int_result open_r = yetty_ygui_popup_menu_is_open(d->menu);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, open_r, "ynodes_on_press: is_open");
        if (open_r.value) {
            struct yetty_ycore_void_result cr = yetty_ygui_popup_menu_close(d->menu);
            if (YETTY_IS_ERR(cr)) {
                return YETTY_ERR(yetty_ycore_int, "ynodes_on_press: close menu", cr);
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }
    /* Right-press on empty canvas → context menu; left-press → pan. */
    if (button == 1) {
        struct yetty_ycore_void_result mr = yetty_ygui_ynodes_open_canvas_menu(obj, x, y);
        if (YETTY_IS_ERR(mr)) {
            return YETTY_ERR(yetty_ycore_int, "ynodes_on_press: canvas menu", mr);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    d->panning = 1;
    d->last_x = x;
    d->last_y = y;
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:ynodes:widget_on_motion")]]
static struct yetty_ycore_int_result ynodes_on_motion(struct yetty_yclass_ctx *yclass_ctx,
                                                      struct yetty_yclass_object *yclass_obj,
                                                      float x, float y)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct nodes_data *d = ynodes_data(obj);
    if (!d->panning) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    d->pan_x += x - d->last_x;
    d->pan_y += y - d->last_y;
    d->last_x = x;
    d->last_y = y;
    struct yetty_ycore_void_result rr = yetty_ygui_ynodes_reflow(obj);
    if (YETTY_IS_ERR(rr)) {
        return YETTY_ERR(yetty_ycore_int, "ynodes_on_motion: reflow", rr);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:ynodes:widget_on_release")]]
static struct yetty_ycore_int_result ynodes_on_release(struct yetty_yclass_ctx *yclass_ctx,
                                                       struct yetty_yclass_object *yclass_obj,
                                                       float x, float y, int button)
{
    (void)yclass_ctx;
    (void)x;
    (void)y;
    (void)button;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct nodes_data *d = ynodes_data(obj);
    d->panning = 0;
    return YETTY_OK(yetty_ycore_int, 1);
}

/* Wheel → zoom toward the cursor (the graph point under the pointer stays
 * put). */
[[clang::annotate("override@ygui:ynodes:widget_on_scroll")]]
static struct yetty_ycore_int_result ynodes_on_scroll(struct yetty_yclass_ctx *yclass_ctx,
                                                      struct yetty_yclass_object *yclass_obj,
                                                      float x, float y, float dx, float dy)
{
    (void)yclass_ctx;
    (void)dx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct nodes_data *d = ynodes_data(obj);
    if (dy == 0.0f) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float old_zoom = d->zoom;
    float new_zoom = old_zoom * (dy > 0.0f ? 1.1f : 1.0f / 1.1f);
    if (new_zoom < YNODES_ZOOM_MIN) {
        new_zoom = YNODES_ZOOM_MIN;
    }
    if (new_zoom > YNODES_ZOOM_MAX) {
        new_zoom = YNODES_ZOOM_MAX;
    }
    if (new_zoom == old_zoom) {
        return YETTY_OK(yetty_ycore_int, 1);
    }
    /* graph_x = (x - r.min.x - pan_x) / zoom is invariant across the zoom. */
    float graph_x = (x - r.min.x - d->pan_x) / old_zoom;
    float graph_y = (y - r.min.y - d->pan_y) / old_zoom;
    d->zoom = new_zoom;
    d->pan_x = x - r.min.x - graph_x * new_zoom;
    d->pan_y = y - r.min.y - graph_y * new_zoom;
    struct yetty_ycore_void_result rr = yetty_ygui_ynodes_reflow(obj);
    if (YETTY_IS_ERR(rr)) {
        return YETTY_ERR(yetty_ycore_int, "ynodes_on_scroll: reflow", rr);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

#include "ynodes.gen.c"

#ifdef YCLASS_CODEGEN
#include <yetty/ycore/result.h>
#include <yetty/ygui/object.h>
typedef struct yetty_ycore_void_result (*yetty_ygui_ynodes_link_cb)(
    struct yetty_ygui_object *editor, struct yetty_ygui_object *from, int out_idx,
    struct yetty_ygui_object *to, int in_idx, void *userdata);
#endif
