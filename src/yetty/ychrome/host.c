/*
 * host.c — reusable window-chrome glue (see host.h). Plain C, no yclass
 * annotations: it wires a ychrome:chrome engine to a backdrop + caption that
 * composite the chrome's self-painted bar over an app's content. Extracted
 * from the ygui demo runner so every app shares one integration instead of
 * copy-pasting it.
 *
 * Two sinks, one engine:
 *   - LOCAL (standalone): the backdrop + caption are pinned scene figures
 *     owned by a yfigure_container the app renders itself.
 *   - WIRE (client / in-terminal): the same two figures are driven on the
 *     hosting yetty's root figure container PROXY via the generated typed
 *     yclass-RPC stubs (yetty_yfigure_create_child / set_child_z /
 *     delete_child). Each stub is one-way fire-and-forget and flushes its
 *     request synchronously, so the chrome renders in the host's pane with
 *     no per-app boilerplate. The owner attaches the session and
 *     hands this host the container proxy + session.
 *
 * The backdrop is an opaque BRAND_BG surface beneath all app content. In
 * client mode it is what hides the host terminal's text under the app; in
 * standalone it is harmless (the per-frame window clear already covers the
 * canvas).
 */
#include <yetty/ychrome/host.h>

#include "yetty/gen/impl/ychrome/chrome.h"
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/api/yfigure/container.h>
#include <yetty/api/yfigure/figure.h>
#include <yetty/yevent/event.h> /* yetty_yui_event + mouse-event kinds */
#include <yetty/yfigure/kind.h>
#ifdef YETTY_YCHROME_HAS_LOCAL
/* yscene is GPU-backed (composites through a pipeline). Only the LOCAL sink —
 * pinned scene figures the app renders itself — needs it. The WIRE sink emits
 * figure-tree records over a producer and is GPU-free, so the headless
 * no-WebGPU cross targets (riscv guest) build the wire path without yscene. */
#include <yetty/api/yscene/scene.h>
#endif
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <stdlib.h>

/* Backdrop fill — opaque BRAND_BG (#0B1014), packed 0xAABBGGRR (the encoding
 * ydraw uses). The chrome owns this so every app that mounts it gets an opaque
 * surface behind its content. */
#define YCHROME_BACKDROP_COLOR 0xFF14100Bu

enum {
    /* Stacking order: the backdrop sits far below every app figure; the
     * caption sits far above so the window controls are never occluded. */
    YCHROME_BACKDROP_Z = -1000000,
    YCHROME_CAPTION_Z = 100000,
};

/* Fixed child ids for the local-sink figures (container add_child). */
#define YCHROME_LOCAL_BACKDROP_ID 0x7FFF0000u
#define YCHROME_LOCAL_CAPTION_ID 0x7FFF0001u
/* Fixed child ids for the wire-sink figures. A distinct high range so they
 * never collide with a ygui framework's id space on the same pane (widget ids
 * climb from 1; its chrome surface is 0xFE000001). */
#define YCHROME_WIRE_BACKDROP_ID 0x7FFE0001u
#define YCHROME_WIRE_CAPTION_ID 0x7FFE0002u

struct yetty_ychrome_host {
    struct yetty_yclass_object *chrome; /* ychrome:chrome engine */

#ifdef YETTY_YCHROME_HAS_LOCAL
    /* LOCAL sink — pinned figures owned by the app's container (NULL in wire
     * mode). Only built when yscene is available (GPU targets). */
    struct yetty_yscene_scene *caption;
    struct yetty_yscene_scene *backdrop;
#endif

    /* WIRE sink — figures driven on the host's root container proxy via the
     * typed yclass-RPC stubs (NULL in local mode). The owner navigates to the
     * container proxy and passes it in; it is borrowed (the owner keeps
     * ownership and disconnects the session after this host is destroyed). */
    struct yetty_yclass_object *container; /* root container proxy */

