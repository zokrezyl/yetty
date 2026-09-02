/*
 * ygui2 plot — a yplot complex drawable as a first-class widget (rich
 * content with ergonomics on top of the T5 carrier idea). The widget owns
 * ONE api_yplot object: builders declare title/ranges/expression curves
 * and a SIZE-ONLY streaming buffer; the RETAINED hook renders the
 * accumulated DSL at the widget's rect with the widget's own minted
 * stream id — the creation record lives directly in the containment
 * group, addressable at [ancestors..., widget, id]. The plot draws ALL of
 * its own chrome (frame, grid, ticks, labels, title, legend): shader-side
 * for the frame/grid/curves, and as a self-owned chrome GROUP of text
 * prims the RECEIVER re-renders locally from the record's retained state.
 *
 * Wire costs by operation:
 *   append (live feed)  — the new samples + a ring-head op, one envelope
 *                         (~40 bytes/sample; plot_append_samples).
 *   bulk load           — the full window + a zeroed tail + a linear
 *                         ring-head op, one envelope (plot_stream_samples).
 *   RESIZE              — ONE addressed geometry op (~28 bytes) in the
 *                         frame envelope; the receiver re-plans the
 *                         runtime and its chrome locally. The record and
 *                         its sample data are NEVER re-sent on resize.
 *   range change (live) — one addressed ranges op, same local re-plan.
 *   structural change   — expression / buffer declaration changes replace
 *                         the record; the cached window replays inside
 *                         the SAME insertion envelope (plot_paint).
 *
 * The streaming buffer is declared size-only on purpose: inline values
 * silently cap the receiver slot at the DSL's inline maximum, and any
 * larger streamed chunk would be rejected as an overflow.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/api/ydrawlist2/drawable.h>
#include <yetty/api/yplot/plot.h>
#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ygui2/defs.h>
#include <yetty/yplot/yplot.h>

#include "yetty/gen/impl/ygui2/widget.h"

/* Cross-class within-module (accessor pattern). */
struct yetty_ycore_void_result yetty_ygui2_framework_stream_update(
    struct yetty_yclass_object *widget_obj, uint32_t child_node_id, const void *payload,
    size_t payload_size);
struct yetty_ycore_void_result yetty_ygui2_framework_stream_update_batch(
    struct yetty_yclass_object *widget_obj, uint32_t child_node_id, const void *const *payloads,
    const size_t *payload_sizes, uint32_t payload_count);
struct yetty_ycore_uint32_result yetty_ygui2_framework_mint_node_id(
    struct yetty_yclass_object *framework_obj);
struct yetty_ycore_void_result yetty_ygui2_framework_append_addressed_update(
    struct yetty_yclass_object *widget_obj, struct yetty_ydraw_drawable_list *list,
    uint32_t child_node_id, const void *payload, size_t payload_size);
/* api_yplot property setter — declared ahead of the regenerated api
 * header so the first codegen pass resolves it. */
struct yetty_ycore_void_result yetty_api_yplot_plot_chrome_group_set(
    struct yetty_yclass_object *obj, uint32_t chrome_group);

YETTY_YRESULT_DECLARE(yetty_ygui2_plot_ptr, struct yetty_ygui2_plot *);
struct yetty_yclass_ptr_result yetty_ygui2_plot_class_get(void);
struct yetty_ygui2_plot_ptr_result yetty_ygui2_plot_from(struct yetty_yclass_object *obj);

/* Streaming-slot capacity bounds. The lower bound matches the shader's
 * "draw when len >= 2" rule (a 1-sample buffer never renders); the upper
 * bound is the same operational limit the yplot-stream frontend enforces
 * and keeps every wire object (creation record, full-window update) far
 * inside its u32 byte-length fields with overflow-safe ring arithmetic. */
enum {
    YGUI2_PLOT_STREAM_CAPACITY_MIN = 2,
    YGUI2_PLOT_STREAM_CAPACITY_MAX = 65536,
};

