/*
 * ygui-engine.c — engine lifecycle, ID allocator, two-pass wire emission.
 *
 * The engine owns app-global state for one ygui instance: wire transport
 * (output_pty), id allocator with free-list, the engine-side container
 * + ygrid that hold every widget, and reusable per-emit buffers.
 *
 * One yetty_ygui_framework_emit cycle:
 *   1) Clear per-emit buffers; ensure the engine's container + ygrid
 *      have been emitted at least once.
 *   2) Pass 1 — walk tree, dispatch yetty_ygui_widget_emit_container on
 *      every widget. Default impl writes CREATE_CHILD records for figure
 *      widgets into ctx->container_records.
 *   3) Pass 2 — walk tree, dispatch yetty_ygui_widget_emit_body. Chrome
 *      widgets append to ctx->ygrid_body (CMD_GROUP record bytes);
 *      figure widgets append to ctx->figure_bodies.
 *   4) Flush pending deletes as DELETE_CHILD records.
 *   5) Concatenate {container_records → ygrid_body → figure_bodies}
 *      and ship via yface envelope over output_pty.
 */

#include "internal.h"

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yface/yface.h>
#include <yetty/yfigure/wire.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yterm/osc-codes.h>
#include <stdlib.h>
#include <string.h>

/* The engine's ygrid id starts in a high range so it never collides
 * with the widget id allocator's first allocations. Per the wire
 * model, the receiver IS the root container — there is no
 * intermediate engine container; the ygrid is a direct child of the
 * root, alongside any figure widgets the engine emits.
 *
 * Pending a yetty service for unique terminal-wide allocation, the
 * id is a placeholder constant. */
#define YGUI_ENGINE_YGRID_ID_BASE 0xFE000001u

/*===========================================================================
 * Engine lifecycle.
 *=========================================================================*/

struct yetty_ygui_framework_ptr_result yetty_ygui_framework_create(
    struct yetty_platform_pty *output_pty)
{
    if (!output_pty) {
        return YETTY_ERR(yetty_ygui_framework_ptr, "yetty_ygui_framework_create: NULL output_pty");
    }
    struct yetty_ygui_runtime *engine = calloc(1, sizeof(*engine));
    if (!engine) {
        return YETTY_ERR(yetty_ygui_framework_ptr, "yetty_ygui_framework_create: calloc failed");
    }
    engine->output_pty = output_pty;
    /* Widget ids start at 1; the receiver uses 0 to mean "admin". */
    engine->next_id = 1;
    engine->ygrid_id = YGUI_ENGINE_YGRID_ID_BASE;
    engine->ygrid_created = 0;
    engine->viewport_w = 800.0f;
    engine->viewport_h = 600.0f;
    engine->dirty = 1;
    return YETTY_OK(yetty_ygui_framework_ptr, engine);
}

struct yetty_ycore_void_result yetty_ygui_framework_destroy(struct yetty_ygui_runtime *engine)
{
    if (!engine) {
        return YETTY_OK_VOID();
    }
    if (engine->root) {
        struct yetty_ycore_void_result r = yetty_ygui_del(engine->root);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        engine->root = NULL;
    }
    /* output_pty is borrowed — caller destroys it. */
    free(engine->free_ids);
    free(engine->pending_deletes);
    free(engine->minted_figures);
    yetty_ycore_buffer_destroy(&engine->container_records);
    if (engine->ygrid_draw_list) {
        yetty_ydraw_draw_list_destroy(engine->ygrid_draw_list);
        engine->ygrid_draw_list = NULL;
    }
    yetty_ycore_buffer_destroy(&engine->figure_bodies);
    free(engine);
    return YETTY_OK_VOID();
}

/* Membership probe + insert for the minted_figures set. Linear scan —
 * the set is small (handful of figure widgets per app). */
static int figure_is_minted(const struct yetty_ygui_runtime *engine, uint32_t id)
{
    for (size_t i = 0; i < engine->minted_figure_count; ++i) {
        if (engine->minted_figures[i] == id) return 1;
    }
    return 0;
}