    /* Window size, in LOGICAL px (framebuffer / content_scale). Callers pass
     * framebuffer values on create / resized; the host divides once here so
     * the wrapped chrome engine and the pinned figure rects stay in a single
     * coordinate system that the receiving yscene then multiplies back up for
     * display (view_scale on absolute-coords figures). */
    float width;
    float height;
    float caption_height;
    /* HiDPI factor captured at create time — 1.0 on non-HiDPI. Used to
     * convert framebuffer inputs (dimensions, mouse events) to logical px
     * for the chrome engine and the figure rects. */
    float content_scale;
    int last_hover; /* last hovered control — re-paint the caption when it changes */
};

/* Scale a framebuffer-pixel dimension to logical px using the host's captured
 * content_scale. A non-positive scale (should not happen for a validated host)
 * falls back to 1.0 so callers still get sane numbers. */
static float host_to_logical(const struct yetty_ychrome_host *host, float value_fb)
{
    float scale = host->content_scale > 0.0f ? host->content_scale : 1.0f;
    return value_fb / scale;
}

#ifdef YETTY_YCHROME_HAS_LOCAL
/* The figure's yclass object sits one slot before the figure pointer (the data
 * slice follows the object header) — same accessor the runner/figure code uses. */
static struct yetty_yclass_object *scene_figure_obj(struct yetty_yscene_scene *scene)
{
    struct yetty_yfigure_figure *figure = yetty_yscene_as_figure(scene);
    return (struct yetty_yclass_object *)(figure)-1;
}
#endif

/*===========================================================================
 * Backdrop / caption content.
 *=========================================================================*/

/* A single opaque box spanning (width × height) — the backdrop fill. */
static struct yetty_ydraw_drawable_list_result host_make_backdrop_list(float width, float height)
{
    struct yetty_ydraw_drawable_list_config config = {
        .scene_min_x = 0.0f, .scene_min_y = 0.0f, .scene_max_x = width, .scene_max_y = height};
    struct yetty_ydraw_drawable_list_result list_result =
        yetty_ydraw_drawable_list_config_buffer_create(&config);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, list_result,
                        "chrome host backdrop: list create");
    struct yetty_ydraw_drawable_list *list = list_result.value;
    struct yetty_ysdf_box box = {width * 0.5f, height * 0.5f, width * 0.5f, height * 0.5f, 0.0f};
    struct yetty_ycore_void_result add_box_result = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, 0, 0, YCHROME_BACKDROP_COLOR, 0u, 0.0f, &box);
    if (YETTY_IS_ERR(add_box_result)) {
        yetty_ydraw_drawable_list_destroy(list);
        return YETTY_ERR(yetty_ydraw_drawable_list, "chrome host backdrop: add_box",
                         add_box_result);
    }
    return YETTY_OK(yetty_ydraw_drawable_list, list);
}

/*===========================================================================
 * LOCAL sink. Pinned scene figures — GPU-backed, so built only on targets
 * that have yscene (YETTY_YCHROME_HAS_LOCAL).
 *=========================================================================*/
#ifdef YETTY_YCHROME_HAS_LOCAL

/* Reset a pinned figure's content and load a fresh record stream into it. */
static struct yetty_ycore_void_result host_local_load(struct yetty_yclass_object *figure_obj,
                                                      const uint8_t *data, size_t size)
{
    struct yetty_ycore_void_result reset_result = yetty_yfigure_reset_content(figure_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, reset_result, "chrome host load: reset_content");
    if (data && size > 0) {
        struct yetty_ycore_void_result process_result =
            yetty_yfigure_process_bytes(figure_obj, data, size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, process_result, "chrome host load: process_bytes");
    }
    struct yetty_ycore_void_result dirty_result = yetty_yfigure_figure_dirty_set(figure_obj, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dirty_result, "chrome host load: dirty_set");
    return YETTY_OK_VOID();
}

/* Create a pinned scene figure under `container`, load `body` into it, set its
 * z + absolute-coords flag, and add it as `id`. */
