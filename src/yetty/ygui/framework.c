/*
 * framework.c — framework lifecycle, ID allocator, two-pass wire emission.
 *
 * The framework owns app-global state for one ygui instance: wire transport
 * (output_pty), id allocator with free-list, the framework-side container
 * + ygrid that hold every widget, and reusable per-emit buffers.
 *
 * One yetty_ygui_framework_emit cycle:
 *   1) Clear per-emit buffers; ensure the framework's container + ygrid
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
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/mixins/draggable.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widget.h>

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yface/yface.h>
#include <yetty/yfigure/container.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ygui/theme.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>

/* The framework's ygrid id starts in a high range so it never collides
 * with the widget id allocator's first allocations. Per the wire
 * model, the receiver IS the root container — there is no
 * intermediate framework container; the ygrid is a direct child of the
 * root, alongside any figure widgets the framework emits.
 *
 * Pending a yetty service for unique terminal-wide allocation, the
 * id is a placeholder constant. */
#define YGUI_framework_YGRID_ID_BASE 0xFE000001u

/*===========================================================================
 * framework lifecycle.
 *=========================================================================*/

struct yetty_ygui_framework_ptr_result yetty_ygui_framework_create(
    struct yetty_platform_pty *output_pty)
{
    /* output_pty may be NULL: an in-process host (yui) wires a receiver
     * container via yetty_ygui_framework_set_container_obj and never
     * touches the pty path. framework_flush only writes to output_pty in
     * the yface-over-pty fallback, which is taken only when no container
     * is wired — so a NULL pty is safe as long as a container is set
     * before the first emit. */
    struct yetty_ygui_framework *framework = calloc(1, sizeof(*framework));
    if (!framework) {
        return YETTY_ERR(yetty_ygui_framework_ptr, "yetty_ygui_framework_create: calloc failed");
    }
    framework->output_pty = output_pty;
    /* Widget ids start at 1; the receiver uses 0 to mean "admin". */
    framework->next_id = 1;
    framework->next_raise_z = YETTY_YGUI_Z_FLOATING_BASE;
    framework->ygrid_id = YGUI_framework_YGRID_ID_BASE;
    framework->ygrid_created = 0;
    framework->viewport_w = 800.0f;
    framework->viewport_h = 600.0f;
    /* Brand theme — widgets consult this at paint time. Default
     * palette is the yetty brand; yetty_ygui_framework_set_theme (or
     * apply_config_to_theme) overlays user config. The framework owns the
     * theme by default; replacement transfers ownership in. */
    framework->theme = yetty_ygui_theme_create_default();
    if (!framework->theme) {
        free(framework);
        return YETTY_ERR(yetty_ygui_framework_ptr,
                         "yetty_ygui_framework_create: theme alloc failed");
    }
    framework->dirty = 1;
    /* yclass dispatch defaults: in-process (no remote session), no
     * container wired yet. Host calls set_container_obj / set_session
     * after framework_create to opt into yclass-slot shipping. Until
     * then framework_flush falls back to the yface-over-pty path. */
    framework->yclass_ctx.session = NULL;
    framework->container_obj = NULL;
    return YETTY_OK(yetty_ygui_framework_ptr, framework);
}

struct yetty_ycore_void_result yetty_ygui_framework_set_container_obj(
    struct yetty_ygui_framework *framework, struct yetty_yclass_object *container)
{
    if (!framework) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ygui_framework_set_container_obj: NULL framework");
    }
    framework->container_obj = container;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_framework_set_session(
    struct yetty_ygui_framework *framework, struct yetty_yclass_rpc_session *session)
{
    if (!framework) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_set_session: NULL framework");
    }
    framework->yclass_ctx.session = session;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_framework_destroy(struct yetty_ygui_framework *framework)
{
    if (!framework) {
        return YETTY_OK_VOID();
    }
    if (framework->root) {
        struct yetty_ycore_void_result r = yetty_ygui_widget_destroy(framework->root);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        framework->root = NULL;
    }
    /* output_pty is borrowed — caller destroys it. */
    free(framework->free_ids);
    free(framework->pending_deletes);
    free(framework->minted_figures);
    yetty_ycore_buffer_destroy(&framework->container_records);
    if (framework->ygrid_drawable_list) {
        yetty_ydraw_drawable_list_destroy(framework->ygrid_drawable_list);
        framework->ygrid_drawable_list = NULL;
    }
    yetty_ycore_buffer_destroy(&framework->figure_bodies);
    if (framework->theme) {
        yetty_ygui_theme_destroy(framework->theme);
        framework->theme = NULL;
    }
    free(framework);
    return YETTY_OK_VOID();
}

/* Membership probe + insert for the minted_figures set. Linear scan —
 * the set is small (handful of figure widgets per app). */
static int figure_is_minted(const struct yetty_ygui_framework *framework, uint32_t id)
{
    for (size_t i = 0; i < framework->minted_figure_count; ++i) {
        if (framework->minted_figures[i] == id) {
            return 1;
        }
    }
    return 0;
}

/* Variant that also consults the in-flight emit's staged set. Used by
 * emit-time callers so a child minted earlier in the same tick isn't
 * re-CREATEd within that tick (and so a flush failure doesn't leave a
 * staged child "remembered" on the sender that the receiver never saw). */
static int figure_is_minted_or_staged(const struct yetty_ygui_emit_ctx *ctx, uint32_t id)
{
    if (!ctx) {
        return 0;
    }
    if (figure_is_minted(ctx->framework, id)) {
        return 1;
    }
    for (size_t i = 0; i < ctx->staged_mint_count; ++i) {
        if (ctx->staged_mints[i] == id) {
            return 1;
        }
    }
    return 0;
}

/* Append `id` to the per-tick staged set. Pushed onto framework->minted_figures
 * by framework_emit only after framework_flush returns OK. */
static struct yetty_ycore_void_result figure_stage_mint(struct yetty_ygui_emit_ctx *ctx,
                                                        uint32_t id)
{
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "figure_stage_mint: NULL ctx");
    }
    if (ctx->staged_mint_count == ctx->staged_mint_cap) {
        size_t ncap = ctx->staged_mint_cap ? ctx->staged_mint_cap * 2 : 8;
        uint32_t *na = realloc(ctx->staged_mints, ncap * sizeof(*na));
        if (!na) {
            return YETTY_ERR(yetty_ycore_void, "figure_stage_mint: realloc");
        }
        ctx->staged_mints = na;
        ctx->staged_mint_cap = ncap;
    }
    ctx->staged_mints[ctx->staged_mint_count++] = id;
    return YETTY_OK_VOID();
}