static struct yetty_ycore_void_result figure_mark_minted(struct yetty_ygui_runtime *engine,
                                                         uint32_t id)
{
    if (figure_is_minted(engine, id)) return YETTY_OK_VOID();
    if (engine->minted_figure_count == engine->minted_figure_cap) {
        size_t ncap = engine->minted_figure_cap ? engine->minted_figure_cap * 2 : 8;
        uint32_t *na = realloc(engine->minted_figures, ncap * sizeof(*na));
        if (!na) return YETTY_ERR(yetty_ycore_void, "figure_mark_minted: realloc");
        engine->minted_figures = na;
        engine->minted_figure_cap = ncap;
    }
    engine->minted_figures[engine->minted_figure_count++] = id;
    return YETTY_OK_VOID();
}

static void figure_forget_minted(struct yetty_ygui_runtime *engine, uint32_t id)
{
    for (size_t i = 0; i < engine->minted_figure_count; ++i) {
        if (engine->minted_figures[i] == id) {
            engine->minted_figures[i] =
                engine->minted_figures[--engine->minted_figure_count];
            return;
        }
    }
}

struct yetty_ycore_void_result yetty_ygui_emit_ensure_child(
    struct yetty_ygui_emit_ctx *ctx, uint32_t child_id, uint32_t kind, float min_x, float min_y,
    float max_x, float max_y, const uint8_t *init_payload, uint32_t init_payload_bytes)
{
    if (!ctx || !ctx->engine) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_ensure_child: NULL ctx");
    }
    if (figure_is_minted(ctx->engine, child_id)) {
        return yetty_ygui_emit_set_child_rect(ctx, child_id, min_x, min_y, max_x, max_y);
    }
    struct yetty_ycore_void_result r = yetty_ygui_emit_create_child(
        ctx, child_id, kind, min_x, min_y, max_x, max_y, init_payload, init_payload_bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_emit_ensure_child: create");
    return figure_mark_minted(ctx->engine, child_id);
}

struct yetty_ycore_void_result yetty_ygui_framework_set_viewport(struct yetty_ygui_runtime *engine,
                                                              float width_px, float height_px)
{
    if (!engine) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_set_viewport: NULL engine");
    }
    engine->viewport_w = width_px;
    engine->viewport_h = height_px;
    engine->dirty = 1;
    return YETTY_OK_VOID();
}

void yetty_ygui_framework_viewport(const struct yetty_ygui_runtime *engine, float *width_px,
                                float *height_px)
{
    if (!engine) {
        if (width_px) {
            *width_px = 0;
        }
        if (height_px) {
            *height_px = 0;
        }
        return;
    }
    if (width_px) {
        *width_px = engine->viewport_w;
    }
    if (height_px) {
        *height_px = engine->viewport_h;
    }
}

void yetty_ygui_framework_mark_dirty(struct yetty_ygui_runtime *engine)
{
    if (engine) {
        engine->dirty = 1;
    }
}

int yetty_ygui_framework_is_dirty(const struct yetty_ygui_runtime *engine)
{
    return engine ? engine->dirty : 0;
}

void yetty_ygui_framework_set_key_cb(struct yetty_ygui_runtime *engine, yetty_ygui_key_cb cb,
                                  void *userdata)
{
    if (!engine) {
        return;
    }
    engine->key_cb = cb;
    engine->key_userdata = userdata;
}

/*-----------------------------------------------------------------------------
 * Input byte-stream decoder.
 *
 * Caller pushes raw bytes (ASCII control codes + CSI escape sequences
 * for arrows / function keys). The decoder produces YETTY_YGUI_KEY_*
 * codes and dispatches to the engine's key callback. One code path —
 * works identically regardless of whether bytes came from real stdin
 * or were synthesised by an in-process KEY_DOWN→bytes adapter.
 *---------------------------------------------------------------------------*/
static int csi_decode_mods(const char *p, int len)
{
    /* ";<n>" with <n> = 1 + modifier_bits. */
    for (int i = 0; i < len - 1; ++i) {
        if (p[i] == ';') {
            int v = 0;
            int j = i + 1;
            while (j < len && p[j] >= '0' && p[j] <= '9') {
                v = v * 10 + (p[j] - '0');
                j++;
            }
            if (v >= 1) {
                return v - 1;
            }
        }
    }
    return 0;
}

static void dispatch_key(struct yetty_ygui_runtime *engine, uint32_t key, int mods)
{
    if (engine->key_cb) {
        (void)engine->key_cb(engine, key, mods, engine->key_userdata);
    }
    engine->dirty = 1;
}