static struct yetty_yscene_scene_ptr_result host_pin_scene(
    struct yetty_yclass_object *container_obj, const struct yetty_context *ctx,
    struct yetty_yfont_font *font, struct yetty_ycore_rectangle rect, const uint8_t *body,
    size_t body_len, int z, uint32_t id)
{
    struct yetty_yscene_scene_ptr_result scene_result = yetty_yscene_create(rect, ctx);
    YETTY_RETURN_IF_ERR(yetty_yscene_scene_ptr, scene_result, "chrome host pin: scene create");
    struct yetty_yscene_scene *scene = scene_result.value;
    struct yetty_yclass_object *figure_obj = scene_figure_obj(scene);
    if (font) {
        struct yetty_ycore_void_result font_result =
            yetty_yscene_set_default_font(figure_obj, font);
        YETTY_RETURN_IF_ERR(yetty_yscene_scene_ptr, font_result, "chrome host pin: set_font");
    }
    struct yetty_ycore_void_result absolute_result =
        yetty_yfigure_figure_absolute_coords_set(figure_obj, 1);
    YETTY_RETURN_IF_ERR(yetty_yscene_scene_ptr, absolute_result,
                        "chrome host pin: absolute_coords");
    struct yetty_ycore_void_result z_result = yetty_yfigure_figure_z_set(figure_obj, z);
    YETTY_RETURN_IF_ERR(yetty_yscene_scene_ptr, z_result, "chrome host pin: z_set");
    if (body && body_len > 0) {
        struct yetty_ycore_void_result load_result = host_local_load(figure_obj, body, body_len);
        YETTY_RETURN_IF_ERR(yetty_yscene_scene_ptr, load_result, "chrome host pin: load");
    }
    struct yetty_ycore_void_result add_result =
        yetty_yfigure_container_add_child(container_obj, yetty_yscene_as_figure(scene), id);
    YETTY_RETURN_IF_ERR(yetty_yscene_scene_ptr, add_result, "chrome host pin: add_child");
    return YETTY_OK(yetty_yscene_scene_ptr, scene);
}

#endif /* YETTY_YCHROME_HAS_LOCAL */

/*===========================================================================
 * WIRE sink.
 *=========================================================================*/

/* (Re)create one of the chrome figures on the far end via the typed stubs. A
 * CREATE_CHILD on an existing id re-mints the figure with fresh content, so
 * this doubles as the refresh path. The stubs are one-way and flush their
 * request synchronously, so there is no separate flush step. */
static struct yetty_ycore_void_result host_wire_emit(struct yetty_ychrome_host *host, uint32_t id,
                                                     int z, struct yetty_ycore_rectangle rect,
                                                     const uint8_t *body, uint32_t body_len)
{
    /* The create_child stub copies the init bytes synchronously; point at a
     * valid (unused) address rather than NULL for an empty body so its
     * memcpy(dst, src, 0) never gets a NULL source. */
    uint8_t empty_body = 0;
    struct yetty_ycore_buffer init = {
        .data = body ? (uint8_t *)body : &empty_body, .capacity = 0, .size = body_len};
    struct yetty_ycore_void_result create_result = yetty_yfigure_create_child(
        host->container, yetty_yfigure_kind_token("ygrid"), id, rect, init);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, create_result, "chrome host wire: create_child");
    struct yetty_ycore_void_result z_result = yetty_yfigure_set_child_z(host->container, id, z);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, z_result, "chrome host wire: set_child_z");
    /* Chrome figures are INPUT-PASSTHROUGH: their interactivity (caption
     * drag / window buttons) arrives via the chrome engine's raw event
     * feed — taking container hits would steal clicks from the app figure
     * beneath (the topmost-wins hit test put the caption strip exactly
     * over ygreeter's tabs). Void one-way call, safe on a pipelined
     * producer session. */
    struct yetty_ycore_void_result passthrough_result =
        yetty_yfigure_set_child_input_passthrough(host->container, id, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, passthrough_result,
                        "chrome host wire: set_child_input_passthrough");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result host_wire_backdrop_emit(struct yetty_ychrome_host *host)
{
    struct yetty_ydraw_drawable_list_result list_result =
        host_make_backdrop_list(host->width, host->height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_result, "chrome host wire backdrop: list");
    struct yetty_ydraw_drawable_list *list = list_result.value;
    struct yetty_ycore_rectangle rect = {.min = {0.0f, 0.0f}, .max = {host->width, host->height}};
    struct yetty_ycore_void_result emit_result =
        host_wire_emit(host, YCHROME_WIRE_BACKDROP_ID, YCHROME_BACKDROP_Z, rect,
                       (const uint8_t *)yetty_ydraw_drawable_list_data(list),
                       (uint32_t)yetty_ydraw_drawable_list_size(list));
    yetty_ydraw_drawable_list_destroy(list);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_result, "chrome host wire backdrop: emit");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result host_wire_caption_emit(struct yetty_ychrome_host *host)
{
    struct yetty_ydraw_drawable_list_result render_result = yetty_ychrome_render(host->chrome);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_result, "chrome host wire caption: render");
    struct yetty_ydraw_drawable_list *list = render_result.value;
    struct yetty_ycore_rectangle rect = {.min = {0.0f, 0.0f},
                                         .max = {host->width, host->caption_height}};
    struct yetty_ycore_void_result emit_result =
        host_wire_emit(host, YCHROME_WIRE_CAPTION_ID, YCHROME_CAPTION_Z, rect,
                       (const uint8_t *)yetty_ydraw_drawable_list_data(list),
                       (uint32_t)yetty_ydraw_drawable_list_size(list));
    yetty_ydraw_drawable_list_destroy(list);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_result, "chrome host wire caption: emit");
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Caption repaint — dispatch on sink.
 *=========================================================================*/

