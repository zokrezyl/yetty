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
 *         yetty_yview_configure(NULL, obj, fd, child_id, kind, bg, rect…);
 *         yetty_yview_set_content(NULL, obj, drawables);
 *         yetty_yview_scroll_to / _scroll_by / _set_rect / _set_content_size;
 *         yetty_yview_destroy(NULL, obj);   // clears the surface + frees
 */
#include <yetty/yclass/class.h>
#include <yetty/yview/view.h>

#include <yetty/yface/yface.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/yterminal/dcs-codes.h>

#include <stdlib.h>
#include <string.h>

/* Default figure kind when configure() is passed 0: a text/SDF grid, the only
 * scrollable kind. */
#define YVIEW_DEFAULT_KIND ((uint32_t)YETTY_YFIGURE_KIND_YGRID)

struct [[clang::annotate("class@yview:view")]] view_data {
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

static struct view_data *view_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yview_view_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yetty_ycore_error_destroy(class_r.error);
        return NULL;
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        yetty_ycore_error_destroy(slice_r.error);
        return NULL;
    }
    return (struct view_data *)slice_r.value;
}

/* Ship a built record stream as one YCOMPOSITOR_BIN envelope. */
static struct yetty_ycore_void_result view_emit_records(struct view_data *view,
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
                                                         uint32_t bg_color, float box_w, float box_h)
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
[[clang::annotate("override@yview:view:configure")]] [[clang::annotate("local@yview:configure")]]
static struct yetty_ycore_void_result view_configure(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj, int fd,
                                                     uint32_t child_id, uint32_t kind,
                                                     uint32_t bg_color, float min_x, float min_y,
                                                     float max_x, float max_y)
{
    (void)ctx;
    struct view_data *view = view_from_obj(obj);
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "yview configure: bad object");
    }
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
[[clang::annotate("override@yview:view:set_content")]] [[clang::annotate(
    "local@yview:set_content")]]
static struct yetty_ycore_void_result view_set_content(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    const struct yetty_ydraw_drawable_list *content)
{
    (void)ctx;
    struct view_data *view = view_from_obj(obj);
    if (!view || !content) {
        return YETTY_ERR(yetty_ycore_void, "yview set_content: bad arg");
    }

    const void *content_data = yetty_ydraw_drawable_list_data(content);
    size_t content_size = yetty_ydraw_drawable_list_size(content);

    float content_w = yetty_ydraw_drawable_list_scene_max_x(content) -
                      yetty_ydraw_drawable_list_scene_min_x(content);
    float content_h = yetty_ydraw_drawable_list_scene_max_y(content) -
                      yetty_ydraw_drawable_list_scene_min_y(content);
    view->content_w = content_w > 0.0f ? content_w : 0.0f;
    view->content_h = content_h > 0.0f ? content_h : 0.0f;

    struct yetty_ydraw_drawable_list_result env_r =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, env_r, "yview set_content: envelope create");
    struct yetty_ydraw_drawable_list *env = env_r.value;

    struct yetty_ydraw_id_result marker_r = yetty_ydraw_drawable_list_begin_admin_create_child(
        env, view->child_id, view->kind, view->rect.min.x, view->rect.min.y, view->rect.max.x,
        view->rect.max.y);
    if (YETTY_IS_ERR(marker_r)) {
        yetty_ydraw_drawable_list_destroy(env);
        return YETTY_ERR(yetty_ycore_void, "yview set_content: begin create_child", marker_r);
    }

    if ((view->bg_color & 0xFF000000u) && view->kind == YVIEW_DEFAULT_KIND) {
        float rect_w = view->rect.max.x - view->rect.min.x;
        float rect_h = view->rect.max.y - view->rect.min.y;
        float box_w = view->content_w > rect_w ? view->content_w : rect_w;
        float box_h = view->content_h > rect_h ? view->content_h : rect_h;
        struct yetty_ycore_void_result bg = add_background_box(env, view->bg_color, box_w, box_h);
        if (YETTY_IS_ERR(bg)) {
            yetty_ydraw_drawable_list_destroy(env);
            return YETTY_ERR(yetty_ycore_void, "yview set_content: background", bg);
        }
    }

    if (content_size > 0) {
        struct yetty_ydraw_id_result pr =
            yetty_ydraw_drawable_list_add_prim(env, content_data, content_size);
        if (YETTY_IS_ERR(pr)) {
            yetty_ydraw_drawable_list_destroy(env);
            return YETTY_ERR(yetty_ycore_void, "yview set_content: content prims", pr);
        }
    }

    struct yetty_ycore_void_result cr =
        yetty_ydraw_drawable_list_end_admin_create_child(env, marker_r.value);
    if (YETTY_IS_ERR(cr)) {
        yetty_ydraw_drawable_list_destroy(env);
        return YETTY_ERR(yetty_ycore_void, "yview set_content: end create_child", cr);
    }

    if (view->content_w > 0.0f || view->content_h > 0.0f) {
        struct yetty_ycore_void_result szr =
            append_admin_id_f32x2(env, (uint32_t)YETTY_YFIGURE_ADMIN_SET_CHILD_CONTENT_SIZE,
                                  view->child_id, view->content_w, view->content_h);
        if (YETTY_IS_ERR(szr)) {
            yetty_ydraw_drawable_list_destroy(env);
            return YETTY_ERR(yetty_ycore_void, "yview set_content: content_size", szr);
        }
    }

    struct yetty_ycore_void_result er = view_emit_records(view, env);
    yetty_ydraw_drawable_list_destroy(env);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "yview set_content: emit");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yview:view:set_content_size")]] [[clang::annotate(
    "local@yview:set_content_size")]]
static struct yetty_ycore_void_result view_set_content_size(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj,
                                                            float content_w, float content_h)
{
    (void)ctx;
    struct view_data *view = view_from_obj(obj);
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "yview set_content_size: bad object");
    }
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

[[clang::annotate("override@yview:view:scroll_to")]] [[clang::annotate("local@yview:scroll_to")]]
static struct yetty_ycore_void_result view_scroll_to(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj,
                                                     float scroll_x, float scroll_y)
{
    (void)ctx;
    struct view_data *view = view_from_obj(obj);
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "yview scroll_to: bad object");
    }
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

[[clang::annotate("override@yview:view:scroll_by")]] [[clang::annotate("local@yview:scroll_by")]]
static struct yetty_ycore_void_result view_scroll_by(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj, float delta_x,
                                                     float delta_y)
{
    struct view_data *view = view_from_obj(obj);
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "yview scroll_by: bad object");
    }
    return view_scroll_to(ctx, obj, view->scroll_x + delta_x, view->scroll_y + delta_y);
}

[[clang::annotate("override@yview:view:set_rect")]] [[clang::annotate("local@yview:set_rect")]]
static struct yetty_ycore_void_result view_set_rect(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj, float min_x,
                                                    float min_y, float max_x, float max_y)
{
    (void)ctx;
    struct view_data *view = view_from_obj(obj);
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "yview set_rect: bad object");
    }
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
[[clang::annotate("override@yview:view:destroy")]] [[clang::annotate("local@yview:destroy")]]
static struct yetty_ycore_void_result view_destroy(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct view_data *view = view_from_obj(obj);
    struct yetty_ycore_void_result result = YETTY_OK_VOID();
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
