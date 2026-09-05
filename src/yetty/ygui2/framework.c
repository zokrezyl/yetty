/*
 * ygui2 framework — layout, dirty tracking, and the wire emit pipeline.
 *
 * The framework owns the widget tree's projection onto the drawable
 * contract (strategy.md §3–§4): the FIRST emit is one insertion
 * (RESERVE + the whole tree + one offset update per minted nonzero-origin
 * group); every later emit issues the smallest sufficient operation per
 * dirty widget — an offset update for a move, an addressed reopen
 * (CMD_PATH + GROUP) for a repaint. A clean frame emits nothing.
 *
 * The widget class struct is private to widget.c (yclass convention); every
 * walk here goes through the exposed widget accessors.
 *
 * Output goes to a sink callback (headless tests, in-process hosts) or a
 * write_fd as a YETTY_DCS_YDRAW_BIN envelope — the same envelope every C
 * producer tool ships.
 */
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/cmds.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/yface/yface.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/yterminal/client-input.h>
#include <yetty/ymgui/wire.h>
#include <yetty/ygui2/defs.h>
#include <yetty/yplatform/io.h>
#include <yetty/ytrace/ytrace.h>

#include "yetty/gen/impl/ygui2/widget.h"

struct yetty_yclass_ptr_result yetty_ygui2_framework_class_get(void);

struct YETTY_ANNOTATE("class@ygui2:framework") yetty_ygui2_framework {
    struct yetty_yclass_object *root;
    float viewport_w;
    float viewport_h;
    float content_scale;
    int viewport_valid;
    /* First insertion shipped — later emits are incremental. */
    int inserted;
    /* Monotonic wire node ids (global uniqueness implies per-scope
     * uniqueness; 1 = the root group). */
    uint32_t next_node_id;
    yetty_ygui2_sink_fn sink;
    void *sink_userdata;
    int write_fd;
    int write_fd_valid;
    yetty_ygui2_key_cb key_cb;
    void *key_cb_userdata;
    /* Reused batch builder. */
    struct yetty_ydraw_drawable_list *list;
    /* Input state: click-focused widget + the drag's capture target. */
    struct yetty_yclass_object *focused;
    struct yetty_yclass_object *mouse_capture;
    /* Overlay tree: a second top-level wire group (id 2) hosting
     * absolutely-placed popups/dialogs/tooltips above the root tree. */
    struct yetty_yclass_object *overlay_root;
    /* Deferred-destruction list: overlay widgets whose OWNER died while
     * dispatch may still be executing on their stack frames (a dropdown
     * removed from its popup's selection callback). The owner's cleanup
     * severs callbacks and hides the widget — inert immediately — and
     * the framework reclaims it at the next feed/emit boundary, never
     * inside the dispatch itself. */
    struct yetty_yclass_object **orphans;
    uint32_t orphan_count;
    uint32_t orphan_capacity;
    /* Shared palette; widgets read it at paint time (0-valued per-widget
     * overrides fall back to these roles). */
    struct yetty_ygui2_theme theme;
    /* Streaming input parser state: PTY reads arrive at arbitrary
     * boundaries, so incomplete OSC/DCS/CSI sequences are retained here
     * and re-parsed when the next chunk lands. Bounded — on overflow the
     * buffer resets (resync) rather than growing. */
    uint8_t input_pending[8192];
    size_t input_pending_length;
    /* Teardown barrier: set when the host's DCS HOLD-ACK envelope is
     * parsed (the host armed its input barrier; teardown may proceed). */
    int hold_ack_seen;
    /* Height (px) the LIVE insertion actually reserved (the budget). */
    float reserved_h;
    /* SHIP-FAILURE recovery only (resize never sets this): the frame's
     * arrival is unknown, so the next emit deletes both roots and
     * re-inserts from scratch — the one case where local and terminal
     * state may have diverged. */
    int rebuild_pending;
    /* Viewport changed within the reserved span: relayout on next emit
     * (targeted reopens/offsets from set_rect), no deletion. */
    int viewport_dirty;
    /* The next insertion must home the terminal cursor first (set by
     * clear/rebuild — after a fullscreen insertion the cursor sits at the
     * reservation bottom, which would anchor a reinsert at the wrong
     * row). */
    int home_before_insert;
    /* Reservation mode (strategy.md §5). FULLSCREEN (default — the
     * alt-screen ownership contract) reserves the full supported
     * viewport range so every accepted resize is in-budget. INLINE
     * reserves the declared viewport height only: the insertion sits in
     * the user's scrollback flow, and over-reserving would scroll the
     * transcript away; growth past the reservation is an explicit
     * rejection (clear() + emit re-inserts at the new size). */
    int fullscreen;
};

YETTY_YRESULT_DECLARE(yetty_ygui2_framework_ptr, struct yetty_ygui2_framework *);
struct yetty_ygui2_framework_ptr_result yetty_ygui2_framework_from(struct yetty_yclass_object *obj);

static struct yetty_ycore_void_result framework_ship(struct yetty_ygui2_framework *framework);
/* Cross-class within-module: raw recursive teardown is INTERNAL (not in
 * the generated headers) — only dispose/rollback paths may call it. */
struct yetty_ycore_void_result yetty_ygui2_widget_destroy(struct yetty_yclass_object *obj);
/* Retained-content dispatcher (declared ahead of the generated impl
 * header so the first codegen pass resolves it). */
struct yetty_ycore_void_result yetty_ygui2_widget_paint_retained(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *list);
/* Geometry follow-up dispatcher + its dirty accessor (declared ahead of
 * the generated impl header so the first codegen pass resolves them). */
struct yetty_ycore_void_result yetty_ygui2_widget_emit_geometry(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *list);
struct yetty_ycore_void_result yetty_ygui2_widget_geometry_dirty(struct yetty_yclass_object *obj,
                                                                 int *out_geometry);
/* Skin-subgroup identity accessors (declared ahead of the generated impl
 * header so the first codegen pass resolves them). */
struct yetty_ycore_uint32_result yetty_ygui2_widget_skin_node_id(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_widget_set_skin_node_id(struct yetty_yclass_object *obj,
                                                                   uint32_t skin_node_id);
static void framework_reap_orphans(struct yetty_ygui2_framework *framework);
static void restyle_subtree(struct yetty_yclass_object *obj);
static struct yetty_yclass_object *tree_first_child(struct yetty_yclass_object *obj);
static struct yetty_yclass_object *tree_next_sibling(struct yetty_yclass_object *obj);
static int tree_flag(struct yetty_ycore_int_result flag_res);
static struct yetty_yclass_object *hit_test(struct yetty_yclass_object *obj, float x, float y);
static void framework_mouse_button(struct yetty_ygui2_framework *framework,
                                   struct yetty_yclass_object *obj, float x, float y, int button,
                                   int pressed, int mods, int *out_consumed);
static void framework_mouse_motion(struct yetty_ygui2_framework *framework,
                                   struct yetty_yclass_object *obj, float x, float y,
                                   uint32_t buttons_held, int *out_consumed);
static void framework_mouse_scroll(struct yetty_ygui2_framework *framework,
                                   struct yetty_yclass_object *obj, float x, float y,
                                   float wheel_dy, int *out_consumed);
static struct yetty_ycore_void_result framework_send_client_struct(
    struct yetty_ygui2_framework *framework, uint32_t code, const void *payload,
    size_t payload_size);
struct yetty_ycore_void_result yetty_ygui2_framework_feed_mouse_button(
    struct yetty_yclass_object *obj, float x, float y, int button, int pressed, int mods);
struct yetty_ycore_void_result yetty_ygui2_framework_feed_mouse_motion(
    struct yetty_yclass_object *obj, float x, float y, uint32_t buttons_held);
struct yetty_ycore_void_result yetty_ygui2_framework_feed_mouse_scroll(
    struct yetty_yclass_object *obj, float x, float y, float wheel_dy);

/* Explicit lifecycle (the grid pattern): make allocates + initializes,
 * dispose tears down + frees. No virtual ctor/dtor — the widget base owns
 * those slot names for the whole module. */
YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui2_framework_make(void)
{
    struct yetty_yclass_ptr_result class_res = yetty_ygui2_framework_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_res, "ygui2 framework_make: class");
    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_res, "ygui2 framework_make: alloc");
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(object_res.value);
    if (YETTY_IS_ERR(data_res)) {
        free(object_res.value);
        return YETTY_ERR(yetty_yclass_object_ptr, "ygui2 framework_make: data", data_res);
    }
    struct yetty_ygui2_framework *framework = data_res.value;
    memset(framework, 0, sizeof(*framework));
    framework->content_scale = 1.0f;
    framework->fullscreen = 1;
    framework->next_node_id = 3u; /* 1 = root, 2 = overlay root */
    framework->write_fd = -1;
    /* Brand palette, packed 0xAABBGGRR. */
    framework->theme.bg = 0xFF14100Bu;
    framework->theme.bg_lifted = 0xFF1F1A14u;
    framework->theme.bg_row = 0xFF2C261Eu;
    framework->theme.border = 0xFF474A36u;
    framework->theme.text_muted = 0xFF626155u;
    framework->theme.text_secondary = 0xFFA8A79Fu;
    framework->theme.text_primary = 0xFFE4E5E0u;
    framework->theme.accent_deep = 0xFF79895Au;
    framework->theme.accent = 0xFF92A86Bu;
    framework->theme.accent_bright = 0xFFA5C574u;
    return object_res;
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_dispose(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 framework_dispose: data");
    struct yetty_ygui2_framework *framework = data_res.value;
    /* Reclaim pre-dispose orphans through the normal removal path while
     * both trees are intact. Cleanups during the teardown below may
     * append again — those entries are freed BY the overlay teardown, so
     * the list is only released as storage afterwards, never re-walked. */
    framework_reap_orphans(framework);
    if (framework->root) {
        struct yetty_ycore_void_result destroy_res = yetty_ygui2_widget_destroy(framework->root);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        framework->root = NULL;
    }
    if (framework->overlay_root) {
        struct yetty_ycore_void_result overlay_destroy_res =
            yetty_ygui2_widget_destroy(framework->overlay_root);
        if (YETTY_IS_ERR(overlay_destroy_res)) {
            yetty_ycore_error_destroy(overlay_destroy_res.error);
        }
        framework->overlay_root = NULL;
    }
    if (framework->list) {
        yetty_ydraw_drawable_list_destroy(framework->list);
        framework->list = NULL;
    }
    free(framework->orphans);
    framework->orphans = NULL;
    framework->orphan_count = 0;
    framework->orphan_capacity = 0;
    free(obj);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Widget instantiation.
 *=========================================================================*/

/* Best-effort teardown on construction-failure paths. */
static void widget_destroy_quiet(struct yetty_yclass_object *obj)
{
    struct yetty_ycore_void_result destroy_res = yetty_ygui2_widget_destroy(obj);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
}

static struct yetty_yclass_object_ptr_result widget_instantiate(
    struct yetty_ygui2_framework *framework, struct yetty_yclass_object *framework_obj,
    const struct yetty_yclass *cls, struct yetty_yclass_object *parent)
{
    if (!cls) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ygui2 widget_instantiate: NULL class");
    }
    /* Wire-depth enforcement at MINT time: a widget whose minted-ancestor
     * chain fills the CMD_PATH id budget could never be addressed — its
     * own group id would not fit behind the prefix. Reject the add rather
     * than ever truncating an address later. */
    if (parent) {
        uint32_t minted_depth = 0;
        struct yetty_yclass_object *walk = parent;
        while (walk) {
            struct yetty_ycore_int_result transparent_res = yetty_ygui2_widget_is_transparent(walk);
            int transparent = YETTY_IS_OK(transparent_res) ? transparent_res.value : 0;
            if (YETTY_IS_ERR(transparent_res)) {
                yetty_ycore_error_destroy(transparent_res.error);
            }
            if (!transparent) {
                minted_depth++;
            }
            struct yetty_yclass_object_ptr_result parent_res = yetty_ygui2_widget_parent_obj(walk);
            if (YETTY_IS_ERR(parent_res)) {
                yetty_ycore_error_destroy(parent_res.error);
                break;
            }
            walk = parent_res.value;
        }
        /* Containment depth budget: yvterm's ingest stack holds 8 nested
         * groups and every minted widget adds a skin SUBGROUP below its
         * containment group — so the deepest accepted containment level
         * is 7 (7 containment + 1 skin = the full stack). The CMD_PATH
         * budget is separate (8 ids) and still covers every address this
         * tree can produce. */
        if (minted_depth >= 7u) {
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "ygui2 widget_instantiate: minted depth exceeds wire path budget");
        }
    }
    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(cls);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_res, "ygui2 widget_instantiate: alloc");
    struct yetty_yclass_object *obj = object_res.value;
    struct yetty_ycore_void_result init_res =
        yetty_ygui2_widget_init_base(obj, framework_obj, parent, framework->next_node_id++);
    if (YETTY_IS_ERR(init_res)) {
        free(obj);
        return YETTY_ERR(yetty_yclass_object_ptr, "ygui2 widget_instantiate: init", init_res);
    }
    if (parent) {
        struct yetty_ycore_void_result link_res = yetty_ygui2_widget_link_child(parent, obj);
        if (YETTY_IS_ERR(link_res)) {
            free(obj);
            return YETTY_ERR(yetty_yclass_object_ptr, "ygui2 widget_instantiate: link", link_res);
        }
    }
    return YETTY_OK(yetty_yclass_object_ptr, obj);
}

/* Create the ROOT widget of the tree (wire GROUP id 1) and the overlay
 * root beside it (wire GROUP id 2, always present so popups can mount
 * later without a fresh top-level insertion). */
YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui2_framework_root_create(
    struct yetty_yclass_object *obj, const struct yetty_yclass *cls)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, data_res, "ygui2 root_create: data");
    struct yetty_ygui2_framework *framework = data_res.value;
    if (framework->root) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ygui2 root_create: root already set");
    }
    uint32_t saved_next_id = framework->next_node_id;
    struct yetty_yclass_object_ptr_result root_res = widget_instantiate(framework, obj, cls, NULL);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, root_res, "ygui2 root_create: instantiate");
    struct yetty_ycore_void_result id_res = yetty_ygui2_widget_set_node_id(root_res.value, 1u);
    if (YETTY_IS_ERR(id_res)) {
        widget_destroy_quiet(root_res.value);
        return YETTY_ERR(yetty_yclass_object_ptr, "ygui2 root_create: id", id_res);
    }
    struct yetty_yclass_ptr_result widget_class_res = yetty_ygui2_widget_class_get();
    if (YETTY_IS_ERR(widget_class_res)) {
        widget_destroy_quiet(root_res.value);
        return YETTY_ERR(yetty_yclass_object_ptr, "ygui2 root_create: widget class",
                         widget_class_res);
    }
    struct yetty_yclass_object_ptr_result overlay_res =
        widget_instantiate(framework, obj, widget_class_res.value, NULL);
    if (YETTY_IS_ERR(overlay_res)) {
        widget_destroy_quiet(root_res.value);
        return YETTY_ERR(yetty_yclass_object_ptr, "ygui2 root_create: overlay", overlay_res);
    }
    struct yetty_ycore_void_result overlay_id_res =
        yetty_ygui2_widget_set_node_id(overlay_res.value, 2u);
    if (YETTY_IS_ERR(overlay_id_res)) {
        widget_destroy_quiet(overlay_res.value);
        widget_destroy_quiet(root_res.value);
        return YETTY_ERR(yetty_yclass_object_ptr, "ygui2 root_create: overlay id", overlay_id_res);
    }
    framework->next_node_id = saved_next_id;
    framework->root = root_res.value;
    framework->overlay_root = overlay_res.value;
    return root_res;
}