static void feed_byte(struct yetty_ygui_runtime *engine, struct yetty_ygui_input_state *st,
                     unsigned char c)
{
    switch (st->st) {
    case YETTY_YGUI_CSI_NORMAL:
        if (c == 0x1B) {
            st->st = YETTY_YGUI_CSI_ESC;
            st->params_len = 0;
            return;
        }
        dispatch_key(engine, c, 0);
        return;

    case YETTY_YGUI_CSI_ESC:
        if (c == '[') {
            st->st = YETTY_YGUI_CSI_BRACKET;
            return;
        }
        /* ESC followed by something else — emit ESC then re-process the byte. */
        dispatch_key(engine, 0x1B, 0);
        st->st = YETTY_YGUI_CSI_NORMAL;
        feed_byte(engine, st, c);
        return;

    case YETTY_YGUI_CSI_BRACKET:
        if ((c >= '0' && c <= '9') || c == ';') {
            if (st->params_len < (int)sizeof(st->params) - 1) {
                st->params[st->params_len++] = (char)c;
            }
            return;
        }
        {
            int mods = csi_decode_mods(st->params, st->params_len);
            uint32_t key = 0;
            switch (c) {
            case 'A':
                key = YETTY_YGUI_KEY_ARROW_UP;
                break;
            case 'B':
                key = YETTY_YGUI_KEY_ARROW_DOWN;
                break;
            case 'C':
                key = YETTY_YGUI_KEY_ARROW_RIGHT;
                break;
            case 'D':
                key = YETTY_YGUI_KEY_ARROW_LEFT;
                break;
            case 'H':
                key = YETTY_YGUI_KEY_HOME;
                break;
            case 'F':
                key = YETTY_YGUI_KEY_END;
                break;
            case '~':
                if (st->params_len > 0) {
                    switch (st->params[0]) {
                    case '5':
                        key = YETTY_YGUI_KEY_PAGE_UP;
                        break;
                    case '6':
                        key = YETTY_YGUI_KEY_PAGE_DOWN;
                        break;
                    case '2':
                        key = YETTY_YGUI_KEY_INSERT;
                        break;
                    case '3':
                        key = YETTY_YGUI_KEY_DELETE;
                        break;
                    default:
                        break;
                    }
                }
                break;
            default:
                break;
            }
            if (key != 0) {
                dispatch_key(engine, key, mods);
            }
        }
        st->st = YETTY_YGUI_CSI_NORMAL;
        st->params_len = 0;
        return;
    }
}

struct yetty_ycore_void_result yetty_ygui_framework_feed_input(struct yetty_ygui_runtime *engine,
                                                               const char *bytes, size_t n)
{
    if (!engine) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_feed_input: NULL engine");
    }
    if (!bytes || n == 0) {
        return YETTY_OK_VOID();
    }
    for (size_t i = 0; i < n; ++i) {
        feed_byte(engine, &engine->input, (unsigned char)bytes[i]);
    }
    return YETTY_OK_VOID();
}

/*-----------------------------------------------------------------------------
 * Mouse hit-test + dispatch.
 *
 * Walk the widget tree depth-first, picking the deepest descendant whose
 * rect contains (x, y). Dispatch on_press / on_release to that leaf
 * first; if it doesn't consume, bubble up to ancestors. Defaults on the
 * base widget class return 0 (not-consumed) so non-clickable widgets
 * naturally pass through.
 *---------------------------------------------------------------------------*/

static int rect_contains(struct yetty_ycore_rectangle r, float x, float y)
{
    return x >= r.min.x && x < r.max.x && y >= r.min.y && y < r.max.y;
}

/* Returns the deepest descendant of `node` whose rect contains (x, y),
 * or NULL if no descendant matches. Children later in sibling order
 * win over earlier ones (paint order — last-drawn is on top). */
