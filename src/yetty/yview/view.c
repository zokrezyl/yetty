/*
 * view.c — yclass class `yview:view`: client emitter for a server-side
 * scrollable content surface.
 *
 * A view is one positioned figure under a running yetty's root figure
 * container, shipped over DCS YETTY_DCS_YCOMPOSITOR_BIN (the compositor wire
 * path — NOT the scrolling ydraw layer ycat uses). The content is sent ONCE;
 * scrolling, clipping and repositioning are server-side state thereafter.
 *
 * This is a yclass class so codegen emits the public header (view.h), the
 * method dispatch, model.yaml, and the FFI / host-language binding surface.
 * The only hand-written file is this annotated .c; view.gen.c (the accessor
 * body) is #included at the foot. Every slot is `local@` — a view is an
 * in-process emitter, never proxied over RPC; the model still records the
 * methods so the binding generators can emit them.
 *
 * Usage:  obj = yetty_yview_view_create(NULL);
 *         yetty_yview_configure(obj, fd, child_id, kind, bg, rect…);
 *         yetty_yview_set_content(obj, drawables);
 *         yetty_yview_scroll_to / _scroll_by / _set_rect / _set_content_size;
 *         yetty_yview_destroy(obj);   // clears the surface + frees
 *
 * This TU deliberately does NOT include its own generated header
 * `yetty/yview/view.h` — that header is a downstream artifact for other
 * modules. The foundational types this TU and the appended view.gen.c need
 * (yclass identity, Result, rectangle) are pulled in directly here, and this
 * TU declares its own `yetty_yview_view_ptr_result` (after the class struct).
 */
#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <yetty/yface/yface.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yfigure/wire.h>
#include <yetty/yplot/yplot.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/yterminal/dcs-codes.h>

#include <stdlib.h>
#include <string.h>

/* Default figure kind when configure() is passed 0: a text/SDF grid, the only
 * scrollable kind. */
#define YVIEW_DEFAULT_KIND ((uint32_t)YETTY_YFIGURE_KIND_YGRID)

struct YETTY_ANNOTATE("class@yview:view") yetty_yview_view {
    int fd;
    uint32_t child_id;
    uint32_t kind;
    uint32_t bg_color;
    struct yetty_ycore_rectangle rect;
    /* Content extent in px (0 = unknown / same as rect). */
    float content_w;
    float content_h;
    /* Shadow of the server scroll offset, for scroll_by + clamping. */
    float scroll_x;
    float scroll_y;
};

/* Result wrapper for the view handle. Declared here (not pulled from view.h,
 * which this TU does not include) so the appended view.gen.c — which defines
 * yetty_yview_view_from() returning it — has the type in scope. The public
 * view.h publishes the identical declaration for other modules. */
YETTY_YRESULT_DECLARE(yetty_yview_view_ptr, struct yetty_yview_view *);

/* Defined in the appended view.gen.c (foot of this TU). Forward-declared here
 * because this TU does not include its own generated header — the class
 * accessor and the obj→body downcast are used by view_from_obj below. */
struct yetty_yclass_ptr_result yetty_yview_view_class_get(void);
struct yetty_yview_view_ptr_result yetty_yview_view_from(struct yetty_yclass_object *obj);

/* Resolve the object's yetty_yview_view slice, preserving the class_get / object_data
 * error chain (returned as a void-ptr result; callers cast .value). */
static struct yetty_yclass_void_ptr_result view_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yview_view_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "view_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_r, "view_from_obj: object_data");
    return slice_r;
}

/* Ship a built record stream as one YCOMPOSITOR_BIN envelope. */
static struct yetty_ycore_void_result view_emit_records(struct yetty_yview_view *view,
                                                        struct yetty_ydraw_drawable_list *records)
{
    const uint8_t *body = (const uint8_t *)yetty_ydraw_drawable_list_data(records);
    size_t body_len = yetty_ydraw_drawable_list_size(records);
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = body_len,
        .reserved = {0, 0},
    };
    return yetty_yface_emit_to_fd(view->fd, YETTY_DCS_YCOMPOSITOR_BIN, /*compressed=*/1, &meta,
                                  sizeof(meta), body, body_len);
}

/* Append an admin record `{u32 admin_op, u32 child_id, f32, f32}` framed as
 * `{length, id=0, payload}` (id=0 marks it admin). */