/* Cross-class within-module: scrollarea's children mount under an OWNED
 * minted content group (declared here; class in widgets/scrollarea.c). */
struct yetty_yclass_ptr_result yetty_ygui2_scrollarea_class_get(void);

/* A scrollarea owns ONE minted content child; every user child mounts
 * beneath it. Scrolling then moves the content group — ONE offset update
 * on the wire regardless of how many minted descendants the content has
 * (the strategy's viewport contract). Lazily created on the first add. */
static struct yetty_yclass_object_ptr_result widget_add_target(
    struct yetty_ygui2_framework *framework, struct yetty_yclass_object *framework_obj,
    struct yetty_yclass_object *parent)
{
    struct yetty_yclass_ptr_result scroll_class_res = yetty_ygui2_scrollarea_class_get();
    if (YETTY_IS_ERR(scroll_class_res)) {
        yetty_ycore_error_destroy(scroll_class_res.error);
        return YETTY_OK(yetty_yclass_object_ptr, parent);
    }
    if (parent->klass != scroll_class_res.value) {
        return YETTY_OK(yetty_yclass_object_ptr, parent);
    }
    struct yetty_yclass_object *content = tree_first_child(parent);
    if (content) {
        return YETTY_OK(yetty_yclass_object_ptr, content);
    }
    struct yetty_yclass_ptr_result widget_class_res = yetty_ygui2_widget_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, widget_class_res,
                        "ygui2 widget_add: content class");
    struct yetty_yclass_object_ptr_result content_res =
        widget_instantiate(framework, framework_obj, widget_class_res.value, parent);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, content_res, "ygui2 widget_add: content");
    /* Only the SIZING fields live on the content group (fill the
     * viewport); its FLOW spec (direction/gap/pads) is read from the
     * scrollarea on every layout pass, so later layout_set() calls on
     * the viewport keep working — no stale creation-time snapshot. */
    struct yetty_ygui2_layout content_spec = {.grow = 1.0f};
    struct yetty_ycore_void_result spec_res =
        yetty_ygui2_widget_layout_set(content_res.value, &content_spec);
    if (YETTY_IS_ERR(spec_res)) {
        /* Transactional: a half-configured content group must not
         * survive a failed add. */
        struct yetty_ycore_void_result remove_res = yetty_ygui2_widget_remove(content_res.value);
        if (YETTY_IS_ERR(remove_res)) {
            yetty_ycore_error_destroy(remove_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ygui2 widget_add: content layout", spec_res);
    }
    return content_res;
}

/* Add a child widget under `parent` (any widget of this framework). */
YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui2_widget_add(struct yetty_yclass_object *parent,
                                                             const struct yetty_yclass *cls)
{
    struct yetty_yclass_object_ptr_result framework_obj_res =
        yetty_ygui2_widget_framework_obj(parent);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, framework_obj_res, "ygui2 widget_add: framework");
    struct yetty_ygui2_framework_ptr_result framework_res =
        yetty_ygui2_framework_from(framework_obj_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, framework_res, "ygui2 widget_add: data");
    int parent_had_content = tree_first_child(parent) != NULL;
    struct yetty_yclass_object_ptr_result target_res =
        widget_add_target(framework_res.value, framework_obj_res.value, parent);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, target_res, "ygui2 widget_add: target");
    struct yetty_yclass_object_ptr_result child_res =
        widget_instantiate(framework_res.value, framework_obj_res.value, cls, target_res.value);
    if (YETTY_IS_ERR(child_res) && target_res.value != parent && !parent_had_content) {
        /* The content group was created FOR this add — roll it back so a
         * failed add leaves the scrollarea exactly as it was. */
        struct yetty_ycore_void_result remove_res = yetty_ygui2_widget_remove(target_res.value);
        if (YETTY_IS_ERR(remove_res)) {
            yetty_ycore_error_destroy(remove_res.error);
        }
    }
    return child_res;
}

/*===========================================================================
 * Framework configuration.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_set_sink(struct yetty_yclass_object *obj,
                                                              yetty_ygui2_sink_fn sink,
                                                              void *userdata)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_sink: data");
    data_res.value->sink = sink;
    data_res.value->sink_userdata = userdata;
    return YETTY_OK_VOID();
}

/* Hard upper bound on either viewport dimension — beyond any real pane
 * and safely inside the float/row arithmetic the layout and reservation
 * paths perform. */
enum { YGUI2_VIEWPORT_MAX_PX = 32768 };

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_set_viewport(struct yetty_yclass_object *obj,
                                                                  float width, float height)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_viewport: data");
    struct yetty_ygui2_framework *framework = data_res.value;
    if (!(isfinite(width) && width > 0.0f && isfinite(height) && height > 0.0f)) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 set_viewport: size must be positive finite");
    }
    if (width > (float)YGUI2_VIEWPORT_MAX_PX || height > (float)YGUI2_VIEWPORT_MAX_PX) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 set_viewport: size beyond the supported range");
    }
    if (framework->inserted && framework->reserved_h > 0.0f && height > framework->reserved_h) {
        /* FULLSCREEN: unreachable by construction (the insertion
         * reserved the full supported range, so any height that passed
         * the range check is in-budget) — kept as a hard guard because
         * destroying retained runtimes behind a "successful" resize is
         * the one thing this path must never do. INLINE: the documented
         * rejection — the insertion reserved only its declared content
         * height; the app grows by clear() + emit (an explicit fresh
         * insertion), never by a silent clip or a destructive rebuild.
         * The committed viewport stays untouched either way. */
        return YETTY_ERR(yetty_ycore_void,
                         "ygui2 set_viewport: height exceeds the live insertion's reservation");
    }
    int changed = framework->viewport_w != width || framework->viewport_h != height;
    framework->viewport_w = width;
    framework->viewport_h = height;
    framework->viewport_valid = 1;
    if (changed && framework->inserted) {
        /* Resize NEVER rebuilds — the reservation covers every accepted
         * height, so the live insertion (and every complex runtime
         * inside it) survives. Relayout only; set_rect turns actual size
         * changes into targeted reopens and unchanged widgets ship
         * nothing. */
        framework->viewport_dirty = 1;
    }
    return YETTY_OK_VOID();
}

/* Reservation mode (strategy.md §5): fullscreen (default) reserves the
 * full supported viewport range so every accepted resize is in-budget
 * relayout; inline (`fullscreen = 0`) reserves the declared viewport
 * height only — the insertion lives in the scrollback flow, and growth
 * past that reservation is an explicit set_viewport rejection (the app
 * re-inserts via clear() + emit). Must be chosen BEFORE the first
 * insertion: the reservation is immutable for the insertion's life. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_set_fullscreen(struct yetty_yclass_object *obj,
                                                                    int fullscreen)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_fullscreen: data");
    struct yetty_ygui2_framework *framework = data_res.value;
    if (framework->inserted) {
        return YETTY_ERR(yetty_ycore_void,
                         "ygui2 set_fullscreen: the live insertion's reservation is immutable");
    }
    framework->fullscreen = fullscreen ? 1 : 0;
    return YETTY_OK_VOID();
}

/* The committed HiDPI input divisor. The pane-resize envelope path
 * commits it only after a successful viewport transition, so this always
 * matches the projection mouse coordinates are divided against. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_content_scale(struct yetty_yclass_object *obj,
                                                                   float *out_scale)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 content_scale: data");
    if (out_scale) {
        *out_scale = data_res.value->content_scale > 0.0f ? data_res.value->content_scale : 1.0f;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_set_key_cb(struct yetty_yclass_object *obj,
                                                                yetty_ygui2_key_cb callback,
                                                                void *userdata)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_key_cb: data");
    data_res.value->key_cb = callback;
    data_res.value->key_cb_userdata = userdata;
    return YETTY_OK_VOID();
}

/* Convenience containers: transparent flex boxes (strategy.md §3 — layout
 * only, no wire group). */
static struct yetty_yclass_object_ptr_result flex_box_add(struct yetty_yclass_object *parent,
                                                          uint32_t direction)
{
    struct yetty_yclass_ptr_result class_res = yetty_ygui2_widget_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_res, "ygui2 flex_box_add: class");
    struct yetty_yclass_object_ptr_result box_res = yetty_ygui2_widget_add(parent, class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, box_res, "ygui2 flex_box_add: add");
    struct yetty_ycore_void_result transparent_res =
        yetty_ygui2_widget_set_transparent(box_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, transparent_res, "ygui2 flex_box_add: flag");
    struct yetty_ygui2_layout spec = {0};
    struct yetty_ycore_void_result copy_res = yetty_ygui2_widget_layout_copy(box_res.value, &spec);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, copy_res, "ygui2 flex_box_add: layout copy");
    spec.direction = direction;
    struct yetty_ycore_void_result set_res = yetty_ygui2_widget_layout_set(box_res.value, &spec);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, set_res, "ygui2 flex_box_add: layout set");
    return box_res;
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui2_row_add(struct yetty_yclass_object *parent)
{
    return flex_box_add(parent, YETTY_YGUI2_DIRECTION_ROW);
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui2_column_add(struct yetty_yclass_object *parent)
{
    return flex_box_add(parent, YETTY_YGUI2_DIRECTION_COLUMN);
}

/* Raw bytes to the terminal stream — sink in headless/in-process mode
 * (defined infallible: a capture callback has no delivery to fail),
 * write_fd otherwise (COMPLETE write, EINTR-safe; a short or failed
 * write is an ERROR — control envelopes built on this must never report
 * success for bytes that did not leave the process). */
static struct yetty_ycore_void_result framework_write_bytes(struct yetty_ygui2_framework *framework,
                                                            const void *bytes, size_t byte_count)
{
    if (framework->sink) {
        framework->sink(bytes, byte_count, framework->sink_userdata);
        return YETTY_OK_VOID();
    }
    if (!framework->write_fd_valid) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 write_bytes: no sink and no write_fd");
    }
    struct yetty_ycore_void_result write_res =
        yetty_yplatform_io_write_all(framework->write_fd, bytes, byte_count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_res,
                        "ygui2 write_bytes: short/failed terminal write");
    return YETTY_OK_VOID();
}

/* Ship one client-input struct as a yface DCS envelope on `code` — the
 * framing every CS client-input channel uses. Complete write or error. */
static struct yetty_ycore_void_result framework_send_client_struct(
    struct yetty_ygui2_framework *framework, uint32_t code, const void *payload,
    size_t payload_size)
{
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = 0,
        .compression_algo = 0,
        .raw_size = payload_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result emit_res = yetty_yface_emit(
        code, /*compressed=*/0, &meta, sizeof(meta), payload, payload_size, &envelope);
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_void, "ygui2 send_client_struct: emit", emit_res);
    }
    struct yetty_ycore_void_result write_res = YETTY_OK_VOID();
    if (envelope.size > 0) {
        write_res = framework_write_bytes(framework, envelope.data, envelope.size);
    }
    yetty_ycore_buffer_destroy(&envelope);
    return write_res;
}

/* Subscription envelope declaring the FULL desired state (the terminal
 * latches flags absolutely, so flags=0 unsubscribes everything). */
static struct yetty_ycore_void_result framework_send_input_sub(
    struct yetty_ygui2_framework *framework, uint32_t flags)
{
    struct yetty_client_input_sub sub = {
        .magic = YETTY_CLIENT_INPUT_SUB_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .flags = flags,
    };
    return framework_send_client_struct(framework, YETTY_OSC_CS_CLIENT_INPUT_SUB, &sub,
                                        sizeof(sub));
}

/* Attach the framework to a PTY: envelopes ship to write_fd; the app keeps
 * owning its read loop and forwards bytes through feed_input. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_attach(struct yetty_yclass_object *obj,
                                                            int read_fd, int write_fd)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 attach: data");
    (void)read_fd;
    struct yetty_ygui2_framework *framework = data_res.value;
    framework->write_fd = write_fd;
    framework->write_fd_valid = 1;
    /* Pane-wide input subscription (strategy.md §7): mouse click/move/wheel
     * + geometry envelopes. Ships as a yface DCS envelope — the SAME framing
     * every client-input channel uses (the terminal registers 610010 as a
     * DCS channel, not an OSC). A failed subscription write is a failed
     * attach: roll the fd state back so a retry is possible. */
    struct yetty_ycore_void_result sub_res = framework_send_input_sub(
        framework, YETTY_CLIENT_INPUT_SUB_MOUSE_CLICK | YETTY_CLIENT_INPUT_SUB_MOUSE_MOVE |
                       YETTY_CLIENT_INPUT_SUB_MOUSE_WHEEL | YETTY_CLIENT_INPUT_SUB_RESIZE);
    if (YETTY_IS_ERR(sub_res)) {
        framework->write_fd_valid = 0;
        framework->write_fd = -1;
        return YETTY_ERR(yetty_ycore_void, "ygui2 attach: subscription write", sub_res);
    }
    return YETTY_OK_VOID();
}

/* Arm the terminal's exit-window input barrier: the host holds keystrokes
 * host-side until this client's PTY closes, and answers with a HOLD-ACK
 * envelope (watch hold_ack_seen while pumping feed_input). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_send_hold(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 send_hold: data");
    data_res.value->hold_ack_seen = 0; /* a NEW barrier round wants a fresh ack */
    struct yetty_ycore_void_result send_res =
        framework_send_client_struct(data_res.value, YETTY_OSC_CS_CLIENT_INPUT_HOLD, NULL, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, send_res, "ygui2 send_hold: write");
    return YETTY_OK_VOID();
}