static struct yetty_ygui_object *hit_test(struct yetty_ygui_object *node, float x, float y)
{
    if (!node) {
        return NULL;
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(node);
    if (!rect_contains(r, x, y)) {
        return NULL;
    }
    struct yetty_ygui_object *deepest = node;
    for (struct yetty_ygui_object *c = node->first_child; c; c = c->next_sibling) {
        struct yetty_ygui_object *child_hit = hit_test(c, x, y);
        if (child_hit) {
            deepest = child_hit;
        }
    }
    return deepest;
}

struct yetty_ycore_void_result yetty_ygui_framework_feed_mouse_button(
    struct yetty_ygui_runtime *engine, float x, float y, int button, int pressed, int mods)
{
    (void)mods;
    if (!engine) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_feed_mouse_button: NULL engine");
    }
    if (!engine->root) {
        return YETTY_OK_VOID();
    }
    struct yetty_ygui_object *target = hit_test(engine->root, x, y);
    while (target) {
        struct yetty_ycore_int_result r =
            pressed ? yetty_ygui_widget_on_press(target, x, y, button)
                    : yetty_ygui_widget_on_release(target, x, y, button);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ygui_framework_feed_mouse_button: on_press/release", r);
        }
        if (r.value) {
            engine->dirty = 1;
            return YETTY_OK_VOID();
        }
        target = target->parent;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_framework_feed_mouse_motion(
    struct yetty_ygui_runtime *engine, float x, float y)
{
    if (!engine) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_feed_mouse_motion: NULL engine");
    }
    if (!engine->root) {
        return YETTY_OK_VOID();
    }
    struct yetty_ygui_object *target = hit_test(engine->root, x, y);
    /* Hover bookkeeping — flip enter/leave when the deepest-hit widget
     * changes from the previous motion event. Mark both old and new
     * dirty so the next emit repaints them with the correct variant. */
    if (target != engine->hovered_obj) {
        if (engine->hovered_obj) {
            engine->hovered_obj->hovered = 0;
            engine->hovered_obj->dirty = 1;
        }
        if (target) {
            target->hovered = 1;
            target->dirty = 1;
        }
        engine->hovered_obj = target;
        engine->dirty = 1;
    }
    while (target) {
        struct yetty_ycore_int_result r = yetty_ygui_widget_on_motion(target, x, y);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ygui_framework_feed_mouse_motion: on_motion", r);
        }
        if (r.value) {
            return YETTY_OK_VOID();
        }
        target = target->parent;
    }
    return YETTY_OK_VOID();
}

struct yetty_ygui_object *yetty_ygui_framework_root(struct yetty_ygui_runtime *engine)
{
    return engine ? engine->root : NULL;
}

struct yetty_ycore_void_result yetty_ygui_framework_set_root(struct yetty_ygui_runtime *engine,
                                                          struct yetty_ygui_object *root)
{
    if (!engine || !root) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_set_root: NULL arg");
    }
    engine->root = root;
    root->engine = engine;
    /* Allocate the root's wire id retroactively. */
    if (root->id == 0) {
        struct uint32_result idr = yetty_ygui_framework_alloc_id(engine);
        if (YETTY_IS_ERR(idr)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_set_root: alloc_id failed", idr);
        }
        root->id = idr.value;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * ID allocator.
 *=========================================================================*/

struct uint32_result yetty_ygui_framework_alloc_id(struct yetty_ygui_runtime *engine)
{
    if (!engine) {
        return YETTY_ERR(uint32, "yetty_ygui_framework_alloc_id: NULL engine");
    }
    if (engine->free_id_count > 0) {
        uint32_t id = engine->free_ids[--engine->free_id_count];
        return YETTY_OK(uint32, id);
    }
    uint32_t id = engine->next_id++;
    return YETTY_OK(uint32, id);
}

struct yetty_ycore_void_result yetty_ygui_framework_free_id(struct yetty_ygui_runtime *engine,
                                                         uint32_t id)
{
    if (!engine || id == 0) {
        return YETTY_OK_VOID();
    }
    /* If the id was a minted figure, drop it from the set so a later
     * id reuse re-fires CREATE_CHILD instead of SET_CHILD_RECT. */
    figure_forget_minted(engine, id);
    /* Queue a delete for the next envelope. */
    if (engine->pending_delete_count == engine->pending_delete_cap) {
        size_t ncap = engine->pending_delete_cap ? engine->pending_delete_cap * 2 : 16;
        uint32_t *na = realloc(engine->pending_deletes, ncap * sizeof(*na));
        if (!na) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_free_id: realloc pending failed");
        }
        engine->pending_deletes = na;
        engine->pending_delete_cap = ncap;
    }
    engine->pending_deletes[engine->pending_delete_count++] = id;

    /* Push id back onto the free list. */
    if (engine->free_id_count == engine->free_id_cap) {
        size_t ncap = engine->free_id_cap ? engine->free_id_cap * 2 : 16;
        uint32_t *na = realloc(engine->free_ids, ncap * sizeof(*na));
        if (!na) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_free_id: realloc free_ids");
        }
        engine->free_ids = na;
        engine->free_id_cap = ncap;
    }
    engine->free_ids[engine->free_id_count++] = id;
    return YETTY_OK_VOID();
}

