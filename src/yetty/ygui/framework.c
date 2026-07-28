/*
 * framework.c — framework lifecycle, ID allocator, two-pass wire emission.
 *
 * The framework owns app-global state for one ygui instance: the borrowed
 * output_pty, id allocator with free-list, the receiver-side container
 * object + ygrid that hold every widget, and reusable per-emit buffers.
 *
 * One yetty_ygui_framework_emit cycle:
 *   1) Clear per-emit buffers; ensure the framework's container + ygrid
 *      have been minted at least once.
 *   2) Pass 1 — walk tree, dispatch yetty_ygui_widget_emit_container on
 *      every widget. Default impl drives figure-tree mutations
 *      (create/rect/z/hidden) on the container object through the typed
 *      yclass stubs (yetty_yfigure_create_child, …), which dispatch
 *      in-process or over yrpc depending on the session.
 *   3) Pass 2 — walk tree, dispatch yetty_ygui_widget_emit_body. Chrome
 *      widgets append to the shared ygrid drawable_list; figure widgets
 *      apply their body to the container via yetty_yfigure_apply_child_body.
 *   4) Flush pending deletes via yetty_yfigure_delete_child.
 *   5) Hand the accumulated ygrid drawable_list to the ygrid child through
 *      apply_child_body (framework_flush).
 */

#include "internal.h"
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/mixins/draggable.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widget.h>
#include <yetty/ygui/widgets/menubar.h>
#include <yetty/ygui/widgets/popup_menu.h>

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/api/yfigure/container.h>
#include <yetty/api/yterminal/terminal.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ywire/connection.h>
#include <yetty/yfigure/kind.h>
#include <yetty/ygui/theme.h>
#include <yetty/yplatform/pty.h>
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
 * Class data slice — the framework is a root yclass class. Its per-instance
 * state lives in this slice; the object itself is a yetty_yclass_object. The
 * struct is defined here (not in internal.h) so the codegen source scan finds
 * the class@ annotation. Other ygui TUs see only the opaque forward decl and
 * reach this state through the generated object-keyed accessors below.
 *=========================================================================*/

struct YETTY_ANNOTATE("class@ygui:framework") YETTY_ANNOTATE("include@yetty/ygui/framework-defs.h")
    yetty_ygui_framework {
    /* Back-pointer to the owning yclass object, stashed by the constructor.
     * Lets internal helpers that hold only the concrete data slice (the input
     * decoder's key dispatch) recover the object to hand to object-keyed
     * callbacks. */
    struct yetty_yclass_object *self_obj;

    uint32_t next_id;
    uint32_t *free_ids;
    size_t free_id_count;
    size_t free_id_cap;

    /* Monotonic floating-window raise allocator (see
     * yetty_ygui_framework_next_raise_z). Sits in the floating z band. */
    int32_t next_raise_z;

    /* Pending deletes — ids whose receiver-side figures need to go
     * away on the next envelope. */
    uint32_t *pending_deletes;
    size_t pending_delete_count;
    size_t pending_delete_cap;

    /* Receiver-side ygrid id. Primitive widgets share this one ygrid;
     * figure widgets emit their own CREATE_CHILD records with their
     * own ids alongside it. */
    uint32_t ygrid_id;
    int ygrid_created;

    /* Set of figure ids that have already been minted on the receiver
     * via CREATE_CHILD. Each frame: figure widgets check this set; if
     * their id is present they emit SET_CHILD_RECT (cheap rect update);
     * otherwise they emit CREATE_CHILD and add themselves to the set.
     *
     * The set is a sorted dense array kept small — figure widgets are
     * rare (a handful per app). free_id drops the id back out. */
    uint32_t *minted_figures;
    size_t minted_figure_count;
    size_t minted_figure_cap;

    struct yetty_yclass_object *root;

    /* Viewport in pixels — root widget bounds for the next layout
     * pass. Defaults to 800x600 until set_viewport is called. */
    float viewport_w;
    float viewport_h;

    /* Chrome palette + canonical sizes. Owned by the framework: created
     * in the constructor with the brand defaults; destroyed in the
     * destructor. yetty_ygui_framework_set_theme replaces the owned theme
     * (caller passes ownership in). Widget paint code consults this via
     * yetty_ygui_framework_theme(framework). */
    struct yetty_ygui_theme *theme;

    /* Borrowed measurement font — the same font the shared ygrid renders text
     * with (font_id 0). Set by the app after framework_create so widgets (e.g.
     * textinput) can place carets and hit-test clicks against real glyph
     * advances instead of a fixed per-char approximation. NULL until set, in
     * which case widgets fall back to the fixed advance. Not owned — no
     * destroy. */
    struct yetty_yfont_font *font;

    /* framework-level dirty flag. Cleared by emit. */
    int dirty;

    /* Shared ydraw drawable_list — primitive widgets append SDF / glyph
     * records here. Lazily created on first emit; reused across
     * frames. */
    struct yetty_ydraw_drawable_list *ygrid_drawable_list;

    /* Byte-stream input decoder state. */
    struct yetty_ygui_input_state input;

    /* App-level key callback. */
    yetty_ygui_key_cb key_cb;
    void *key_userdata;

    /* Deepest widget currently under the mouse, tracked by
     * feed_mouse_motion. Used to dispatch enter/leave + flip the
     * obj->hovered flag so widgets can paint a hover variant. */
    struct yetty_yclass_object *hovered_obj;

    /* Pointer-capture target. Set to the widget that consumed the last
     * press; subsequent motion + the matching release are routed here
     * regardless of hit-test, so click-and-drag (slider, splitter)
     * keeps working when the cursor leaves the widget's rect. Cleared
     * on release and on destroy of the captured object. */
    struct yetty_yclass_object *pressed_obj;

    /* yclass-dispatch state for driving the receiver-side yfigure root
     * container. The emit walk calls the typed yfigure stubs
     * (yetty_yfigure_create_child / _set_child_rect / _apply_child_body /
     * …) directly on `container_obj`. Each slot dispatches locally
     * (ctx.session == NULL → the impl runs directly on the in-process
     * container, zero copy) or via yrpc (ctx.session set → the stub
     * marshals the call over the session's transport).
     *
     * The runtime tracks ONLY the root container at the yclass level;
     * every child figure is addressed by parent-scoped uint32_t id passed
     * to the typed stubs (the container routes each call to the right
     * child). No per-child yclass proxy is kept here.
     *
     * Both pointers are caller-owned (borrowed) — the host (e.g. yui)
     * wires them post-create via yetty_ygui_framework_set_container_obj
     * / _set_session and keeps the underlying objects alive for as
     * long as the framework. `container_obj` must be set before emit. */
    struct yetty_yclass_ctx yclass_ctx;
    struct yetty_yclass_object *container_obj;

    /* Terminal session root, set when the framework attaches to a host
     * figure container over the yclass RPC transport via
     * yetty_ygui_framework_attach. It owns the underlying transport +
     * RPC session + root-container proxy; container_obj and
     * yclass_ctx.session above point INTO it. NULL when the framework was
     * never attached (in-process host that wired container_obj directly).
     * Its session owns the connection stack (channel + connection + pty),
     * all torn down by the destructor via yetty_yclass_rpc_disconnect.
     * container_obj is a proxy navigated from it. NULL if never attached. */
    struct yetty_yclass_object *rpc_root;
};