struct YETTY_ANNOTATE("class@ygui2:plot") YETTY_ANNOTATE("parent@ygui2:widget") yetty_ygui2_plot {
    struct yetty_yclass_object *figure; /* owned api_yplot plot object */
    uint32_t stream_node_id;            /* the complex record's addressable id */
    uint32_t chrome_node_id;            /* the self-owned chrome GROUP's id */
    uint32_t stream_capacity;           /* declared streaming-slot samples */
    /* Ring-append state. The wire carries only new samples, but the
     * producer keeps a CACHED copy of the window: an intentional
     * structural replacement (DSL change, rebuild recovery) recreates the
     * runtime zero-filled, and the insertion envelope REPLAYS this cache
     * so the visible history survives. Resize is NOT structural — it is
     * one addressed geometry op (widget_emit_geometry below). */
    uint32_t stream_cursor; /* next physical slot to write */
    int stream_wrapped;     /* the window has been filled at least once */
    float *window;          /* cached samples, capacity entries (owned) */
    int window_streamed;    /* anything ever streamed (replay-worthy) */
    float painted_width;    /* size last shipped (record or geometry op) */
    float painted_height;
};

/* The wrapped api_yplot object, created on first use. */
static struct yetty_ycore_void_result plot_figure_ensure(struct yetty_yclass_object *obj,
                                                         struct yetty_ygui2_plot *plot)
{
    (void)obj;
    if (plot->figure) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object_ptr_result figure_res = yetty_api_yplot_plot_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, figure_res, "ygui2 plot: figure create");
    plot->figure = figure_res.value;
    return YETTY_OK_VOID();
}

/* RETAINED hook — the rendered plot (complex record + its self-owned
 * chrome group) lives in the CONTAINMENT group. Runs on structural
 * insertion/replacement only; a plain RESIZE never comes through here —
 * it is one addressed geometry op (plot_emit_geometry below). */
YETTY_ANNOTATE("override@ygui2:widget:widget_paint_retained")
static struct yetty_ycore_void_result plot_paint(struct yetty_yclass_object *obj,
                                                 struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_plot_ptr_result data_res = yetty_ygui2_plot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 plot paint: data");
    struct yetty_ygui2_plot *plot = data_res.value;
    struct yetty_ycore_void_result ensure_res = plot_figure_ensure(obj, plot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_res, "ygui2 plot paint: figure");
    if (!plot->stream_node_id) {
        struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_widget_framework_obj(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res, "ygui2 plot paint: framework");
        struct yetty_ycore_uint32_result mint_res =
            yetty_ygui2_framework_mint_node_id(framework_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, mint_res, "ygui2 plot paint: mint id");
        plot->stream_node_id = mint_res.value;
        struct yetty_ycore_uint32_result chrome_mint_res =
            yetty_ygui2_framework_mint_node_id(framework_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, chrome_mint_res, "ygui2 plot paint: chrome id");
        plot->chrome_node_id = chrome_mint_res.value;
    }
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 plot paint: rect");
    /* Widget-local origin: the minted group's offset places the figure. */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yetty_api_yplot_plot_x_set(plot->figure, 0.0f),
                        "ygui2 plot paint: x");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yetty_api_yplot_plot_y_set(plot->figure, 0.0f),
                        "ygui2 plot paint: y");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yetty_api_yplot_plot_width_set(plot->figure, width),
                        "ygui2 plot paint: width");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yetty_api_yplot_plot_height_set(plot->figure, height),
                        "ygui2 plot paint: height");
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yetty_api_yplot_plot_id_set(plot->figure, plot->stream_node_id),
                        "ygui2 plot paint: id");
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yetty_api_yplot_plot_chrome_group_set(plot->figure, plot->chrome_node_id),
                        "ygui2 plot paint: chrome group");
    plot->painted_width = width;
    plot->painted_height = height;
    struct yetty_ycore_void_result pack_res = yetty_ydrawlist2_pack(plot->figure, list);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pack_res, "ygui2 plot paint: pack");
    if (plot->window_streamed && plot->stream_capacity && plot->window) {
        /* Replay the authoritative cached window IN THIS SAME envelope,
         * right after the freshly created (zero-filled) runtime became
         * addressable: the visible state survives EVERY replacement —
         * own resize, ancestor reopen, rebuild recovery — with no
         * dependence on the application ever streaming again. The bare
         * UPDATE folds at the ambient scope (this widget's open group),
         * exactly where NODE_ID bound the record above. */
        size_t window_size = 3u * sizeof(uint32_t) + (size_t)plot->stream_capacity * sizeof(float);
        uint8_t *window_payload = malloc(window_size);
        if (!window_payload) {
            return YETTY_ERR(yetty_ycore_void, "ygui2 plot paint: replay alloc");
        }
        uint32_t window_header[3] = {0u, 0u, plot->stream_capacity};
        memcpy(window_payload, window_header, sizeof(window_header));
        memcpy(window_payload + sizeof(window_header), plot->window,
               (size_t)plot->stream_capacity * sizeof(float));
        struct yetty_ycore_void_result window_res = yetty_ydraw_drawable_list_add_cmd_update(
            list, plot->stream_node_id, window_payload, window_size);
        free(window_payload);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, window_res, "ygui2 plot paint: replay window");
        uint32_t head_index = plot->stream_wrapped ? plot->stream_cursor : 0u;
        uint32_t head_payload[3] = {0u, YETTY_YPLOT_UPDATE_OP_RING_HEAD, head_index};
        struct yetty_ycore_void_result head_res = yetty_ydraw_drawable_list_add_cmd_update(
            list, plot->stream_node_id, head_payload, sizeof(head_payload));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, head_res, "ygui2 plot paint: replay head");
    }
    return YETTY_OK_VOID();
}