/* Nonzero once the host's HOLD-ACK envelope has been parsed — its arrival
 * proves the input barrier is armed and teardown may proceed. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui2_framework_hold_ack_seen(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 hold_ack_seen: data");
    return YETTY_OK(yetty_ycore_int, data_res.value->hold_ack_seen);
}

/* Detach: unsubscribe from pane input (flags=0 clears every bit in the
 * terminal). MUST run on every app exit path — a subscription that
 * outlives its client leaves the pane spraying mouse envelopes at the
 * shell, and no amount of `stty sane` can cure that from outside. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_detach(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 detach: data");
    struct yetty_ygui2_framework *framework = data_res.value;
    if (framework->write_fd_valid) {
        struct yetty_ycore_void_result unsub_res = framework_send_input_sub(framework, 0u);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, unsub_res,
                            "ygui2 detach: unsubscribe write (fd state kept for retry)");
        framework->write_fd_valid = 0;
        framework->write_fd = -1;
    }
    return YETTY_OK_VOID();
}

/* Reset the per-widget emitted-state caches for `obj`'s subtree. Runs
 * whenever the wire-side group instances are (about to be) destroyed —
 * clear, rebuild, and ancestor reopens — so the next emission re-sends
 * every non-default projection state instead of trusting caches that
 * described the previous instances. */
static void invalidate_emitted_subtree(struct yetty_yclass_object *obj)
{
    struct yetty_ycore_void_result reset_res = yetty_ygui2_widget_reset_emitted(obj);
    if (YETTY_IS_ERR(reset_res)) {
        yetty_ycore_error_destroy(reset_res.error);
    }
    for (struct yetty_yclass_object *child = tree_first_child(obj); child;
         child = tree_next_sibling(child)) {
        invalidate_emitted_subtree(child);
    }
}

static void framework_invalidate_projection(struct yetty_ygui2_framework *framework)
{
    if (framework->root) {
        invalidate_emitted_subtree(framework->root);
    }
    if (framework->overlay_root) {
        invalidate_emitted_subtree(framework->overlay_root);
    }
    framework->inserted = 0;
    /* Homing recovers the FULLSCREEN cursor (it sits at the bottom of the
     * old reservation). An inline insertion lives in the transcript flow:
     * its replacement must land at the CURRENT cursor as a new transcript
     * block — homing would stamp it over visible row 1 (strategy.md §5). */
    framework->home_before_insert = framework->fullscreen ? 1 : 0;
}

/* Clear the surface: delete both top-level groups; the next emit homes the
 * cursor and re-inserts from scratch. Local state is only committed after
 * the delete envelope actually shipped. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_clear(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 clear: data");
    struct yetty_ygui2_framework *framework = data_res.value;
    if (!framework->inserted || !framework->list) {
        framework->inserted = 0;
        return YETTY_OK_VOID();
    }
    yetty_ydraw_drawable_list_clear(framework->list);
    struct yetty_ycore_void_result delete_res =
        yetty_ydraw_drawable_list_add_cmd_delete(framework->list, 1u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, delete_res, "ygui2 clear: delete root");
    struct yetty_ycore_void_result overlay_delete_res =
        yetty_ydraw_drawable_list_add_cmd_delete(framework->list, 2u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, overlay_delete_res, "ygui2 clear: delete overlay");
    struct yetty_ycore_void_result ship_res = framework_ship(framework);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ship_res, "ygui2 clear: ship");
    framework_invalidate_projection(framework);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Theme (shared palette; widgets read it at paint time).
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_theme_copy(struct yetty_yclass_object *obj,
                                                                struct yetty_ygui2_theme *out_theme)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 theme_copy: data");
    if (!out_theme) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 theme_copy: NULL out");
    }
    *out_theme = data_res.value->theme;
    return YETTY_OK_VOID();
}

/* Restyle: every minted widget repaints (one reopen each) at next emit. */
static void restyle_subtree(struct yetty_yclass_object *obj)
{
    if (!tree_flag(yetty_ygui2_widget_is_transparent(obj))) {
        struct yetty_ycore_void_result dirty_res = yetty_ygui2_widget_mark_skin_dirty(obj);
        if (YETTY_IS_ERR(dirty_res)) {
            yetty_ycore_error_destroy(dirty_res.error);
        }
    }
    for (struct yetty_yclass_object *child = tree_first_child(obj); child;
         child = tree_next_sibling(child)) {
        restyle_subtree(child);
    }
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_set_theme(
    struct yetty_yclass_object *obj, const struct yetty_ygui2_theme *theme)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_theme: data");
    if (!theme) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 set_theme: NULL theme");
    }
    struct yetty_ygui2_framework *framework = data_res.value;
    if (memcmp(&framework->theme, theme, sizeof(framework->theme)) == 0) {
        return YETTY_OK_VOID();
    }
    framework->theme = *theme;
    if (framework->root) {
        restyle_subtree(framework->root);
    }
    if (framework->overlay_root) {
        restyle_subtree(framework->overlay_root);
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Overlay (strategy.md §10 modifier): absolutely-placed widgets in the
 * second top-level group, above the root tree.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui2_framework_overlay_add(
    struct yetty_yclass_object *obj, const struct yetty_yclass *cls)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, data_res, "ygui2 overlay_add: data");
    struct yetty_ygui2_framework *framework = data_res.value;
    if (!framework->overlay_root) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ygui2 overlay_add: no root yet");
    }
    return widget_instantiate(framework, obj, cls, framework->overlay_root);
}

/* Deepest hit among VISIBLE overlay children. The overlay root spans the
 * viewport but is never itself a target. NULL = no overlay widget hit. */
static struct yetty_yclass_object *overlay_hit(struct yetty_ygui2_framework *framework, float x,
                                               float y)
{
    if (!framework->overlay_root) {
        return NULL;
    }
    struct yetty_yclass_object *best = NULL;
    for (struct yetty_yclass_object *child = tree_first_child(framework->overlay_root); child;
         child = tree_next_sibling(child)) {
        struct yetty_yclass_object *candidate = hit_test(child, x, y);
        if (candidate) {
            best = candidate; /* later siblings override — paint order wins */
        }
    }
    return best;
}

static int overlay_any_visible(struct yetty_ygui2_framework *framework)
{
    if (!framework->overlay_root) {
        return 0;
    }
    for (struct yetty_yclass_object *child = tree_first_child(framework->overlay_root); child;
         child = tree_next_sibling(child)) {
        if (tree_flag(yetty_ygui2_widget_is_visible(child))) {
            return 1;
        }
    }
    return 0;
}

/* Hide visible overlay children — all of them (Esc), or only the
 * dismiss-on-outside ones (a press that missed every overlay widget).
 * Returns the number hidden (each hide bubbles a structure reopen). */
static uint32_t overlay_dismiss(struct yetty_ygui2_framework *framework, int only_dismissable)
{
    if (!framework->overlay_root) {
        return 0;
    }
    uint32_t hidden = 0;
    for (struct yetty_yclass_object *child = tree_first_child(framework->overlay_root); child;
         child = tree_next_sibling(child)) {
        if (!tree_flag(yetty_ygui2_widget_is_visible(child))) {
            continue;
        }
        if (only_dismissable && !tree_flag(yetty_ygui2_widget_dismiss_on_outside(child))) {
            continue;
        }
        struct yetty_ycore_void_result hide_res = yetty_ygui2_widget_set_visible(child, 0);
        if (YETTY_IS_ERR(hide_res)) {
            yetty_ycore_error_destroy(hide_res.error);
        } else {
            hidden++;
        }
    }
    return hidden;
}

/*===========================================================================
 * Focus — click-to-focus on the nearest focusable ancestor; Tab / Shift-Tab
 * walk tree order over visible focusable widgets (root tree, then overlay).
 *=========================================================================*/

enum { YGUI2_FOCUS_MAX = 128 };

static void focus_collect(struct yetty_yclass_object *obj, struct yetty_yclass_object **out_order,
                          uint32_t *count)
{
    if (!tree_flag(yetty_ygui2_widget_is_visible(obj))) {
        return;
    }
    if (tree_flag(yetty_ygui2_widget_is_focusable(obj)) && *count < YGUI2_FOCUS_MAX) {
        out_order[(*count)++] = obj;
    }
    for (struct yetty_yclass_object *child = tree_first_child(obj); child;
         child = tree_next_sibling(child)) {
        focus_collect(child, out_order, count);
    }
}

/* Repaint hook for a focus transition endpoint (focus rings are skin). */
static void focus_mark(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return;
    }
    struct yetty_ycore_void_result dirty_res = yetty_ygui2_widget_mark_skin_dirty(obj);
    if (YETTY_IS_ERR(dirty_res)) {
        yetty_ycore_error_destroy(dirty_res.error);
    }
}

static void focus_move(struct yetty_ygui2_framework *framework,
                       struct yetty_yclass_object *next_focused)
{
    if (framework->focused == next_focused) {
        return;
    }
    focus_mark(framework->focused);
    framework->focused = next_focused;
    focus_mark(framework->focused);
}

static void focus_advance(struct yetty_ygui2_framework *framework, int direction)
{
    struct yetty_yclass_object *order[YGUI2_FOCUS_MAX];
    uint32_t count = 0;
    if (framework->root) {
        focus_collect(framework->root, order, &count);
    }
    if (framework->overlay_root) {
        focus_collect(framework->overlay_root, order, &count);
    }
    if (!count) {
        return;
    }
    int current = -1;
    for (uint32_t index = 0; index < count; ++index) {
        if (order[index] == framework->focused) {
            current = (int)index;
            break;
        }
    }
    int next = current < 0 ? (direction > 0 ? 0 : (int)count - 1)
                           : (current + direction + (int)count) % (int)count;
    focus_move(framework, order[next]);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui2_framework_widget_is_focused(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *widget_obj)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 widget_is_focused: data");
    return YETTY_OK(yetty_ycore_int, data_res.value->focused == widget_obj ? 1 : 0);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_focus_set(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *widget_obj)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 focus_set: data");
    focus_move(data_res.value, widget_obj);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Input — a STREAMING parser. PTY reads arrive at arbitrary boundaries;
 * incomplete OSC/DCS/CSI sequences are retained in input_pending and
 * completed by later chunks. Nothing is ever dropped mid-sequence.
 *=========================================================================*/

/* Widget participates in dispatch: attached under a live root and visible
 * along the whole ancestor chain. */
static int target_alive(struct yetty_ygui2_framework *framework, struct yetty_yclass_object *obj)
{
    struct yetty_yclass_object *walk = obj;
    while (walk) {
        if (!tree_flag(yetty_ygui2_widget_is_visible(walk))) {
            return 0;
        }
        if (walk == framework->root || walk == framework->overlay_root) {
            return 1;
        }
        struct yetty_yclass_object_ptr_result parent_res = yetty_ygui2_widget_parent_obj(walk);
        if (YETTY_IS_ERR(parent_res)) {
            yetty_ycore_error_destroy(parent_res.error);
            return 0;
        }
        walk = parent_res.value;
    }
    return 0;
}

/* Focus/capture hygiene for a subtree leaving the live tree (removal). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_forget_subtree(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *widget_obj)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 forget_subtree: data");
    struct yetty_ygui2_framework *framework = data_res.value;
    struct yetty_yclass_object *walk = framework->focused;
    while (walk) {
        if (walk == widget_obj) {
            framework->focused = NULL;
            break;
        }
        struct yetty_yclass_object_ptr_result parent_res = yetty_ygui2_widget_parent_obj(walk);
        walk = YETTY_IS_OK(parent_res) ? parent_res.value : NULL;
        if (YETTY_IS_ERR(parent_res)) {
            yetty_ycore_error_destroy(parent_res.error);
        }
    }
    walk = framework->mouse_capture;
    while (walk) {
        if (walk == widget_obj) {
            framework->mouse_capture = NULL;
            break;
        }
        struct yetty_yclass_object_ptr_result parent_res = yetty_ygui2_widget_parent_obj(walk);
        walk = YETTY_IS_OK(parent_res) ? parent_res.value : NULL;
        if (YETTY_IS_ERR(parent_res)) {
            yetty_ycore_error_destroy(parent_res.error);
        }
    }
    return YETTY_OK_VOID();
}

/* Cross-class within-module: rich widgets mint additional wire ids for
 * their own addressable child nodes (the plot widget's stream target).
 * Same monotonic sequence as widget/skin group ids — globally unique. */
struct yetty_ycore_uint32_result yetty_ygui2_framework_mint_node_id(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, data_res, "ygui2 mint_node_id: data");
    return YETTY_OK(yetty_ycore_uint32, data_res.value->next_node_id++);
}

/* Adopt an overlay widget for deferred destruction (see the `orphans`
 * field). The caller must already have made the widget INERT — callbacks
 * severed, hidden — because it stays in the overlay tree until the next
 * safe boundary. Cross-class within-module: the owner's cleanup calls
 * this while its own destruction is in flight. */
struct yetty_ycore_void_result yetty_ygui2_framework_orphan_overlay(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *widget_obj)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 orphan_overlay: data");
    struct yetty_ygui2_framework *framework = data_res.value;
    if (!widget_obj) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 orphan_overlay: NULL widget");
    }
    if (framework->orphan_count == framework->orphan_capacity) {
        uint32_t grown_capacity = framework->orphan_capacity ? framework->orphan_capacity * 2u : 4u;
        struct yetty_yclass_object **grown =
            realloc(framework->orphans, grown_capacity * sizeof(*grown));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "ygui2 orphan_overlay: alloc");
        }
        framework->orphans = grown;
        framework->orphan_capacity = grown_capacity;
    }
    framework->orphans[framework->orphan_count++] = widget_obj;
    return YETTY_OK_VOID();
}

/* Reclaim orphans at a safe boundary — never inside the dispatch that
 * orphaned them. Pop-from-end so a cleanup running during a removal may
 * append without invalidating the walk. */
static void framework_reap_orphans(struct yetty_ygui2_framework *framework)
{
    while (framework->orphan_count) {
        struct yetty_yclass_object *orphan = framework->orphans[--framework->orphan_count];
        struct yetty_ycore_void_result remove_res = yetty_ygui2_widget_remove(orphan);
        if (YETTY_IS_ERR(remove_res)) {
            yetty_ycore_error_destroy(remove_res.error);
        }
    }
}

/* One key to the focused widget (validated alive) then the app callback. */
static void deliver_key(struct yetty_ygui2_framework *framework, uint32_t key)
{
    if (framework->focused && !target_alive(framework, framework->focused)) {
        focus_move(framework, NULL); /* hidden/removed widgets lose focus */
    }
    int consumed = 0;
    if (framework->focused) {
        struct yetty_ycore_int_result key_res =
            yetty_ygui2_widget_on_key(framework->focused, key, 0);
        consumed = YETTY_IS_OK(key_res) ? key_res.value : 0;
        if (YETTY_IS_ERR(key_res)) {
            yetty_ycore_error_destroy(key_res.error);
        }
    }
    if (!consumed && framework->key_cb) {
        framework->key_cb(key, 0, framework->key_cb_userdata);
    }
}

/* Esc: close any open overlay, else a plain key. */
static void deliver_escape(struct yetty_ygui2_framework *framework)
{
    if (overlay_any_visible(framework)) {
        overlay_dismiss(framework, /*only_dismissable=*/0);
        return;
    }
    deliver_key(framework, YETTY_YGUI2_KEY_ESCAPE);
}