/* Defined in the appended framework.gen.c (foot of this TU); forward-declared
 * here because this TU does not include its own generated header. The object
 * accessor recovers the data slice from a framework yclass_object. */
struct yetty_yclass_ptr_result yetty_ygui_framework_class_get(void);
struct yetty_ygui_framework_ptr_result yetty_ygui_framework_from(struct yetty_yclass_object *obj);

/* Module-wide destructor dispatcher (generated; runs the per-class destructor
 * chain). Declared here so yetty_ygui_framework_destroy below can drive
 * teardown before releasing the object. */
struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_object *obj);

/* Unwrap a framework object to its data slice for the raw-return accessors
 * below (theme/root/is_dirty/…). They predate Result-returning getters and
 * keep their plain return types, so the from_obj Result is absorbed here:
 * NULL obj or an unregistered class yields NULL, which every accessor treats
 * as the empty/default case. The Result-returning methods use
 * yetty_ygui_framework_from directly with YETTY_RETURN_IF_ERR. */
static struct yetty_ygui_framework *framework_data(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    if (YETTY_IS_ERR(framework_res)) {
        yetty_ycore_error_destroy(framework_res.error);
        return NULL;
    }
    return framework_res.value;
}

/*===========================================================================
 * framework lifecycle — constructor / destructor run by the generated
 * yetty_ygui_framework_create(ctx) / yetty_ygui_framework_destroy(obj).
 *=========================================================================*/

YETTY_ANNOTATE("override@ygui:framework:constructor")
static struct yetty_ycore_void_result framework_constructor(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res, "framework_constructor: from_obj");
    struct yetty_ygui_framework *framework = framework_res.value;
    framework->self_obj = obj;
    /* Widget ids start at 1; the receiver uses 0 to mean "admin". */
    framework->next_id = 1;
    framework->next_raise_z = YETTY_YGUI_Z_FLOATING_BASE;
    framework->ygrid_id = YGUI_framework_YGRID_ID_BASE;
    framework->ygrid_created = 0;
    framework->viewport_w = 800.0f;
    framework->viewport_h = 600.0f;
    /* Brand theme — widgets consult this at paint time. Default palette is the
     * yetty brand; yetty_ygui_framework_set_theme (or apply_config_to_theme)
     * overlays user config. The framework owns the theme by default;
     * replacement transfers ownership in. */
    framework->theme = yetty_ygui_theme_create_default();
    if (!framework->theme) {
        return YETTY_ERR(yetty_ycore_void, "framework_constructor: theme alloc failed");
    }
    framework->dirty = 1;
    /* yclass dispatch defaults: in-process (no remote session), no container
     * wired yet. Host calls set_container_obj / set_session (or attach) after
     * create to opt into yclass-slot shipping. A container must be set before
     * the first emit. */
    framework->yclass_ctx.session = NULL;
    framework->container_obj = NULL;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui:framework:destructor")
static struct yetty_ycore_void_result framework_destructor(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res, "framework_destructor: from_obj");
    struct yetty_ygui_framework *framework = framework_res.value;
    if (framework->root) {
        struct yetty_ycore_void_result r = yetty_ygui_widget_destroy(framework->root);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        framework->root = NULL;
    }
    /* Tear down the attached session (channel + connection + pty, plus the
     * root proxy). container_obj / yclass_ctx.session pointed into it, so
     * clear them first. Best-effort — stash and surface any error after the
     * rest of teardown. */
    struct yetty_ycore_void_result detach_res = YETTY_OK_VOID();
    if (framework->rpc_root) {
        /* container_obj is a navigated proxy (a plain calloc'd carrier); free
         * it, then disconnect (session_destroy CLOSEs our channel + tears down
         * the connection stack the session owns). */
        free(framework->container_obj);
        framework->container_obj = NULL;
        framework->yclass_ctx.session = NULL;
        detach_res = yetty_yclass_rpc_disconnect(framework->rpc_root);
        framework->rpc_root = NULL;
    }
    free(framework->free_ids);
    free(framework->pending_deletes);
    free(framework->minted_figures);
    if (framework->ygrid_drawable_list) {
        yetty_ydraw_drawable_list_destroy(framework->ygrid_drawable_list);
        framework->ygrid_drawable_list = NULL;
    }
    if (framework->theme) {
        yetty_ygui_theme_destroy(framework->theme);
        framework->theme = NULL;
    }
    return detach_res;
}

/* Public teardown — symmetric to the generated yetty_ygui_framework_create.
 * Runs the destructor chain (framework_destructor frees the widget tree, owned
 * theme, and attached session) then releases the yclass object. Best-effort:
 * both steps run; the first error is surfaced. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_framework_destroy(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result destructor_res = yetty_ygui_destructor(obj);
    struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
    if (YETTY_IS_ERR(destructor_res)) {
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_destroy: destructor",
                         destructor_res);
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, free_res, "yetty_ygui_framework_destroy: object free");
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_framework_set_container_obj(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *container)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res,
                        "yetty_ygui_framework_set_container_obj: from_obj");
    framework_res.value->container_obj = container;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_framework_set_session(
    struct yetty_yclass_object *obj, struct yetty_yclass_rpc_session *session)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res,
                        "yetty_ygui_framework_set_session: from_obj");
    framework_res.value->yclass_ctx.session = session;
    return YETTY_OK_VOID();
}

/* Wire the framework's dual-dispatch to an attached session root. The
 * container proxy carries its own RPC session (set at proxy create), so the
 * typed yfigure_* stubs marshal over the wire from container_obj alone; we
 * mirror that session onto yclass_ctx for callers that consult it. Shared by
 * the fd-based attach and the injected-transport (endpoint) attach. */
/* Given the session root (the terminal), navigate to the figure container,
 * prime its slots (attach window), install the container + session on the
 * framework, and take ownership of the root for teardown. On any failure the
 * whole connection stack is disconnected. */
static struct yetty_ycore_void_result framework_wire_root(struct yetty_yclass_object *obj,
                                                          struct yetty_ygui_framework *framework,
                                                          struct yetty_yclass_object *rpc_root)
{
    struct yetty_yclass_object_ptr_result container_res =
        yetty_yterminal_figure_root_container(rpc_root);
    if (YETTY_IS_ERR(container_res)) {
        struct yetty_ycore_void_result dr = yetty_yclass_rpc_disconnect(rpc_root);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_attach: figure_root_container",
                         container_res);
    }
    framework->container_obj = container_res.value;

    /* Prime the container's slots now (pipeline empty): steady-state pipelined
     * mutations never mid-stream RESOLVE_SLOT (forbidden in async mode). */
    struct yetty_ycore_void_result prime_res =
        yetty_yclass_rpc_session_translate_class(rpc_root->session, "yetty_yfigure_container");
    if (YETTY_IS_ERR(prime_res)) {
        yetty_ycore_error_destroy(prime_res.error);
    }

    struct yetty_ycore_void_result session_install_res =
        yetty_ygui_framework_set_session(obj, rpc_root->session);
    if (YETTY_IS_ERR(session_install_res)) {
        free(framework->container_obj);
        framework->container_obj = NULL;
        struct yetty_ycore_void_result dr = yetty_yclass_rpc_disconnect(rpc_root);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_attach: set_session",
                         session_install_res);
    }
    framework->rpc_root = rpc_root;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_framework_attach(struct yetty_yclass_object *obj,
                                                           int read_fd, int write_fd,
                                                           int compressed)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res, "yetty_ygui_framework_attach: from_obj");
    (void)compressed; /* connect_fds picks the connection's compression */
    struct yetty_yclass_object_ptr_result root_res =
        yetty_yclass_rpc_connect_fds(read_fd, write_fd);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, root_res, "yetty_ygui_framework_attach: connect_fds");
    return framework_wire_root(obj, framework_res.value, root_res.value);
}