static void figure_forget_minted(struct yetty_ygui_framework *framework, uint32_t id)
{
    for (size_t i = 0; i < framework->minted_figure_count; ++i) {
        if (framework->minted_figures[i] == id) {
            framework->minted_figures[i] =
                framework->minted_figures[--framework->minted_figure_count];
            return;
        }
    }
}

struct yetty_ycore_void_result yetty_ygui_emit_ensure_child(
    struct yetty_ygui_emit_ctx *ctx, uint32_t child_id, uint32_t kind, float min_x, float min_y,
    float max_x, float max_y, const uint8_t *init_payload, uint32_t init_payload_bytes)
{
    ydebug("ensure_child id=%u kind=%u rect=(%.1f,%.1f)-(%.1f,%.1f) minted=%d staged=%d", child_id,
           kind, min_x, min_y, max_x, max_y,
           ctx && ctx->framework ? figure_is_minted(ctx->framework, child_id) : -1,
           ctx ? (int)ctx->staged_mint_count : -1);
    if (!ctx || !ctx->framework) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_ensure_child: NULL ctx");
    }
    /* Consult both framework state (prior ticks) and the staged set
     * (this tick): if a CREATE_CHILD for `child_id` was already
     * appended this tick, fall through to SET_CHILD_RECT just like we
     * would for a previously committed mint — the receiver will see
     * both records in order. */
    if (figure_is_minted_or_staged(ctx, child_id)) {
        return yetty_ygui_emit_set_child_rect(ctx, child_id, min_x, min_y, max_x, max_y);
    }
    struct yetty_ycore_void_result r = yetty_ygui_emit_create_child(
        ctx, child_id, kind, min_x, min_y, max_x, max_y, init_payload, init_payload_bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_emit_ensure_child: create");
    return figure_stage_mint(ctx, child_id);
}

struct yetty_ycore_void_result yetty_ygui_framework_set_viewport(
    struct yetty_ygui_framework *framework, float width_px, float height_px)
{
    if (!framework) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_set_viewport: NULL framework");
    }
    framework->viewport_w = width_px;
    framework->viewport_h = height_px;
    framework->dirty = 1;
    return YETTY_OK_VOID();
}

struct yetty_ygui_theme *yetty_ygui_framework_theme(struct yetty_ygui_framework *framework)
{
    return framework ? framework->theme : NULL;
}

struct yetty_ycore_void_result yetty_ygui_framework_set_theme(
    struct yetty_ygui_framework *framework, struct yetty_ygui_theme *theme)
{
    if (!framework) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_set_theme: NULL framework");
    }
    if (!theme) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_set_theme: NULL theme");
    }
    if (framework->theme && framework->theme != theme) {
        yetty_ygui_theme_destroy(framework->theme);
    }
    framework->theme = theme;
    framework->dirty = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_framework_apply_config_to_theme(
    struct yetty_ygui_framework *framework, const struct yetty_yconfig_config *config)
{
    if (!framework) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ygui_framework_apply_config_to_theme: NULL framework");
    }
    if (!framework->theme) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ygui_framework_apply_config_to_theme: framework has no theme");
    }
    struct yetty_ycore_void_result r = yetty_ygui_theme_apply_config(framework->theme, config);
    if (YETTY_IS_OK(r)) {
        framework->dirty = 1;
    }
    return r;
}

void yetty_ygui_framework_viewport(const struct yetty_ygui_framework *framework, float *width_px,
                                   float *height_px)
{
    if (!framework) {
        if (width_px) {
            *width_px = 0;
        }
        if (height_px) {
            *height_px = 0;
        }
        return;
    }
    if (width_px) {
        *width_px = framework->viewport_w;
    }
    if (height_px) {
        *height_px = framework->viewport_h;
    }
}

void yetty_ygui_framework_mark_dirty(struct yetty_ygui_framework *framework)
{
    if (framework) {
        framework->dirty = 1;
    }
}

int yetty_ygui_framework_is_dirty(const struct yetty_ygui_framework *framework)
{
    return framework ? framework->dirty : 0;
}

int yetty_ygui_framework_has_pressed_widget(const struct yetty_ygui_framework *framework)
{
    return framework && framework->pressed_obj ? 1 : 0;
}

struct yetty_yclass_object *yetty_ygui_framework_pressed_widget(
    struct yetty_ygui_framework *framework)
{
    return framework ? framework->pressed_obj : NULL;
}

struct yetty_yclass_object *yetty_ygui_framework_hovered_widget(
    struct yetty_ygui_framework *framework)
{
    return framework ? framework->hovered_obj : NULL;
}

void yetty_ygui_framework_notify(struct yetty_ygui_framework *framework, int severity,
                                 const char *msg)
{
    (void)framework;
    ydebug("ygui notify[%d]: %s", severity, msg ? msg : "");
}

void yetty_ygui_framework_notify_ttl(struct yetty_ygui_framework *framework, int severity,
                                     const char *msg, float ttl_seconds)
{
    (void)framework;
    (void)ttl_seconds;
    ydebug("ygui notify[%d]: %s", severity, msg ? msg : "");
}

void yetty_ygui_framework_set_key_cb(struct yetty_ygui_framework *framework, yetty_ygui_key_cb cb,
                                     void *userdata)
{
    if (!framework) {
        return;
    }
    framework->key_cb = cb;
    framework->key_userdata = userdata;
}