static size_t input_b64_decode(const char *body, size_t start, size_t length, uint8_t *out,
                               size_t out_capacity)
{
    size_t decoded_length = 0;
    uint32_t accumulator = 0;
    int bits = 0;
    for (size_t index = start; index < length; ++index) {
        char c = body[index];
        int value;
        if (c >= 'A' && c <= 'Z') {
            value = c - 'A';
        } else if (c >= 'a' && c <= 'z') {
            value = c - 'a' + 26;
        } else if (c >= '0' && c <= '9') {
            value = c - '0' + 52;
        } else if (c == '+') {
            value = 62;
        } else if (c == '/') {
            value = 63;
        } else {
            break;
        }
        accumulator = (accumulator << 6) | (uint32_t)value;
        bits += 6;
        if (bits >= 8 && decoded_length < out_capacity) {
            bits -= 8;
            out[decoded_length++] = (uint8_t)(accumulator >> bits);
        }
    }
    return decoded_length;
}

/* Apply one decoded SC client-input struct. Unconsumed mouse events are
 * REINJECTED (the terminal contract: the pane-wide subscriber must bounce
 * what it does not consume, so selection/scrollback defaults still run). */
static void framework_apply_client_envelope(struct yetty_ygui2_framework *framework,
                                            struct yetty_yclass_object *obj, uint32_t code,
                                            const uint8_t *decoded, size_t decoded_length)
{
    float scale = framework->content_scale > 0.0f ? framework->content_scale : 1.0f;
    if (code == YETTY_OSC_SC_CLIENT_INPUT_MOUSE &&
        decoded_length >= sizeof(struct yetty_client_input_mouse)) {
        struct yetty_client_input_mouse mouse;
        memcpy(&mouse, decoded, sizeof(mouse));
        if (mouse.magic != YETTY_CLIENT_INPUT_MOUSE_MAGIC) {
            return;
        }
        int consumed = 0;
        int reinjectable = 0;
        if (mouse.kind == YETTY_YMGUI_INPUT_MOUSE_BUTTON) {
            framework_mouse_button(framework, obj, mouse.x / scale, mouse.y / scale, mouse.button,
                                   mouse.pressed, mouse.mods, &consumed);
            reinjectable = 1;
        } else if (mouse.kind == YETTY_YMGUI_INPUT_MOUSE_POS) {
            framework_mouse_motion(framework, obj, mouse.x / scale, mouse.y / scale,
                                   mouse.buttons_held, &consumed);
            /* Hover-only motion has no terminal default — only drags
             * (selection) are worth bouncing. */
            reinjectable = mouse.buttons_held != 0;
        } else if (mouse.kind == YETTY_YMGUI_INPUT_MOUSE_WHEEL) {
            framework_mouse_scroll(framework, obj, mouse.x / scale, mouse.y / scale, mouse.wheel_dy,
                                   &consumed);
            reinjectable = 1;
        }
        if (!consumed && reinjectable) {
            /* Best-effort by design: a lost reinject degrades a terminal
             * default (selection/scrollback), never the app. */
            struct yetty_ycore_void_result reinject_res = framework_send_client_struct(
                framework, YETTY_OSC_CS_CLIENT_INPUT_REINJECT, &mouse, sizeof(mouse));
            if (YETTY_IS_ERR(reinject_res)) {
                yetty_ycore_error_destroy(reinject_res.error);
            }
        }
    } else if (code == YETTY_OSC_SC_CLIENT_INPUT_RESIZE &&
               decoded_length >= sizeof(struct yetty_client_input_resize)) {
        struct yetty_client_input_resize resize;
        memcpy(&resize, decoded, sizeof(resize));
        /* TRANSACTIONAL: validate the whole envelope — dimensions AND
         * scale — then attempt the viewport transition with the CANDIDATE
         * scale, and commit content_scale only after it succeeded. A
         * rejected transition must never leave the input divisor changed
         * while layout kept the old projection (mouse coordinates would
         * silently diverge from the drawn UI). */
        int resize_valid = resize.magic == YETTY_CLIENT_INPUT_RESIZE_MAGIC &&
                           isfinite(resize.width) && resize.width > 0.0f &&
                           isfinite(resize.height) && resize.height > 0.0f;
        float candidate_scale = framework->content_scale > 0.0f ? framework->content_scale : 1.0f;
        if (resize_valid && resize.content_scale != 0.0f) {
            /* A present scale must be positive finite; a garbage one
             * rejects the whole envelope rather than poisoning input
             * mapping. NO upper ceiling: the host legally commits
             * products a fixed client-side cap would exclude (display
             * density 2.625 x structural zoom 8 ≈ 21), and rejecting
             * them silently froze the old viewport and input divisor
             * while the terminal rendered the new projection. The
             * transactional viewport validation below is the real
             * guard — an unusable logical viewport rejects there with
             * the committed state untouched. */
            if (isfinite(resize.content_scale) && resize.content_scale > 0.0f) {
                candidate_scale = resize.content_scale;
            } else {
                resize_valid = 0;
            }
        }
        if (resize_valid) {
            struct yetty_ycore_void_result viewport_res = yetty_ygui2_framework_set_viewport(
                obj, resize.width / candidate_scale, resize.height / candidate_scale);
            if (YETTY_IS_ERR(viewport_res)) {
                yetty_ycore_error_destroy(viewport_res.error);
            } else {
                framework->content_scale = candidate_scale;
            }
        }
    }
}

/* Try to consume ONE item at the head of `bytes`. Returns the byte count
 * consumed, or 0 when the head is an incomplete sequence (keep the tail
 * and wait for more input). */
static size_t input_parse_one(struct yetty_ygui2_framework *framework,
                              struct yetty_yclass_object *obj, const uint8_t *bytes,
                              size_t byte_count)
{
    uint8_t head = bytes[0];
    if (head == 0x1b) {
        if (byte_count < 2) {
            return 0; /* lone ESC so far — flush resolves it as a key */
        }
        uint8_t kind = bytes[1];
        if (kind == ']') { /* OSC: <code> ; … ; base64, BEL|ST terminated */
            size_t end = 2;
            int terminator_length = 0;
            while (end < byte_count) {
                if (bytes[end] == 0x07) {
                    terminator_length = 1;
                    break;
                }
                if (bytes[end] == 0x1b) {
                    if (end + 1 >= byte_count) {
                        return 0; /* ST split across reads */
                    }
                    if (bytes[end + 1] == '\\') {
                        terminator_length = 2;
                        break;
                    }
                }
                end++;
            }
            if (!terminator_length) {
                return 0;
            }
            const char *body = (const char *)bytes + 2;
            size_t body_length = end - 2;
            uint32_t code = 0;
            size_t cursor = 0;
            while (cursor < body_length && body[cursor] >= '0' && body[cursor] <= '9') {
                code = code * 10u + (uint32_t)(body[cursor] - '0');
                cursor++;
            }
            if (cursor < body_length && body[cursor] == ';') {
                size_t b64_start = cursor + 1;
                while (b64_start < body_length && body[b64_start] == ';') {
                    b64_start++;
                }
                uint8_t decoded[192];
                size_t decoded_length =
                    input_b64_decode(body, b64_start, body_length, decoded, sizeof(decoded));
                framework_apply_client_envelope(framework, obj, code, decoded, decoded_length);
            }
            return end + (size_t)terminator_length;
        }
        if (kind == 'P') { /* DCS: <code> y b64(meta) ; b64(payload), ST */
            size_t end = 2;
            while (end + 1 < byte_count && !(bytes[end] == 0x1b && bytes[end + 1] == '\\')) {
                end++;
            }
            if (end + 1 >= byte_count) {
                return 0; /* wait for the ST */
            }
            const char *body = (const char *)bytes + 2;
            size_t body_length = end - 2;
            uint32_t code = 0;
            size_t cursor = 0;
            while (cursor < body_length && body[cursor] >= '0' && body[cursor] <= '9') {
                code = code * 10u + (uint32_t)(body[cursor] - '0');
                cursor++;
            }
            if (code == YETTY_OSC_CS_CLIENT_INPUT_HOLD_ACK) {
                framework->hold_ack_seen = 1; /* teardown barrier armed */
            } else if (cursor < body_length && body[cursor] == 'y' &&
                       (code == YETTY_OSC_SC_CLIENT_INPUT_MOUSE ||
                        code == YETTY_OSC_SC_CLIENT_INPUT_RESIZE)) {
                cursor++;
                size_t semicolon = cursor;
                while (semicolon < body_length && body[semicolon] != ';') {
                    semicolon++;
                }
                uint8_t decoded[192];
                size_t decoded_length =
                    input_b64_decode(body, semicolon + 1, body_length, decoded, sizeof(decoded));
                framework_apply_client_envelope(framework, obj, code, decoded, decoded_length);
            }
            return end + 2;
        }
        if (kind == '[') { /* CSI: params/intermediates then one final byte */
            size_t index = 2;
            while (index < byte_count && bytes[index] >= 0x20 && bytes[index] <= 0x3f) {
                index++;
            }
            if (index >= byte_count) {
                return 0; /* split mid-sequence */
            }
            uint8_t final = bytes[index];
            if (final == 'Z') {
                focus_advance(framework, -1); /* Shift-Tab */
            } else if (final == 'A') {
                deliver_key(framework, YETTY_YGUI2_KEY_UP);
            } else if (final == 'B') {
                deliver_key(framework, YETTY_YGUI2_KEY_DOWN);
            } else if (final == 'C') {
                deliver_key(framework, YETTY_YGUI2_KEY_RIGHT);
            } else if (final == 'D') {
                deliver_key(framework, YETTY_YGUI2_KEY_LEFT);
            }
            /* Every other CSI is swallowed WHOLE — pieces of an unknown
             * escape sequence must never leak into a text widget. */
            return index + 1;
        }
        /* ESC followed by an unrelated byte: a real Escape keypress. */
        deliver_escape(framework);
        return 1;
    }
    if (head == YETTY_YGUI2_KEY_TAB) {
        focus_advance(framework, +1);
        return 1;
    }
    if (head == 0x0a || head == 0x0d) {
        /* Normalize: hosts without a fully raw termios deliver Enter as
         * LF (ICRNL); widgets bind the CR code. */
        deliver_key(framework, YETTY_YGUI2_KEY_ENTER);
        return 1;
    }
    deliver_key(framework, head);
    return 1;
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_feed_input(struct yetty_yclass_object *obj,
                                                                const uint8_t *bytes,
                                                                size_t byte_count)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 feed_input: data");
    struct yetty_ygui2_framework *framework = data_res.value;
    framework_reap_orphans(framework);
    if (!bytes && byte_count) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 feed_input: NULL bytes");
    }
    size_t offset = 0;
    while (offset < byte_count) {
        size_t space = sizeof(framework->input_pending) - framework->input_pending_length;
        size_t take = byte_count - offset;
        if (take > space) {
            take = space;
        }
        memcpy(framework->input_pending + framework->input_pending_length, bytes + offset, take);
        framework->input_pending_length += take;
        offset += take;
        size_t consumed_total = 0;
        while (consumed_total < framework->input_pending_length) {
            size_t consumed =
                input_parse_one(framework, obj, framework->input_pending + consumed_total,
                                framework->input_pending_length - consumed_total);
            if (consumed == 0) {
                break; /* incomplete head — retain and wait */
            }
            consumed_total += consumed;
        }
        if (consumed_total > 0) {
            memmove(framework->input_pending, framework->input_pending + consumed_total,
                    framework->input_pending_length - consumed_total);
            framework->input_pending_length -= consumed_total;
        }
        if (framework->input_pending_length == sizeof(framework->input_pending)) {
            /* A "sequence" longer than the whole buffer is garbage, not a
             * fragment. Drop it and resync rather than deadlock. */
            framework->input_pending_length = 0;
        }
    }
    return YETTY_OK_VOID();
}

/* Idle flush: a retained LONE Escape byte is a real Escape keypress, not
 * the start of a sequence — the host calls this on its select timeout so
 * a bare Esc does not wait for the next unrelated key. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_feed_input_flush(
    struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 feed_input_flush: data");
    struct yetty_ygui2_framework *framework = data_res.value;
    framework_reap_orphans(framework);
    if (framework->input_pending_length == 1 && framework->input_pending[0] == 0x1b) {
        framework->input_pending_length = 0;
        deliver_escape(framework);
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Tree helpers (accessor-based).
 *=========================================================================*/

static struct yetty_yclass_object *tree_first_child(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui2_widget_first_child(obj);
    if (YETTY_IS_ERR(child_res)) {
        yetty_ycore_error_destroy(child_res.error);
        return NULL;
    }
    return child_res.value;
}

static struct yetty_yclass_object *tree_next_sibling(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_object_ptr_result sibling_res = yetty_ygui2_widget_next_sibling(obj);
    if (YETTY_IS_ERR(sibling_res)) {
        yetty_ycore_error_destroy(sibling_res.error);
        return NULL;
    }
    return sibling_res.value;
}

static int tree_flag(struct yetty_ycore_int_result flag_res)
{
    if (YETTY_IS_ERR(flag_res)) {
        yetty_ycore_error_destroy(flag_res.error);
        return 0;
    }
    return flag_res.value;
}

static uint32_t tree_node_id(struct yetty_yclass_object *obj)
{
    struct yetty_ycore_uint32_result id_res = yetty_ygui2_widget_node_id(obj);
    if (YETTY_IS_ERR(id_res)) {
        yetty_ycore_error_destroy(id_res.error);
        return 0;
    }
    return id_res.value;
}

static void tree_rect(struct yetty_yclass_object *obj, float *out_x, float *out_y, float *out_w,
                      float *out_h)
{
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, out_x, out_y, out_w, out_h);
    if (YETTY_IS_ERR(rect_res)) {
        yetty_ycore_error_destroy(rect_res.error);
    }
}