uint32_t yetty_ygui_framework_ygrid_id(const struct yetty_ygui_runtime *engine)
{
    return engine ? engine->ygrid_id : 0;
}

/*===========================================================================
 * Wire record helpers.
 *=========================================================================*/

static struct yetty_ycore_void_result write_u32_le(struct yetty_ycore_buffer *dst, uint32_t v)
{
    uint8_t bytes[4] = {(uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff),
                        (uint8_t)((v >> 16) & 0xff), (uint8_t)((v >> 24) & 0xff)};
    return yetty_ycore_buffer_write(dst, bytes, sizeof(bytes));
}

static struct yetty_ycore_void_result write_f32_le(struct yetty_ycore_buffer *dst, float v)
{
    /* IEEE-754 reinterpretation — both host (yetty's supported platforms)
     * and wire are little-endian; this is a memcpy. */
    uint32_t raw;
    memcpy(&raw, &v, sizeof(raw));
    return write_u32_le(dst, raw);
}

struct yetty_ycore_void_result yetty_ygui_wire_append_record(struct yetty_ycore_buffer *dst,
                                                             uint32_t id, const uint8_t *payload,
                                                             uint32_t payload_len)
{
    /* Record header: u32 length | u32 id. `length` is payload bytes,
     * not including the 8-byte header itself. */
    struct yetty_ycore_void_result r;
    r = write_u32_le(dst, payload_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_wire_append_record: write length");
    r = write_u32_le(dst, id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_wire_append_record: write id");
    if (payload_len > 0 && payload) {
        r = yetty_ycore_buffer_write(dst, payload, payload_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_wire_append_record: write payload");
    }
    return YETTY_OK_VOID();
}

/* Append an admin record (id=0) targeting a specific container's records
 * buffer. The bytes between admin_op and end-of-payload are caller-
 * supplied; this helper writes the {length, id=0, admin_op, …} prelude. */
static struct yetty_ycore_void_result append_admin_record(struct yetty_ycore_buffer *dst,
                                                          uint32_t admin_op, const uint8_t *body,
                                                          uint32_t body_len)
{
    /* Payload = u32 admin_op | body. */
    uint32_t total = 4 + body_len;
    struct yetty_ycore_void_result r;
    r = write_u32_le(dst, total);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "append_admin_record: write length");
    r = write_u32_le(dst, 0u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "append_admin_record: write id=0");
    r = write_u32_le(dst, admin_op);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "append_admin_record: write admin_op");
    if (body_len > 0 && body) {
        r = yetty_ycore_buffer_write(dst, body, body_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "append_admin_record: write body");
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Emit-side helpers used by widget emit_container implementations.
 *=========================================================================*/

struct yetty_ycore_void_result yetty_ygui_emit_create_child(
    struct yetty_ygui_emit_ctx *ctx, uint32_t child_id, uint32_t kind, float min_x, float min_y,
    float max_x, float max_y, const uint8_t *init_payload, uint32_t init_payload_bytes)
{
    if (!ctx || !ctx->container_records) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_create_child: NULL ctx");
    }
    /* Body layout:
     *   u32 child_id | u32 kind | f32x4 rect | u32 init_len | bytes init */
    uint32_t body_len = 4 + 4 + 16 + 4 + init_payload_bytes;
    /* Compose into a small temp buffer then append. */
    struct yetty_ycore_buffer tmp = {0};
    struct yetty_ycore_void_result r;
    r = write_u32_le(&tmp, child_id);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    r = write_u32_le(&tmp, kind);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    r = write_f32_le(&tmp, min_x);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    r = write_f32_le(&tmp, min_y);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    r = write_f32_le(&tmp, max_x);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    r = write_f32_le(&tmp, max_y);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    r = write_u32_le(&tmp, init_payload_bytes);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    if (init_payload_bytes > 0 && init_payload) {
        r = yetty_ycore_buffer_write(&tmp, init_payload, init_payload_bytes);
        if (YETTY_IS_ERR(r)) {
            goto fail;
        }
    }
    (void)body_len; /* sanity-only, body length encoded inside record */
    r = append_admin_record(ctx->container_records, YETTY_YFIGURE_ADMIN_CREATE_CHILD, tmp.data,
                            (uint32_t)tmp.size);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    yetty_ycore_buffer_destroy(&tmp);
    return YETTY_OK_VOID();
fail:
    yetty_ycore_buffer_destroy(&tmp);
    return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_create_child: write failed", r);
}

struct yetty_ycore_void_result yetty_ygui_emit_delete_child(struct yetty_ygui_emit_ctx *ctx,
                                                            uint32_t child_id)
{
    if (!ctx || !ctx->container_records) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_delete_child: NULL ctx");
    }
    struct yetty_ycore_buffer tmp = {0};
    struct yetty_ycore_void_result r = write_u32_le(&tmp, child_id);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&tmp);
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_delete_child: write child_id", r);
    }
    r = append_admin_record(ctx->container_records, YETTY_YFIGURE_ADMIN_DELETE_CHILD, tmp.data,
                            (uint32_t)tmp.size);
    yetty_ycore_buffer_destroy(&tmp);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_emit_delete_child: admin record");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_emit_figure_body(struct yetty_ygui_emit_ctx *ctx,
                                                           uint32_t figure_id,
                                                           const uint8_t *payload,
                                                           uint32_t payload_len)
{
    if (!ctx || !ctx->figure_bodies) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_figure_body: NULL ctx");
    }
    if (figure_id == 0) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_figure_body: figure_id is 0");
    }
    return yetty_ygui_wire_append_record(ctx->figure_bodies, figure_id, payload, payload_len);
}