/* Geometry follow-up — the widget's size changed. Ships ONE addressed
 * geometry op (~28 bytes) in the frame envelope being assembled: the
 * receiver re-plans the retained record at the new figure size and
 * re-renders the chrome group locally. The record and its sample data
 * are NEVER re-sent on resize — the entire point of the self-owned
 * chrome contract (a 10 GB drawable resizes for the same 28 bytes). */
YETTY_ANNOTATE("override@ygui2:widget:widget_emit_geometry")
static struct yetty_ycore_void_result plot_emit_geometry(struct yetty_yclass_object *obj,
                                                         struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_plot_ptr_result data_res = yetty_ygui2_plot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 plot geometry: data");
    struct yetty_ygui2_plot *plot = data_res.value;
    if (!plot->stream_node_id) {
        return YETTY_OK_VOID(); /* not inserted yet — the paint bakes the size */
    }
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 plot geometry: rect");
    if (width <= 0.0f || height <= 0.0f ||
        (width == plot->painted_width && height == plot->painted_height)) {
        return YETTY_OK_VOID();
    }
    uint32_t payload[4] = {0u, YETTY_YPLOT_UPDATE_OP_GEOMETRY, 0u, 0u};
    memcpy(&payload[2], &width, sizeof(float));
    memcpy(&payload[3], &height, sizeof(float));
    struct yetty_ycore_void_result update_res = yetty_ygui2_framework_append_addressed_update(
        obj, list, plot->stream_node_id, payload, sizeof(payload));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, update_res, "ygui2 plot geometry: op");
    plot->painted_width = width;
    plot->painted_height = height;
    return YETTY_OK_VOID();
}

/* Figure/axis configuration — thin forwarders to the wrapped api_yplot
 * object (each appends its DSL directive; last one wins on the wire). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_plot_set_title(struct yetty_yclass_object *obj,
                                                          const char *title)
{
    struct yetty_ygui2_plot_ptr_result data_res = yetty_ygui2_plot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 plot_set_title: data");
    struct yetty_ycore_void_result ensure_res = plot_figure_ensure(obj, data_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_res, "ygui2 plot_set_title: figure");
    struct yetty_ycore_void_result title_res =
        yetty_api_yplot_set_title(data_res.value->figure, title);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, title_res, "ygui2 plot_set_title: forward");
    return yetty_ygui2_widget_mark_structure_dirty(obj);
}

/* Append a raw plot-DSL fragment — curves ("sin(3*x) * 0.8"), per-curve
 * colors, axis attributes. The full expression language of yplot. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_plot_set_expression(struct yetty_yclass_object *obj,
                                                               const char *source)
{
    struct yetty_ygui2_plot_ptr_result data_res = yetty_ygui2_plot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 plot_set_expression: data");
    struct yetty_ycore_void_result ensure_res = plot_figure_ensure(obj, data_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_res, "ygui2 plot_set_expression: figure");
    struct yetty_ycore_void_result expression_res =
        yetty_api_yplot_set_expression(data_res.value->figure, source);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, expression_res, "ygui2 plot_set_expression: forward");
    return yetty_ygui2_widget_mark_structure_dirty(obj);
}

/* Change one axis range. Before the first paint this only accumulates in
 * the DSL (the insertion bakes it). On a LIVE plot it ships one addressed
 * ranges op — the receiver re-plans ticks/grid/labels locally and the
 * curves rescale in the shader; nothing structural, nothing re-sent. The
 * DSL is updated either way so a later structural repack agrees. */