static int widget_tree_dirty(struct yetty_yclass_object *obj)
{
    int skin = 0;
    int structure = 0;
    int position = 0;
    struct yetty_ycore_void_result flags_res =
        yetty_ygui2_widget_dirty_flags(obj, &skin, &structure, &position);
    if (YETTY_IS_ERR(flags_res)) {
        yetty_ycore_error_destroy(flags_res.error);
        return 0;
    }
    if (skin || structure || position) {
        return 1;
    }
    for (struct yetty_yclass_object *child = tree_first_child(obj); child;
         child = tree_next_sibling(child)) {
        if (widget_tree_dirty(child)) {
            return 1;
        }
    }
    return 0;
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui2_framework_is_dirty(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 is_dirty: data");
    struct yetty_ygui2_framework *framework = data_res.value;
    if (!framework->root || !framework->viewport_valid) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    if (!framework->inserted || framework->rebuild_pending || framework->viewport_dirty) {
        return YETTY_OK(yetty_ycore_int, 1);
    }
    int dirty = widget_tree_dirty(framework->root);
    if (!dirty && framework->overlay_root) {
        dirty = widget_tree_dirty(framework->overlay_root);
    }
    return YETTY_OK(yetty_ycore_int, dirty);
}

/*===========================================================================
 * Input — hit-test, dispatch with bubbling, focus (strategy.md §7).
 *=========================================================================*/

/* Deepest visible widget containing the pane-local point; later siblings
 * win (paint order). TRANSPARENT widgets are layout-only: their own rect
 * never gates the walk (a flex box without an explicit main size has a
 * degenerate rect, and children may legitimately overflow it) — they
 * become the hit only when the point is actually inside them, but their
 * children are always probed. */
static struct yetty_yclass_object *hit_test(struct yetty_yclass_object *obj, float x, float y)
{
    if (!tree_flag(yetty_ygui2_widget_is_visible(obj))) {
        return NULL;
    }
    int transparent = tree_flag(yetty_ygui2_widget_is_transparent(obj));
    float rect_x = 0.0f, rect_y = 0.0f, rect_w = 0.0f, rect_h = 0.0f;
    tree_rect(obj, &rect_x, &rect_y, &rect_w, &rect_h);
    int inside = !(x < rect_x || y < rect_y || x >= rect_x + rect_w || y >= rect_y + rect_h);
    if (!inside && !transparent) {
        return NULL;
    }
    struct yetty_yclass_object *best = inside ? obj : NULL;
    for (struct yetty_yclass_object *child = tree_first_child(obj); child;
         child = tree_next_sibling(child)) {
        struct yetty_yclass_object *candidate = hit_test(child, x, y);
        if (candidate) {
            best = candidate; /* later siblings override — paint order wins */
        }
    }
    return best;
}

/* Dispatch helper: call `slot` on `target` and bubble up the parent chain
 * until consumed. Coordinates are made widget-local per receiver. */
enum ygui2_pointer_slot {
    YGUI2_POINTER_PRESS,
    YGUI2_POINTER_RELEASE,
    YGUI2_POINTER_MOTION,
    YGUI2_POINTER_SCROLL,
};

static struct yetty_yclass_object *dispatch_pointer(struct yetty_ygui2_framework *framework,
                                                    struct yetty_yclass_object *target, float x,
                                                    float y, enum ygui2_pointer_slot slot,
                                                    int button, int pressed_or_mods, float wheel_dy)
{
    (void)framework;
    struct yetty_yclass_object *walk = target;
    while (walk) {
        float rect_x = 0.0f, rect_y = 0.0f;
        tree_rect(walk, &rect_x, &rect_y, NULL, NULL);
        float local_x = x - rect_x;
        float local_y = y - rect_y;
        struct yetty_ycore_int_result consumed_res;
        switch (slot) {
        case YGUI2_POINTER_PRESS:
            consumed_res =
                yetty_ygui2_widget_on_press(walk, local_x, local_y, button, pressed_or_mods);
            break;
        case YGUI2_POINTER_RELEASE:
            consumed_res =
                yetty_ygui2_widget_on_release(walk, local_x, local_y, button, pressed_or_mods);
            break;
        case YGUI2_POINTER_MOTION:
            consumed_res =
                yetty_ygui2_widget_on_motion(walk, local_x, local_y, (uint32_t)pressed_or_mods);
            break;
        case YGUI2_POINTER_SCROLL:
        default:
            consumed_res = yetty_ygui2_widget_on_scroll(walk, local_x, local_y, wheel_dy);
            break;
        }
        if (YETTY_IS_ERR(consumed_res)) {
            yetty_ycore_error_destroy(consumed_res.error);
            return NULL;
        }
        if (consumed_res.value) {
            return walk;
        }
        struct yetty_yclass_object_ptr_result parent_res = yetty_ygui2_widget_parent_obj(walk);
        if (YETTY_IS_ERR(parent_res)) {
            yetty_ycore_error_destroy(parent_res.error);
            return NULL;
        }
        walk = parent_res.value;
    }
    return NULL;
}

/* Internal pointer feeds: dispatch + capture bookkeeping, reporting
 * whether ANY widget consumed the event (drives reinjection — the
 * terminal contract requires bouncing what the subscriber did not use). */
static void framework_mouse_button(struct yetty_ygui2_framework *framework,
                                   struct yetty_yclass_object *obj, float x, float y, int button,
                                   int pressed, int mods, int *out_consumed)
{
    (void)obj;
    *out_consumed = 0;
    if (!framework->root) {
        return;
    }
    if (pressed) {
        struct yetty_yclass_object *target = overlay_hit(framework, x, y);
        if (!target && overlay_any_visible(framework)) {
            /* Outside press: popups/dropdowns close and swallow the click;
             * non-dismissable overlays (dialogs) leave it to the root tree. */
            if (overlay_dismiss(framework, /*only_dismissable=*/1) > 0) {
                *out_consumed = 1;
                return;
            }
        }
        if (!target) {
            target = hit_test(framework->root, x, y);
        }
        if (!target) {
            return;
        }
        /* Click-to-focus: nearest focusable ancestor of the hit (or focus
         * clears — a click on inert chrome drops the ring). */
        struct yetty_yclass_object *focus_walk = target;
        while (focus_walk && !tree_flag(yetty_ygui2_widget_is_focusable(focus_walk))) {
            struct yetty_yclass_object_ptr_result parent_res =
                yetty_ygui2_widget_parent_obj(focus_walk);
            if (YETTY_IS_ERR(parent_res)) {
                yetty_ycore_error_destroy(parent_res.error);
                focus_walk = NULL;
                break;
            }
            focus_walk = parent_res.value;
        }
        focus_move(framework, focus_walk);
        struct yetty_yclass_object *consumer =
            dispatch_pointer(framework, target, x, y, YGUI2_POINTER_PRESS, button, mods, 0.0f);
        *out_consumed = consumer != NULL;
        /* Capture the drag either way — the matching release must reach
         * the same widget even when the press itself was bounced. */
        framework->mouse_capture = consumer ? consumer : target;
    } else {
        struct yetty_yclass_object *target = framework->mouse_capture;
        if (target && !target_alive(framework, target)) {
            target = NULL; /* hidden/removed mid-drag */
        }
        if (!target) {
            target = overlay_hit(framework, x, y);
        }
        if (!target) {
            target = hit_test(framework->root, x, y);
        }
        if (target) {
            struct yetty_yclass_object *consumer = dispatch_pointer(
                framework, target, x, y, YGUI2_POINTER_RELEASE, button, mods, 0.0f);
            *out_consumed = consumer != NULL;
        }
        framework->mouse_capture = NULL;
    }
}

static void framework_mouse_motion(struct yetty_ygui2_framework *framework,
                                   struct yetty_yclass_object *obj, float x, float y,
                                   uint32_t buttons_held, int *out_consumed)
{
    (void)obj;
    *out_consumed = 0;
    if (!framework->root) {
        return;
    }
    struct yetty_yclass_object *target = NULL;
    if (buttons_held && framework->mouse_capture &&
        target_alive(framework, framework->mouse_capture)) {
        target = framework->mouse_capture;
    } else {
        target = overlay_hit(framework, x, y);
        if (!target) {
            target = hit_test(framework->root, x, y);
        }
    }
    if (target) {
        struct yetty_yclass_object *consumer = dispatch_pointer(
            framework, target, x, y, YGUI2_POINTER_MOTION, 0, (int)buttons_held, 0.0f);
        *out_consumed = consumer != NULL;
    }
}

static void framework_mouse_scroll(struct yetty_ygui2_framework *framework,
                                   struct yetty_yclass_object *obj, float x, float y,
                                   float wheel_dy, int *out_consumed)
{
    (void)obj;
    *out_consumed = 0;
    if (!framework->root) {
        return;
    }
    struct yetty_yclass_object *target = overlay_hit(framework, x, y);
    if (!target) {
        target = hit_test(framework->root, x, y);
    }
    if (target) {
        struct yetty_yclass_object *consumer =
            dispatch_pointer(framework, target, x, y, YGUI2_POINTER_SCROLL, 0, 0, wheel_dy);
        *out_consumed = consumer != NULL;
    }
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_feed_mouse_button(
    struct yetty_yclass_object *obj, float x, float y, int button, int pressed, int mods)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 feed_mouse_button: data");
    framework_reap_orphans(data_res.value);
    int consumed = 0;
    framework_mouse_button(data_res.value, obj, x, y, button, pressed, mods, &consumed);
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_feed_mouse_motion(
    struct yetty_yclass_object *obj, float x, float y, uint32_t buttons_held)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 feed_mouse_motion: data");
    framework_reap_orphans(data_res.value);
    int consumed = 0;
    framework_mouse_motion(data_res.value, obj, x, y, buttons_held, &consumed);
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_feed_mouse_scroll(
    struct yetty_yclass_object *obj, float x, float y, float wheel_dy)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 feed_mouse_scroll: data");
    framework_reap_orphans(data_res.value);
    int consumed = 0;
    framework_mouse_scroll(data_res.value, obj, x, y, wheel_dy, &consumed);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Layout — one flex pass (strategy.md §4 step 1).
 *=========================================================================*/

/* Main-axis flow length of `obj`'s flex children under the given flow
 * spec — the natural content size of a flow container (grow only
 * distributes surplus, so basis flow IS the extent). The spec is passed
 * in because a scroll-content group flows by its VIEWPORT's spec. */
/* Minimum size of `obj`'s CONTENT along the axis `row` (1 = x, 0 = y),
 * measured recursively: when the widget's own flow runs along that axis
 * the children sum (declared main floored by their own measure, plus
 * gaps and pads); perpendicular, the tallest/widest child governs
 * (declared cross floored by its own measure, plus the pads on the
 * measured axis). Leaves answer 0 — callers max() this against declared
 * sizes. Without the recursion, grouping containers (row_add/column_add
 * boxes with zeroed specs) contribute nothing and a scrollarea's content
 * collapses to the viewport. */
static float widget_measure_axis(struct yetty_yclass_object *obj, int row)
{
    if (!tree_first_child(obj)) {
        return 0.0f;
    }
    struct yetty_ygui2_layout spec;
    struct yetty_ycore_void_result spec_res = yetty_ygui2_widget_layout_copy(obj, &spec);
    if (YETTY_IS_ERR(spec_res)) {
        yetty_ycore_error_destroy(spec_res.error);
        return 0.0f;
    }
    int own_row = spec.direction == YETTY_YGUI2_DIRECTION_ROW;
    float lead_pad = row ? spec.pad_left : spec.pad_top;
    float trail_pad = row ? spec.pad_right : spec.pad_bottom;
    float extent = 0.0f;
    uint32_t flow_children = 0;
    for (struct yetty_yclass_object *child = tree_first_child(obj); child;
         child = tree_next_sibling(child)) {
        if (!tree_flag(yetty_ygui2_widget_is_visible(child))) {
            continue;
        }
        int child_absolute = 0;
        struct yetty_ycore_void_result abs_res =
            yetty_ygui2_widget_absolute_rect(child, &child_absolute, NULL, NULL, NULL, NULL);
        if (YETTY_IS_ERR(abs_res)) {
            yetty_ycore_error_destroy(abs_res.error);
            continue;
        }
        if (child_absolute) {
            continue;
        }
        struct yetty_ygui2_layout child_spec;
        struct yetty_ycore_void_result child_spec_res =
            yetty_ygui2_widget_layout_copy(child, &child_spec);
        if (YETTY_IS_ERR(child_spec_res)) {
            yetty_ycore_error_destroy(child_spec_res.error);
            continue;
        }
        float measured = widget_measure_axis(child, row);
        if (own_row == row) {
            /* Flow runs along the measured axis: children sum. The
             * declared main (basis/min_main) applies on this axis. */
            float declared =
                child_spec.basis > child_spec.min_main ? child_spec.basis : child_spec.min_main;
            float contribution = declared > measured ? declared : measured;
            extent += contribution + spec.gap;
            flow_children++;
        } else {
            /* Perpendicular: the largest child governs; the declared
             * CROSS size applies on this axis. */
            float contribution =
                child_spec.cross_size > measured ? child_spec.cross_size : measured;
            if (contribution > extent) {
                extent = contribution;
            }
        }
    }
    if (own_row == row && flow_children) {
        extent -= spec.gap;
    }
    return lead_pad + extent + trail_pad;
}

static float widget_flow_extent(struct yetty_yclass_object *obj,
                                const struct yetty_ygui2_layout *flow_spec)
{
    const struct yetty_ygui2_layout *spec_view = flow_spec;
    int row = spec_view->direction == YETTY_YGUI2_DIRECTION_ROW;
    float extent = row ? spec_view->pad_left : spec_view->pad_top;
    uint32_t flow_children = 0;
    for (struct yetty_yclass_object *child = tree_first_child(obj); child;
         child = tree_next_sibling(child)) {
        if (!tree_flag(yetty_ygui2_widget_is_visible(child))) {
            continue;
        }
        int child_absolute = 0;
        struct yetty_ycore_void_result abs_res =
            yetty_ygui2_widget_absolute_rect(child, &child_absolute, NULL, NULL, NULL, NULL);
        if (YETTY_IS_ERR(abs_res)) {
            yetty_ycore_error_destroy(abs_res.error);
            continue;
        }
        if (child_absolute) {
            continue;
        }
        struct yetty_ygui2_layout child_spec;
        struct yetty_ycore_void_result child_spec_res =
            yetty_ygui2_widget_layout_copy(child, &child_spec);
        if (YETTY_IS_ERR(child_spec_res)) {
            yetty_ycore_error_destroy(child_spec_res.error);
            continue;
        }
        float basis =
            child_spec.basis > child_spec.min_main ? child_spec.basis : child_spec.min_main;
        /* Nested grouping containers declare nothing — measure them. */
        float measured = widget_measure_axis(child, row);
        if (measured > basis) {
            basis = measured;
        }
        extent += basis + spec_view->gap;
        flow_children++;
    }
    if (flow_children) {
        extent -= spec_view->gap;
    }
    extent += row ? spec_view->pad_right : spec_view->pad_bottom;
    return extent;
}

/* Available scroll range of a scrollarea along its flow axis: the
 * MEASURED content extent minus the viewport, floored at 0 — the shared
 * measure sizing the content group also clamps the wheel, so users can
 * always reach the last rows and never scroll into blank space. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_scroll_limit(struct yetty_yclass_object *obj,
                                                               float *out_limit)
{
    if (out_limit) {
        *out_limit = 0.0f;
    }
    struct yetty_ygui2_layout spec;
    struct yetty_ycore_void_result spec_res = yetty_ygui2_widget_layout_copy(obj, &spec);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, spec_res, "ygui2 scroll_limit: spec");
    int row = spec.direction == YETTY_YGUI2_DIRECTION_ROW;
    struct yetty_yclass_object *content = tree_first_child(obj);
    if (!content) {
        return YETTY_OK_VOID();
    }
    float extent = widget_flow_extent(content, &spec);
    float viewport_w = 0.0f;
    float viewport_h = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &viewport_w, &viewport_h);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 scroll_limit: rect");
    float viewport_main = row ? viewport_w : viewport_h;
    if (out_limit && extent > viewport_main) {
        *out_limit = extent - viewport_main;
    }
    return YETTY_OK_VOID();
}

static void layout_widget(struct yetty_yclass_object *obj, float x, float y, float w, float h)
{
    struct yetty_ycore_void_result rect_res = yetty_ygui2_widget_set_rect(obj, x, y, w, h);
    if (YETTY_IS_ERR(rect_res)) {
        yetty_ycore_error_destroy(rect_res.error);
        return;
    }
    if (!tree_first_child(obj)) {
        return;
    }
    struct yetty_ygui2_layout spec;
    struct yetty_ycore_void_result spec_res = yetty_ygui2_widget_layout_copy(obj, &spec);
    if (YETTY_IS_ERR(spec_res)) {
        yetty_ycore_error_destroy(spec_res.error);
        return;
    }
    /* Scroll-viewport relationships, resolved BEFORE any geometry derives
     * from the spec:
     *   - obj IS a scrollarea: its only child is the owned content group,
     *     sized below to span the children's whole flow;
     *   - obj IS that content group: its FLOW fields (direction, gap,
     *     pads) are the VIEWPORT's, read live every pass — layout_set()
     *     on the scrollarea keeps working after children exist, with no
     *     stale creation-time snapshot. */
    int obj_is_scrollarea = 0;
    {
        struct yetty_yclass_ptr_result scroll_class_res = yetty_ygui2_scrollarea_class_get();
        if (YETTY_IS_OK(scroll_class_res)) {
            obj_is_scrollarea = obj->klass == scroll_class_res.value;
            if (!obj_is_scrollarea && obj->klass == yetty_ygui2_widget_class_get().value) {
                struct yetty_yclass_object_ptr_result parent_res =
                    yetty_ygui2_widget_parent_obj(obj);
                if (YETTY_IS_OK(parent_res) && parent_res.value &&
                    parent_res.value->klass == scroll_class_res.value) {
                    struct yetty_ygui2_layout viewport_spec;
                    struct yetty_ycore_void_result viewport_spec_res =
                        yetty_ygui2_widget_layout_copy(parent_res.value, &viewport_spec);
                    if (YETTY_IS_OK(viewport_spec_res)) {
                        spec.direction = viewport_spec.direction;
                        spec.gap = viewport_spec.gap;
                        spec.pad_left = viewport_spec.pad_left;
                        spec.pad_top = viewport_spec.pad_top;
                        spec.pad_right = viewport_spec.pad_right;
                        spec.pad_bottom = viewport_spec.pad_bottom;
                    } else {
                        yetty_ycore_error_destroy(viewport_spec_res.error);
                    }
                } else if (YETTY_IS_ERR(parent_res)) {
                    yetty_ycore_error_destroy(parent_res.error);
                }
            }
        } else {
            yetty_ycore_error_destroy(scroll_class_res.error);
        }
    }
    float content_x = x + spec.pad_left;
    float content_y = y + spec.pad_top;
    float content_w = w - spec.pad_left - spec.pad_right;
    float content_h = h - spec.pad_top - spec.pad_bottom;
    if (obj_is_scrollarea) {
        /* The owned content group spans the RAW viewport box — the flow
         * pads belong to the CONTENT group's own child pass (it reads
         * this viewport's spec live), so they apply exactly once. */
        content_x = x;
        content_y = y;
        content_w = w;
        content_h = h;
    }
    int row = spec.direction == YETTY_YGUI2_DIRECTION_ROW;
    float main_size = row ? content_w : content_h;
    float total_basis = 0.0f;
    float total_grow = 0.0f;
    uint32_t visible_count = 0;
    for (struct yetty_yclass_object *child = tree_first_child(obj); child;
         child = tree_next_sibling(child)) {
        if (!tree_flag(yetty_ygui2_widget_is_visible(child))) {
            continue;
        }
        int child_absolute = 0;
        struct yetty_ycore_void_result abs_res =
            yetty_ygui2_widget_absolute_rect(child, &child_absolute, NULL, NULL, NULL, NULL);
        if (YETTY_IS_ERR(abs_res)) {
            yetty_ycore_error_destroy(abs_res.error);
            return;
        }
        if (child_absolute) {
            continue; /* placed at its own rect, not in the flow */
        }
        struct yetty_ygui2_layout child_spec;
        struct yetty_ycore_void_result child_spec_res =
            yetty_ygui2_widget_layout_copy(child, &child_spec);
        if (YETTY_IS_ERR(child_spec_res)) {
            yetty_ycore_error_destroy(child_spec_res.error);
            return;
        }
        float basis =
            child_spec.basis > child_spec.min_main ? child_spec.basis : child_spec.min_main;
        total_basis += basis;
        total_grow += child_spec.grow;
        visible_count++;
    }
    /* No early-out on zero FLEX children — absolute children still need
     * their placement pass below. */
    float gaps = visible_count > 0 ? spec.gap * (float)(visible_count - 1u) : 0.0f;
    float remaining = main_size - total_basis - gaps;
    if (remaining < 0.0f) {
        remaining = 0.0f;
    }
    float scroll_x_amount = 0.0f;
    float scroll_y_amount = 0.0f;
    {
        struct yetty_ycore_void_result scroll_res =
            yetty_ygui2_widget_scroll(obj, &scroll_x_amount, &scroll_y_amount);
        if (YETTY_IS_ERR(scroll_res)) {
            yetty_ycore_error_destroy(scroll_res.error);
        }
    }
    float cursor = (row ? content_x - scroll_x_amount : content_y - scroll_y_amount);
    for (struct yetty_yclass_object *child = tree_first_child(obj); child;
         child = tree_next_sibling(child)) {
        if (!tree_flag(yetty_ygui2_widget_is_visible(child))) {
            continue;
        }
        int child_absolute = 0;
        float abs_x = 0.0f, abs_y = 0.0f, abs_w = 0.0f, abs_h = 0.0f;
        struct yetty_ycore_void_result abs_res = yetty_ygui2_widget_absolute_rect(
            child, &child_absolute, &abs_x, &abs_y, &abs_w, &abs_h);
        if (YETTY_IS_ERR(abs_res)) {
            yetty_ycore_error_destroy(abs_res.error);
            return;
        }
        if (child_absolute) {
            layout_widget(child, abs_x, abs_y, abs_w, abs_h);
            continue;
        }
        struct yetty_ygui2_layout child_spec;
        struct yetty_ycore_void_result child_spec_res =
            yetty_ygui2_widget_layout_copy(child, &child_spec);
        if (YETTY_IS_ERR(child_spec_res)) {
            yetty_ycore_error_destroy(child_spec_res.error);
            return;
        }
        float basis =
            child_spec.basis > child_spec.min_main ? child_spec.basis : child_spec.min_main;
        float grow_share = total_grow > 0.0f ? remaining * (child_spec.grow / total_grow) : 0.0f;
        float child_main = basis + grow_share;
        if (obj_is_scrollarea) {
            /* The content group spans its children's whole flow, laid out
             * under the VIEWPORT's flow spec (this scrollarea's). */
            float extent = widget_flow_extent(child, &spec);
            if (extent > child_main) {
                child_main = extent;
            }
        }
        float child_cross =
            child_spec.cross_size > 0.0f ? child_spec.cross_size : (row ? content_h : content_w);
        if (row) {
            layout_widget(child, cursor, content_y, child_main, child_cross);
        } else {
            layout_widget(child, content_x, cursor, child_cross, child_main);
        }
        cursor += child_main + spec.gap;
    }
}

/*===========================================================================
 * Emit — the wire pipeline (strategy.md §4).
 *=========================================================================*/

enum { YGUI2_PATH_MAX = 8 };

/* Ambient paint-z band for the overlay tree: everything the overlay emits
 * stacks above app-tree primitives. Hosted records (complexes, embeds)
 * carrying z >= this band are outside the guarantee — documented app
 * responsibility. */
enum { YGUI2_OVERLAY_PAINT_Z = 1000 };

/* The RESERVE budget is YGUI2_VIEWPORT_MAX_PX (see set_viewport): the
 * span is fixed for the insertion's whole life and no reopen can grow
 * it, so the first frame reserves the FULL supported viewport range.
 * Every accepted resize is then relayout-only — the live insertion (and
 * every complex runtime in it) survives unconditionally. Content beyond
 * the current pane is projection-clipped at zero cost. */

/* Overflow sentinel for widget_wire_path: an address must NEVER be
 * truncated — a shortened path is a different node. */
#define YGUI2_PATH_OVERFLOW UINT32_MAX

/* Path of minted-ancestor ids from the root DOWN TO (excluding) `obj`'s own
 * group — the CMD_PATH prefix for addressing it. Returns the id count, or
 * YGUI2_PATH_OVERFLOW when the minted depth exceeds the wire maximum. */
static uint32_t widget_wire_path(struct yetty_yclass_object *obj, uint32_t *out_ids)
{
    uint32_t reversed[YGUI2_PATH_MAX];
    uint32_t count = 0;
    struct yetty_yclass_object_ptr_result walk_res = yetty_ygui2_widget_parent_obj(obj);
    struct yetty_yclass_object *walk = YETTY_IS_OK(walk_res) ? walk_res.value : NULL;
    if (YETTY_IS_ERR(walk_res)) {
        yetty_ycore_error_destroy(walk_res.error);
    }
    while (walk) {
        if (!tree_flag(yetty_ygui2_widget_is_transparent(walk))) {
            if (count >= YGUI2_PATH_MAX) {
                return YGUI2_PATH_OVERFLOW;
            }
            reversed[count++] = tree_node_id(walk);
        }
        struct yetty_yclass_object_ptr_result parent_res = yetty_ygui2_widget_parent_obj(walk);
        if (YETTY_IS_ERR(parent_res)) {
            yetty_ycore_error_destroy(parent_res.error);
            break;
        }
        walk = parent_res.value;
    }
    for (uint32_t index = 0; index < count; ++index) {
        out_ids[index] = reversed[count - 1u - index];
    }
    return count;
}

/* Nearest minted ancestor's pane-absolute origin (0,0 for the root scope). */
static void widget_minted_parent_origin(struct yetty_yclass_object *obj, float *out_x, float *out_y)
{
    *out_x = 0.0f;
    *out_y = 0.0f;
    struct yetty_yclass_object_ptr_result walk_res = yetty_ygui2_widget_parent_obj(obj);
    struct yetty_yclass_object *walk = YETTY_IS_OK(walk_res) ? walk_res.value : NULL;
    if (YETTY_IS_ERR(walk_res)) {
        yetty_ycore_error_destroy(walk_res.error);
    }
    while (walk) {
        if (!tree_flag(yetty_ygui2_widget_is_transparent(walk))) {
            tree_rect(walk, out_x, out_y, NULL, NULL);
            return;
        }
        struct yetty_yclass_object_ptr_result parent_res = yetty_ygui2_widget_parent_obj(walk);
        if (YETTY_IS_ERR(parent_res)) {
            yetty_ycore_error_destroy(parent_res.error);
            return;
        }
        walk = parent_res.value;
    }
}

static struct yetty_ycore_void_result emit_offset_update(struct yetty_ydraw_drawable_list *list,
                                                         struct yetty_yclass_object *obj)
{
    uint32_t path_ids[YGUI2_PATH_MAX];
    uint32_t path_count = widget_wire_path(obj, path_ids);
    if (path_count == YGUI2_PATH_OVERFLOW) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 emit: widget path exceeds wire depth");
    }
    if (path_count > 0) {
        struct yetty_ycore_void_result path_res =
            yetty_ydraw_drawable_list_add_cmd_path(list, path_ids, path_count);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, path_res, "ygui2 emit: cmd_path");
    }
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    widget_minted_parent_origin(obj, &origin_x, &origin_y);
    float widget_x = 0.0f;
    float widget_y = 0.0f;
    tree_rect(obj, &widget_x, &widget_y, NULL, NULL);
    float offset_x = widget_x - origin_x;
    float offset_y = widget_y - origin_y;
    uint32_t payload[3];
    payload[0] = YETTY_YDRAW_GROUP_FIELD_OFFSET;
    memcpy(&payload[1], &offset_x, sizeof(float));
    memcpy(&payload[2], &offset_y, sizeof(float));
    struct yetty_ycore_void_result update_res =
        yetty_ydraw_drawable_list_add_cmd_update(list, tree_node_id(obj), payload, sizeof(payload));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, update_res, "ygui2 emit: offset update");
    return yetty_ygui2_widget_set_emitted_offset(obj, offset_x, offset_y);
}