struct yetty_ycore_void_result yetty_ygui_emit_set_child_rect(struct yetty_ygui_emit_ctx *ctx,
                                                              uint32_t child_id, float min_x,
                                                              float min_y, float max_x, float max_y)
{
    if (!ctx || !ctx->container_records) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_set_child_rect: NULL ctx");
    }
    struct yetty_ycore_buffer tmp = {0};
    struct yetty_ycore_void_result r;
    r = write_u32_le(&tmp, child_id);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    r = write_f32_le(&tmp, min_x);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    r = write_f32_le(&tmp, min_y);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    r = write_f32_le(&tmp, max_x);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    r = write_f32_le(&tmp, max_y);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    r = append_admin_record(ctx->container_records, YETTY_YFIGURE_ADMIN_SET_CHILD_RECT, tmp.data,
                            (uint32_t)tmp.size);
fail:
    yetty_ycore_buffer_destroy(&tmp);
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_set_child_rect: write failed", r);
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Tree walkers.
 *=========================================================================*/

/* Skip an absolute-positioned widget (popup, dialog, tooltip) when its
 * layout width or height is zero. Closed popups and closed dialogs sit
 * in the tree at width/height = 0 and rely on their own paint to
 * short-circuit on an internal `open` flag — but the framework's tree
 * walker still recurses into their children, and children paint at the
 * popup's (0, 0) resolved position, leaking text and pills onto the
 * screen. Anchor the skip to the absolute-positioned case so flex
 * children that the layout pass legitimately gave a 0-px main-axis
 * size (e.g. labels without explicit height) still emit — they paint
 * their content from `rect.min` outward and a 0-height row is intended. */
static int should_skip_subtree(const struct yetty_ygui_object *node)
{
    const struct yetty_ygui_layout *l = yetty_ygui_widget_layout_get(node);
    if (!l || !l->absolute) return 0;
    return l->width <= 0.0f || l->height <= 0.0f;
}