/* Attach over a caller-provided multiplexed connection — the endpoint path.
 * The framework opens its OWN dynamic RPC channel on the connection (the SSH
 * model), so it never shares/tears a lane with other clients on the PTY. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_framework_attach_connection(
    struct yetty_yclass_object *obj, struct yetty_ywire_connection *connection)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res,
                        "yetty_ygui_framework_attach_connection: from_obj");
    struct yetty_yclass_object_ptr_result root_res = yetty_yclass_rpc_connect_channel(connection);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, root_res,
                        "yetty_ygui_framework_attach_connection: connect_channel");
    return framework_wire_root(obj, framework_res.value, root_res.value);
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

int yetty_ygui_emit_child_committed(const struct yetty_ygui_emit_ctx *ctx, uint32_t child_id)
{
    /* Committed set only — mints staged this tick don't count, so a widget
     * whose CREATE_CHILD is still in flight (or was discarded by a failed
     * flush) never skips its body emission. */
    if (!ctx || !ctx->framework) {
        return 0;
    }
    return figure_is_minted(ctx->framework, child_id);
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

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_framework_set_viewport(struct yetty_yclass_object *obj,
                                                                 float width_px, float height_px)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res,
                        "yetty_ygui_framework_set_viewport: from_obj");
    struct yetty_ygui_framework *framework = framework_res.value;
    framework->viewport_w = width_px;
    framework->viewport_h = height_px;
    framework->dirty = 1;
    return YETTY_OK_VOID();
}

struct yetty_ygui_theme *yetty_ygui_framework_theme(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
    return framework ? framework->theme : NULL;
}

struct yetty_yfont_font *yetty_ygui_framework_font(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
    return framework ? framework->font : NULL;
}

void yetty_ygui_framework_set_font(struct yetty_yclass_object *obj, struct yetty_yfont_font *font)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
    if (framework) {
        framework->font = font; /* borrowed — not owned */
    }
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_framework_set_theme(struct yetty_yclass_object *obj,
                                                              struct yetty_ygui_theme *theme)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res,
                        "yetty_ygui_framework_set_theme: from_obj");
    struct yetty_ygui_framework *framework = framework_res.value;
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

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_framework_apply_config_to_theme(
    struct yetty_yclass_object *obj, const struct yetty_yconfig_config *config)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res,
                        "yetty_ygui_framework_apply_config_to_theme: from_obj");
    struct yetty_ygui_framework *framework = framework_res.value;
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

void yetty_ygui_framework_viewport(struct yetty_yclass_object *obj, float *width_px,
                                   float *height_px)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
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

void yetty_ygui_framework_mark_dirty(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
    if (framework) {
        framework->dirty = 1;
    }
}

int yetty_ygui_framework_is_dirty(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
    return framework ? framework->dirty : 0;
}

int yetty_ygui_framework_has_pressed_widget(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
    return framework && framework->pressed_obj ? 1 : 0;
}

struct yetty_yclass_object *yetty_ygui_framework_pressed_widget(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
    return framework ? framework->pressed_obj : NULL;
}

struct yetty_yclass_object *yetty_ygui_framework_hovered_widget(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
    return framework ? framework->hovered_obj : NULL;
}

/* Clear any hover / press capture the framework holds on `widget` — called by
 * widget destroy so a freed widget never lingers as the hovered/pressed
 * target. Internal (declared in internal.h); the framework data slice is
 * opaque to other ygui TUs. */
struct yetty_ycore_void_result yetty_ygui_framework_forget_widget(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *widget)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
    if (!framework) {
        return YETTY_OK_VOID();
    }
    if (framework->hovered_obj == widget) {
        framework->hovered_obj = NULL;
    }
    if (framework->pressed_obj == widget) {
        framework->pressed_obj = NULL;
    }
    return YETTY_OK_VOID();
}

void yetty_ygui_framework_notify(struct yetty_yclass_object *obj, int severity, const char *msg)
{
    (void)obj;
    ydebug("ygui notify[%d]: %s", severity, msg ? msg : "");
}

void yetty_ygui_framework_notify_ttl(struct yetty_yclass_object *obj, int severity, const char *msg,
                                     float ttl_seconds)
{
    (void)obj;
    (void)ttl_seconds;
    ydebug("ygui notify[%d]: %s", severity, msg ? msg : "");
}

void yetty_ygui_framework_set_key_cb(struct yetty_yclass_object *obj, yetty_ygui_key_cb cb,
                                     void *userdata)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
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

/* Forward decl — defined later in this file; dispatch_key routes unconsumed
 * scroll keys through the same wheel path a scrollarea already handles. */
struct yetty_ycore_int_result yetty_ygui_framework_feed_mouse_scroll(
    struct yetty_yclass_object *obj, float x, float y, float dx, float dy);