static struct yetty_ycore_void_result host_caption_refresh(struct yetty_ychrome_host *host)
{
    if (host->container) {
        return host_wire_caption_emit(host);
    }
#ifdef YETTY_YCHROME_HAS_LOCAL
    /* LOCAL: ychrome renders its caption to a drawable list (pure ydraw); load
     * that record stream into the pinned scene figure. */
    struct yetty_ydraw_drawable_list_result render_result = yetty_ychrome_render(host->chrome);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_result, "chrome host caption refresh: render");
    struct yetty_ydraw_drawable_list *list = render_result.value;
    struct yetty_ycore_void_result load_result = host_local_load(
        scene_figure_obj(host->caption), (const uint8_t *)yetty_ydraw_drawable_list_data(list),
        yetty_ydraw_drawable_list_size(list));
    yetty_ydraw_drawable_list_destroy(list);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, load_result, "chrome host caption refresh: load");
    return YETTY_OK_VOID();
#else
    /* Wire-only build: a host with no container proxy has no sink. Unreachable
     * in practice (client hosts always carry a container proxy). */
    return YETTY_ERR(yetty_ycore_void, "chrome host caption refresh: no sink (wire-only build)");
#endif
}

/*===========================================================================
 * Engine setup shared by both create paths.
 *=========================================================================*/

static struct yetty_ychrome_host_ptr_result host_alloc(struct yetty_yclass_object *window_chrome,
                                                       float width_fb, float height_fb,
                                                       float content_scale, float caption_height,
                                                       float edge_size, unsigned int flags)
{
    struct yetty_ycore_void_result register_result = yetty_ychrome_register();
    YETTY_RETURN_IF_ERR(yetty_ychrome_host_ptr, register_result, "ychrome_host: register");

    struct yetty_yclass_object_ptr_result chrome_result = yetty_ychrome_chrome_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ychrome_host_ptr, chrome_result, "ychrome_host: chrome create");

    struct yetty_ychrome_host *host = calloc(1, sizeof(*host));
    if (!host) {
        struct yetty_ycore_void_result destroy_result = yetty_ychrome_destroy(chrome_result.value);
        if (YETTY_IS_ERR(destroy_result)) {
            return YETTY_ERR(yetty_ychrome_host_ptr, "ychrome_host: alloc (and chrome teardown)",
                             destroy_result);
        }
        return YETTY_ERR(yetty_ychrome_host_ptr, "ychrome_host: alloc");
    }
    host->chrome = chrome_result.value;
    host->content_scale = content_scale > 0.0f ? content_scale : 1.0f;
    host->width = host_to_logical(host, width_fb);
    host->height = host_to_logical(host, height_fb);
    host->caption_height = caption_height;

    struct yetty_ycore_void_result configure_result =
        yetty_ychrome_configure(host->chrome, window_chrome, caption_height, edge_size, flags);
    if (YETTY_IS_ERR(configure_result)) {
        struct yetty_ycore_void_result destroy_result = yetty_ychrome_destroy(host->chrome);
        free(host);
        struct yetty_ycore_void_result chained =
            yetty_ycore_void_chain(configure_result, destroy_result);
        return YETTY_ERR(yetty_ychrome_host_ptr, "ychrome_host: configure", chained);
    }
    struct yetty_ycore_void_result size_result =
        yetty_ychrome_set_size(host->chrome, host->width, host->height);
    if (YETTY_IS_ERR(size_result)) {
        struct yetty_ycore_void_result destroy_result = yetty_ychrome_destroy(host->chrome);
        free(host);
        struct yetty_ycore_void_result chained =
            yetty_ycore_void_chain(size_result, destroy_result);
        return YETTY_ERR(yetty_ychrome_host_ptr, "ychrome_host: set_size", chained);
    }
    return YETTY_OK(yetty_ychrome_host_ptr, host);
}