struct yetty_ycore_void_result yetty_ygui_framework_walk_emit_container(
    struct yetty_ygui_object *node, struct yetty_ygui_emit_ctx *ctx)
{
    if (!node) {
        return YETTY_OK_VOID();
    }
    if (should_skip_subtree(node)) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result r = yetty_ygui_widget_emit_container(node, ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                        "yetty_ygui_framework_walk_emit_container: emit_container");
    for (struct yetty_ygui_object *c = node->first_child; c; c = c->next_sibling) {
        struct yetty_ycore_void_result rc = yetty_ygui_framework_walk_emit_container(c, ctx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rc,
                            "yetty_ygui_framework_walk_emit_container: child walk");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_framework_walk_emit_body(struct yetty_ygui_object *node,
                                                                struct yetty_ygui_emit_ctx *ctx)
{
    if (!node) {
        return YETTY_OK_VOID();
    }
    if (should_skip_subtree(node)) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result r = yetty_ygui_widget_emit_body(node, ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_framework_walk_emit_body: emit_body");
    for (struct yetty_ygui_object *c = node->first_child; c; c = c->next_sibling) {
        struct yetty_ycore_void_result rc = yetty_ygui_framework_walk_emit_body(c, ctx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rc, "yetty_ygui_framework_walk_emit_body: child walk");
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Chrome setup — ensure the shared ygrid exists on the receiver.
 *
 * The receiver IS the root container; no synthetic engine container
 * is needed (the registry has no factory for kind=CONTAINER). The
 * engine just mints one ygrid as a direct child of the root. Every
 * chrome widget (figure_kind == 0) drops its prims into the ygrid's
 * body; figure widgets (figure_kind != 0) emit their own CREATE_CHILD
 * records at the same root level alongside the ygrid. */
struct yetty_ycore_void_result yetty_ygui_framework_ensure_chrome(struct yetty_ygui_runtime *engine,
                                                               struct yetty_ygui_emit_ctx *ctx)
{
    if (!engine->ygrid_created) {
        struct yetty_ycore_void_result r = yetty_ygui_emit_create_child(
            ctx, engine->ygrid_id, YETTY_YFIGURE_KIND_YGRID, 0.0f, 0.0f, engine->viewport_w,
            engine->viewport_h, NULL, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                            "yetty_ygui_framework_ensure_chrome: ygrid CREATE_CHILD");
        engine->ygrid_created = 1;
    } else {
        /* Keep the ygrid's rect in sync with the viewport. */
        struct yetty_ycore_void_result r = yetty_ygui_emit_set_child_rect(
            ctx, engine->ygrid_id, 0.0f, 0.0f, engine->viewport_w, engine->viewport_h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                            "yetty_ygui_framework_ensure_chrome: ygrid SET_CHILD_RECT");
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Flush: concatenate streams + envelope + write to output pty.
 *=========================================================================*/

static struct yetty_ycore_void_result flush_pending_deletes(struct yetty_ygui_runtime *engine,
                                                            struct yetty_ygui_emit_ctx *ctx)
{
    for (size_t i = 0; i < engine->pending_delete_count; ++i) {
        struct yetty_ycore_void_result r =
            yetty_ygui_emit_delete_child(ctx, engine->pending_deletes[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "flush_pending_deletes: DELETE_CHILD");
    }
    engine->pending_delete_count = 0;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_framework_flush(struct yetty_ygui_runtime *engine)
{
    /* Compose envelope body:
     *   - container_records (already record-framed as admin records id=0)
     *   - one record { ygrid_body bytes, id=ygrid_id }
     *   - figure_bodies (already record-framed by widgets at append time)
     */
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result r;

    if (engine->container_records.size > 0) {
        r = yetty_ycore_buffer_write(&envelope, engine->container_records.data,
                                     engine->container_records.size);
        if (YETTY_IS_ERR(r)) {
            goto fail;
        }
    }
    /* Bytes accumulated by chrome widgets into the engine's per-emit
     * ydraw draw_list. The list's primitives buffer is the raw ydraw
     * FAM stream that ygrid->process_bytes consumes. */
    if (engine->ygrid_draw_list) {
        size_t dl_size = yetty_ydraw_draw_list_size(engine->ygrid_draw_list);
        if (dl_size > 0) {
            const void *dl_data = yetty_ydraw_draw_list_data(engine->ygrid_draw_list);
            r = yetty_ygui_wire_append_record(&envelope, engine->ygrid_id, dl_data,
                                              (uint32_t)dl_size);
            if (YETTY_IS_ERR(r)) {
                goto fail;
            }
        }
    }
    if (engine->figure_bodies.size > 0) {
        r = yetty_ycore_buffer_write(&envelope, engine->figure_bodies.data,
                                     engine->figure_bodies.size);
        if (YETTY_IS_ERR(r)) {
            goto fail;
        }
    }

    if (envelope.size == 0) {
        /* Nothing to ship this tick. */
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_OK_VOID();
    }

    /* Wrap in a yface envelope and write to pty. */
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = envelope.size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_buffer wire_out = {0};
    r = yetty_yface_emit(YETTY_OSC_YCOMPOSITOR_BIN, /*compressed=*/1, &meta, sizeof(meta),
                         envelope.data, envelope.size, &wire_out);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&envelope);
        yetty_ycore_buffer_destroy(&wire_out);
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_flush: yface_emit", r);
    }
    yetty_ycore_buffer_destroy(&envelope);

    if (wire_out.size > 0 && engine->output_pty && engine->output_pty->ops &&
        engine->output_pty->ops->write) {
        struct yetty_ycore_size_result wr = engine->output_pty->ops->write(
            engine->output_pty, (const char *)wire_out.data, wire_out.size);
        if (YETTY_IS_ERR(wr)) {
            yetty_ycore_buffer_destroy(&wire_out);
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_flush: pty write", wr);
        }
        if (wr.value != wire_out.size) {
            yetty_ycore_buffer_destroy(&wire_out);
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_flush: short write");
        }
    }
    yetty_ycore_buffer_destroy(&wire_out);
    return YETTY_OK_VOID();

fail:
    yetty_ycore_buffer_destroy(&envelope);
    return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_flush: envelope compose", r);
}

struct yetty_ycore_void_result yetty_ygui_framework_emit(struct yetty_ygui_runtime *engine)
{
    if (!engine) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_emit: NULL engine");
    }

    yetty_ycore_buffer_clear(&engine->container_records);
    yetty_ycore_buffer_clear(&engine->figure_bodies);

    /* Per-emit ydraw draw_list — created lazily on first emit, reused
     * across frames. clear() rewinds the primitives byte cursor without
     * freeing the allocation. */
    if (!engine->ygrid_draw_list) {
        struct yetty_ydraw_draw_list_result dlr = yetty_ydraw_draw_list_config_buffer_create(NULL);
        if (YETTY_IS_ERR(dlr)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_emit: draw_list create", dlr);
        }
        engine->ygrid_draw_list = dlr.value;
    } else {
        yetty_ydraw_draw_list_clear(engine->ygrid_draw_list);
    }

    /* Full-redraw model: prepend CMD_ZERO so the receiver wipes its
     * ygrid display list before applying this frame's prims. Without
     * this the prims accumulate on the receiver every frame —
     * documented in include/yetty/ydraw-core/cmds.h:89. */
    {
        struct yetty_ycore_void_result zr =
            yetty_ydraw_draw_list_add_cmd_zero(engine->ygrid_draw_list);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, zr, "yetty_ygui_framework_emit: CMD_ZERO");
    }

    struct yetty_ygui_emit_ctx ctx = {
        .engine = engine,
        .container_records = &engine->container_records,
        .ygrid_draw_list = engine->ygrid_draw_list,
        .figure_bodies = &engine->figure_bodies,
        .current_figure_id = 0,
    };

    /* Run the layout pass — without this every widget's rect stays
     * (0,0)-(0,0) and paint emits zero-sized geometry. The root rect
     * is the engine's viewport. */
    if (engine->root) {
        struct yetty_ycore_rectangle root_rect = {
            .min = {0.0f, 0.0f}, .max = {engine->viewport_w, engine->viewport_h}};
        struct yetty_ycore_void_result lr = yetty_ygui_layout_compute(engine->root, root_rect);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "yetty_ygui_framework_emit: layout_compute");
    }

    /* Pending deletes go through the container records stream first. The
     * id allocator hands freed ids back via free_ids, so a widget added
     * between two emits can be assigned the id of a widget destroyed in
     * the same window. Emitting DELETE_CHILD ahead of any CREATE_CHILD
     * for that id keeps the receiver's view ordered: old gone, new in. */
    struct yetty_ycore_void_result r = flush_pending_deletes(engine, &ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_framework_emit: flush_pending_deletes");

    r = yetty_ygui_framework_ensure_chrome(engine, &ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_framework_emit: ensure_chrome");

    /* Pass 1: container records. */
    if (engine->root) {
        r = yetty_ygui_framework_walk_emit_container(engine->root, &ctx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_framework_emit: walk pass 1");
    }

    /* Pass 2: body records. */
    if (engine->root) {
        r = yetty_ygui_framework_walk_emit_body(engine->root, &ctx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_framework_emit: walk pass 2");
    }

    /* Concatenate streams and ship. */
    r = yetty_ygui_framework_flush(engine);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_framework_emit: flush");

    /* Mark the engine clean — every per-frame full re-emit is still the
     * model; per-widget dirty tracking will gate the incremental emit
     * in a follow-up. */
    engine->dirty = 0;
    return YETTY_OK_VOID();
}