static struct yetty_ycore_void_result append_admin_id_f32x2(struct yetty_ydraw_drawable_list *buf,
                                                            uint32_t admin_op, uint32_t child_id,
                                                            float first, float second)
{
    uint8_t payload[4 + 4 + 4 + 4];
    memcpy(payload + 0, &admin_op, 4);
    memcpy(payload + 4, &child_id, 4);
    memcpy(payload + 8, &first, 4);
    memcpy(payload + 12, &second, 4);
    return yetty_ydraw_drawable_list_add_record(buf, /*id=*/0u, payload, sizeof(payload));
}

/* Prepend an opaque filled box covering (0,0)-(box_w,box_h) in content coords.
 * Authored first so it composites UNDER the content glyphs, and sized to the
 * full content extent so it always fills the visible window at any scroll
 * offset. Matches the SDF ROUNDED_BOX wire record (5-word header + 8 geom). */
static struct yetty_ycore_void_result add_background_box(struct yetty_ydraw_drawable_list *buf,
                                                         uint32_t bg_color, float box_w,
                                                         float box_h)
{
    float cx = box_w * 0.5f;
    float cy = box_h * 0.5f;
    uint32_t record[13];
    record[0] = (uint32_t)YETTY_YSDF_ROUNDED_BOX;
    record[1] = 0u;       /* z_order within the figure */
    record[2] = bg_color; /* fill (0xAARRGGBB) */
    record[3] = 0u;       /* stroke color */
    float stroke_width = 0.0f;
    memcpy(&record[4], &stroke_width, 4);
    float geom[8] = {cx, cy, cx, cy, 0.0f, 0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 8; i++) {
        memcpy(&record[5 + i], &geom[i], 4);
    }
    struct yetty_ydraw_id_result r =
        yetty_ydraw_drawable_list_add_prim(buf, record, sizeof(record));
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ycore_void, "add_background_box: add_prim", r);
    }
    return YETTY_OK_VOID();
}

/* Clamp a scroll axis to [0, max(0, content - viewport)]. */
static float clamp_scroll(float offset, float content, float viewport)
{
    float limit = content - viewport;
    if (limit < 0.0f) {
        limit = 0.0f;
    }
    if (offset < 0.0f) {
        return 0.0f;
    }
    if (offset > limit) {
        return limit;
    }
    return offset;
}

/*=============================================================================
 * Method slots
 *===========================================================================*/

/* configure: set the output fd, figure id/kind, opaque background, and rect.
 * Call once after create(), before set_content(). */
YETTY_ANNOTATE("virtual@yview:view:configure")
YETTY_ANNOTATE("local@yview:configure")
static struct yetty_ycore_void_result view_configure(struct yetty_yclass_object *obj, int fd,
                                                     uint32_t child_id, uint32_t kind,
                                                     uint32_t bg_color, float min_x, float min_y,
                                                     float max_x, float max_y)
{
    struct yetty_yclass_void_ptr_result view_r = view_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, view_r, "yview configure: object");
    struct yetty_yview_view *view = (struct yetty_yview_view *)view_r.value;
    view->fd = fd;
    view->child_id = child_id ? child_id : 1u;
    view->kind = kind ? kind : YVIEW_DEFAULT_KIND;
    view->bg_color = bg_color;
    view->rect = (struct yetty_ycore_rectangle){.min = {.x = min_x, .y = min_y},
                                                .max = {.x = max_x, .y = max_y}};
    return YETTY_OK_VOID();
}

/* set_content: ship the drawable list as the child figure's body (CREATE_CHILD
 * init payload), optionally under an opaque background, and declare the content
 * extent from the scene bounds. Pointer arg → local-only. */
/* Build the CREATE_CHILD envelope (optional opaque background, then the
 * content prim stream, then a content-size record) and ship it. The caller
 * sets view->content_w/h first. Shared by set_content (external drawable list)
 * and set_text (internally built list). */