/*-----------------------------------------------------------------------------
 * Input byte-stream decoder.
 *
 * Caller pushes raw bytes (ASCII control codes + CSI escape sequences
 * for arrows / function keys). The decoder produces YETTY_YGUI_KEY_*
 * codes and dispatches to the framework's key callback. One code path —
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

static void dispatch_key(struct yetty_ygui_framework *framework, uint32_t key, int mods)
{
    if (framework->key_cb) {
        (void)framework->key_cb(framework, key, mods, framework->key_userdata);
    }
    framework->dirty = 1;
}

static void feed_byte(struct yetty_ygui_framework *framework, struct yetty_ygui_input_state *st,
                      unsigned char c)
{
    switch (st->st) {
    case YETTY_YGUI_CSI_NORMAL:
        if (c == 0x1B) {
            st->st = YETTY_YGUI_CSI_ESC;
            st->params_len = 0;
            return;
        }
        dispatch_key(framework, c, 0);
        return;

    case YETTY_YGUI_CSI_ESC:
        if (c == '[') {
            st->st = YETTY_YGUI_CSI_BRACKET;
            return;
        }
        /* ESC followed by something else — emit ESC then re-process the byte. */
        dispatch_key(framework, 0x1B, 0);
        st->st = YETTY_YGUI_CSI_NORMAL;
        feed_byte(framework, st, c);
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
                dispatch_key(framework, key, mods);
            }
        }
        st->st = YETTY_YGUI_CSI_NORMAL;
        st->params_len = 0;
        return;
    }
}

struct yetty_ycore_void_result yetty_ygui_framework_feed_input(
    struct yetty_ygui_framework *framework, const char *bytes, size_t n)
{
    if (!framework) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_feed_input: NULL framework");
    }
    if (!bytes || n == 0) {
        return YETTY_OK_VOID();
    }
    for (size_t i = 0; i < n; ++i) {
        feed_byte(framework, &framework->input, (unsigned char)bytes[i]);
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
static struct yetty_yclass_object *hit_test(struct yetty_yclass_object *node, float x, float y)
{
    if (!node) {
        return NULL;
    }
    /* A hidden subtree receives no hits — even though the layout pass
     * skips it (so it keeps whatever rect it last had), a folded-away or
     * closed-and-stale overlay must not intercept clicks meant for the
     * visible widgets it happens to overlap. Mirrors the emit walk's
     * should_skip_subtree. */
    const struct yetty_ygui_layout *l = yetty_ygui_widget_layout_get(node);
    if (l && l->hidden) {
        return NULL;
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(node);
    if (!rect_contains(r, x, y)) {
        return NULL;
    }
    struct yetty_yclass_object *deepest = node;
    for (struct yetty_yclass_object *c = ygui_tree(node)->first_child; c;
         c = ygui_tree(c)->next_sibling) {
        struct yetty_yclass_object *child_hit = hit_test(c, x, y);
        if (child_hit) {
            deepest = child_hit;
        }
    }
    return deepest;
}

struct yetty_ycore_int_result yetty_ygui_framework_feed_mouse_button(
    struct yetty_ygui_framework *framework, float x, float y, int button, int pressed, int mods)
{
    (void)mods;
    if (!framework) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_framework_feed_mouse_button: NULL framework");
    }
    if (!framework->root) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_yclass_object *target;
    if (pressed) {
        target = hit_test(framework->root, x, y);
        /* Click-to-front: a press anywhere inside a floating overlay
         * (dialog / debug window) moves that overlay to the end of its
         * parent's child list, so it paints last (front) within the
         * shared chrome ygrid AND wins the next overlap hit-test. Walk up
         * to the nearest floating ancestor — independent of which inner
         * widget ends up handling the press. */
        for (struct yetty_yclass_object *a = target; a; a = ygui_tree(a)->parent) {
            if (ygui_tree(a)->floating) {
                yetty_ygui_widget_raise(a);
                ygui_tree(a)->dirty = 1;
                framework->dirty = 1;
                break;
            }
        }
    } else {
        /* Release goes to the capture target (if any) so the widget that
         * began a drag also sees its end, even off-rect. */
        target = framework->pressed_obj ? framework->pressed_obj : hit_test(framework->root, x, y);
        framework->pressed_obj = NULL;
    }
    while (target) {
        struct yetty_ycore_int_result r =
            pressed ? yetty_ygui_widget_on_press(NULL, (struct yetty_yclass_object *)target, x, y,
                                                 button)
                    : yetty_ygui_widget_on_release(NULL, (struct yetty_yclass_object *)target, x, y,
                                                   button);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ygui_framework_feed_mouse_button: on_press/release", r);
        }
        if (r.value) {
            if (pressed) {
                framework->pressed_obj = target; /* capture for the drag */
            }
            framework->dirty = 1;
            return YETTY_OK(yetty_ycore_int, 1); /* an interactive widget consumed it */
        }
        target = ygui_tree(target)->parent;
    }
    return YETTY_OK(yetty_ycore_int, 0); /* fell through — chrome should handle it */
}

struct yetty_ycore_int_result yetty_ygui_framework_feed_mouse_motion(
    struct yetty_ygui_framework *framework, float x, float y)
{
    if (!framework) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_framework_feed_mouse_motion: NULL framework");
    }
    if (!framework->root) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    /* During a drag, route motion to the capture target regardless of
     * where the cursor is — that's what makes slider / splitter drags
     * track past the widget's own rect. A drag is in progress, so the client
     * owns the pointer: report consumed regardless of which widget handled it. */
    if (framework->pressed_obj) {
        struct yetty_yclass_object *cap = framework->pressed_obj;
        while (cap) {
            struct yetty_ycore_int_result r =
                yetty_ygui_widget_on_motion(NULL, (struct yetty_yclass_object *)cap, x, y);
            if (YETTY_IS_ERR(r)) {
                return YETTY_ERR(yetty_ycore_int,
                                 "yetty_ygui_framework_feed_mouse_motion: capture on_motion", r);
            }
            if (r.value) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
            cap = ygui_tree(cap)->parent;
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    struct yetty_yclass_object *target = hit_test(framework->root, x, y);
    /* Hover bookkeeping — flip enter/leave when the deepest-hit widget
     * changes from the previous motion event. Mark both old and new
     * dirty so the next emit repaints them with the correct variant. */
    if (target != framework->hovered_obj) {
        if (framework->hovered_obj) {
            ygui_tree(framework->hovered_obj)->hovered = 0;
            ygui_tree(framework->hovered_obj)->dirty = 1;
        }
        if (target) {
            ygui_tree(target)->hovered = 1;
            ygui_tree(target)->dirty = 1;
        }
        framework->hovered_obj = target;
        framework->dirty = 1;
    }
    while (target) {
        struct yetty_ycore_int_result r =
            yetty_ygui_widget_on_motion(NULL, (struct yetty_yclass_object *)target, x, y);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_framework_feed_mouse_motion: on_motion",
                             r);
        }
        if (r.value) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
        target = ygui_tree(target)->parent;
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

struct yetty_ycore_void_result yetty_ygui_framework_feed_mouse_scroll(
    struct yetty_ygui_framework *framework, float x, float y, float dx, float dy)
{
    if (!framework) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ygui_framework_feed_mouse_scroll: NULL framework");
    }
    if (!framework->root) {
        return YETTY_OK_VOID();
    }
    /* Deliver to the widget under the pointer, bubbling up until one
     * consumes it (a scrollarea / filepicker). Mirrors the press path. */
    struct yetty_yclass_object *target = hit_test(framework->root, x, y);
    while (target) {
        struct yetty_ycore_int_result r =
            yetty_ygui_widget_on_scroll(NULL, (struct yetty_yclass_object *)target, x, y, dx, dy);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_feed_mouse_scroll: on_scroll",
                             r);
        }
        if (r.value) {
            framework->dirty = 1;
            return YETTY_OK_VOID();
        }
        target = ygui_tree(target)->parent;
    }
    return YETTY_OK_VOID();
}