static void dispatch_key(struct yetty_ygui_framework *framework, uint32_t key, int mods)
{
    int consumed = 0;
    if (framework->key_cb) {
        consumed = framework->key_cb(framework->self_obj, key, mods, framework->key_userdata);
    }
    /* Keyboard scrolling: when the app didn't claim the key, PageUp/PageDown/
     * Up/Down scroll the scroll region the same way the wheel does — a
     * scrollarea's on_scroll slides its content, and if there's nothing to
     * scroll the key is simply inert. dy>0 = toward the top (on_scroll's
     * convention); one notch per arrow, one viewport per page. The scroll is
     * offered at the viewport centre, which lands inside the primary scroll
     * region for the common full-area layout. */
    if (!consumed) {
        float dy = 0.0f;
        switch (key) {
        case YETTY_YGUI_KEY_ARROW_UP:
            dy = 1.0f;
            break;
        case YETTY_YGUI_KEY_ARROW_DOWN:
            dy = -1.0f;
            break;
        case YETTY_YGUI_KEY_PAGE_UP:
            dy = framework->viewport_h / 48.0f;
            break;
        case YETTY_YGUI_KEY_PAGE_DOWN:
            dy = -(framework->viewport_h / 48.0f);
            break;
        default:
            break;
        }
        if (dy != 0.0f) {
            struct yetty_ycore_int_result scroll_res = yetty_ygui_framework_feed_mouse_scroll(
                framework->self_obj, framework->viewport_w * 0.5f, framework->viewport_h * 0.5f,
                0.0f, dy);
            if (YETTY_IS_ERR(scroll_res)) {
                yetty_ycore_error_destroy(scroll_res.error);
            }
        }
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
            case '~': {
                /* The parameter is the key id, with an optional ";<mods>"
                 * suffix (decoded separately by csi_decode_mods). Parse the
                 * whole leading integer so multi-digit ids aren't truncated to
                 * their first digit — e.g. F12 arrives as CSI 24~, which the
                 * old single-char switch misread as Insert (CSI 2~). */
                int id = 0;
                for (int i = 0; i < st->params_len && st->params[i] >= '0' && st->params[i] <= '9';
                     ++i) {
                    id = id * 10 + (st->params[i] - '0');
                }
                switch (id) {
                case 2:
                    key = YETTY_YGUI_KEY_INSERT;
                    break;
                case 3:
                    key = YETTY_YGUI_KEY_DELETE;
                    break;
                case 5:
                    key = YETTY_YGUI_KEY_PAGE_UP;
                    break;
                case 6:
                    key = YETTY_YGUI_KEY_PAGE_DOWN;
                    break;
                case 24:
                    key = YETTY_YGUI_KEY_F12;
                    break;
                default:
                    break;
                }
                break;
            }
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

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_framework_feed_input(struct yetty_yclass_object *obj,
                                                               const char *bytes, size_t n)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res,
                        "yetty_ygui_framework_feed_input: from_obj");
    struct yetty_ygui_framework *framework = framework_res.value;
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
 * win over earlier ones (paint order — last-drawn is on top).
 *
 * Figure/scrollarea clipping is already enforced here WITHOUT a separate
 * scissor parameter: the recursion prunes the whole subtree the moment the
 * point falls outside a node's rect, so reaching a descendant requires the
 * point to lie inside EVERY ancestor's rect. The emit walk's fig_clip is the
 * intersection of the figure ancestors' rects (a subset of all ancestors),
 * so it can only be looser than the per-ancestor test applied here. A point
 * this walk accepts is therefore always inside fig_clip — a scrollarea child
 * scrolled out of the viewport has its rect moved outside the scrollarea's
 * rect (the layout pass bakes scroll_main into child rects) and is rejected
 * at the scrollarea node. No off-viewport child is ever hittable; threading
 * an explicit clip rect through here would be dead code. */
static struct yetty_yclass_object_ptr_result hit_test(struct yetty_yclass_object *node, float x,
                                                      float y)
{
    if (!node) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    /* A hidden subtree receives no hits — even though the layout pass
     * skips it (so it keeps whatever rect it last had), a folded-away or
     * closed-and-stale overlay must not intercept clicks meant for the
     * visible widgets it happens to overlap. Mirrors the emit walk's
     * should_skip_subtree. */
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(node);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, layout_res, "hit_test: layout_get");
    if (layout_res.value && layout_res.value->hidden) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(node);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, rect_res, "hit_test: rect");
    if (!rect_contains(rect_res.value, x, y)) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_object *deepest = node;
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(node);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, child_res, "hit_test: first_child");
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        struct yetty_yclass_object_ptr_result child_hit = hit_test(c, x, y);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, child_hit, "hit_test: child");
        if (child_hit.value) {
            deepest = child_hit.value;
        }
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, next_res, "hit_test: next_sibling");
        c = next_res.value;
    }
    return YETTY_OK(yetty_yclass_object_ptr, deepest);
}

/* True if `target` (or any ancestor) is a menubar. A press on a menubar trigger
 * manages its own menu open/close (mutual-exclusive toggle), so the generic
 * click-outside dismissal below must skip it — otherwise the just-opened menu
 * would be closed again in the same click. */
/* Exact class identity — `_from`/object_data is a downcast that succeeds on any
 * widget (returns a garbage slice), so it can't be used as a type test. The
 * object's minted class pointer can. */
static int object_class_is(struct yetty_yclass_object *obj, struct yetty_yclass_object *class_obj)
{
    struct yetty_yclass_ptr_result class_res = yetty_yclass_object_class(obj);
    if (YETTY_IS_ERR(class_res)) {
        yetty_ycore_error_destroy(class_res.error);
        return 0;
    }
    return (struct yetty_yclass_object *)class_res.value == class_obj;
}