static struct yetty_ycore_void_result view_ship(struct yetty_yview_view *view,
                                                const void *content_data, size_t content_size)
{
    struct yetty_ydraw_drawable_list_result env_r =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, env_r, "yview ship: envelope create");
    struct yetty_ydraw_drawable_list *env = env_r.value;

    struct yetty_ydraw_id_result marker_r = yetty_ydraw_drawable_list_begin_admin_create_child(
        env, view->child_id, view->kind, view->rect.min.x, view->rect.min.y, view->rect.max.x,
        view->rect.max.y);
    if (YETTY_IS_ERR(marker_r)) {
        yetty_ydraw_drawable_list_destroy(env);
        return YETTY_ERR(yetty_ycore_void, "yview ship: begin create_child", marker_r);
    }

    if ((view->bg_color & 0xFF000000u) && view->kind == YVIEW_DEFAULT_KIND) {
        float rect_w = view->rect.max.x - view->rect.min.x;
        float rect_h = view->rect.max.y - view->rect.min.y;
        float box_w = view->content_w > rect_w ? view->content_w : rect_w;
        float box_h = view->content_h > rect_h ? view->content_h : rect_h;
        struct yetty_ycore_void_result bg = add_background_box(env, view->bg_color, box_w, box_h);
        if (YETTY_IS_ERR(bg)) {
            yetty_ydraw_drawable_list_destroy(env);
            return YETTY_ERR(yetty_ycore_void, "yview ship: background", bg);
        }
    }

    if (content_size > 0 && content_data) {
        struct yetty_ydraw_id_result pr =
            yetty_ydraw_drawable_list_add_prim(env, content_data, content_size);
        if (YETTY_IS_ERR(pr)) {
            yetty_ydraw_drawable_list_destroy(env);
            return YETTY_ERR(yetty_ycore_void, "yview ship: content prims", pr);
        }
    }

    struct yetty_ycore_void_result cr =
        yetty_ydraw_drawable_list_end_admin_create_child(env, marker_r.value);
    if (YETTY_IS_ERR(cr)) {
        yetty_ydraw_drawable_list_destroy(env);
        return YETTY_ERR(yetty_ycore_void, "yview ship: end create_child", cr);
    }

    if (view->content_w > 0.0f || view->content_h > 0.0f) {
        struct yetty_ycore_void_result szr =
            append_admin_id_f32x2(env, (uint32_t)YETTY_YFIGURE_ADMIN_SET_CHILD_CONTENT_SIZE,
                                  view->child_id, view->content_w, view->content_h);
        if (YETTY_IS_ERR(szr)) {
            yetty_ydraw_drawable_list_destroy(env);
            return YETTY_ERR(yetty_ycore_void, "yview ship: content_size", szr);
        }
    }

    struct yetty_ycore_void_result er = view_emit_records(view, env);
    yetty_ydraw_drawable_list_destroy(env);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "yview ship: emit");
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@yview:view:set_content")
YETTY_ANNOTATE("local@yview:set_content")
static struct yetty_ycore_void_result view_set_content(
    struct yetty_yclass_object *obj, const struct yetty_ydraw_drawable_list *content)
{
    struct yetty_yclass_void_ptr_result view_r = view_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, view_r, "yview set_content: object");
    struct yetty_yview_view *view = (struct yetty_yview_view *)view_r.value;
    if (!content) {
        return YETTY_ERR(yetty_ycore_void, "yview set_content: NULL content");
    }
    float content_w = yetty_ydraw_drawable_list_scene_max_x(content) -
                      yetty_ydraw_drawable_list_scene_min_x(content);
    float content_h = yetty_ydraw_drawable_list_scene_max_y(content) -
                      yetty_ydraw_drawable_list_scene_min_y(content);
    view->content_w = content_w > 0.0f ? content_w : 0.0f;
    view->content_h = content_h > 0.0f ? content_h : 0.0f;
    struct yetty_ycore_void_result sr = view_ship(view, yetty_ydraw_drawable_list_data(content),
                                                  yetty_ydraw_drawable_list_size(content));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yview set_content: ship");
    return YETTY_OK_VOID();
}

/* Append one TEXT_DRAWABLE_LIST record (a UTF-8 run the server lays out with
 * the figure's default font) at content-local (x, y). font_id = -1 → default
 * slot. Wire layout per include/yetty/ydraw-core/text-drawable-list.h. */