/* Tear down a partially-built host and wrap `cause` (the failure that aborted
 * the build) into a host-typed error, chaining the chrome teardown under it. */
static struct yetty_ychrome_host_ptr_result host_build_fail(struct yetty_ychrome_host *host,
                                                            const char *message,
                                                            struct yetty_ycore_void_result cause)
{
    struct yetty_ycore_void_result chrome_destroy_result = yetty_ychrome_destroy(host->chrome);
    struct yetty_ycore_void_result chained = yetty_ycore_void_chain(cause, chrome_destroy_result);
    free(host);
    return YETTY_ERR(yetty_ychrome_host_ptr, message, chained);
}

/*===========================================================================
 * Public API.
 *=========================================================================*/

#ifdef YETTY_YCHROME_HAS_LOCAL
struct yetty_ychrome_host_ptr_result yetty_ychrome_host_create(
    struct yetty_yclass_object *container_obj, struct yetty_yfont_font *font,
    const struct yetty_context *ctx, struct yetty_yclass_object *window_chrome, float width,
    float height, float content_scale, float caption_height, float edge_size, unsigned int flags)
{
    if (!container_obj) {
        return YETTY_ERR(yetty_ychrome_host_ptr, "ychrome_host_create: container required");
    }
    struct yetty_ychrome_host_ptr_result alloc_result =
        host_alloc(window_chrome, width, height, content_scale, caption_height, edge_size, flags);
    YETTY_RETURN_IF_ERR(yetty_ychrome_host_ptr, alloc_result, "ychrome_host_create");
    struct yetty_ychrome_host *host = alloc_result.value;

    /* NO backdrop in the local path. The local chrome (yui's window chrome) is
     * an OVERLAY composited on top of the live terminals in the same container;
     * an opaque fill here would hide them. A backdrop only makes sense when the
     * chrome's container IS the app's whole content surface — that's the wire /
     * in-terminal case, handled in yetty_ychrome_host_create_wire. */

    /* Caption — empty for now; host_caption_refresh paints it with hover state.
     * Rect is in LOGICAL px (matches chrome's authoring space); the receiving
     * yscene scales absolute-coords figures by content_scale for display. */
    struct yetty_ycore_rectangle caption_rect = {.min = {0.0f, 0.0f},
                                                 .max = {host->width, host->caption_height}};
    struct yetty_yscene_scene_ptr_result caption_pin =
        host_pin_scene(container_obj, ctx, font, caption_rect, NULL, 0, YCHROME_CAPTION_Z,
                       YCHROME_LOCAL_CAPTION_ID);
    if (YETTY_IS_ERR(caption_pin)) {
        return host_build_fail(host, "ychrome_host_create: caption pin",
                               YETTY_ERR(yetty_ycore_void, "caption pin", caption_pin));
    }
    host->caption = caption_pin.value;

    struct yetty_ycore_void_result refresh_result = host_caption_refresh(host);
    if (YETTY_IS_ERR(refresh_result)) {
        return host_build_fail(host, "ychrome_host_create: caption refresh", refresh_result);
    }
    return YETTY_OK(yetty_ychrome_host_ptr, host);
}
#endif /* YETTY_YCHROME_HAS_LOCAL */