/* A hidden subtree has nothing on the wire and its accumulated dirt is
 * meaningless until shown (showing bubbles structure dirt to the parent,
 * which re-inserts the subtree fresh). Clear it so a hidden overlay child
 * cannot hold is_dirty true forever — the phantom that made every emit
 * ship an empty overlay band. */
static void clear_dirty_subtree(struct yetty_yclass_object *obj)
{
    struct yetty_ycore_void_result clear_res = yetty_ygui2_widget_clear_dirty(obj);
    if (YETTY_IS_ERR(clear_res)) {
        yetty_ycore_error_destroy(clear_res.error);
    }
    for (struct yetty_yclass_object *child = tree_first_child(obj); child;
         child = tree_next_sibling(child)) {
        clear_dirty_subtree(child);
    }
}

/* Mint the skin subgroup id on first use. Lazy (not at add time) so the
 * primary node-id sequence — part of the observable wire contract — is
 * unchanged by the skin-subgroup mechanism. */
static uint32_t tree_skin_node_id(struct yetty_yclass_object *obj)
{
    struct yetty_ycore_uint32_result id_res = yetty_ygui2_widget_skin_node_id(obj);
    if (YETTY_IS_ERR(id_res)) {
        yetty_ycore_error_destroy(id_res.error);
        return 0;
    }
    return id_res.value;
}

static struct yetty_ycore_void_result widget_ensure_skin_id(struct yetty_yclass_object *obj)
{
    uint32_t skin_id = tree_skin_node_id(obj);
    if (skin_id) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object_ptr_result framework_obj_res = yetty_ygui2_widget_framework_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_obj_res, "ygui2 skin id: framework obj");
    struct yetty_ygui2_framework_ptr_result data_res =
        yetty_ygui2_framework_from(framework_obj_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 skin id: framework");
    return yetty_ygui2_widget_set_skin_node_id(obj, data_res.value->next_node_id++);
}