static struct yetty_ycore_void_result append_text_line(struct yetty_ydraw_drawable_list *buf,
                                                       const char *text, uint32_t text_len, float x,
                                                       float y, float font_size, uint32_t color)
{
    enum { HEADER_BYTES = 8, FIXED_BYTES = 40 };
    uint32_t payload_size = (FIXED_BYTES + text_len + 3u) & ~3u;
    size_t record_size = HEADER_BYTES + payload_size;
    uint8_t *record = (uint8_t *)calloc(1, record_size);
    if (!record) {
        return YETTY_ERR(yetty_ycore_void, "yview text: record oom");
    }
    uint32_t type = 0x40000002u; /* YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST */
    uint32_t layer = 0u;
    int32_t font_id = -1;
    float rotation = 0.0f, char_spacing = 0.0f, word_spacing = 0.0f;
    memcpy(record + 0, &type, 4);
    memcpy(record + 4, &payload_size, 4);
    uint8_t *payload = record + HEADER_BYTES;
    memcpy(payload + 0, &x, 4);
    memcpy(payload + 4, &y, 4);
    memcpy(payload + 8, &font_size, 4);
    memcpy(payload + 12, &rotation, 4);
    memcpy(payload + 16, &color, 4);
    memcpy(payload + 20, &layer, 4);
    memcpy(payload + 24, &font_id, 4);
    memcpy(payload + 28, &text_len, 4);
    memcpy(payload + 32, &char_spacing, 4);
    memcpy(payload + 36, &word_spacing, 4);
    if (text_len > 0) {
        memcpy(payload + 40, text, text_len);
    }
    struct yetty_ydraw_id_result r = yetty_ydraw_drawable_list_add_prim(buf, record, record_size);
    free(record);
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ycore_void, "yview text: add_prim", r);
    }
    return YETTY_OK_VOID();
}

/* set_text: render a UTF-8 string (newline-split into lines) as the view's
 * content using the server figure's default font. The convenience FFI callers
 * want when they have text but no drawable list (the pager / nvim case).
 * font_size <= 0 selects a default. */
YETTY_ANNOTATE("virtual@yview:view:set_text")
YETTY_ANNOTATE("local@yview:set_text")
static struct yetty_ycore_void_result view_set_text(struct yetty_yclass_object *obj,
                                                    const char *text, float font_size)
{
    struct yetty_yclass_void_ptr_result view_r = view_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, view_r, "yview set_text: object");
    struct yetty_yview_view *view = (struct yetty_yview_view *)view_r.value;
    if (!text) {
        return YETTY_ERR(yetty_ycore_void, "yview set_text: NULL text");
    }
    if (font_size <= 0.0f) {
        font_size = 16.0f;
    }
    float line_height = font_size * 1.4f;
    float pad = font_size * 0.5f;
    uint32_t text_color = 0xFFE0E5E4u; /* BRAND_TEXT_PRIMARY */

    struct yetty_ydraw_drawable_list_result list_r =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_r, "yview set_text: list create");
    struct yetty_ydraw_drawable_list *list = list_r.value;

    const char *cursor = text;
    uint32_t line_index = 0;
    for (;;) {
        const char *newline = strchr(cursor, '\n');
        uint32_t len = newline ? (uint32_t)(newline - cursor) : (uint32_t)strlen(cursor);
        float baseline = pad + (float)line_index * line_height + font_size;
        struct yetty_ycore_void_result tr =
            append_text_line(list, cursor, len, pad, baseline, font_size, text_color);
        if (YETTY_IS_ERR(tr)) {
            yetty_ydraw_drawable_list_destroy(list);
            return YETTY_ERR(yetty_ycore_void, "yview set_text: line", tr);
        }
        line_index++;
        if (!newline) {
            break;
        }
        cursor = newline + 1;
    }

    float rect_w = view->rect.max.x - view->rect.min.x;
    view->content_w = rect_w > 0.0f ? rect_w : 0.0f; /* no horizontal scroll */
    view->content_h = pad * 2.0f + (float)line_index * line_height;
    yetty_ydraw_drawable_list_set_scene_bounds(list, 0.0f, 0.0f, view->content_w, view->content_h);

    struct yetty_ycore_void_result sr =
        view_ship(view, yetty_ydraw_drawable_list_data(list), yetty_ydraw_drawable_list_size(list));
    yetty_ydraw_drawable_list_destroy(list);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yview set_text: ship");
    return YETTY_OK_VOID();
}

/* set_plot: render a yplot expression (yexpr-plot syntax — "sin(x)", or
 * "f=sin(x); g=cos(x)") as the view's content: one yplot figure filling the
 * rect (a plot doesn't scroll). x_max<=x_min or y_max<=y_min selects yplot's
 * default ranges. The ygrid figure renders the yplot composite prim via the
 * terminal's composite factory. */
