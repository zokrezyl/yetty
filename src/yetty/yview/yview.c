/*
 * yview.c — client emitter for a server-side scrollable content surface.
 *
 * Builds figure-tree wire records into a ydraw-core drawable list (the raw
 * `{length,id,payload}` record stream, NOT the serialize() scene blob) and
 * ships them on DCS YETTY_DCS_YCOMPOSITOR_BIN via yface. The receiving
 * yetty's root figure container mints/updates the child figure; scrolling and
 * clipping are then server-side state.
 */
#include <yetty/yview/yview.h>

#include <yetty/yface/yface.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/yterminal/dcs-codes.h>

#include <stdlib.h>
#include <string.h>

struct yetty_yview {
    int fd;
    uint32_t child_id;
    uint32_t kind;
    uint32_t flags;
    uint32_t bg_color;
    struct yetty_ycore_rectangle rect;
    /* Content extent in px (0 = unknown / same as rect). */
    float content_w;
    float content_h;
    /* Last applied scroll offset (shadow of the server state, for _by + clamp). */
    float scroll_x;
    float scroll_y;
    /* CREATE_CHILD emitted at least once. */
    int created;
};

/* Ship a built record stream as one YCOMPOSITOR_BIN envelope. */
static struct yetty_ycore_void_result yview_emit_records(struct yetty_yview *view,
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

/* Append an admin record whose payload is `{u32 admin_op, u32 child_id, f32, f32}`
 * (the SET_CHILD_SCROLL / SET_CHILD_CONTENT_SIZE shape). add_record frames it
 * as `{length, id=0, payload}` — id=0 marks it admin. */
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
 * Authored as the first prim so it composites UNDER the content glyphs, and
 * sized to the full content extent so it always fills the visible window at any
 * scroll offset (the box scrolls with the content but spans it). Matches the
 * SDF ROUNDED_BOX wire record ygrid expects: 5-word header (type, z_order,
 * fill, stroke, stroke_width) + 8 geom floats (cx, cy, hw, hh, r,r,r,r). */
static struct yetty_ycore_void_result add_background_box(struct yetty_ydraw_drawable_list *buf,
                                                         uint32_t bg_color, float box_w, float box_h)
{
    float cx = box_w * 0.5f;
    float cy = box_h * 0.5f;
    uint32_t record[13];
    record[0] = (uint32_t)YETTY_YSDF_ROUNDED_BOX;
    record[1] = 0u;        /* z_order within the figure */
    record[2] = bg_color;  /* fill (0xAARRGGBB) */
    record[3] = 0u;        /* stroke color */
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

struct yetty_yview_ptr_result yetty_yview_create(const struct yetty_yview_config *config)
{
    if (!config) {
        return YETTY_ERR(yetty_yview_ptr, "yetty_yview_create: NULL config");
    }
    struct yetty_yview *view = calloc(1, sizeof(struct yetty_yview));
    if (!view) {
        return YETTY_ERR(yetty_yview_ptr, "yetty_yview_create: alloc oom");
    }
    view->fd = config->fd;
    view->child_id = config->child_id ? config->child_id : 1u;
    view->kind = config->kind ? config->kind : (uint32_t)YETTY_YFIGURE_KIND_YGRID;
    view->flags = config->flags;
    view->bg_color = config->bg_color;
    view->rect = config->rect;
    return YETTY_OK(yetty_yview_ptr, view);
}

struct yetty_ycore_void_result yetty_yview_set_content(
    struct yetty_yview *view, const struct yetty_ydraw_drawable_list *content)
{
    if (!view || !content) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_content: NULL arg");
    }

    const void *content_data = yetty_ydraw_drawable_list_data(content);
    size_t content_size = yetty_ydraw_drawable_list_size(content);

    /* Content extent from the scene bounds. When larger than the rect the
     * child becomes a scroll viewport. */
    float content_w = yetty_ydraw_drawable_list_scene_max_x(content) -
                      yetty_ydraw_drawable_list_scene_min_x(content);
    float content_h = yetty_ydraw_drawable_list_scene_max_y(content) -
                      yetty_ydraw_drawable_list_scene_min_y(content);
    view->content_w = content_w > 0.0f ? content_w : 0.0f;
    view->content_h = content_h > 0.0f ? content_h : 0.0f;

    struct yetty_ydraw_drawable_list_result env_r =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, env_r, "yetty_yview_set_content: envelope create");
    struct yetty_ydraw_drawable_list *env = env_r.value;

    /* CREATE_CHILD mints (or, by reused id, refreshes) the child figure. Build
     * the init payload inline: an optional opaque background box (under), then
     * the content prim stream. begin/end backfills the record + init length. */
    struct yetty_ydraw_id_result marker_r = yetty_ydraw_drawable_list_begin_admin_create_child(
        env, view->child_id, view->kind, view->rect.min.x, view->rect.min.y, view->rect.max.x,
        view->rect.max.y);
    if (YETTY_IS_ERR(marker_r)) {
        yetty_ydraw_drawable_list_destroy(env);
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_content: begin create_child", marker_r);
    }

    /* Opaque background (alpha > 0), YGRID only — sized to the full content
     * extent so it fills the viewport at any scroll offset. */
    if ((view->bg_color & 0xFF000000u) && view->kind == (uint32_t)YETTY_YFIGURE_KIND_YGRID) {
        float rect_w = view->rect.max.x - view->rect.min.x;
        float rect_h = view->rect.max.y - view->rect.min.y;
        float box_w = view->content_w > rect_w ? view->content_w : rect_w;
        float box_h = view->content_h > rect_h ? view->content_h : rect_h;
        struct yetty_ycore_void_result bg = add_background_box(env, view->bg_color, box_w, box_h);
        if (YETTY_IS_ERR(bg)) {
            yetty_ydraw_drawable_list_destroy(env);
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_content: background", bg);
        }
    }

    if (content_size > 0) {
        struct yetty_ydraw_id_result pr =
            yetty_ydraw_drawable_list_add_prim(env, content_data, content_size);
        if (YETTY_IS_ERR(pr)) {
            yetty_ydraw_drawable_list_destroy(env);
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_content: content prims", pr);
        }
    }

    struct yetty_ycore_void_result cr =
        yetty_ydraw_drawable_list_end_admin_create_child(env, marker_r.value);
    if (YETTY_IS_ERR(cr)) {
        yetty_ydraw_drawable_list_destroy(env);
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_content: end create_child", cr);
    }

    /* Declare the content extent so the child becomes a scroll viewport. */
    if (view->content_w > 0.0f || view->content_h > 0.0f) {
        struct yetty_ycore_void_result szr =
            append_admin_id_f32x2(env, (uint32_t)YETTY_YFIGURE_ADMIN_SET_CHILD_CONTENT_SIZE,
                                  view->child_id, view->content_w, view->content_h);
        if (YETTY_IS_ERR(szr)) {
            yetty_ydraw_drawable_list_destroy(env);
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_content: content_size", szr);
        }
    }

    struct yetty_ycore_void_result er = yview_emit_records(view, env);
    yetty_ydraw_drawable_list_destroy(env);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "yetty_yview_set_content: emit");
    view->created = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yview_set_content_size(struct yetty_yview *view,
                                                            float content_w, float content_h)
{
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_content_size: NULL view");
    }
    view->content_w = content_w > 0.0f ? content_w : 0.0f;
    view->content_h = content_h > 0.0f ? content_h : 0.0f;

    struct yetty_ydraw_drawable_list_result env_r =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, env_r, "yetty_yview_set_content_size: envelope create");
    struct yetty_ydraw_drawable_list *env = env_r.value;
    struct yetty_ycore_void_result ar =
        append_admin_id_f32x2(env, (uint32_t)YETTY_YFIGURE_ADMIN_SET_CHILD_CONTENT_SIZE,
                              view->child_id, view->content_w, view->content_h);
    if (YETTY_IS_ERR(ar)) {
        yetty_ydraw_drawable_list_destroy(env);
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_content_size: append", ar);
    }
    struct yetty_ycore_void_result er = yview_emit_records(view, env);
    yetty_ydraw_drawable_list_destroy(env);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "yetty_yview_set_content_size: emit");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yview_scroll_to(struct yetty_yview *view, float scroll_x,
                                                     float scroll_y)
{
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_scroll_to: NULL view");
    }
    float viewport_w = view->rect.max.x - view->rect.min.x;
    float viewport_h = view->rect.max.y - view->rect.min.y;
    /* Clamp only on axes whose content extent is known (>0). */
    view->scroll_x = view->content_w > 0.0f ? clamp_scroll(scroll_x, view->content_w, viewport_w)
                                            : (scroll_x > 0.0f ? scroll_x : 0.0f);
    view->scroll_y = view->content_h > 0.0f ? clamp_scroll(scroll_y, view->content_h, viewport_h)
                                            : (scroll_y > 0.0f ? scroll_y : 0.0f);

    struct yetty_ydraw_drawable_list_result env_r =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, env_r, "yetty_yview_scroll_to: envelope create");
    struct yetty_ydraw_drawable_list *env = env_r.value;
    struct yetty_ycore_void_result ar =
        append_admin_id_f32x2(env, (uint32_t)YETTY_YFIGURE_ADMIN_SET_CHILD_SCROLL, view->child_id,
                              view->scroll_x, view->scroll_y);
    if (YETTY_IS_ERR(ar)) {
        yetty_ydraw_drawable_list_destroy(env);
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_scroll_to: append", ar);
    }
    struct yetty_ycore_void_result er = yview_emit_records(view, env);
    yetty_ydraw_drawable_list_destroy(env);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "yetty_yview_scroll_to: emit");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yview_scroll_by(struct yetty_yview *view, float delta_x,
                                                     float delta_y)
{
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_scroll_by: NULL view");
    }
    return yetty_yview_scroll_to(view, view->scroll_x + delta_x, view->scroll_y + delta_y);
}