/* Insert `obj`'s subtree into the open scope: mint the group, paint local
 * prims, recurse. Offsets are NOT in the group record — emit_offsets_pass
 * sends them after the inserts (a fresh group starts at (0,0)). */
static struct yetty_ycore_void_result insert_subtree(struct yetty_ydraw_drawable_list *list,
                                                     struct yetty_yclass_object *obj)
{
    if (!tree_flag(yetty_ygui2_widget_is_visible(obj))) {
        clear_dirty_subtree(obj);
        return YETTY_OK_VOID();
    }
    int transparent = tree_flag(yetty_ygui2_widget_is_transparent(obj));
    uint32_t group_marker = 0;
    if (!transparent) {
        struct yetty_ydraw_id_result begin_res =
            yetty_ydraw_drawable_list_begin_group(list, tree_node_id(obj));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, begin_res, "ygui2 insert: begin_group");
        group_marker = begin_res.value;
        /* The widget's own primitives live in a SKIN subgroup, so a later
         * skin-only change reopens just that subgroup and the containment
         * group above it (children, hosted complex runtimes) stays live. */
        struct yetty_ycore_void_result skin_id_res = widget_ensure_skin_id(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, skin_id_res, "ygui2 insert: skin id");
        struct yetty_ydraw_id_result skin_begin_res =
            yetty_ydraw_drawable_list_begin_group(list, tree_skin_node_id(obj));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, skin_begin_res, "ygui2 insert: skin begin");
        struct yetty_ycore_void_result paint_res = yetty_ygui2_widget_paint(obj, list);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, paint_res, "ygui2 insert: paint");
        struct yetty_ycore_void_result skin_end_res =
            yetty_ydraw_drawable_list_end_group(list, skin_begin_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, skin_end_res, "ygui2 insert: skin end");
        /* RETAINED content lands DIRECTLY in the containment group: skin
         * reopens, theme restyles and ancestor repaints never touch it —
         * only an intentional structural reopen of this widget replaces
         * the hosted runtime. */
        struct yetty_ycore_void_result retained_res = yetty_ygui2_widget_paint_retained(obj, list);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, retained_res, "ygui2 insert: retained");
    }
    for (struct yetty_yclass_object *child = tree_first_child(obj); child;
         child = tree_next_sibling(child)) {
        struct yetty_ycore_void_result child_insert_res = insert_subtree(list, child);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, child_insert_res, "ygui2 insert: child");
    }
    if (!transparent) {
        struct yetty_ycore_void_result end_res =
            yetty_ydraw_drawable_list_end_group(list, group_marker);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, end_res, "ygui2 insert: end_group");
    }
    struct yetty_ycore_void_result clear_res = yetty_ygui2_widget_clear_dirty(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_res, "ygui2 insert: clear dirty");
    return YETTY_OK_VOID();
}

/* Post-insert offsets: one GROUP_FIELD_OFFSET update per minted widget
 * whose offset (relative to the nearest minted ancestor) is nonzero. */
static struct yetty_ycore_void_result emit_offsets_pass(struct yetty_ydraw_drawable_list *list,
                                                        struct yetty_yclass_object *obj)
{
    if (!tree_flag(yetty_ygui2_widget_is_visible(obj))) {
        return YETTY_OK_VOID();
    }
    if (!tree_flag(yetty_ygui2_widget_is_transparent(obj))) {
        float origin_x = 0.0f;
        float origin_y = 0.0f;
        widget_minted_parent_origin(obj, &origin_x, &origin_y);
        float widget_x = 0.0f;
        float widget_y = 0.0f;
        tree_rect(obj, &widget_x, &widget_y, NULL, NULL);
        if (widget_x - origin_x != 0.0f || widget_y - origin_y != 0.0f) {
            struct yetty_ycore_void_result update_res = emit_offset_update(list, obj);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, update_res, "ygui2 offsets: update");
        } else {
            struct yetty_ycore_void_result mark_res =
                yetty_ygui2_widget_set_emitted_offset(obj, 0.0f, 0.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, mark_res, "ygui2 offsets: mark");
        }
        int clip_enabled = 0;
        float emitted_w = 0.0f;
        float emitted_h = 0.0f;
        struct yetty_ycore_void_result clip_state_res =
            yetty_ygui2_widget_clip_state(obj, &clip_enabled, &emitted_w, &emitted_h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, clip_state_res, "ygui2 offsets: clip state");
        if (clip_enabled) {
            float widget_w = 0.0f;
            float widget_h = 0.0f;
            tree_rect(obj, NULL, NULL, &widget_w, &widget_h);
            if (widget_w != emitted_w || widget_h != emitted_h) {
                uint32_t path_ids[YGUI2_PATH_MAX];
                uint32_t path_count = widget_wire_path(obj, path_ids);
                if (path_count == YGUI2_PATH_OVERFLOW) {
                    return YETTY_ERR(yetty_ycore_void,
                                     "ygui2 offsets: widget path exceeds wire depth");
                }
                if (path_count > 0) {
                    struct yetty_ycore_void_result path_res =
                        yetty_ydraw_drawable_list_add_cmd_path(list, path_ids, path_count);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, path_res, "ygui2 offsets: clip path");
                }
                uint32_t payload[5];
                payload[0] = YETTY_YDRAW_GROUP_FIELD_CLIP;
                float clip_rect[4] = {0.0f, 0.0f, widget_w, widget_h};
                memcpy(&payload[1], clip_rect, sizeof(clip_rect));
                struct yetty_ycore_void_result clip_update_res =
                    yetty_ydraw_drawable_list_add_cmd_update(list, tree_node_id(obj), payload,
                                                             sizeof(payload));
                YETTY_RETURN_IF_ERR(yetty_ycore_void, clip_update_res, "ygui2 offsets: clip");
                struct yetty_ycore_void_result clip_mark_res =
                    yetty_ygui2_widget_set_emitted_clip(obj, widget_w, widget_h);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, clip_mark_res, "ygui2 offsets: clip mark");
            }
        }
    }
    for (struct yetty_yclass_object *child = tree_first_child(obj); child;
         child = tree_next_sibling(child)) {
        struct yetty_ycore_void_result child_pass_res = emit_offsets_pass(list, child);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, child_pass_res, "ygui2 offsets: child");
    }
    return YETTY_OK_VOID();
}

/* Whether this minted widget's CONTAINMENT group must be re-inserted: its
 * own STRUCTURE dirt (membership/order changed), or any dirt on a
 * TRANSPARENT descendant (which has no group of its own and bubbles to
 * the nearest minted ancestor). Skin dirt alone does NOT reinsert — the
 * skin subgroup reopens by itself and every descendant stays live. */
static int subtree_needs_reinsert(struct yetty_yclass_object *obj)
{
    int structure = 0;
    struct yetty_ycore_void_result flags_res =
        yetty_ygui2_widget_dirty_flags(obj, NULL, &structure, NULL);
    if (YETTY_IS_ERR(flags_res)) {
        yetty_ycore_error_destroy(flags_res.error);
        return 0;
    }
    if (structure) {
        return 1;
    }
    for (struct yetty_yclass_object *child = tree_first_child(obj); child;
         child = tree_next_sibling(child)) {
        if (!tree_flag(yetty_ygui2_widget_is_transparent(child))) {
            continue;
        }
        int child_skin = 0;
        int child_structure = 0;
        struct yetty_ycore_void_result child_flags_res =
            yetty_ygui2_widget_dirty_flags(child, &child_skin, &child_structure, NULL);
        if (YETTY_IS_ERR(child_flags_res)) {
            yetty_ycore_error_destroy(child_flags_res.error);
            continue;
        }
        if (child_skin || child_structure || subtree_needs_reinsert(child)) {
            return 1;
        }
    }
    return 0;
}

/* Reopen ONLY the skin subgroup: CMD_PATH down to (including) the widget
 * itself, then GROUP(skin_id){ paint }. The containment group is never
 * touched, so children — hosted complex runtimes included — survive. */
static struct yetty_ycore_void_result emit_skin_reopen(struct yetty_ydraw_drawable_list *list,
                                                       struct yetty_yclass_object *obj)
{
    uint32_t path_ids[YGUI2_PATH_MAX];
    uint32_t path_count = widget_wire_path(obj, path_ids);
    if (path_count == YGUI2_PATH_OVERFLOW || path_count >= YGUI2_PATH_MAX) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 skin reopen: widget path exceeds wire depth");
    }
    path_ids[path_count++] = tree_node_id(obj);
    struct yetty_ycore_void_result path_res =
        yetty_ydraw_drawable_list_add_cmd_path(list, path_ids, path_count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, path_res, "ygui2 skin reopen: path");
    struct yetty_ydraw_id_result begin_res =
        yetty_ydraw_drawable_list_begin_group(list, tree_skin_node_id(obj));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, begin_res, "ygui2 skin reopen: begin");
    struct yetty_ycore_void_result paint_res = yetty_ygui2_widget_paint(obj, list);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, paint_res, "ygui2 skin reopen: paint");
    return yetty_ydraw_drawable_list_end_group(list, begin_res.value);
}

/* The widget's CLIP projection state (viewport widgets): re-send when the
 * clipped size changed. Shared by the post-reinsert offsets pass and the
 * incremental walk. */
static struct yetty_ycore_void_result emit_clip_if_changed(struct yetty_ydraw_drawable_list *list,
                                                           struct yetty_yclass_object *obj)
{
    int clip_enabled = 0;
    float emitted_w = 0.0f;
    float emitted_h = 0.0f;
    struct yetty_ycore_void_result clip_state_res =
        yetty_ygui2_widget_clip_state(obj, &clip_enabled, &emitted_w, &emitted_h);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clip_state_res, "ygui2 clip: state");
    if (!clip_enabled) {
        return YETTY_OK_VOID();
    }
    float widget_w = 0.0f;
    float widget_h = 0.0f;
    tree_rect(obj, NULL, NULL, &widget_w, &widget_h);
    if (widget_w == emitted_w && widget_h == emitted_h) {
        return YETTY_OK_VOID();
    }
    uint32_t path_ids[YGUI2_PATH_MAX];
    uint32_t path_count = widget_wire_path(obj, path_ids);
    if (path_count == YGUI2_PATH_OVERFLOW) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 clip: widget path exceeds wire depth");
    }
    if (path_count > 0) {
        struct yetty_ycore_void_result path_res =
            yetty_ydraw_drawable_list_add_cmd_path(list, path_ids, path_count);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, path_res, "ygui2 clip: path");
    }
    uint32_t payload[5];
    payload[0] = YETTY_YDRAW_GROUP_FIELD_CLIP;
    float clip_rect[4] = {0.0f, 0.0f, widget_w, widget_h};
    memcpy(&payload[1], clip_rect, sizeof(clip_rect));
    struct yetty_ycore_void_result clip_update_res =
        yetty_ydraw_drawable_list_add_cmd_update(list, tree_node_id(obj), payload, sizeof(payload));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clip_update_res, "ygui2 clip: update");
    return yetty_ygui2_widget_set_emitted_clip(obj, widget_w, widget_h);
}

/* Incremental walk: smallest sufficient operation per dirty widget.
 * STRUCTURE dirt (membership changed) re-inserts the minted widget whole
 * via the addressed reopen (CMD_PATH + GROUP at its live path). SKIN dirt
 * reopens only the widget's skin subgroup — children, hosted complex
 * runtimes included, stay live. A moved widget gets one offset update. */
static struct yetty_ycore_void_result emit_incremental(struct yetty_ydraw_drawable_list *list,
                                                       struct yetty_yclass_object *obj)
{
    if (!tree_flag(yetty_ygui2_widget_is_visible(obj))) {
        clear_dirty_subtree(obj);
        return YETTY_OK_VOID();
    }
    if (!tree_flag(yetty_ygui2_widget_is_transparent(obj))) {
        if (subtree_needs_reinsert(obj)) {
            uint32_t path_ids[YGUI2_PATH_MAX];
            uint32_t path_count = widget_wire_path(obj, path_ids);
            if (path_count == YGUI2_PATH_OVERFLOW) {
                return YETTY_ERR(yetty_ycore_void,
                                 "ygui2 incremental: widget path exceeds wire depth");
            }
            if (path_count > 0) {
                struct yetty_ycore_void_result path_res =
                    yetty_ydraw_drawable_list_add_cmd_path(list, path_ids, path_count);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, path_res, "ygui2 incremental: path");
            }
            /* The reopen destroys and recreates every descendant group
             * with DEFAULT state — the emitted-state caches describe the
             * dead instances. Reset them so the offsets/clip pass below
             * re-sends all non-default projection state. */
            invalidate_emitted_subtree(obj);
            struct yetty_ycore_void_result reinsert_res = insert_subtree(list, obj);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, reinsert_res, "ygui2 incremental: reopen");
            struct yetty_ycore_void_result offsets_res = emit_offsets_pass(list, obj);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, offsets_res, "ygui2 incremental: offsets");
            return YETTY_OK_VOID();
        }
        float origin_x = 0.0f;
        float origin_y = 0.0f;
        widget_minted_parent_origin(obj, &origin_x, &origin_y);
        float widget_x = 0.0f;
        float widget_y = 0.0f;
        tree_rect(obj, &widget_x, &widget_y, NULL, NULL);
        float emitted_x = 0.0f;
        float emitted_y = 0.0f;
        int ever_emitted = 0;
        struct yetty_ycore_void_result emitted_res =
            yetty_ygui2_widget_emitted_offset(obj, &emitted_x, &emitted_y, &ever_emitted);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, emitted_res, "ygui2 incremental: emitted");
        float offset_x = widget_x - origin_x;
        float offset_y = widget_y - origin_y;
        if (!ever_emitted || offset_x != emitted_x || offset_y != emitted_y) {
            struct yetty_ycore_void_result move_res = emit_offset_update(list, obj);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, move_res, "ygui2 incremental: move");
        }
        int skin = 0;
        struct yetty_ycore_void_result flags_res =
            yetty_ygui2_widget_dirty_flags(obj, &skin, NULL, NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flags_res, "ygui2 incremental: flags");
        if (skin) {
            struct yetty_ycore_void_result skin_res = emit_skin_reopen(list, obj);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, skin_res, "ygui2 incremental: skin");
        }
        struct yetty_ycore_void_result clip_res = emit_clip_if_changed(list, obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, clip_res, "ygui2 incremental: clip");
        /* Size changed: let the widget follow up with its addressed
         * geometry op (plot → one ~28-byte update; the receiver re-plans
         * the runtime and its chrome locally). Rides THIS envelope —
         * never a structural re-send of retained content. */
        int geometry = 0;
        struct yetty_ycore_void_result geometry_flag_res =
            yetty_ygui2_widget_geometry_dirty(obj, &geometry);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, geometry_flag_res, "ygui2 incremental: geometry");
        if (geometry) {
            struct yetty_ycore_void_result geometry_res =
                yetty_ygui2_widget_emit_geometry(obj, list);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, geometry_res, "ygui2 incremental: geometry op");
        }
        /* Skin, position and geometry dirt are consumed by the operations
         * above (structure dirt is impossible in this branch); clear so a
         * handled widget cannot hold is_dirty true forever. */
        struct yetty_ycore_void_result clear_res = yetty_ygui2_widget_clear_dirty(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_res, "ygui2 incremental: clear");
    } else {
        /* Transparent layout nodes carry relayout dirt (layout_set /
         * absolute transitions) that the surrounding walk has consumed
         * through rect deltas — clear it the same way. */
        int transparent_structure = 0;
        struct yetty_ycore_void_result flags_res =
            yetty_ygui2_widget_dirty_flags(obj, NULL, &transparent_structure, NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flags_res, "ygui2 incremental: flags");
        if (!transparent_structure) {
            struct yetty_ycore_void_result clear_res = yetty_ygui2_widget_clear_dirty(obj);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_res, "ygui2 incremental: clear");
        }
    }
    for (struct yetty_yclass_object *child = tree_first_child(obj); child;
         child = tree_next_sibling(child)) {
        struct yetty_ycore_void_result child_walk_res = emit_incremental(list, child);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, child_walk_res, "ygui2 incremental: child");
    }
    return YETTY_OK_VOID();
}