static struct yetty_ycore_int_result press_hit_menubar(struct yetty_yclass_object *target)
{
    struct yetty_yclass_ptr_result menubar_class = yetty_ygui_menubar_class_get();
    YETTY_RETURN_IF_ERR(yetty_ycore_int, menubar_class, "press_hit_menubar: menubar class");
    for (struct yetty_yclass_object *ancestor = target; ancestor;) {
        if (object_class_is(ancestor, (struct yetty_yclass_object *)menubar_class.value)) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
        struct yetty_yclass_object_ptr_result parent_res = yetty_ygui_widget_parent(ancestor);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, parent_res, "press_hit_menubar: parent");
        ancestor = parent_res.value;
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

/* Close every open popup_menu in `node`'s subtree whose rect does NOT contain
 * (x,y) — i.e. dismiss menus the user clicked away from. A click inside an open
 * menu keeps it open (its own on_press handles the item / drill). */
static struct yetty_ycore_void_result close_menus_clicked_outside(struct yetty_yclass_object *node,
                                                                  float x, float y)
{
    if (!node) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_ptr_result menu_class = yetty_ygui_popup_menu_class_get();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, menu_class, "close_menus_clicked_outside: menu class");
    if (object_class_is(node, (struct yetty_yclass_object *)menu_class.value)) {
        struct yetty_ycore_int_result open_res = yetty_ygui_popup_menu_is_open(node);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, open_res, "close_menus_clicked_outside: is_open");
        if (open_res.value) {
            struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(node);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "close_menus_clicked_outside: rect");
            if (!rect_contains(rect_res.value, x, y)) {
                struct yetty_ycore_void_result close_res = yetty_ygui_popup_menu_close(node);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, close_res,
                                    "close_menus_clicked_outside: close");
            }
        }
    }
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res, "close_menus_clicked_outside: first_child");
    for (struct yetty_yclass_object *child = child_res.value; child;) {
        struct yetty_ycore_void_result walk_res = close_menus_clicked_outside(child, x, y);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, walk_res, "close_menus_clicked_outside: child");
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(child);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, next_res, "close_menus_clicked_outside: next");
        child = next_res.value;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_framework_feed_mouse_button(
    struct yetty_yclass_object *obj, float x, float y, int button, int pressed, int mods)
{
    (void)mods;
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, framework_res,
                        "yetty_ygui_framework_feed_mouse_button: from_obj");
    struct yetty_ygui_framework *framework = framework_res.value;
    if (!framework->root) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_yclass_object *target;
    if (pressed) {
        struct yetty_yclass_object_ptr_result hit_res = hit_test(framework->root, x, y);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, hit_res,
                            "yetty_ygui_framework_feed_mouse_button: hit_test");
        target = hit_res.value;
        /* Click-to-front: a press anywhere inside a floating overlay
         * (dialog / debug window) moves that overlay to the end of its
         * parent's child list, so it paints last (front) within the
         * shared chrome ygrid AND wins the next overlap hit-test. Walk up
         * to the nearest floating ancestor — independent of which inner
         * widget ends up handling the press. */
        for (struct yetty_yclass_object *a = target; a;) {
            struct yetty_ycore_int_result floating_res = yetty_ygui_widget_is_floating(a);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, floating_res,
                                "yetty_ygui_framework_feed_mouse_button: is_floating");
            if (floating_res.value) {
                struct yetty_ycore_void_result raise_res = yetty_ygui_widget_raise(a);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, raise_res,
                                    "yetty_ygui_framework_feed_mouse_button: raise");
                struct yetty_ycore_void_result dirty_res = yetty_ygui_widget_set_dirty_flag(a, 1);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, dirty_res,
                                    "yetty_ygui_framework_feed_mouse_button: set_dirty_flag");
                framework->dirty = 1;
                break;
            }
            struct yetty_yclass_object_ptr_result parent_res = yetty_ygui_widget_parent(a);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, parent_res,
                                "yetty_ygui_framework_feed_mouse_button: parent");
            a = parent_res.value;
        }
        /* Click-outside-to-dismiss: unless the press landed on a menubar trigger
         * (which toggles its own menu), close any open menu the user clicked away
         * from. Runs before dispatch so a press in the document both dismisses the
         * menu and still places the caret. */
        struct yetty_ycore_int_result menubar_hit_res = press_hit_menubar(target);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, menubar_hit_res,
                            "yetty_ygui_framework_feed_mouse_button: press_hit_menubar");
        if (!menubar_hit_res.value) {
            struct yetty_ycore_void_result dismiss_res =
                close_menus_clicked_outside(framework->root, x, y);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, dismiss_res,
                                "yetty_ygui_framework_feed_mouse_button: dismiss menus");
        }
    } else {
        /* Release goes to the capture target (if any) so the widget that
         * began a drag also sees its end, even off-rect. */
        if (framework->pressed_obj) {
            target = framework->pressed_obj;
        } else {
            struct yetty_yclass_object_ptr_result hit_res = hit_test(framework->root, x, y);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, hit_res,
                                "yetty_ygui_framework_feed_mouse_button: hit_test");
            target = hit_res.value;
        }
        framework->pressed_obj = NULL;
    }
    while (target) {
        struct yetty_ycore_int_result r =
            pressed
                ? yetty_ygui_widget_on_press((struct yetty_yclass_object *)target, x, y, button)
                : yetty_ygui_widget_on_release((struct yetty_yclass_object *)target, x, y, button);
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
        struct yetty_yclass_object_ptr_result parent_res = yetty_ygui_widget_parent(target);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, parent_res,
                            "yetty_ygui_framework_feed_mouse_button: parent");
        target = parent_res.value;
    }
    return YETTY_OK(yetty_ycore_int, 0); /* fell through — chrome should handle it */
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_framework_feed_mouse_motion(
    struct yetty_yclass_object *obj, float x, float y)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, framework_res,
                        "yetty_ygui_framework_feed_mouse_motion: from_obj");
    struct yetty_ygui_framework *framework = framework_res.value;
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
                yetty_ygui_widget_on_motion((struct yetty_yclass_object *)cap, x, y);
            if (YETTY_IS_ERR(r)) {
                return YETTY_ERR(yetty_ycore_int,
                                 "yetty_ygui_framework_feed_mouse_motion: capture on_motion", r);
            }
            if (r.value) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
            struct yetty_yclass_object_ptr_result parent_res = yetty_ygui_widget_parent(cap);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, parent_res,
                                "yetty_ygui_framework_feed_mouse_motion: capture parent");
            cap = parent_res.value;
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    struct yetty_yclass_object_ptr_result hit_res = hit_test(framework->root, x, y);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, hit_res,
                        "yetty_ygui_framework_feed_mouse_motion: hit_test");
    struct yetty_yclass_object *target = hit_res.value;
    /* Hover bookkeeping — flip enter/leave when the deepest-hit widget
     * changes from the previous motion event. Mark both old and new
     * dirty so the next emit repaints them with the correct variant. */
    if (target != framework->hovered_obj) {
        if (framework->hovered_obj) {
            struct yetty_ycore_void_result hov_res =
                yetty_ygui_widget_set_hovered(framework->hovered_obj, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, hov_res,
                                "yetty_ygui_framework_feed_mouse_motion: clear hovered");
            struct yetty_ycore_void_result dirty_res =
                yetty_ygui_widget_set_dirty_flag(framework->hovered_obj, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, dirty_res,
                                "yetty_ygui_framework_feed_mouse_motion: clear hovered dirty");
        }
        if (target) {
            struct yetty_ycore_void_result hov_res = yetty_ygui_widget_set_hovered(target, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, hov_res,
                                "yetty_ygui_framework_feed_mouse_motion: set hovered");
            struct yetty_ycore_void_result dirty_res = yetty_ygui_widget_set_dirty_flag(target, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, dirty_res,
                                "yetty_ygui_framework_feed_mouse_motion: set hovered dirty");
        }
        framework->hovered_obj = target;
        framework->dirty = 1;
    }
    while (target) {
        struct yetty_ycore_int_result r =
            yetty_ygui_widget_on_motion((struct yetty_yclass_object *)target, x, y);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_framework_feed_mouse_motion: on_motion",
                             r);
        }
        if (r.value) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
        struct yetty_yclass_object_ptr_result parent_res = yetty_ygui_widget_parent(target);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, parent_res,
                            "yetty_ygui_framework_feed_mouse_motion: parent");
        target = parent_res.value;
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

YETTY_ANNOTATE("expose")
/* Returns 1 if a widget consumed the scroll (a scrollarea / filepicker took
 * it), 0 if it bubbled to the root unhandled. The in-terminal host uses the 0
 * case to bounce the wheel back to terminal scrollback. */
struct yetty_ycore_int_result yetty_ygui_framework_feed_mouse_scroll(
    struct yetty_yclass_object *obj, float x, float y, float dx, float dy)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, framework_res,
                        "yetty_ygui_framework_feed_mouse_scroll: from_obj");
    struct yetty_ygui_framework *framework = framework_res.value;
    if (!framework->root) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    /* Deliver to the widget under the pointer, bubbling up until one
     * consumes it (a scrollarea / filepicker). Mirrors the press path. */
    struct yetty_yclass_object_ptr_result hit_res = hit_test(framework->root, x, y);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, hit_res,
                        "yetty_ygui_framework_feed_mouse_scroll: hit_test");
    struct yetty_yclass_object *target = hit_res.value;
    while (target) {
        struct yetty_ycore_int_result r =
            yetty_ygui_widget_on_scroll((struct yetty_yclass_object *)target, x, y, dx, dy);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_framework_feed_mouse_scroll: on_scroll",
                             r);
        }
        if (r.value) {
            framework->dirty = 1;
            return YETTY_OK(yetty_ycore_int, 1);
        }
        struct yetty_yclass_object_ptr_result parent_res = yetty_ygui_widget_parent(target);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, parent_res,
                            "yetty_ygui_framework_feed_mouse_scroll: parent");
        target = parent_res.value;
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