struct yetty_ycore_void_result yetty_yview_set_rect(struct yetty_yview *view,
                                                    struct yetty_ycore_rectangle rect)
{
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_rect: NULL view");
    }
    view->rect = rect;

    struct yetty_ydraw_drawable_list_result env_r =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, env_r, "yetty_yview_set_rect: envelope create");
    struct yetty_ydraw_drawable_list *env = env_r.value;
    struct yetty_ycore_void_result ar = yetty_ydraw_drawable_list_add_admin_set_child_rect(
        env, view->child_id, rect.min.x, rect.min.y, rect.max.x, rect.max.y);
    if (YETTY_IS_ERR(ar)) {
        yetty_ydraw_drawable_list_destroy(env);
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_rect: append", ar);
    }
    struct yetty_ycore_void_result er = yview_emit_records(view, env);
    yetty_ydraw_drawable_list_destroy(env);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "yetty_yview_set_rect: emit");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yview_destroy(struct yetty_yview *view)
{
    if (!view) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result result = YETTY_OK_VOID();
    if (view->created) {
        struct yetty_ydraw_drawable_list_result env_r =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        if (YETTY_IS_OK(env_r)) {
            struct yetty_ydraw_drawable_list *env = env_r.value;
            struct yetty_ycore_void_result ar =
                yetty_ydraw_drawable_list_add_admin_delete_child(env, view->child_id);
            if (YETTY_IS_OK(ar)) {
                result = yview_emit_records(view, env);
            } else {
                result = YETTY_ERR(yetty_ycore_void, "yetty_yview_destroy: delete_child", ar);
            }
            yetty_ydraw_drawable_list_destroy(env);
        } else {
            result = YETTY_ERR(yetty_ycore_void, "yetty_yview_destroy: envelope create", env_r);
        }
    }
    free(view);
    return result;
}