/* Ship SEVERAL addressed updates for one node inside `widget_obj`'s
 * group in ONE envelope: (CMD_PATH + UPDATE(child_id)) per payload. The
 * low-bandwidth streaming primitive — a ring append is a tiny sample
 * chunk plus a ring-head op, ~40 bytes total, one envelope. */
struct yetty_ycore_void_result yetty_ygui2_framework_stream_update_batch(
    struct yetty_yclass_object *widget_obj, uint32_t child_node_id, const void *const *payloads,
    const size_t *payload_sizes, uint32_t payload_count);

/* Ship ONE addressed update for a node inside `widget_obj`'s group:
 * CMD_PATH(widget's full path incl. its own group) + UPDATE(child_id).
 * Its own tiny envelope — the streaming path (complex data without any
 * repaint). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_stream_update(
    struct yetty_yclass_object *widget_obj, uint32_t child_node_id, const void *payload,
    size_t payload_size)
{
    return yetty_ygui2_framework_stream_update_batch(widget_obj, child_node_id, &payload,
                                                     &payload_size, 1u);
}

/* Append ONE addressed update for a node inside `widget_obj`'s group into
 * a CALLER-OWNED list (no ship): CMD_PATH(widget path incl. its own
 * group) + UPDATE(child_id). The building block for per-widget follow-ups
 * that must ride the frame envelope being assembled (widget_emit_geometry
 * — the framework's own list is in use during emit, so the streaming path
 * above cannot be taken). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_append_addressed_update(
    struct yetty_yclass_object *widget_obj, struct yetty_ydraw_drawable_list *list,
    uint32_t child_node_id, const void *payload, size_t payload_size)
{
    if (!list || !payload || payload_size == 0u) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 addressed update: bad arguments");
    }
    uint32_t path_ids[YGUI2_PATH_MAX];
    uint32_t path_count = widget_wire_path(widget_obj, path_ids);
    if (path_count == YGUI2_PATH_OVERFLOW) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 addressed update: path exceeds wire depth");
    }
    if (!tree_flag(yetty_ygui2_widget_is_transparent(widget_obj))) {
        /* Retained nodes live DIRECTLY in the containment group — descend
         * the widget id only, never the skin subgroup. */
        if (path_count >= YGUI2_PATH_MAX) {
            return YETTY_ERR(yetty_ycore_void, "ygui2 addressed update: path exceeds wire depth");
        }
        path_ids[path_count++] = tree_node_id(widget_obj);
    }
    if (path_count > 0) {
        struct yetty_ycore_void_result path_res =
            yetty_ydraw_drawable_list_add_cmd_path(list, path_ids, path_count);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, path_res, "ygui2 addressed update: path");
    }
    return yetty_ydraw_drawable_list_add_cmd_update(list, child_node_id, payload, payload_size);
}

struct yetty_ycore_void_result yetty_ygui2_framework_stream_update_batch(
    struct yetty_yclass_object *widget_obj, uint32_t child_node_id, const void *const *payloads,
    const size_t *payload_sizes, uint32_t payload_count)
{
    struct yetty_yclass_object_ptr_result framework_obj_res =
        yetty_ygui2_widget_framework_obj(widget_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_obj_res, "ygui2 stream: framework obj");
    struct yetty_ygui2_framework_ptr_result data_res =
        yetty_ygui2_framework_from(framework_obj_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 stream: data");
    struct yetty_ygui2_framework *framework = data_res.value;
    if (!framework->inserted) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 stream: nothing inserted yet");
    }
    if (!framework->list) {
        struct yetty_ydraw_drawable_list_result list_res =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "ygui2 stream: list create");
        framework->list = list_res.value;
    } else {
        yetty_ydraw_drawable_list_clear(framework->list);
    }
    uint32_t path_ids[YGUI2_PATH_MAX];
    uint32_t path_count = widget_wire_path(widget_obj, path_ids);
    if (path_count == YGUI2_PATH_OVERFLOW) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 stream: widget path exceeds wire depth");
    }
    if (!tree_flag(yetty_ygui2_widget_is_transparent(widget_obj))) {
        /* Retained nodes are emitted DIRECTLY in the containment group
         * (widget_paint_retained), so the address descends the widget id
         * only — never the skin subgroup. Never truncate an address. */
        if (path_count >= YGUI2_PATH_MAX) {
            return YETTY_ERR(yetty_ycore_void, "ygui2 stream: widget path exceeds wire depth");
        }
        path_ids[path_count++] = tree_node_id(widget_obj);
    }
    if (!payloads || !payload_sizes || payload_count == 0u) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 stream: no payloads");
    }
    for (uint32_t payload_index = 0; payload_index < payload_count; payload_index++) {
        /* CMD_PATH is a one-shot latch consumed by the following command
         * — re-emit it before every update in the batch. */
        if (path_count > 0) {
            struct yetty_ycore_void_result path_res =
                yetty_ydraw_drawable_list_add_cmd_path(framework->list, path_ids, path_count);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, path_res, "ygui2 stream: path");
        }
        struct yetty_ycore_void_result update_res = yetty_ydraw_drawable_list_add_cmd_update(
            framework->list, child_node_id, payloads[payload_index], payload_sizes[payload_index]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, update_res, "ygui2 stream: update");
    }
    struct yetty_ycore_void_result ship_res = framework_ship(framework);
    if (YETTY_IS_ERR(ship_res)) {
        /* An uncertain/failed stream ship gets the SAME recovery as a
         * failed frame: schedule the full projection rebuild. The next
         * emit deletes + re-inserts, and retained widgets replay their
         * cached state inside that insertion — producer and receiver
         * converge without the app doing anything. */
        if (framework->root) {
            restyle_subtree(framework->root);
        }
        if (framework->overlay_root) {
            restyle_subtree(framework->overlay_root);
        }
        if (framework->inserted) {
            framework->rebuild_pending = 1;
        }
        return YETTY_ERR(yetty_ycore_void, "ygui2 stream: ship", ship_res);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result framework_ship(struct yetty_ygui2_framework *framework)
{
    const uint8_t *raw = NULL;
    size_t raw_size = yetty_ydraw_drawable_list_serialize(framework->list, &raw);
    if (raw_size <= YETTY_YDRAW_SERIAL_HEADER_BYTES || !raw) {
        return YETTY_OK_VOID(); /* clean frame: only the stream header — ship nothing */
    }
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result emit_res = yetty_yface_emit(
        YETTY_DCS_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size, &envelope);
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_void, "ygui2 ship: yface_emit", emit_res);
    }
    if (framework->sink) {
        framework->sink(envelope.data, envelope.size, framework->sink_userdata);
    } else if (framework->write_fd_valid) {
        struct yetty_ycore_void_result write_res =
            yetty_yplatform_io_write_all(framework->write_fd, envelope.data, envelope.size);
        if (YETTY_IS_ERR(write_res)) {
            yetty_ycore_buffer_destroy(&envelope);
            return YETTY_ERR(yetty_ycore_void, "ygui2 ship: short write", write_res);
        }
    }
    yetty_ycore_buffer_destroy(&envelope);
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_framework_emit(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_framework_ptr_result data_res = yetty_ygui2_framework_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 emit: data");
    struct yetty_ygui2_framework *framework = data_res.value;
    framework_reap_orphans(framework);
    if (!framework->root) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 emit: no root");
    }
    if (!framework->viewport_valid) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 emit: no viewport");
    }
    if (!framework->list) {
        struct yetty_ydraw_drawable_list_result list_res =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "ygui2 emit: list create");
        framework->list = list_res.value;
    } else {
        yetty_ydraw_drawable_list_clear(framework->list);
    }
    layout_widget(framework->root, 0.0f, 0.0f, framework->viewport_w, framework->viewport_h);
    if (framework->overlay_root) {
        layout_widget(framework->overlay_root, 0.0f, 0.0f, framework->viewport_w,
                      framework->viewport_h);
    }
    framework->viewport_dirty = 0; /* consumed by this layout pass */
    if (framework->rebuild_pending) {
        if (framework->inserted) {
            /* Projection rebuild (viewport change / ship-failure recovery):
             * a reopen cannot change the reserved span, so delete the live
             * roots first — the fresh insertion below re-reserves. */
            yetty_ydraw_drawable_list_clear(framework->list);
            struct yetty_ycore_void_result delete_res =
                yetty_ydraw_drawable_list_add_cmd_delete(framework->list, 1u);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, delete_res, "ygui2 rebuild: delete root");
            struct yetty_ycore_void_result overlay_delete_res =
                yetty_ydraw_drawable_list_add_cmd_delete(framework->list, 2u);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, overlay_delete_res,
                                "ygui2 rebuild: delete overlay");
            struct yetty_ycore_void_result delete_ship_res = framework_ship(framework);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, delete_ship_res, "ygui2 rebuild: ship");
            framework_invalidate_projection(framework);
            yetty_ydraw_drawable_list_clear(framework->list);
        }
        framework->rebuild_pending = 0;
    }
    int first_frame = !framework->inserted;
    if (first_frame) {
        if (framework->home_before_insert) {
            /* After a fullscreen insertion the cursor sits at the bottom
             * of the reservation; a reinsert anchored there would land at
             * the wrong row. Home it first (same terminal stream). */
            struct yetty_ycore_void_result home_res = framework_write_bytes(framework, "\x1b[H", 3);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, home_res, "ygui2 emit: cursor home write");
            framework->home_before_insert = 0;
        }
        /* FULLSCREEN reserves the FULL supported viewport range up
         * front (the terminal accepts reserves to 16 Mpx; an alt-screen
         * app's span is bookkeeping, not storage) — that is what makes
         * every accepted resize unconditionally non-destructive: any
         * height set_viewport can accept is inside the immutable span,
         * so no resize can ever need a re-insertion and no hosted
         * runtime can be lost to one. INLINE reserves the declared
         * content height only (strategy.md §5): the insertion sits in
         * the user's scrollback flow and the reserve advance really
         * scrolls, so over-reserving would blast the transcript. */
        float reserve_h =
            framework->fullscreen ? (float)YGUI2_VIEWPORT_MAX_PX : framework->viewport_h;
        struct yetty_ycore_void_result reserve_res =
            yetty_ydraw_drawable_list_add_cmd_reserve(framework->list, (uint32_t)ceilf(reserve_h));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, reserve_res, "ygui2 emit: reserve");
        framework->reserved_h = reserve_h;
        struct yetty_ycore_void_result insert_res =
            insert_subtree(framework->list, framework->root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, insert_res, "ygui2 emit: insert");
        if (framework->overlay_root) {
            /* The overlay tree paints in its own ambient z band, above any
             * app-tree primitive (and any hosted record below the band). */
            struct yetty_ycore_void_result band_res =
                yetty_ydraw_drawable_list_add_cmd_paint_z(framework->list, YGUI2_OVERLAY_PAINT_Z);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, band_res, "ygui2 emit: overlay band");
            struct yetty_ycore_void_result overlay_insert_res =
                insert_subtree(framework->list, framework->overlay_root);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, overlay_insert_res, "ygui2 emit: overlay insert");
            struct yetty_ycore_void_result band_end_res =
                yetty_ydraw_drawable_list_add_cmd_paint_z_end(framework->list);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, band_end_res, "ygui2 emit: overlay band end");
        }
        struct yetty_ycore_void_result offsets_res =
            emit_offsets_pass(framework->list, framework->root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, offsets_res, "ygui2 emit: offsets");
        if (framework->overlay_root) {
            struct yetty_ycore_void_result overlay_offsets_res =
                emit_offsets_pass(framework->list, framework->overlay_root);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, overlay_offsets_res,
                                "ygui2 emit: overlay offsets");
        }
    } else {
        struct yetty_ycore_void_result walk_res =
            emit_incremental(framework->list, framework->root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, walk_res, "ygui2 emit: incremental");
        if (framework->overlay_root && widget_tree_dirty(framework->overlay_root)) {
            struct yetty_ycore_void_result band_res =
                yetty_ydraw_drawable_list_add_cmd_paint_z(framework->list, YGUI2_OVERLAY_PAINT_Z);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, band_res, "ygui2 emit: overlay band");
            struct yetty_ycore_void_result overlay_walk_res =
                emit_incremental(framework->list, framework->overlay_root);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, overlay_walk_res,
                                "ygui2 emit: overlay incremental");
            struct yetty_ycore_void_result band_end_res =
                yetty_ydraw_drawable_list_add_cmd_paint_z_end(framework->list);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, band_end_res, "ygui2 emit: overlay band end");
        } else if (framework->overlay_root) {
            struct yetty_ycore_void_result overlay_walk_res =
                emit_incremental(framework->list, framework->overlay_root);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, overlay_walk_res,
                                "ygui2 emit: overlay incremental");
        }
    }
    struct yetty_ycore_void_result ship_res = framework_ship(framework);
    if (YETTY_IS_ERR(ship_res)) {
        /* The frame's arrival is unknown and per-widget dirt was consumed
         * while building: recover by scheduling a FULL rebuild — the next
         * emit deletes both roots (idempotent if they never landed) and
         * re-inserts everything. Restyle so is_dirty stays true. */
        if (framework->root) {
            restyle_subtree(framework->root);
        }
        if (framework->overlay_root) {
            restyle_subtree(framework->overlay_root);
        }
        if (!first_frame) {
            framework->rebuild_pending = 1;
        } else {
            framework->home_before_insert = framework->fullscreen ? 1 : 0;
        }
        return YETTY_ERR(yetty_ycore_void, "ygui2 emit: ship", ship_res);
    }
    if (first_frame) {
        framework->inserted = 1;
    }
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui2/framework.c"