struct yetty_yclass_object *yetty_ygui_framework_root(struct yetty_ygui_framework *framework)
{
    return framework ? framework->root : NULL;
}

struct yetty_ycore_void_result yetty_ygui_framework_set_root(struct yetty_ygui_framework *framework,
                                                             struct yetty_yclass_object *root)
{
    if (!framework || !root) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_set_root: NULL arg");
    }
    framework->root = root;
    ygui_tree(root)->framework = framework;
    /* Allocate the root's wire id retroactively. */
    if (ygui_tree(root)->id == 0) {
        struct uint32_result idr = yetty_ygui_framework_alloc_id(framework);
        if (YETTY_IS_ERR(idr)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_set_root: alloc_id failed",
                             idr);
        }
        ygui_tree(root)->id = idr.value;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * ID allocator.
 *=========================================================================*/

struct uint32_result yetty_ygui_framework_alloc_id(struct yetty_ygui_framework *framework)
{
    if (!framework) {
        return YETTY_ERR(uint32, "yetty_ygui_framework_alloc_id: NULL framework");
    }
    if (framework->free_id_count > 0) {
        uint32_t id = framework->free_ids[--framework->free_id_count];
        return YETTY_OK(uint32, id);
    }
    uint32_t id = framework->next_id++;
    return YETTY_OK(uint32, id);
}

struct yetty_ycore_void_result yetty_ygui_framework_free_id(struct yetty_ygui_framework *framework,
                                                            uint32_t id)
{
    if (!framework || id == 0) {
        return YETTY_OK_VOID();
    }
    /* If the id was a minted figure, drop it from the set so a later
     * id reuse re-fires CREATE_CHILD instead of SET_CHILD_RECT. */
    figure_forget_minted(framework, id);
    /* Queue a delete for the next envelope. */
    if (framework->pending_delete_count == framework->pending_delete_cap) {
        size_t ncap = framework->pending_delete_cap ? framework->pending_delete_cap * 2 : 16;
        uint32_t *na = realloc(framework->pending_deletes, ncap * sizeof(*na));
        if (!na) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ygui_framework_free_id: realloc pending failed");
        }
        framework->pending_deletes = na;
        framework->pending_delete_cap = ncap;
    }
    framework->pending_deletes[framework->pending_delete_count++] = id;

    /* Push id back onto the free list. */
    if (framework->free_id_count == framework->free_id_cap) {
        size_t ncap = framework->free_id_cap ? framework->free_id_cap * 2 : 16;
        uint32_t *na = realloc(framework->free_ids, ncap * sizeof(*na));
        if (!na) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_free_id: realloc free_ids");
        }
        framework->free_ids = na;
        framework->free_id_cap = ncap;
    }
    framework->free_ids[framework->free_id_count++] = id;
    return YETTY_OK_VOID();
}

uint32_t yetty_ygui_framework_ygrid_id(const struct yetty_ygui_framework *framework)
{
    return framework ? framework->ygrid_id : 0;
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
    ydebug("emit_figure_body id=%u size=%u", figure_id, payload_len);
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

int32_t yetty_ygui_framework_next_raise_z(struct yetty_ygui_framework *framework)
{
    if (!framework) {
        return YETTY_YGUI_Z_FLOATING_BASE;
    }
    /* Pre-increment so the result is strictly above the previous raise.
     * Clamp below the menu band so a long-lived session that raises many
     * times can never climb on top of an open menu. */
    if (framework->next_raise_z < YETTY_YGUI_Z_MENU - 1) {
        framework->next_raise_z++;
    }
    return framework->next_raise_z;
}

struct yetty_ycore_void_result yetty_ygui_emit_set_child_z(struct yetty_ygui_emit_ctx *ctx,
                                                           uint32_t child_id, int32_t z)
{
    if (!ctx || !ctx->container_records) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_set_child_z: NULL ctx");
    }
    struct yetty_ycore_buffer tmp = {0};
    struct yetty_ycore_void_result r;
    r = write_u32_le(&tmp, child_id);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    /* z is signed; its two's-complement bit pattern rides the u32 writer
     * and the receiver memcpy's it straight back into an int32_t. */
    r = write_u32_le(&tmp, (uint32_t)z);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    r = append_admin_record(ctx->container_records, YETTY_YFIGURE_ADMIN_SET_CHILD_Z, tmp.data,
                            (uint32_t)tmp.size);
fail:
    yetty_ycore_buffer_destroy(&tmp);
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_set_child_z: write failed", r);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_emit_set_child_hidden(struct yetty_ygui_emit_ctx *ctx,
                                                                uint32_t child_id, int hidden)
{
    if (!ctx || !ctx->container_records) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_set_child_hidden: NULL ctx");
    }
    struct yetty_ycore_buffer tmp = {0};
    struct yetty_ycore_void_result r;
    r = write_u32_le(&tmp, child_id);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    r = write_u32_le(&tmp, hidden ? 1u : 0u);
    if (YETTY_IS_ERR(r)) {
        goto fail;
    }
    r = append_admin_record(ctx->container_records, YETTY_YFIGURE_ADMIN_SET_CHILD_HIDDEN, tmp.data,
                            (uint32_t)tmp.size);