YETTY_ANNOTATE("virtual@yview:view:set_plot")
YETTY_ANNOTATE("local@yview:set_plot")
static struct yetty_ycore_void_result view_set_plot(struct yetty_yclass_object *obj,
                                                    const char *expr, float x_min, float x_max,
                                                    float y_min, float y_max)
{
    struct yetty_yclass_void_ptr_result view_r = view_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, view_r, "yview set_plot: object");
    struct yetty_yview_view *view = (struct yetty_yview_view *)view_r.value;
    if (!expr) {
        return YETTY_ERR(yetty_ycore_void, "yview set_plot: NULL expr");
    }

    float rect_w = view->rect.max.x - view->rect.min.x;
    float rect_h = view->rect.max.y - view->rect.min.y;
    struct yetty_yplot_render_config cfg = {
        .bounds_w = rect_w,
        .bounds_h = rect_h,
        .x_min = x_min,
        .x_max = x_max,
        .y_min = y_min,
        .y_max = y_max,
        .flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS,
    };
    if (!(x_max > x_min)) {
        cfg.x_min = -3.14159f;
        cfg.x_max = 3.14159f;
    }
    if (!(y_max > y_min)) {
        cfg.y_min = -1.5f;
        cfg.y_max = 1.5f;
    }

    struct yetty_ydraw_drawable_list_result plot_r = yetty_yplot_render(expr, strlen(expr), &cfg);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "yview set_plot: render");
    struct yetty_ydraw_drawable_list *plot = plot_r.value;

    view->content_w = rect_w > 0.0f ? rect_w : 0.0f; /* fills the rect, no scroll */
    view->content_h = rect_h > 0.0f ? rect_h : 0.0f;
    struct yetty_ycore_void_result sr =
        view_ship(view, yetty_ydraw_drawable_list_data(plot), yetty_ydraw_drawable_list_size(plot));
    yetty_ydraw_drawable_list_destroy(plot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yview set_plot: ship");
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@yview:view:set_content_size")
YETTY_ANNOTATE("local@yview:set_content_size")
static struct yetty_ycore_void_result view_set_content_size(struct yetty_yclass_object *obj,
                                                            float content_w, float content_h)
{
    struct yetty_yclass_void_ptr_result view_r = view_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, view_r, "yview set_content_size: object");
    struct yetty_yview_view *view = (struct yetty_yview_view *)view_r.value;
    view->content_w = content_w > 0.0f ? content_w : 0.0f;
    view->content_h = content_h > 0.0f ? content_h : 0.0f;

    struct yetty_ydraw_drawable_list_result env_r =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, env_r, "yview set_content_size: envelope create");
    struct yetty_ydraw_drawable_list *env = env_r.value;
    struct yetty_ycore_void_result ar =
        append_admin_id_f32x2(env, (uint32_t)YETTY_YFIGURE_ADMIN_SET_CHILD_CONTENT_SIZE,
                              view->child_id, view->content_w, view->content_h);
    if (YETTY_IS_ERR(ar)) {
        yetty_ydraw_drawable_list_destroy(env);
        return YETTY_ERR(yetty_ycore_void, "yview set_content_size: append", ar);
    }
    struct yetty_ycore_void_result er = view_emit_records(view, env);
    yetty_ydraw_drawable_list_destroy(env);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "yview set_content_size: emit");
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@yview:view:scroll_to")
YETTY_ANNOTATE("local@yview:scroll_to")
static struct yetty_ycore_void_result view_scroll_to(struct yetty_yclass_object *obj,
                                                     float scroll_x, float scroll_y)
{
    struct yetty_yclass_void_ptr_result view_r = view_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, view_r, "yview scroll_to: object");
    struct yetty_yview_view *view = (struct yetty_yview_view *)view_r.value;
    float viewport_w = view->rect.max.x - view->rect.min.x;
    float viewport_h = view->rect.max.y - view->rect.min.y;
    view->scroll_x = view->content_w > 0.0f ? clamp_scroll(scroll_x, view->content_w, viewport_w)
                                            : (scroll_x > 0.0f ? scroll_x : 0.0f);
    view->scroll_y = view->content_h > 0.0f ? clamp_scroll(scroll_y, view->content_h, viewport_h)
                                            : (scroll_y > 0.0f ? scroll_y : 0.0f);

    struct yetty_ydraw_drawable_list_result env_r =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, env_r, "yview scroll_to: envelope create");
    struct yetty_ydraw_drawable_list *env = env_r.value;
    struct yetty_ycore_void_result ar =
        append_admin_id_f32x2(env, (uint32_t)YETTY_YFIGURE_ADMIN_SET_CHILD_SCROLL, view->child_id,
                              view->scroll_x, view->scroll_y);
    if (YETTY_IS_ERR(ar)) {
        yetty_ydraw_drawable_list_destroy(env);
        return YETTY_ERR(yetty_ycore_void, "yview scroll_to: append", ar);
    }
    struct yetty_ycore_void_result er = view_emit_records(view, env);
    yetty_ydraw_drawable_list_destroy(env);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "yview scroll_to: emit");
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@yview:view:scroll_by")
YETTY_ANNOTATE("local@yview:scroll_by")
static struct yetty_ycore_void_result view_scroll_by(struct yetty_yclass_object *obj, float delta_x,
                                                     float delta_y)
{
    struct yetty_yclass_void_ptr_result view_r = view_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, view_r, "yview scroll_by: object");
    struct yetty_yview_view *view = (struct yetty_yview_view *)view_r.value;
    struct yetty_ycore_void_result r =
        view_scroll_to(obj, view->scroll_x + delta_x, view->scroll_y + delta_y);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yview scroll_by");
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@yview:view:set_rect")
YETTY_ANNOTATE("local@yview:set_rect")
static struct yetty_ycore_void_result view_set_rect(struct yetty_yclass_object *obj, float min_x,
                                                    float min_y, float max_x, float max_y)
{
    struct yetty_yclass_void_ptr_result view_r = view_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, view_r, "yview set_rect: object");
    struct yetty_yview_view *view = (struct yetty_yview_view *)view_r.value;
    view->rect = (struct yetty_ycore_rectangle){.min = {.x = min_x, .y = min_y},
                                                .max = {.x = max_x, .y = max_y}};

    struct yetty_ydraw_drawable_list_result env_r =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, env_r, "yview set_rect: envelope create");
    struct yetty_ydraw_drawable_list *env = env_r.value;
    struct yetty_ycore_void_result ar = yetty_ydraw_drawable_list_add_admin_set_child_rect(
        env, view->child_id, min_x, min_y, max_x, max_y);
    if (YETTY_IS_ERR(ar)) {
        yetty_ydraw_drawable_list_destroy(env);
        return YETTY_ERR(yetty_ycore_void, "yview set_rect: append", ar);
    }
    struct yetty_ycore_void_result er = view_emit_records(view, env);
    yetty_ydraw_drawable_list_destroy(env);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "yview set_rect: emit");
    return YETTY_OK_VOID();
}