struct yetty_yclass_object *yetty_ygui_framework_root(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
    return framework ? framework->root : NULL;
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_framework_set_root(struct yetty_yclass_object *obj,
                                                             struct yetty_yclass_object *root)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res, "yetty_ygui_framework_set_root: from_obj");
    struct yetty_ygui_framework *framework = framework_res.value;
    if (!root) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_set_root: NULL root");
    }
    framework->root = root;
    struct yetty_ycore_void_result set_fw_res = yetty_ygui_widget_set_framework(root, obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_fw_res,
                        "yetty_ygui_framework_set_root: set_framework");
    /* Allocate the root's wire id retroactively. */
    struct yetty_ycore_uint32_result id_res = yetty_ygui_widget_id(root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "yetty_ygui_framework_set_root: id");
    if (id_res.value == 0) {
        struct uint32_result idr = yetty_ygui_framework_alloc_id(obj);
        if (YETTY_IS_ERR(idr)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_set_root: alloc_id failed",
                             idr);
        }
        struct yetty_ycore_void_result set_id_res = yetty_ygui_widget_set_id(root, idr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_id_res, "yetty_ygui_framework_set_root: set_id");
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * ID allocator.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct uint32_result yetty_ygui_framework_alloc_id(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(uint32, framework_res, "yetty_ygui_framework_alloc_id: from_obj");
    struct yetty_ygui_framework *framework = framework_res.value;
    if (framework->free_id_count > 0) {
        uint32_t id = framework->free_ids[--framework->free_id_count];
        return YETTY_OK(uint32, id);
    }
    uint32_t id = framework->next_id++;
    return YETTY_OK(uint32, id);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_framework_free_id(struct yetty_yclass_object *obj,
                                                            uint32_t id)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
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

uint32_t yetty_ygui_framework_ygrid_id(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
    return framework ? framework->ygrid_id : 0;
}

/*===========================================================================
 * Emit-side helpers used by widget emit_container implementations.
 *=========================================================================*/

struct yetty_ycore_void_result yetty_ygui_emit_create_child(
    struct yetty_ygui_emit_ctx *ctx, uint32_t child_id, uint32_t kind, float min_x, float min_y,
    float max_x, float max_y, const uint8_t *init_payload, uint32_t init_payload_bytes)
{
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_create_child: NULL ctx");
    }
    if (!ctx->framework || !ctx->framework->container_obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_create_child: no container");
    }
    /* Call the typed yclass stub directly (in-process local dispatch, or RPC
     * when the container proxy carries a session). `kind` is the registry
     * token (yetty_yfigure_kind_token of the kind name). */
    struct yetty_ycore_rectangle rect = {
        .min = {.x = min_x, .y = min_y},
        .max = {.x = max_x, .y = max_y},
    };
    struct yetty_ycore_buffer init_buf = {
        .data = (uint8_t *)init_payload,
        .capacity = 0,
        .size = init_payload_bytes,
    };
    return yetty_yfigure_create_child(ctx->framework->container_obj, kind, child_id, rect,
                                      init_buf);
}

struct yetty_ycore_void_result yetty_ygui_emit_delete_child(struct yetty_ygui_emit_ctx *ctx,
                                                            uint32_t child_id)
{
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_delete_child: NULL ctx");
    }
    if (!ctx->framework || !ctx->framework->container_obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_delete_child: no container");
    }
    return yetty_yfigure_delete_child(ctx->framework->container_obj, child_id);
}

struct yetty_ycore_void_result yetty_ygui_emit_figure_body(struct yetty_ygui_emit_ctx *ctx,
                                                           uint32_t figure_id,
                                                           const uint8_t *payload,
                                                           uint32_t payload_len)
{
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_figure_body: NULL ctx");
    }
    if (figure_id == 0) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_figure_body: figure_id is 0");
    }
    if (!ctx->framework || !ctx->framework->container_obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_figure_body: no container");
    }
    ydebug("emit_figure_body id=%u size=%u", figure_id, payload_len);
    struct yetty_ycore_buffer body = {
        .data = (uint8_t *)payload,
        .capacity = 0,
        .size = payload_len,
    };
    return yetty_yfigure_apply_child_body(ctx->framework->container_obj, figure_id, body);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_framework_ship_figure_delta(
    struct yetty_yclass_object *obj, uint32_t figure_id, const uint8_t *body, uint32_t body_len)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
    if (!framework) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ygui_framework_ship_figure_delta: NULL framework");
    }
    if (figure_id == 0) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ygui_framework_ship_figure_delta: figure_id is 0");
    }
    if (!framework->container_obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_ship_figure_delta: no container");
    }
    if (body_len == 0) {
        return YETTY_OK_VOID();
    }
    ydebug("ship_figure_delta id=%u size=%u", figure_id, body_len);
    struct yetty_ycore_buffer buf = {
        .data = (uint8_t *)body,
        .capacity = 0,
        .size = body_len,
    };
    return yetty_yfigure_apply_child_body(framework->container_obj, figure_id, buf);
}

struct yetty_ycore_void_result yetty_ygui_emit_set_child_rect(struct yetty_ygui_emit_ctx *ctx,
                                                              uint32_t child_id, float min_x,
                                                              float min_y, float max_x, float max_y)
{
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_set_child_rect: NULL ctx");
    }
    if (!ctx->framework || !ctx->framework->container_obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_set_child_rect: no container");
    }
    struct yetty_ycore_rectangle rect = {
        .min = {.x = min_x, .y = min_y},
        .max = {.x = max_x, .y = max_y},
    };
    return yetty_yfigure_set_child_rect(ctx->framework->container_obj, child_id, rect);
}

int32_t yetty_ygui_framework_next_raise_z(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_framework *framework = framework_data(obj);
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
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_set_child_z: NULL ctx");
    }
    if (!ctx->framework || !ctx->framework->container_obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_set_child_z: no container");
    }
    return yetty_yfigure_set_child_z(ctx->framework->container_obj, child_id, z);
}

struct yetty_ycore_void_result yetty_ygui_emit_set_child_hidden(struct yetty_ygui_emit_ctx *ctx,
                                                                uint32_t child_id, int hidden)
{
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_set_child_hidden: NULL ctx");
    }
    if (!ctx->framework || !ctx->framework->container_obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_emit_set_child_hidden: no container");
    }
    return yetty_yfigure_set_child_hidden(ctx->framework->container_obj, child_id,
                                          hidden ? 1u : 0u);
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
static struct yetty_ycore_int_result should_skip_subtree(const struct yetty_yclass_object *node)
{
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, layout_res, "should_skip_subtree: layout_get");
    const struct yetty_ygui_layout *l = layout_res.value;
    if (!l) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    /* Explicitly folded-away subtree (collapsed collapsing_header /
     * tree_node child). Omitting the CMD_GROUP body bytes for the tick
     * removes the prims on the receiver — same mechanism the closed-popup
     * skip below relies on. */
    if (l->hidden) {
        return YETTY_OK(yetty_ycore_int, 1);
    }
    if (!l->absolute) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    return YETTY_OK(yetty_ycore_int, (l->width <= 0.0f || l->height <= 0.0f) ? 1 : 0);
}