struct yetty_ychrome_host_ptr_result yetty_ychrome_host_create_wire(
    struct yetty_yclass_object *container, struct yetty_yclass_object *window_chrome, float width,
    float height, float content_scale, float caption_height, float edge_size, unsigned int flags)
{
    if (!container) {
        return YETTY_ERR(yetty_ychrome_host_ptr, "ychrome_host_create_wire: container required");
    }
    struct yetty_ychrome_host_ptr_result alloc_result =
        host_alloc(window_chrome, width, height, content_scale, caption_height, edge_size, flags);
    YETTY_RETURN_IF_ERR(yetty_ychrome_host_ptr, alloc_result, "ychrome_host_create_wire");
    struct yetty_ychrome_host *host = alloc_result.value;
    host->container = container;

    struct yetty_ycore_void_result backdrop_result = host_wire_backdrop_emit(host);
    if (YETTY_IS_ERR(backdrop_result)) {
        return host_build_fail(host, "ychrome_host_create_wire: backdrop emit", backdrop_result);
    }
    struct yetty_ycore_void_result caption_result = host_wire_caption_emit(host);
    if (YETTY_IS_ERR(caption_result)) {
        return host_build_fail(host, "ychrome_host_create_wire: caption emit", caption_result);
    }
    return YETTY_OK(yetty_ychrome_host_ptr, host);
}

struct yetty_ycore_void_result yetty_ychrome_host_resync(struct yetty_ychrome_host *host)
{
    if (!host || !host->container) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result result = host_wire_backdrop_emit(host);
    result = yetty_ycore_void_chain(result, host_wire_caption_emit(host));
    return result;
}

struct yetty_ycore_void_result yetty_ychrome_host_clear(struct yetty_ychrome_host *host)
{
    if (!host || !host->container) {
        return YETTY_OK_VOID();
    }
    /* Remove our two figures from the host pane via the typed delete_child
     * stubs. Each is one-way and flushes its request synchronously with a
     * blocking write to the session's write_fd — safe at process teardown
     * where an async event loop has already stopped. The owner detaches the
     * session AFTER this returns. */
    struct yetty_ycore_void_result result =
        yetty_yfigure_delete_child(host->container, YCHROME_WIRE_CAPTION_ID);
    result = yetty_ycore_void_chain(
        result, yetty_yfigure_delete_child(host->container, YCHROME_WIRE_BACKDROP_ID));
    return result;
}

struct yetty_yclass_object *yetty_ychrome_host_chrome(struct yetty_ychrome_host *host)
{
    return host ? host->chrome : NULL;
}

struct yetty_ycore_int_result yetty_ychrome_host_handle_event(struct yetty_ychrome_host *host,
                                                              const struct yetty_yui_event *event)
{
    if (!host) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    /* Chrome speaks LOGICAL px; callers hand us framebuffer-px events. Convert
     * mouse coordinates once here so chrome's hit-test matches its rendered
     * button footprint on HiDPI. Non-mouse events (RENDER, KEY_DOWN, RESIZE …)
     * carry no cursor position and pass through untouched. */
    const struct yetty_yui_event *forwarded = event;
    struct yetty_yui_event scaled;
    if (event && host->content_scale > 0.0f && host->content_scale != 1.0f) {
        switch (event->type) {
        case YETTY_YCORE_MOUSE_DOWN:
        case YETTY_YCORE_MOUSE_UP:
        case YETTY_YCORE_MOUSE_MOVE:
        case YETTY_YCORE_MOUSE_DRAG:
        case YETTY_YCORE_MOUSE_DOUBLE_CLICK:
            scaled = *event;
            scaled.mouse.x = event->mouse.x / host->content_scale;
            scaled.mouse.y = event->mouse.y / host->content_scale;
            forwarded = &scaled;
            break;
        default:
            break;
        }
    }
    struct yetty_ycore_int_result handle_result =
        yetty_ychrome_handle_event(host->chrome, forwarded);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, handle_result, "ychrome_host_handle_event: handle");
    int consumed = handle_result.value;

    /* Re-paint the caption when the hovered control changes so the highlight
     * follows the pointer. */
    struct yetty_ycore_int_result hover_result = yetty_ychrome_hover_button(host->chrome);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, hover_result, "ychrome_host_handle_event: hover");
    if (hover_result.value != host->last_hover) {
        host->last_hover = hover_result.value;
        struct yetty_ycore_void_result refresh_result = host_caption_refresh(host);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, refresh_result,
                            "ychrome_host_handle_event: caption refresh");
    }
    return YETTY_OK(yetty_ycore_int, consumed);
}