static struct yetty_ycore_void_result plot_range_apply(struct yetty_yclass_object *obj,
                                                       struct yetty_ygui2_plot *plot, uint32_t axis,
                                                       float min, float max)
{
    if (!plot->stream_node_id) {
        return yetty_ygui2_widget_mark_structure_dirty(obj);
    }
    uint32_t payload[5] = {0u, YETTY_YPLOT_UPDATE_OP_RANGES, axis, 0u, 0u};
    memcpy(&payload[3], &min, sizeof(float));
    memcpy(&payload[4], &max, sizeof(float));
    return yetty_ygui2_framework_stream_update(obj, plot->stream_node_id, payload, sizeof(payload));
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_plot_set_y_range(struct yetty_yclass_object *obj,
                                                            float min, float max)
{
    struct yetty_ygui2_plot_ptr_result data_res = yetty_ygui2_plot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 plot_set_y_range: data");
    struct yetty_ycore_void_result ensure_res = plot_figure_ensure(obj, data_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_res, "ygui2 plot_set_y_range: figure");
    struct yetty_ycore_void_result range_res =
        yetty_api_yplot_set_y_range(data_res.value->figure, min, max);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, range_res, "ygui2 plot_set_y_range: forward");
    return plot_range_apply(obj, data_res.value, /*axis=*/1u, min, max);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_plot_set_x_range(struct yetty_yclass_object *obj,
                                                            float min, float max)
{
    struct yetty_ygui2_plot_ptr_result data_res = yetty_ygui2_plot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 plot_set_x_range: data");
    struct yetty_ycore_void_result ensure_res = plot_figure_ensure(obj, data_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_res, "ygui2 plot_set_x_range: figure");
    struct yetty_ycore_void_result range_res =
        yetty_api_yplot_set_x_range(data_res.value->figure, min, max);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, range_res, "ygui2 plot_set_x_range: forward");
    return plot_range_apply(obj, data_res.value, /*axis=*/0u, min, max);
}

/* Declare the STREAMING buffer: a named, SIZE-ONLY (zero-filled) slot of
 * `capacity` samples plus its reference-curve color. Exactly one such
 * buffer per plot widget — its samples arrive via plot_stream_samples. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_plot_add_stream_buffer(struct yetty_yclass_object *obj,
                                                                  const char *name,
                                                                  uint32_t capacity,
                                                                  const char *color)
{
    struct yetty_ygui2_plot_ptr_result data_res = yetty_ygui2_plot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 plot_add_stream_buffer: data");
    struct yetty_ygui2_plot *plot = data_res.value;
    if (!name || !name[0]) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 plot_add_stream_buffer: empty name");
    }
    if (capacity < YGUI2_PLOT_STREAM_CAPACITY_MIN || capacity > YGUI2_PLOT_STREAM_CAPACITY_MAX) {
        /* Deliberate operational range: below 2 the shader never draws
         * the buffer; above 64 Ki samples the creation/update wire sizes
         * and the ring arithmetic leave their validated envelope. */
        return YETTY_ERR(yetty_ycore_void,
                         "ygui2 plot_add_stream_buffer: capacity must be within 2..65536");
    }
    if (plot->stream_capacity != 0u) {
        /* EXACTLY ONE stream buffer per plot — the streaming API always
         * targets buffer 0; silently accepting a second declaration
         * would validate later sends against the wrong slot. The first
         * buffer stays fully usable after this rejection. */
        return YETTY_ERR(yetty_ycore_void,
                         "ygui2 plot_add_stream_buffer: a stream buffer is already declared");
    }
    struct yetty_ycore_void_result ensure_res = plot_figure_ensure(obj, plot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_res, "ygui2 plot_add_stream_buffer: figure");
    /* TRANSACTIONAL: the cache allocation happens BEFORE the figure DSL
     * is touched — a failed declaration leaves the figure without a
     * buffer, so a retry cannot create the second-buffer ambiguity. */
    float *window = calloc(capacity, sizeof(float));
    if (!window) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 plot_add_stream_buffer: window alloc");
    }
    struct yetty_yclass_object_ptr_result buffer_res = yetty_api_yplot_buffer_create(NULL);
    if (YETTY_IS_ERR(buffer_res)) {
        free(window);
        return YETTY_ERR(yetty_ycore_void, "ygui2 plot_add_stream_buffer: buffer", buffer_res);
    }
    struct yetty_yclass_object *buffer = buffer_res.value;
    struct yetty_ycore_void_result step_res = yetty_api_yplot_set_name(buffer, name);
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_api_yplot_buffer_size_set(buffer, capacity);
    }
    if (YETTY_IS_OK(step_res) && color && color[0]) {
        step_res = yetty_api_yplot_set_color(buffer, color);
    }
    if (YETTY_IS_OK(step_res)) {
        step_res = yetty_api_yplot_add_buffer(plot->figure, buffer);
    }
    struct yetty_ycore_void_result free_res = yetty_yclass_object_free(buffer);
    if (YETTY_IS_ERR(step_res)) {
        free(window);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_ycore_void, "ygui2 plot_add_stream_buffer: declare", step_res);
    }
    if (YETTY_IS_ERR(free_res)) {
        free(window);
        return YETTY_ERR(yetty_ycore_void, "ygui2 plot_add_stream_buffer: free", free_res);
    }
    plot->window = window;
    plot->stream_capacity = capacity;
    return yetty_ygui2_widget_mark_structure_dirty(obj);
}