/* destroy: clear the surface (DELETE_CHILD) and free the object. */
YETTY_ANNOTATE("virtual@yview:view:destroy")
YETTY_ANNOTATE("local@yview:destroy")
static struct yetty_ycore_void_result view_destroy(struct yetty_yclass_object *obj)
{
    /* Best-effort teardown: even if the slice can't be resolved we still free
     * the object. Stash (not swallow) any resolve error as the first error. */
    struct yetty_yclass_void_ptr_result view_r = view_from_obj(obj);
    struct yetty_yview_view *view =
        YETTY_IS_OK(view_r) ? (struct yetty_yview_view *)view_r.value : NULL;
    struct yetty_ycore_void_result result =
        YETTY_IS_ERR(view_r) ? YETTY_ERR(yetty_ycore_void, "yview destroy: object", view_r)
                             : YETTY_OK_VOID();
    if (view) {
        struct yetty_ydraw_drawable_list_result env_r =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        if (YETTY_IS_OK(env_r)) {
            struct yetty_ydraw_drawable_list *env = env_r.value;
            struct yetty_ycore_void_result ar =
                yetty_ydraw_drawable_list_add_admin_delete_child(env, view->child_id);
            if (YETTY_IS_OK(ar)) {
                result = view_emit_records(view, env);
            } else {
                result = YETTY_ERR(yetty_ycore_void, "yview destroy: delete_child", ar);
            }
            yetty_ydraw_drawable_list_destroy(env);
        } else {
            result = YETTY_ERR(yetty_ycore_void, "yview destroy: envelope create", env_r);
        }
    }
    struct yetty_ycore_void_result fr = yetty_yclass_object_free(obj);
    if (YETTY_IS_OK(result) && YETTY_IS_ERR(fr)) {
        return YETTY_ERR(yetty_ycore_void, "yview destroy: object_free", fr);
    }
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
    }
    return result;
}

#include "view.gen.c"