fail:
    yetty_ycore_buffer_destroy(&tmp);
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_set_child_hidden: write failed", r);
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
static int should_skip_subtree(const struct yetty_yclass_object *node)
{
    const struct yetty_ygui_layout *l = yetty_ygui_widget_layout_get(node);
    if (!l) {
        return 0;
    }
    /* Explicitly folded-away subtree (collapsed collapsing_header /
     * tree_node child). Omitting the CMD_GROUP body bytes for the tick
     * removes the prims on the receiver — same mechanism the closed-popup
     * skip below relies on. */
    if (l->hidden) {
        return 1;
    }
    if (!l->absolute) {
        return 0;
    }
    return l->width <= 0.0f || l->height <= 0.0f;
}

/* Does `node` or any descendant that paints into the SAME figure body have
 * its dirty flag set? Recursion stops at nested figure boundaries: a child
 * figure is an independent receiver-side object shipped by its own
 * walk_emit_body pass, so its dirtiness must not force the parent figure's
 * body to be re-shipped. This gates the incremental figure-body skip — a
 * figure whose body is unchanged keeps its last body on the receiver. */
static int subtree_dirty(const struct yetty_yclass_object *node)
{
    if (ygui_tree(node)->dirty) {
        return 1;
    }
    for (const struct yetty_yclass_object *c = ygui_tree(node)->first_child; c;
         c = ygui_tree(c)->next_sibling) {
        if (yetty_ygui_widget_figure_kind(c) != 0) {
            continue; /* shipped as its own figure body */
        }
        if (subtree_dirty(c)) {
            return 1;
        }
    }
    return 0;
}

/* Clear every widget's dirty flag after a successful emit, so the next emit
 * only re-ships subtrees that actually changed since this one. */
static void clear_subtree_dirty(struct yetty_yclass_object *node)
{
    if (!node) {
        return;
    }
    ygui_tree(node)->dirty = 0;
    for (struct yetty_yclass_object *c = ygui_tree(node)->first_child; c;
         c = ygui_tree(c)->next_sibling) {
        clear_subtree_dirty(c);
    }
}

/* A hidden subtree is skipped from emission — but any figure descendants
 * are independent receiver-side objects, so they must be explicitly told
 * to hide or they persist on screen (e.g. a scrollarea figure inside a
 * folded-away tab). Walk the subtree and hide every minted figure. */
static struct yetty_ycore_void_result hide_subtree_figures(struct yetty_yclass_object *node,
                                                           struct yetty_ygui_emit_ctx *ctx)
{
    if (!node) {
        return YETTY_OK_VOID();
    }
    if (yetty_ygui_widget_figure_kind(node) != 0 && ctx->framework &&
        figure_is_minted(ctx->framework, yetty_ygui_widget_id(node))) {
        struct yetty_ycore_void_result hr =
            yetty_ygui_emit_set_child_hidden(ctx, yetty_ygui_widget_id(node), 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "hide_subtree_figures: hide");
    }
    for (struct yetty_yclass_object *c = ygui_tree(node)->first_child; c;
         c = ygui_tree(c)->next_sibling) {
        struct yetty_ycore_void_result rc = hide_subtree_figures(c, ctx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rc, "hide_subtree_figures: child");
    }
    return YETTY_OK_VOID();
}

/* Intersection of two rects (empty result collapses to a point). */
static struct yetty_ycore_rectangle emit_rect_intersect(struct yetty_ycore_rectangle a,
                                                        struct yetty_ycore_rectangle b)
{
    struct yetty_ycore_rectangle o;
    o.min.x = a.min.x > b.min.x ? a.min.x : b.min.x;
    o.min.y = a.min.y > b.min.y ? a.min.y : b.min.y;
    o.max.x = a.max.x < b.max.x ? a.max.x : b.max.x;
    o.max.y = a.max.y < b.max.y ? a.max.y : b.max.y;
    if (o.max.x < o.min.x) {
        o.max.x = o.min.x;
    }
    if (o.max.y < o.min.y) {
        o.max.y = o.min.y;
    }
    return o;
}

struct yetty_ycore_void_result yetty_ygui_framework_walk_emit_container(
    struct yetty_yclass_object *node, struct yetty_ygui_emit_ctx *ctx)
{
    if (!node) {
        return YETTY_OK_VOID();
    }
    uint32_t fkind = yetty_ygui_widget_figure_kind(node);
    int skip = should_skip_subtree(node);
    int saved_clip_active = ctx->fig_clip_active;
    struct yetty_ycore_rectangle saved_clip = ctx->fig_clip;