/* Bulk-load the streamed window: ships the FULL capacity every time —
 * `count` samples plus a zeroed tail — followed by a linear ring-head
 * op, in one envelope. Cache and runtime are identical afterwards. For
 * live feeds use plot_append_samples (O(new samples) on the wire). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_plot_stream_samples(struct yetty_yclass_object *obj,
                                                               const float *samples, uint32_t count)
{
    struct yetty_ygui2_plot_ptr_result data_res = yetty_ygui2_plot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 plot_stream_samples: data");
    struct yetty_ygui2_plot *plot = data_res.value;
    if (!samples || count == 0u) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 plot_stream_samples: no samples");
    }
    if (plot->stream_capacity == 0u) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 plot_stream_samples: no stream buffer declared");
    }
    if (count > plot->stream_capacity) {
        return YETTY_ERR(yetty_ycore_void,
                         "ygui2 plot_stream_samples: count exceeds the declared capacity");
    }
    if (!plot->stream_node_id) {
        return YETTY_ERR(yetty_ycore_void,
                         "ygui2 plot_stream_samples: not painted yet (no live figure)");
    }
    /* Bulk overwrite: the FULL window ships every time — `count` new
     * samples plus an explicitly zeroed tail — so cache and runtime are
     * identical afterwards (a short load never leaves a stale tail) and
     * the ring resets to LINEAR order. Cache/cursor commit only AFTER
     * the send: a failed ship leaves every producer state untouched. */
    size_t payload_size = 3u * sizeof(uint32_t) + (size_t)plot->stream_capacity * sizeof(float);
    uint8_t *payload = malloc(payload_size);
    if (!payload) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 plot_stream_samples: alloc");
    }
    uint32_t header[3] = {0u, 0u, plot->stream_capacity};
    memcpy(payload, header, sizeof(header));
    memcpy(payload + sizeof(header), samples, (size_t)count * sizeof(float));
    memset(payload + sizeof(header) + (size_t)count * sizeof(float), 0,
           (size_t)(plot->stream_capacity - count) * sizeof(float));
    uint8_t head_payload[3u * sizeof(uint32_t)];
    uint32_t head_header[3] = {0u, YETTY_YPLOT_UPDATE_OP_RING_HEAD, 0u};
    memcpy(head_payload, head_header, sizeof(head_header));
    const void *payloads[2] = {payload, head_payload};
    size_t payload_sizes[2] = {payload_size, sizeof(head_header)};
    struct yetty_ycore_void_result stream_res = yetty_ygui2_framework_stream_update_batch(
        obj, plot->stream_node_id, payloads, payload_sizes, 2u);
    if (YETTY_IS_ERR(stream_res)) {
        free(payload);
        return YETTY_ERR(yetty_ycore_void, "ygui2 plot_stream_samples: update", stream_res);
    }
    memcpy(plot->window, payload + sizeof(header), (size_t)plot->stream_capacity * sizeof(float));
    free(payload);
    plot->stream_cursor = count % plot->stream_capacity;
    plot->stream_wrapped = count == plot->stream_capacity;
    plot->window_streamed = 1;
    return YETTY_OK_VOID();
}