/* Does `node` or any descendant that paints into the SAME figure body have
 * its dirty flag set? Recursion stops at nested figure boundaries: a child
 * figure is an independent receiver-side object shipped by its own
 * walk_emit_body pass, so its dirtiness must not force the parent figure's
 * body to be re-shipped. This gates the incremental figure-body skip — a
 * figure whose body is unchanged keeps its last body on the receiver. */
static struct yetty_ycore_int_result subtree_dirty(const struct yetty_yclass_object *node)
{
    struct yetty_ycore_int_result dirty_res = yetty_ygui_widget_is_dirty(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dirty_res, "subtree_dirty: is_dirty");
    if (dirty_res.value) {
        return YETTY_OK(yetty_ycore_int, 1);
    }
    struct yetty_yclass_object_ptr_result child_res =
        yetty_ygui_widget_first_child((struct yetty_yclass_object *)node);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, child_res, "subtree_dirty: first_child");
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        struct yetty_ycore_uint32_result kind_res = yetty_ygui_widget_figure_kind(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, kind_res, "subtree_dirty: figure_kind");
        if (kind_res.value == 0) {
            /* Inline child paints into the same figure body — recurse. A child
             * figure is shipped as its own body, so it does not count. */
            struct yetty_ycore_int_result sub_res = subtree_dirty(c);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, sub_res, "subtree_dirty: child");
            if (sub_res.value) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, next_res, "subtree_dirty: next_sibling");
        c = next_res.value;
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

/* Clear every widget's dirty flag after a successful emit, so the next emit
 * only re-ships subtrees that actually changed since this one. */
static struct yetty_ycore_void_result clear_subtree_dirty(struct yetty_yclass_object *node)
{
    if (!node) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result flag_res = yetty_ygui_widget_set_dirty_flag(node, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flag_res, "clear_subtree_dirty: set_dirty_flag");
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res, "clear_subtree_dirty: first_child");
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        struct yetty_ycore_void_result rc = clear_subtree_dirty(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rc, "clear_subtree_dirty: child");
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, next_res, "clear_subtree_dirty: next_sibling");
        c = next_res.value;
    }
    return YETTY_OK_VOID();
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
    struct yetty_ycore_uint32_result kind_res = yetty_ygui_widget_figure_kind(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, kind_res, "hide_subtree_figures: figure_kind");
    struct yetty_ycore_uint32_result id_res = yetty_ygui_widget_id(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "hide_subtree_figures: id");
    if (kind_res.value != 0 && ctx->framework && figure_is_minted(ctx->framework, id_res.value)) {
        struct yetty_ycore_void_result hr = yetty_ygui_emit_set_child_hidden(ctx, id_res.value, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "hide_subtree_figures: hide");
    }
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res, "hide_subtree_figures: first_child");
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        struct yetty_ycore_void_result rc = hide_subtree_figures(c, ctx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rc, "hide_subtree_figures: child");
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, next_res, "hide_subtree_figures: next_sibling");
        c = next_res.value;
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
    struct yetty_ycore_uint32_result fkind_res = yetty_ygui_widget_figure_kind(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fkind_res,
                        "yetty_ygui_framework_walk_emit_container: figure_kind");
    uint32_t fkind = fkind_res.value;
    struct yetty_ycore_int_result skip_res = should_skip_subtree(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, skip_res,
                        "yetty_ygui_framework_walk_emit_container: should_skip_subtree");
    int skip = skip_res.value;
    struct yetty_ycore_uint32_result id_res = yetty_ygui_widget_id(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "yetty_ygui_framework_walk_emit_container: id");
    uint32_t node_id = id_res.value;
    int saved_clip_active = ctx->fig_clip_active;
    struct yetty_ycore_rectangle saved_clip = ctx->fig_clip;

    /* Figure-boundary node (floating window / menu): it lives as its own
     * receiver-side child figure rather than inlining into the chrome
     * ygrid. We mark the figure hidden instead of deleting it on close —
     * re-showing then costs one record, not a CREATE + full-body re-ship.
     * A boundary that has never been shown is simply not created yet. */
    if (fkind != 0) {
        uint32_t fid = node_id;
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
        struct yetty_ycore_rectangle_result fr_res = yetty_ygui_widget_rect(node);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr_res,
                            "yetty_ygui_framework_walk_emit_container: figure rect");
        struct yetty_ycore_rectangle fr = fr_res.value;
        if (ctx->fig_clip_active) {
            fr = emit_rect_intersect(fr, ctx->fig_clip);
        }
        struct yetty_ycore_void_result er = yetty_ygui_emit_ensure_child(
            ctx, fid, fkind, fr.min.x, fr.min.y, fr.max.x, fr.max.y, NULL, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, er,
                            "yetty_ygui_framework_walk_emit_container: figure ensure_child");
        struct yetty_ycore_int_result fz_res = yetty_ygui_widget_figure_z(node);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fz_res,
                            "yetty_ygui_framework_walk_emit_container: figure_z");
        struct yetty_ycore_void_result zr =
            yetty_ygui_emit_set_child_z(ctx, fid, (int32_t)fz_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, zr,
                            "yetty_ygui_framework_walk_emit_container: figure set_z");
        struct yetty_ycore_void_result hr = yetty_ygui_emit_set_child_hidden(ctx, fid, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hr,
                            "yetty_ygui_framework_walk_emit_container: figure show");
        /* Narrow the clip for this figure's subtree. */
        ctx->fig_clip = fr;
        ctx->fig_clip_active = 1;
    } else if (skip) {
        ydebug("walk_container: SKIP node=%p id=%u", (void *)node, node_id);
        /* Folded-away subtree: don't emit it, but hide any figures inside
         * it so they don't linger (e.g. a scrollarea figure in a hidden
         * tab). */
        return hide_subtree_figures(node, ctx);
    }
    ydebug("walk_container: node=%p id=%u klass=%p", (void *)node, node_id, (void *)node->klass);
    struct yetty_ycore_void_result r =
        yetty_ygui_widget_emit_container((struct yetty_yclass_object *)node, ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                        "yetty_ygui_framework_walk_emit_container: emit_container");
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(node);
    if (YETTY_IS_ERR(child_res)) {
        ctx->fig_clip = saved_clip;
        ctx->fig_clip_active = saved_clip_active;
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_walk_emit_container: first_child",
                         child_res);
    }
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        struct yetty_ycore_void_result rc = yetty_ygui_framework_walk_emit_container(c, ctx);
        if (YETTY_IS_ERR(rc)) {
            ctx->fig_clip = saved_clip;
            ctx->fig_clip_active = saved_clip_active;
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ygui_framework_walk_emit_container: child walk", rc);
        }
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        if (YETTY_IS_ERR(next_res)) {
            ctx->fig_clip = saved_clip;
            ctx->fig_clip_active = saved_clip_active;
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ygui_framework_walk_emit_container: next_sibling", next_res);
        }
        c = next_res.value;
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
        yetty_ygui_widget_emit_body((struct yetty_yclass_object *)node, ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_framework_walk_emit_body: emit_body");
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res,
                        "yetty_ygui_framework_walk_emit_body: first_child");
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        struct yetty_ycore_void_result rc = yetty_ygui_framework_walk_emit_body(c, ctx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rc,
                            "yetty_ygui_framework_walk_emit_body: child walk");
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, next_res,
                            "yetty_ygui_framework_walk_emit_body: next_sibling");
        c = next_res.value;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_framework_walk_emit_body(struct yetty_yclass_object *node,
                                                                   struct yetty_ygui_emit_ctx *ctx)
{
    if (!node) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_int_result skip_res = should_skip_subtree(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, skip_res,
                        "yetty_ygui_framework_walk_emit_body: should_skip_subtree");
    if (skip_res.value) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_uint32_result fkind_res = yetty_ygui_widget_figure_kind(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fkind_res,
                        "yetty_ygui_framework_walk_emit_body: figure_kind");
    /* Non-boundary node: paint into whatever draw list is active. */
    if (fkind_res.value == 0) {
        return walk_emit_body_inline(node, ctx);
    }