struct yetty_ycore_int_result yetty_ychrome_host_in_gesture(struct yetty_ychrome_host *host)
{
    if (!host) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    return yetty_ychrome_in_gesture(host->chrome);
}

struct yetty_ycore_void_result yetty_ychrome_host_resized(struct yetty_ychrome_host *host,
                                                          float width_fb, float height_fb)
{
    if (!host) {
        return YETTY_OK_VOID();
    }
    /* Framebuffer→logical at the boundary: chrome + the pinned figure rects
     * live in logical px. */
    host->width = host_to_logical(host, width_fb);
    host->height = host_to_logical(host, height_fb);
    struct yetty_ycore_void_result result =
        yetty_ychrome_set_size(host->chrome, host->width, host->height);

    if (host->container) {
        /* WIRE: re-emit both figures at the new size (CREATE_CHILD re-mints). */
        result = yetty_ycore_void_chain(result, host_wire_backdrop_emit(host));
        result = yetty_ycore_void_chain(result, host_wire_caption_emit(host));
        return result;
    }

#ifdef YETTY_YCHROME_HAS_LOCAL
    /* LOCAL: resize the backdrop figure + reload its box, reposition the
     * caption strip, then repaint the caption. */
    if (host->backdrop) {
        struct yetty_ycore_rectangle full = {.min = {0.0f, 0.0f},
                                             .max = {host->width, host->height}};
        result = yetty_ycore_void_chain(
            result, yetty_yfigure_figure_rect_set(scene_figure_obj(host->backdrop), full));
        struct yetty_ydraw_drawable_list_result backdrop_list_result =
            host_make_backdrop_list(host->width, host->height);
        if (YETTY_IS_OK(backdrop_list_result)) {
            result = yetty_ycore_void_chain(
                result,
                host_local_load(
                    scene_figure_obj(host->backdrop),
                    (const uint8_t *)yetty_ydraw_drawable_list_data(backdrop_list_result.value),
                    yetty_ydraw_drawable_list_size(backdrop_list_result.value)));
            yetty_ydraw_drawable_list_destroy(backdrop_list_result.value);
        } else {
            result = yetty_ycore_void_chain(result, YETTY_ERR(yetty_ycore_void,
                                                              "ychrome_host_resized: backdrop list",
                                                              backdrop_list_result));
        }
    }
    if (host->caption) {
        struct yetty_ycore_rectangle caption_rect = {.min = {0.0f, 0.0f},
                                                     .max = {host->width, host->caption_height}};
        result = yetty_ycore_void_chain(
            result, yetty_yfigure_figure_rect_set(scene_figure_obj(host->caption), caption_rect));
    }
    result = yetty_ycore_void_chain(result, host_caption_refresh(host));
    return result;
#else
    /* Wire-only build: no local sink. The wire branch above already returned;
     * only the set_size result remains to surface. */
    return result;
#endif /* YETTY_YCHROME_HAS_LOCAL */
}

struct yetty_ycore_void_result yetty_ychrome_host_destroy(struct yetty_ychrome_host *host)
{
    if (!host) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result result = YETTY_OK_VOID();
    /* Figure removal is NOT this destroy's job:
     *  - WIRE: the owner removes our children by calling yetty_ychrome_host_clear
     *    (typed delete_child stubs, each a synchronous request) BEFORE this
     *    destroy, then disconnects the session it owns afterwards.
     *  - LOCAL: the pinned figures are owned by the container (added via
     *    add_child) and destroyed with it.
     * Either way, destroy only frees what is ours: the chrome engine + host.
     * The container proxy is borrowed; the owner disconnects the session. */
    if (host->chrome) {
        result = yetty_ycore_void_chain(result, yetty_ychrome_destroy(host->chrome));
    }
    free(host);
    return result;
}