/* APPEND samples — the low-bandwidth streaming primitive. Steady state
 * ships ONLY the new samples plus a ring-head op (~40 bytes for one
 * sample) in one envelope; the receiver's shader unwraps the ring so
 * the display scrolls with nothing re-sent. The one deliberate re-send
 * is replacement recovery: an intentional structural reopen carries the
 * cached window inside its own insertion envelope (plot_paint). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_plot_append_samples(struct yetty_yclass_object *obj,
                                                               const float *samples, uint32_t count)
{
    struct yetty_ygui2_plot_ptr_result data_res = yetty_ygui2_plot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 plot_append_samples: data");
    struct yetty_ygui2_plot *plot = data_res.value;
    if (!samples || count == 0u) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 plot_append_samples: no samples");
    }
    if (plot->stream_capacity == 0u) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 plot_append_samples: no stream buffer declared");
    }
    if (count > plot->stream_capacity) {
        return YETTY_ERR(yetty_ycore_void,
                         "ygui2 plot_append_samples: count exceeds the declared capacity");
    }
    if (!plot->stream_node_id) {
        return YETTY_ERR(yetty_ycore_void,
                         "ygui2 plot_append_samples: not painted yet (no live figure)");
    }
    /* Build the outgoing chunks FIRST from the samples and the current
     * cursor (no cap beyond the declared capacity — payloads are sized
     * for the accepted count); commit cache, cursor and wrap state
     * together only after the send succeeded, so a failed ship leaves
     * the producer exactly as it was. */
    uint32_t first_count = plot->stream_capacity - plot->stream_cursor;
    if (first_count > count) {
        first_count = count;
    }
    uint32_t second_count = count - first_count;
    uint32_t next_cursor = (plot->stream_cursor + count) % plot->stream_capacity;
    int wrapped = plot->stream_wrapped || plot->stream_cursor + count >= plot->stream_capacity;
    /* Oldest sample: the next slot to be overwritten once the window has
     * filled; before that the read order stays linear from 0. */
    uint32_t head_index = wrapped ? next_cursor : 0u;
    size_t first_size = 3u * sizeof(uint32_t) + (size_t)first_count * sizeof(float);
    size_t second_size = 3u * sizeof(uint32_t) + (size_t)second_count * sizeof(float);
    uint8_t *first_payload = malloc(first_size);
    uint8_t *second_payload = second_count ? malloc(second_size) : NULL;
    if (!first_payload || (second_count && !second_payload)) {
        free(first_payload);
        free(second_payload);
        return YETTY_ERR(yetty_ycore_void, "ygui2 plot_append_samples: alloc");
    }
    uint32_t first_header[3] = {0u, plot->stream_cursor, first_count};
    memcpy(first_payload, first_header, sizeof(first_header));
    memcpy(first_payload + sizeof(first_header), samples, first_count * sizeof(float));
    uint8_t head_payload[3u * sizeof(uint32_t)];
    uint32_t head_header[3] = {0u, YETTY_YPLOT_UPDATE_OP_RING_HEAD, head_index};
    memcpy(head_payload, head_header, sizeof(head_header));
    const void *payloads[3];
    size_t payload_sizes[3];
    uint32_t payload_count = 0;
    payloads[payload_count] = first_payload;
    payload_sizes[payload_count++] = first_size;
    if (second_count) {
        uint32_t second_header[3] = {0u, 0u, second_count};
        memcpy(second_payload, second_header, sizeof(second_header));
        memcpy(second_payload + sizeof(second_header), samples + first_count,
               second_count * sizeof(float));
        payloads[payload_count] = second_payload;
        payload_sizes[payload_count++] = second_size;
    }
    payloads[payload_count] = head_payload;
    payload_sizes[payload_count++] = sizeof(head_payload);
    struct yetty_ycore_void_result stream_res = yetty_ygui2_framework_stream_update_batch(
        obj, plot->stream_node_id, payloads, payload_sizes, payload_count);
    free(first_payload);
    free(second_payload);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, stream_res, "ygui2 plot_append_samples: batch");
    /* Commit point: the receiver has the chunks — mirror them. */
    memcpy(plot->window + plot->stream_cursor, samples, first_count * sizeof(float));
    if (second_count) {
        memcpy(plot->window, samples + first_count, second_count * sizeof(float));
    }
    plot->stream_cursor = next_cursor;
    plot->stream_wrapped = wrapped;
    plot->window_streamed = 1;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui2:widget:widget_cleanup")
static struct yetty_ycore_void_result plot_cleanup(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_plot_ptr_result data_res = yetty_ygui2_plot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 plot cleanup: data");
    if (data_res.value->figure) {
        struct yetty_ycore_void_result destroy_res =
            yetty_api_yplot_destroy(data_res.value->figure);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        data_res.value->figure = NULL;
    }
    free(data_res.value->window);
    data_res.value->window = NULL;
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui2/widgets/plot.c"