    struct yetty_ycore_uint32_result id_res = yetty_ygui_widget_id(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "yetty_ygui_framework_walk_emit_body: id");
    uint32_t node_id = id_res.value;

    /* Incremental figure body: if this figure was already minted on the
     * receiver (its body shipped at least once) and nothing in its body
     * subtree changed, skip re-shipping. The receiver keeps the last body
     * for this figure id; a pure rect move is handled separately by the
     * pass-1 SET_CHILD_RECT. This is what stops an unchanged page (a
     * scrollarea figure) from being re-serialized every emit. */
    if (ctx->framework && figure_is_minted(ctx->framework, node_id)) {
        struct yetty_ycore_int_result dirty_res = subtree_dirty(node);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dirty_res,
                            "yetty_ygui_framework_walk_emit_body: subtree_dirty");
        if (!dirty_res.value) {
            ydebug("figure SKIP (clean, minted) id=%u", node_id);
            return YETTY_OK_VOID();
        }
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
    int saved_clip_active = ctx->fig_clip_active;
    struct yetty_ycore_rectangle saved_clip = ctx->fig_clip;
    ctx->ygrid_drawable_list = figure_dl;
    ctx->current_figure_id = node_id;

    /* Narrow the clip to this figure's rect (a scrollarea's viewport) for the
     * duration of its subtree body paint, mirroring the container walk. The
     * figure GPU-scissors to this rect anyway, so a body-emitting widget
     * (ydraw_embed) can drop primitives outside it up front instead of emitting
     * the whole tall page every frame. Nested figures intersect with this. */
    struct yetty_ycore_rectangle_result frect_res = yetty_ygui_widget_rect(node);
    if (YETTY_IS_OK(frect_res)) {
        struct yetty_ycore_rectangle frect = frect_res.value;
        if (ctx->fig_clip_active) {
            frect = emit_rect_intersect(frect, ctx->fig_clip);
        }
        ctx->fig_clip = frect;
        ctx->fig_clip_active = 1;
    } else {
        yetty_ycore_error_destroy(frect_res.error);
    }

    struct yetty_ycore_void_result br = walk_emit_body_inline(node, ctx);

    ctx->ygrid_drawable_list = saved_dl;
    ctx->current_figure_id = saved_fid;
    ctx->fig_clip = saved_clip;
    ctx->fig_clip_active = saved_clip_active;

    if (YETTY_IS_ERR(br)) {
        yetty_ydraw_drawable_list_destroy(figure_dl);
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_walk_emit_body: figure subtree",
                         br);
    }

    size_t body_size = yetty_ydraw_drawable_list_size(figure_dl);
    if (body_size > UINT32_MAX) {
        yetty_ydraw_drawable_list_destroy(figure_dl);
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ygui_framework_walk_emit_body: figure body exceeds u32");
    }
    if (body_size > 0) {
        const void *body_data = yetty_ydraw_drawable_list_data(figure_dl);
        struct yetty_ycore_void_result fr = yetty_ygui_emit_figure_body(
            ctx, node_id, (const uint8_t *)body_data, (uint32_t)body_size);
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
            ctx, framework->ygrid_id, yetty_yfigure_kind_token("ygrid"), 0.0f, 0.0f,
            framework->viewport_w, framework->viewport_h, NULL, 0);
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
    /* Every figure-tree mutation (create/rect/z/hidden/delete) and every figure
     * body was already applied inline during the emit walk via the typed yclass
     * stubs on the container object. The only stream left is the shared chrome
     * ygrid's accumulated drawable_list, which is serialized once here and handed
     * to the ygrid child through apply_child_body. */
    if (!framework->container_obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_flush: no container");
    }
    if (framework->ygrid_drawable_list) {
        size_t dl_size = yetty_ydraw_drawable_list_size(framework->ygrid_drawable_list);
        if (dl_size > 0) {
            const void *dl_data = yetty_ydraw_drawable_list_data(framework->ygrid_drawable_list);
            struct yetty_ycore_buffer body = {
                .data = (uint8_t *)dl_data,
                .capacity = 0,
                .size = dl_size,
            };
            struct yetty_ycore_void_result br =
                yetty_yfigure_apply_child_body(framework->container_obj, framework->ygrid_id, body);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, br,
                                "yetty_ygui_framework_flush: ygrid apply_child_body");
        }
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_framework_clear(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res, "yetty_ygui_framework_clear: from_obj");
    struct yetty_ygui_framework *framework = framework_res.value;
    if (!framework->container_obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_framework_clear: no container");
    }
    /* Drop every remote figure this framework produced by clearing the host
     * container directly through the typed yclass stub. */
    return yetty_yfigure_clear_all(framework->container_obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_framework_forget_remote(struct yetty_yclass_object *obj)
{
    /* The host dropped every compositor figure this framework minted — a
     * full-screen erase / reset on the pane (CSI 2J/3J or RIS: `clear`,
     * Ctrl-L, a client-side screen clear) wipes the root container behind
     * our back. Forget the remote bookkeeping so the next emit re-creates
     * the chrome ygrid and every figure child from live widget state; the
     * receiver reuses a still-existing id gracefully (reset_content fast
     * path), so forgetting is safe even when nothing was actually wiped. */
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res,
                        "yetty_ygui_framework_forget_remote: from_obj");
    struct yetty_ygui_framework *framework = framework_res.value;
    framework->ygrid_created = 0;
    framework->minted_figure_count = 0;
    framework->dirty = 1;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_framework_emit(struct yetty_yclass_object *obj)
{
    struct yetty_ygui_framework_ptr_result framework_res = yetty_ygui_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res, "yetty_ygui_framework_emit: from_obj");
    struct yetty_ygui_framework *framework = framework_res.value;

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
        .ygrid_drawable_list = framework->ygrid_drawable_list,
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
    struct yetty_ycore_void_result clear_res = clear_subtree_dirty(framework->root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_res,
                        "yetty_ygui_framework_emit: clear_subtree_dirty");
    framework->dirty = 0;
    return YETTY_OK_VOID();
}

/* Codegen-emitted class accessor (yetty_ygui_framework_class_get /
 * _from / _to), the generated create/destroy, and the expose'd public stubs.
 * Appended at the foot like every other yclass module. */
#include "yetty/gen/impl/ygui/framework.c"