    /* Figure-boundary node (floating window / menu): it lives as its own
     * receiver-side child figure rather than inlining into the chrome
     * ygrid. We mark the figure hidden instead of deleting it on close —
     * re-showing then costs one record, not a CREATE + full-body re-ship.
     * A boundary that has never been shown is simply not created yet. */
    if (fkind != 0) {
        uint32_t fid = yetty_ygui_widget_id(node);
        if (skip) {
            /* Hidden/zero-size: only flag it if it already exists. */
            if (ctx->framework && figure_is_minted(ctx->framework, fid)) {
                struct yetty_ycore_void_result hr = yetty_ygui_emit_set_child_hidden(ctx, fid, 1);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, hr,
                                    "yetty_ygui_framework_walk_emit_container: figure hide");
            }
            ydebug("walk_container: SKIP(hidden figure) id=%u", fid);
            return YETTY_OK_VOID();
        }
        /* Clip the figure's rect to the ancestor figures' intersection so a
         * nested scrollable can't paint past its parent's box. In absolute
         * mode the rect is purely the scissor (content is screen-coord), so
         * this is exactly the right clip; the narrowed rect also becomes the
         * clip for this figure's own subtree. */
        struct yetty_ycore_rectangle fr = yetty_ygui_widget_rect(node);
        if (ctx->fig_clip_active) {
            fr = emit_rect_intersect(fr, ctx->fig_clip);
        }
        struct yetty_ycore_void_result er = yetty_ygui_emit_ensure_child(
            ctx, fid, fkind, fr.min.x, fr.min.y, fr.max.x, fr.max.y, NULL, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, er,
                            "yetty_ygui_framework_walk_emit_container: figure ensure_child");
        struct yetty_ycore_void_result zr =
            yetty_ygui_emit_set_child_z(ctx, fid, yetty_ygui_widget_figure_z(node));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, zr,
                            "yetty_ygui_framework_walk_emit_container: figure set_z");
        struct yetty_ycore_void_result hr = yetty_ygui_emit_set_child_hidden(ctx, fid, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hr,
                            "yetty_ygui_framework_walk_emit_container: figure show");
        /* Narrow the clip for this figure's subtree. */
        ctx->fig_clip = fr;
        ctx->fig_clip_active = 1;
    } else if (skip) {
        ydebug("walk_container: SKIP node=%p id=%u", (void *)node, yetty_ygui_widget_id(node));
        /* Folded-away subtree: don't emit it, but hide any figures inside
         * it so they don't linger (e.g. a scrollarea figure in a hidden
         * tab). */
        return hide_subtree_figures(node, ctx);
    }
    ydebug("walk_container: node=%p id=%u klass=%p", (void *)node, yetty_ygui_widget_id(node),
           (void *)node->klass);
    struct yetty_ycore_void_result r =
        yetty_ygui_widget_emit_container(NULL, (struct yetty_yclass_object *)node, ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                        "yetty_ygui_framework_walk_emit_container: emit_container");
    for (struct yetty_yclass_object *c = ygui_tree(node)->first_child; c;
         c = ygui_tree(c)->next_sibling) {
        struct yetty_ycore_void_result rc = yetty_ygui_framework_walk_emit_container(c, ctx);
        if (YETTY_IS_ERR(rc)) {
            ctx->fig_clip = saved_clip;
            ctx->fig_clip_active = saved_clip_active;
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ygui_framework_walk_emit_container: child walk", rc);
        }
    }
    /* Pop the figure clip we may have narrowed for this subtree. */
    ctx->fig_clip = saved_clip;
    ctx->fig_clip_active = saved_clip_active;
    return YETTY_OK_VOID();
}

/* Emit `node` and its subtree's prims into the currently-active draw
 * list (ctx->ygrid_drawable_list). Shared by the normal path and the
 * figure-boundary path below. */
static struct yetty_ycore_void_result walk_emit_body_inline(struct yetty_yclass_object *node,
                                                            struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_ycore_void_result r =
        yetty_ygui_widget_emit_body(NULL, (struct yetty_yclass_object *)node, ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_framework_walk_emit_body: emit_body");
    for (struct yetty_yclass_object *c = ygui_tree(node)->first_child; c;
         c = ygui_tree(c)->next_sibling) {
        struct yetty_ycore_void_result rc = yetty_ygui_framework_walk_emit_body(c, ctx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rc,
                            "yetty_ygui_framework_walk_emit_body: child walk");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_framework_walk_emit_body(struct yetty_yclass_object *node,
                                                                   struct yetty_ygui_emit_ctx *ctx)
{
    if (!node) {
        return YETTY_OK_VOID();
    }
    if (should_skip_subtree(node)) {
        return YETTY_OK_VOID();
    }
    /* Non-boundary node: paint into whatever draw list is active. */
    if (yetty_ygui_widget_figure_kind(node) == 0) {
        return walk_emit_body_inline(node, ctx);
    }

    /* Incremental figure body: if this figure was already minted on the
     * receiver (its body shipped at least once) and nothing in its body
     * subtree changed, skip re-shipping. The receiver keeps the last body
     * for this figure id; a pure rect move is handled separately by the
     * pass-1 SET_CHILD_RECT. This is what stops an unchanged page (a
     * scrollarea figure) from being re-serialized every emit. */
    if (ctx->framework && figure_is_minted(ctx->framework, ygui_tree(node)->id) &&
        !subtree_dirty(node)) {
        ydebug("figure SKIP (clean, minted) id=%u", ygui_tree(node)->id);
        return YETTY_OK_VOID();
    }

    /* Figure boundary: swap in a fresh draw list, paint the whole
     * subtree into it, ship it as the figure's body, then restore. The
     * leading CMD_ZERO wipes this figure's prims on the receiver each
     * frame (full-redraw model, same as the chrome ygrid). */
    struct yetty_ydraw_drawable_list_result dlr =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dlr,
                        "yetty_ygui_framework_walk_emit_body: figure drawable_list create");
    struct yetty_ydraw_drawable_list *figure_dl = dlr.value;
    struct yetty_ycore_void_result zr = yetty_ydraw_drawable_list_add_cmd_zero(figure_dl);
    if (YETTY_IS_ERR(zr)) {
        yetty_ydraw_drawable_list_destroy(figure_dl);
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_walk_emit_body: figure CMD_ZERO",
                         zr);
    }

    struct yetty_ydraw_drawable_list *saved_dl = ctx->ygrid_drawable_list;
    uint32_t saved_fid = ctx->current_figure_id;
    ctx->ygrid_drawable_list = figure_dl;
    ctx->current_figure_id = ygui_tree(node)->id;

    struct yetty_ycore_void_result br = walk_emit_body_inline(node, ctx);

    ctx->ygrid_drawable_list = saved_dl;
    ctx->current_figure_id = saved_fid;

    if (YETTY_IS_ERR(br)) {
        yetty_ydraw_drawable_list_destroy(figure_dl);
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_walk_emit_body: figure subtree",
                         br);
    }

    size_t body_size = yetty_ydraw_drawable_list_size(figure_dl);
    if (body_size > 0) {
        const void *body_data = yetty_ydraw_drawable_list_data(figure_dl);
        struct yetty_ycore_void_result fr = yetty_ygui_emit_figure_body(
            ctx, ygui_tree(node)->id, (const uint8_t *)body_data, (uint32_t)body_size);
        if (YETTY_IS_ERR(fr)) {
            yetty_ydraw_drawable_list_destroy(figure_dl);
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ygui_framework_walk_emit_body: figure body emit", fr);
        }
    }
    yetty_ydraw_drawable_list_destroy(figure_dl);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Chrome setup — ensure the shared ygrid exists on the receiver.
 *
 * The receiver IS the root container; no synthetic framework container
 * is needed (the registry has no factory for kind=CONTAINER). The
 * framework just mints one ygrid as a direct child of the root. Every
 * chrome widget (figure_kind == 0) drops its prims into the ygrid's
 * body; figure widgets (figure_kind != 0) emit their own CREATE_CHILD
 * records at the same root level alongside the ygrid. */
struct yetty_ycore_void_result yetty_ygui_framework_ensure_chrome(
    struct yetty_ygui_framework *framework, struct yetty_ygui_emit_ctx *ctx)
{
    if (!framework->ygrid_created) {
        struct yetty_ycore_void_result r = yetty_ygui_emit_create_child(
            ctx, framework->ygrid_id, YETTY_YFIGURE_KIND_YGRID, 0.0f, 0.0f, framework->viewport_w,
            framework->viewport_h, NULL, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                            "yetty_ygui_framework_ensure_chrome: ygrid CREATE_CHILD");
        /* Staged — framework->ygrid_created flips only after flush succeeds. */
        ctx->staged_ygrid_created = 1;
    } else {
        /* Keep the ygrid's rect in sync with the viewport. */
        struct yetty_ycore_void_result r = yetty_ygui_emit_set_child_rect(
            ctx, framework->ygrid_id, 0.0f, 0.0f, framework->viewport_w, framework->viewport_h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                            "yetty_ygui_framework_ensure_chrome: ygrid SET_CHILD_RECT");
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Flush: concatenate streams + envelope + write to output pty.
 *=========================================================================*/

static struct yetty_ycore_void_result flush_pending_deletes(struct yetty_ygui_framework *framework,
                                                            struct yetty_ygui_emit_ctx *ctx)
{
    for (size_t i = 0; i < framework->pending_delete_count; ++i) {
        struct yetty_ycore_void_result r =
            yetty_ygui_emit_delete_child(ctx, framework->pending_deletes[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "flush_pending_deletes: DELETE_CHILD");
        /* Track how far we got. The queue is shifted only after flush
         * succeeds; on partial failure the unsent prefix stays queued
         * and is retried next tick (drop succeeds-so-far so retries
         * don't double-emit). */
        ctx->staged_deletes_consumed = i + 1;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_framework_flush(struct yetty_ygui_framework *framework)
{
    /* Compose envelope body:
     *   - container_records (already record-framed as admin records id=0)
     *   - one record { ygrid_body bytes, id=ygrid_id }
     *   - figure_bodies (already record-framed by widgets at append time)
     */
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result r;

    if (framework->container_records.size > 0) {
        r = yetty_ycore_buffer_write(&envelope, framework->container_records.data,
                                     framework->container_records.size);
        if (YETTY_IS_ERR(r)) {
            goto fail;
        }
    }
    /* Bytes accumulated by chrome widgets into the framework's per-emit
     * ydraw drawable_list. The list's primitives buffer is the raw ydraw
     * FAM stream that ygrid->process_bytes consumes. */
    if (framework->ygrid_drawable_list) {
        size_t dl_size = yetty_ydraw_drawable_list_size(framework->ygrid_drawable_list);
        if (dl_size > 0) {
            const void *dl_data = yetty_ydraw_drawable_list_data(framework->ygrid_drawable_list);
            r = yetty_ygui_wire_append_record(&envelope, framework->ygrid_id, dl_data,
                                              (uint32_t)dl_size);
            if (YETTY_IS_ERR(r)) {
                goto fail;
            }
        }
    }
    if (framework->figure_bodies.size > 0) {
        r = yetty_ycore_buffer_write(&envelope, framework->figure_bodies.data,
                                     framework->figure_bodies.size);
        if (YETTY_IS_ERR(r)) {
            goto fail;
        }
    }

    if (envelope.size == 0) {
        /* Nothing to ship this tick. */
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_OK_VOID();
    }

    /* If a receiver-side container is wired in, ship the envelope by
     * calling the yfigure `process_records` slot directly. yclass
     * dispatches it locally (in-process: impl runs straight away,
     * zero copy) or via yrpc (when ctx.session is set: stub marshals
     * the buffer over the session's transport). Either way, the
     * receiver-side container's `process_records` does exactly what
     * the consume_envelope coroutine would have done after PTY decode
     * — same record format, no yface framing in between. */
    if (framework->container_obj) {
        struct yetty_ycore_buffer view = {
            .data = envelope.data,
            .capacity = envelope.capacity,
            .size = envelope.size,
        };
        struct yetty_ycore_void_result pr =
            yetty_yfigure_process_records(&framework->yclass_ctx, framework->container_obj, view);
        yetty_ycore_buffer_destroy(&envelope);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr,
                            "yetty_ygui_framework_flush: process_records slot");
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
    r = yetty_yface_emit(YETTY_DCS_YCOMPOSITOR_BIN, /*compressed=*/1, &meta, sizeof(meta),
                         envelope.data, envelope.size, &wire_out);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&envelope);
        yetty_ycore_buffer_destroy(&wire_out);
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_flush: yface_emit", r);
    }
    yetty_ycore_buffer_destroy(&envelope);

    if (wire_out.size > 0 && framework->output_pty && framework->output_pty->ops &&
        framework->output_pty->ops->write) {
        struct yetty_ycore_size_result wr = framework->output_pty->ops->write(
            framework->output_pty, (const char *)wire_out.data, wire_out.size);
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

struct yetty_ycore_void_result yetty_ygui_framework_clear_remote_fd(
    struct yetty_ygui_framework *framework, int fd)
{
    if (!framework || fd < 0) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_clear_remote_fd: bad args");
    }
    /* One CLEAR_ALL admin record on the root (id=0) — the host container's
     * process_records drops every child when it sees this op. */
    struct yetty_ycore_buffer records = {0};
    struct yetty_ycore_void_result r =
        append_admin_record(&records, YETTY_YFIGURE_ADMIN_CLEAR_ALL, NULL, 0);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&records);
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_clear_remote_fd: build record", r);
    }
    /* Same envelope framing as framework_flush, written straight to the fd. */
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = records.size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_void_result er =
        yetty_yface_emit_to_fd(fd, YETTY_DCS_YCOMPOSITOR_BIN, /*compressed=*/1, &meta, sizeof(meta),
                               records.data, records.size);
    yetty_ycore_buffer_destroy(&records);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er,
                        "yetty_ygui_framework_clear_remote_fd: yface_emit_to_fd");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_framework_emit(struct yetty_ygui_framework *framework)
{
    if (!framework) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_emit: NULL framework");
    }

    yetty_ycore_buffer_clear(&framework->container_records);
    yetty_ycore_buffer_clear(&framework->figure_bodies);

    /* Per-emit ydraw drawable_list — created lazily on first emit, reused
     * across frames. clear() rewinds the primitives byte cursor without
     * freeing the allocation. */
    if (!framework->ygrid_drawable_list) {
        struct yetty_ydraw_drawable_list_result dlr =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        if (YETTY_IS_ERR(dlr)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_emit: drawable_list create",
                             dlr);
        }
        framework->ygrid_drawable_list = dlr.value;
    } else {
        yetty_ydraw_drawable_list_clear(framework->ygrid_drawable_list);
    }

    /* Full-redraw model: prepend CMD_ZERO so the receiver wipes its
     * ygrid display list before applying this frame's prims. Without
     * this the prims accumulate on the receiver every frame —
     * documented in include/yetty/ydraw-core/cmds.h:89. */
    {
        struct yetty_ycore_void_result zr =
            yetty_ydraw_drawable_list_add_cmd_zero(framework->ygrid_drawable_list);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, zr, "yetty_ygui_framework_emit: CMD_ZERO");
    }

    struct yetty_ygui_emit_ctx ctx = {
        .framework = framework,
        .container_records = &framework->container_records,
        .ygrid_drawable_list = framework->ygrid_drawable_list,
        .figure_bodies = &framework->figure_bodies,
        .current_figure_id = 0,
        .staged_mints = NULL,
        .staged_mint_count = 0,
        .staged_mint_cap = 0,
        .staged_ygrid_created = 0,
        .staged_deletes_consumed = 0,
    };

    struct yetty_ycore_void_result r = YETTY_OK_VOID();

    /* Run the layout pass — without this every widget's rect stays
     * (0,0)-(0,0) and paint emits zero-sized geometry. The root rect
     * is the framework's viewport. */
    if (framework->root) {
        struct yetty_ycore_rectangle root_rect = {
            .min = {0.0f, 0.0f}, .max = {framework->viewport_w, framework->viewport_h}};
        r = yetty_ygui_layout_compute(framework->root, root_rect);
        if (YETTY_IS_ERR(r)) {
            free(ctx.staged_mints);
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_emit: layout_compute", r);
        }
    }

    /* Pending deletes go through the container records stream first. The
     * id allocator hands freed ids back via free_ids, so a widget added
     * between two emits can be assigned the id of a widget destroyed in
     * the same window. Emitting DELETE_CHILD ahead of any CREATE_CHILD
     * for that id keeps the receiver's view ordered: old gone, new in. */
    r = flush_pending_deletes(framework, &ctx);
    if (YETTY_IS_ERR(r)) {
        free(ctx.staged_mints);
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_emit: flush_pending_deletes", r);
    }

    r = yetty_ygui_framework_ensure_chrome(framework, &ctx);
    if (YETTY_IS_ERR(r)) {
        free(ctx.staged_mints);
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_emit: ensure_chrome", r);
    }

    /* Pass 1: container records. */
    if (framework->root) {
        r = yetty_ygui_framework_walk_emit_container(framework->root, &ctx);
        if (YETTY_IS_ERR(r)) {
            free(ctx.staged_mints);
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_emit: walk pass 1", r);
        }
    }

    /* Pass 2: body records. */
    if (framework->root) {
        r = yetty_ygui_framework_walk_emit_body(framework->root, &ctx);
        if (YETTY_IS_ERR(r)) {
            free(ctx.staged_mints);
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_emit: walk pass 2", r);
        }
    }

    /* Concatenate streams and ship. */
    r = yetty_ygui_framework_flush(framework);
    if (YETTY_IS_ERR(r)) {
        /* Flush failure: leave framework state untouched so the next
         * emit replays CREATE/DELETE for everything staged this tick. */
        free(ctx.staged_mints);
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_emit: flush", r);
    }

    /* Commit staged sender-side bookkeeping now that the receiver has
     * the envelope. The staged_mints become permanent minted_figures
     * entries; ygrid_created flips iff a CREATE_CHILD for the ygrid
     * was actually sent; the consumed prefix of pending_deletes is
     * dropped from the queue. */
    if (ctx.staged_mint_count > 0) {
        size_t want = framework->minted_figure_count + ctx.staged_mint_count;
        if (want > framework->minted_figure_cap) {
            size_t ncap = framework->minted_figure_cap ? framework->minted_figure_cap : 8;
            while (ncap < want) {
                ncap *= 2;
            }
            uint32_t *na = realloc(framework->minted_figures, ncap * sizeof(*na));
            if (!na) {
                free(ctx.staged_mints);
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_ygui_framework_emit: commit mints: realloc");
            }
            framework->minted_figures = na;
            framework->minted_figure_cap = ncap;
        }
        memcpy(framework->minted_figures + framework->minted_figure_count, ctx.staged_mints,
               ctx.staged_mint_count * sizeof(uint32_t));
        framework->minted_figure_count += ctx.staged_mint_count;
    }
    if (ctx.staged_ygrid_created) {
        framework->ygrid_created = 1;
    }
    if (ctx.staged_deletes_consumed > 0) {
        size_t remaining = framework->pending_delete_count - ctx.staged_deletes_consumed;
        if (remaining > 0) {
            memmove(framework->pending_deletes,
                    framework->pending_deletes + ctx.staged_deletes_consumed,
                    remaining * sizeof(uint32_t));
        }
        framework->pending_delete_count = remaining;
    }
    free(ctx.staged_mints);

    /* The envelope shipped — every widget's dirty flag has now been
     * accounted for (either re-emitted, or skipped because its figure body
     * was unchanged). Clear them so the next emit only re-ships subtrees
     * that change after this point. The chrome ygrid is still full-redraw;
     * the per-figure-body skip above is what gates incremental emit. */
    clear_subtree_dirty(framework->root);
    framework->dirty = 0;
    return YETTY_OK_VOID();
}
