/*
 * yjungle.c — incremental scene-canvas test scene.
 *
 * See yjungle.h for the model. This file owns the chain-of-segments
 * data, generates random segment trees, and serialises them as GROUP /
 * DELETE wire commands on every tick. The frontend (tools/yjungle/) is
 * a thin driver that owns time + I/O.
 *
 * Wire emission rules:
 *   - First tick: CMD_ZERO + GROUP(seg_id) for every initial chain
 *     segment. After this the receiver has the full tree.
 *   - Extend event: GROUP(new_id) appended as a new chain entry.
 *   - Replace event: DELETE(old_id) + GROUP(new_id) at the envelope
 *     root. The old segment's slot in the chain is overwritten in
 *     place, preserving start/end so neighbours stay connected. The
 *     new id is fresh — the producer NEVER reuses ids, which keeps
 *     scene-canvas's strict "GROUP id already exists" rejection from
 *     firing.
 *
 * Group nesting: a non-leaf segment is emitted by opening a CMD_GROUP
 * for the segment, then recursing into each child (which may itself
 * open a nested CMD_GROUP). end_group patches the parent's payload_size
 * after the children finish — see draw-list.h. This stresses scene-
 * canvas's process_group_body recursive parser.
 */

#include <yetty/yjungle/yjungle.h>

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <yetty/yplatform/time.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Same 22-shape table as yzoo's emit_shape, so the visual feel is
 * consistent across the two tools. */
#define YJUNGLE_SHAPE_COUNT 22
#define YJUNGLE_TWO_PI      6.28318530717958647692f

/*=============================================================================
 * Segment tree
 *===========================================================================*/

struct yjungle_segment {
    uint32_t group_id; /* wire id; monotonic, never reused */
    float    start_x, start_y;
    float    end_x, end_y;
    uint32_t depth;

    /* If is_group: children[] is the chained sub-segments; primitive
     * fields below are unused. If !is_group: this is a leaf primitive
     * and children[] is empty. */
    bool is_group;
    struct yjungle_segment *children;
    uint32_t children_count;

    /* Primitive parameters (leaf only). */
    int      shape_choice;
    uint32_t color;
    float    stroke_width;
};

/*=============================================================================
 * Engine state
 *===========================================================================*/

struct yetty_yjungle {
    struct yetty_yjungle_config config;
    uint64_t rng_state;

    /* Top-level chain: a flat array of segments, in chain order.
     * chain[i].end == chain[i+1].start by construction. */
    struct yjungle_segment *chain;
    uint32_t chain_len;
    uint32_t chain_cap;

    /* Monotonic group-id counter — 0 reserved for the scene-canvas
     * root entity, so we start at 1. */
    uint32_t next_group_id;

    /* Cursor (= chain tail end) — the next new tail segment starts
     * here. Set to a random canvas-interior point at first tick. */
    float cursor_x, cursor_y;

    /* Event timing. */
    bool     first_tick_done;
    uint64_t last_event_ms;
    uint32_t next_event_delay_ms;
};

/*=============================================================================
 * splitmix64 PRNG — same as ymaze/yzoo.
 *===========================================================================*/

static uint64_t rng_next(struct yetty_yjungle *j)
{
    uint64_t v = (j->rng_state += 0x9E3779B97F4A7C15ULL);
    v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
    v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
    return v ^ (v >> 31);
}

static float rng_f01(struct yetty_yjungle *j)
{
    return (float)(rng_next(j) >> 40) * (1.0f / 16777216.0f);
}

static float rng_range(struct yetty_yjungle *j, float lo, float hi)
{
    return lo + (hi - lo) * rng_f01(j);
}

static uint32_t rng_uint(struct yetty_yjungle *j, uint32_t bound)
{
    if (bound <= 1) {
        return 0;
    }
    return (uint32_t)(rng_next(j) % bound);
}

static uint64_t seed_from_clock(void)
{
    double t = yetty_yplatform_ytime_monotonic_sec();
    uint64_t s = (uint64_t)(t * 1.0e9);
    return s ? s : 1;
}

/*=============================================================================
 * HSL → ABGR random color (same algorithm as yzoo_random_color).
 *===========================================================================*/

static uint32_t random_color(struct yetty_yjungle *j)
{
    float hue = rng_range(j, 0.0f, 360.0f);
    float sat = rng_range(j, 0.6f, 1.0f);
    float lit = rng_range(j, 0.45f, 0.7f);

    float c = (1.0f - fabsf(2.0f * lit - 1.0f)) * sat;
    float x = c * (1.0f - fabsf(fmodf(hue / 60.0f, 2.0f) - 1.0f));
    float m = lit - c / 2.0f;

    float r1, g1, b1;
    if (hue < 60.0f) {
        r1 = c; g1 = x; b1 = 0.0f;
    } else if (hue < 120.0f) {
        r1 = x; g1 = c; b1 = 0.0f;
    } else if (hue < 180.0f) {
        r1 = 0.0f; g1 = c; b1 = x;
    } else if (hue < 240.0f) {
        r1 = 0.0f; g1 = x; b1 = c;
    } else if (hue < 300.0f) {
        r1 = x; g1 = 0.0f; b1 = c;
    } else {
        r1 = c; g1 = 0.0f; b1 = x;
    }

    uint8_t r = (uint8_t)((r1 + m) * 255.0f);
    uint8_t g = (uint8_t)((g1 + m) * 255.0f);
    uint8_t b = (uint8_t)((b1 + m) * 255.0f);
    /* ABGR byte order — matches the pipeline yzoo writes into. */
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

/*=============================================================================
 * Random-walk endpoint.
 *
 * Pick a unit direction uniformly on the circle, scale by a random
 * length in [step_min, step_max], and add to `start`. If the result
 * lands more than `off_canvas_margin` outside the scene rect, reflect
 * the direction (negate it). Single reflection is enough since the step
 * length is small relative to the margin in practice.
 *===========================================================================*/

static void random_next_point(struct yetty_yjungle *j, float sx, float sy,
                              float *ex, float *ey)
{
    const struct yetty_yjungle_config *cfg = &j->config;
    float angle = rng_range(j, 0.0f, YJUNGLE_TWO_PI);
    float len   = rng_range(j, cfg->step_min, cfg->step_max);
    float dx    = cosf(angle) * len;
    float dy    = sinf(angle) * len;

    float candidate_x = sx + dx;
    float candidate_y = sy + dy;

    float xmin = -cfg->off_canvas_margin;
    float ymin = -cfg->off_canvas_margin;
    float xmax = cfg->scene_width  + cfg->off_canvas_margin;
    float ymax = cfg->scene_height + cfg->off_canvas_margin;

    if (candidate_x < xmin || candidate_x > xmax) {
        dx = -dx;
        candidate_x = sx + dx;
    }
    if (candidate_y < ymin || candidate_y > ymax) {
        dy = -dy;
        candidate_y = sy + dy;
    }
    *ex = candidate_x;
    *ey = candidate_y;
}

/*=============================================================================
 * Chain storage growth.
 *===========================================================================*/

static struct yetty_ycore_void_result chain_reserve(struct yetty_yjungle *j, uint32_t need)
{
    if (need <= j->chain_cap) {
        return YETTY_OK_VOID();
    }
    uint32_t nc = j->chain_cap ? j->chain_cap : 16u;
    while (nc < need) {
        nc *= 2u;
    }
    struct yjungle_segment *np = realloc(j->chain, nc * sizeof(*np));
    if (!np) {
        return YETTY_ERR(yetty_ycore_void, "yjungle: chain realloc failed");
    }
    j->chain = np;
    j->chain_cap = nc;
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Segment tree allocation / disposal.
 *===========================================================================*/

static void segment_free(struct yjungle_segment *seg)
{
    if (!seg) {
        return;
    }
    for (uint32_t i = 0; i < seg->children_count; i++) {
        segment_free(&seg->children[i]);
    }
    free(seg->children);
    seg->children = NULL;
    seg->children_count = 0;
}

/*=============================================================================
 * Random segment-tree generation.
 *
 * generate_subtree_into(seg, start, end, depth) fills `seg` with a new
 * random subtree spanning (start, end) at recursion `depth`. If a group
 * is chosen, N children are placed along the start→end segment by
 * picking N-1 intermediate points (slightly jittered perpendicular for
 * a visible meander) and recursing on each sub-span.
 *
 * Returns void; allocation failures cause the partial subtree to be a
 * leaf primitive — no error propagated. Producer-side test code: we
 * prefer "always draw something" over hard failure.
 *===========================================================================*/

static int random_shape_choice(struct yetty_yjungle *j)
{
    return (int)rng_uint(j, YJUNGLE_SHAPE_COUNT);
}

static float group_prob_for_depth(const struct yetty_yjungle *j, uint32_t depth)
{
    if (depth >= j->config.max_depth) {
        return 0.0f;
    }
    /* Halve per level. */
    float p = j->config.group_prob_depth0;
    for (uint32_t i = 0; i < depth; i++) {
        p *= 0.5f;
    }
    return p;
}

static void fill_primitive_leaf(struct yetty_yjungle *j, struct yjungle_segment *seg)
{
    seg->is_group = false;
    seg->children = NULL;
    seg->children_count = 0;
    seg->shape_choice = random_shape_choice(j);
    seg->color = random_color(j);
    seg->stroke_width = rng_range(j, 1.0f, 4.0f);
}

static void generate_subtree_into(struct yetty_yjungle *j, struct yjungle_segment *seg,
                                  float sx, float sy, float ex, float ey,
                                  uint32_t depth)
{
    seg->group_id = j->next_group_id++;
    seg->start_x  = sx;
    seg->start_y  = sy;
    seg->end_x    = ex;
    seg->end_y    = ey;
    seg->depth    = depth;
    seg->children = NULL;
    seg->children_count = 0;

    float gp = group_prob_for_depth(j, depth);
    bool be_group = (gp > 0.0f) && (rng_f01(j) < gp);

    if (!be_group) {
        fill_primitive_leaf(j, seg);
        return;
    }

    /* Group: pick child count in [min, max]. */
    uint32_t lo = j->config.group_children_min;
    uint32_t hi = j->config.group_children_max;
    if (lo < 2u) lo = 2u;
    if (hi < lo) hi = lo;
    uint32_t n_children = lo + rng_uint(j, hi - lo + 1u);

    seg->children = calloc(n_children, sizeof(*seg->children));
    if (!seg->children) {
        /* Fallback: treat as leaf primitive. */
        fill_primitive_leaf(j, seg);
        return;
    }
    seg->is_group = true;
    seg->children_count = n_children;

    /* Subdivide the (sx,sy)→(ex,ey) segment into n_children equal-t
     * sub-spans, with each intermediate point jittered perpendicular by
     * up to ±15% of the span length. The first child's start is the
     * parent's start; the last child's end is the parent's end. */
    float dx = ex - sx;
    float dy = ey - sy;
    float span_len = sqrtf(dx * dx + dy * dy);
    float perp_x = 0.0f, perp_y = 0.0f;
    if (span_len > 1e-3f) {
        perp_x = -dy / span_len;
        perp_y =  dx / span_len;
    }
    float jitter_amp = span_len * 0.15f;

    float prev_x = sx;
    float prev_y = sy;
    for (uint32_t i = 0; i < n_children; i++) {
        float child_ex, child_ey;
        if (i + 1u == n_children) {
            child_ex = ex;
            child_ey = ey;
        } else {
            float t = (float)(i + 1u) / (float)n_children;
            float base_x = sx + dx * t;
            float base_y = sy + dy * t;
            float jitter = rng_range(j, -jitter_amp, jitter_amp);
            child_ex = base_x + perp_x * jitter;
            child_ey = base_y + perp_y * jitter;
        }
        generate_subtree_into(j, &seg->children[i], prev_x, prev_y,
                              child_ex, child_ey, depth + 1u);
        prev_x = child_ex;
        prev_y = child_ey;
    }
}

/*=============================================================================
 * Primitive emission — pick the SDF flavour by shape_choice (modulo 22).
 *
 * The drawable is sized from the segment's start→end length so the chain
 * is visually obvious without forcing every primitive to be a line. The
 * 22-way table mirrors yzoo's emit_shape for visual consistency.
 *===========================================================================*/

static struct yetty_ycore_void_result emit_primitive(struct yetty_ydraw_draw_list *buf,
                                                      uint32_t z_order,
                                                      const struct yjungle_segment *seg)
{
    float sx = seg->start_x, sy = seg->start_y;
    float ex = seg->end_x,   ey = seg->end_y;
    float cx = (sx + ex) * 0.5f;
    float cy = (sy + ey) * 0.5f;
    float dx = ex - sx;
    float dy = ey - sy;
    float length = sqrtf(dx * dx + dy * dy);
    float size = length * 0.5f;
    if (size < 4.0f) {
        size = 4.0f;
    }
    uint32_t color = seg->color;
    float    stroke = seg->stroke_width;
    int      choice = seg->shape_choice;
    if (choice < 0) choice = 0;
    choice %= YJUNGLE_SHAPE_COUNT;

    switch (choice) {
    case 0: {
        struct yetty_ysdf_circle g = {cx, cy, size};
        return yetty_ydraw_draw_list_add_cmd_add_circle(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 1: {
        struct yetty_ysdf_box g = {cx, cy, size, size * 0.6f, size * 0.15f};
        return yetty_ydraw_draw_list_add_cmd_add_box(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 2: {
        /* Real chain line: from start to end, stroked. */
        struct yetty_ysdf_segment g = {sx, sy, ex, ey};
        return yetty_ydraw_draw_list_add_cmd_add_segment(buf, 0, z_order, 0u, color, stroke, &g);
    }
    case 3: {
        struct yetty_ysdf_triangle g = {
            cx, cy - size, cx - size, cy + size * 0.7f, cx + size, cy + size * 0.7f,
        };
        return yetty_ydraw_draw_list_add_cmd_add_triangle(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 4: {
        struct yetty_ysdf_ellipse g = {cx, cy, size, size * 0.6f};
        return yetty_ydraw_draw_list_add_cmd_add_ellipse(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 5: {
        struct yetty_ysdf_arc g = {cx, cy, 0.866f, 0.5f, size, size * 0.2f};
        return yetty_ydraw_draw_list_add_cmd_add_arc(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 6: {
        struct yetty_ysdf_rounded_box g = {
            cx, cy, size, size * 0.6f, size * 0.2f, size * 0.2f, size * 0.2f, size * 0.2f,
        };
        return yetty_ydraw_draw_list_add_cmd_add_rounded_box(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 7: {
        struct yetty_ysdf_rhombus g = {cx, cy, size, size * 1.2f};
        return yetty_ydraw_draw_list_add_cmd_add_rhombus(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 8: {
        struct yetty_ysdf_pentagon g = {cx, cy, size};
        return yetty_ydraw_draw_list_add_cmd_add_pentagon(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 9: {
        struct yetty_ysdf_hexagon g = {cx, cy, size};
        return yetty_ydraw_draw_list_add_cmd_add_hexagon(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 10: {
        struct yetty_ysdf_star g = {cx, cy, size, 5.0f, 2.5f};
        return yetty_ydraw_draw_list_add_cmd_add_star(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 11: {
        struct yetty_ysdf_pie g = {cx, cy, 0.866f, 0.5f, size};
        return yetty_ydraw_draw_list_add_cmd_add_pie(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 12: {
        struct yetty_ysdf_ring g = {cx, cy, 0.866f, 0.5f, size, size * 0.25f};
        return yetty_ydraw_draw_list_add_cmd_add_ring(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 13: {
        struct yetty_ysdf_heart g = {cx, cy, size};
        return yetty_ydraw_draw_list_add_cmd_add_heart(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 14: {
        struct yetty_ysdf_cross g = {cx, cy, size, size * 0.3f, size * 0.1f};
        return yetty_ydraw_draw_list_add_cmd_add_cross(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 15: {
        struct yetty_ysdf_rounded_x g = {cx, cy, size, size * 0.2f};
        return yetty_ydraw_draw_list_add_cmd_add_rounded_x(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 16: {
        struct yetty_ysdf_capsule g = {sx, sy, ex, ey, length * 0.12f};
        return yetty_ydraw_draw_list_add_cmd_add_capsule(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 17: {
        struct yetty_ysdf_moon g = {cx, cy, size * 0.5f, size, size * 0.8f};
        return yetty_ydraw_draw_list_add_cmd_add_moon(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 18: {
        struct yetty_ysdf_egg g = {cx, cy, size, size * 0.6f};
        return yetty_ydraw_draw_list_add_cmd_add_egg(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 19: {
        struct yetty_ysdf_octogon g = {cx, cy, size};
        return yetty_ydraw_draw_list_add_cmd_add_octogon(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 20: {
        struct yetty_ysdf_hexagram g = {cx, cy, size};
        return yetty_ydraw_draw_list_add_cmd_add_hexagram(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 21: {
        struct yetty_ysdf_pentagram g = {cx, cy, size};
        return yetty_ydraw_draw_list_add_cmd_add_pentagram(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    default:
        return YETTY_OK_VOID();
    }
}

/*=============================================================================
 * Wire emission of a segment subtree.
 *
 * Both leaf and group cases open a CMD_GROUP for the segment's own
 * group_id. Leaves put one drawable inside; groups recurse, which opens
 * a nested CMD_GROUP for each child. end_group back-patches each
 * parent's payload_size on the way out.
 *
 * z_order is taken from a monotonic counter so newer drawables paint on
 * top of older ones. Since we never full-redraw between events, the
 * counter rolls forward forever — uint32_t headroom is plenty.
 *===========================================================================*/

static struct yetty_ycore_void_result emit_segment_subtree(
    struct yetty_ydraw_draw_list *buf, const struct yjungle_segment *seg,
    uint32_t *z_order_counter)
{
    struct yetty_ydraw_id_result br =
        yetty_ydraw_draw_list_begin_group(buf, seg->group_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "yjungle: begin_group");
    uint32_t marker = br.value;

    if (seg->is_group) {
        for (uint32_t i = 0; i < seg->children_count; i++) {
            struct yetty_ycore_void_result cr =
                emit_segment_subtree(buf, &seg->children[i], z_order_counter);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "yjungle: child emit");
        }
    } else {
        struct yetty_ycore_void_result pr =
            emit_primitive(buf, (*z_order_counter)++, seg);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "yjungle: primitive emit");
    }

    struct yetty_ycore_void_result er =
        yetty_ydraw_draw_list_end_group(buf, marker);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "yjungle: end_group");
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_yjungle_config yetty_yjungle_config_default(void)
{
    struct yetty_yjungle_config cfg = {
        .scene_width  = 800.0f,
        .scene_height = 600.0f,

        .step_min = 40.0f,
        .step_max = 140.0f,
        .off_canvas_margin = 60.0f,

        .max_depth = 3u,
        .group_prob_depth0 = 0.5f,
        .group_children_min = 2u,
        .group_children_max = 3u,

        .initial_chain_length = 8u,
        .max_chain_length     = 30u,
        .extend_probability   = 0.3f,

        .event_interval_ms_min = 500u,
        .event_interval_ms_max = 2500u,
    };
    return cfg;
}

static struct yetty_yjungle_config clamp_config(struct yetty_yjungle_config c)
{
    if (c.scene_width  < 16.0f) c.scene_width  = 16.0f;
    if (c.scene_height < 16.0f) c.scene_height = 16.0f;
    if (c.step_min < 1.0f)      c.step_min = 1.0f;
    if (c.step_max < c.step_min) c.step_max = c.step_min;
    if (c.off_canvas_margin < 0.0f) c.off_canvas_margin = 0.0f;

    if (c.max_depth > 6u)     c.max_depth = 6u;
    if (c.group_prob_depth0 < 0.0f) c.group_prob_depth0 = 0.0f;
    if (c.group_prob_depth0 > 1.0f) c.group_prob_depth0 = 1.0f;
    if (c.group_children_min < 2u) c.group_children_min = 2u;
    if (c.group_children_max < c.group_children_min) c.group_children_max = c.group_children_min;
    if (c.group_children_max > 6u) c.group_children_max = 6u;

    if (c.initial_chain_length < 1u) c.initial_chain_length = 1u;
    if (c.max_chain_length < c.initial_chain_length) c.max_chain_length = c.initial_chain_length;
    if (c.max_chain_length > 1000u) c.max_chain_length = 1000u;
    if (c.extend_probability < 0.0f) c.extend_probability = 0.0f;
    if (c.extend_probability > 1.0f) c.extend_probability = 1.0f;

    if (c.event_interval_ms_min < 10u) c.event_interval_ms_min = 10u;
    if (c.event_interval_ms_max < c.event_interval_ms_min)
        c.event_interval_ms_max = c.event_interval_ms_min;
    return c;
}

struct yetty_yjungle_ptr_result yetty_yjungle_create(
    const struct yetty_yjungle_config *config, uint32_t seed)
{
    struct yetty_yjungle *j = calloc(1, sizeof(*j));
    if (!j) {
        return YETTY_ERR(yetty_yjungle_ptr, "yjungle: calloc failed");
    }
    j->config = clamp_config(config ? *config : yetty_yjungle_config_default());
    j->rng_state = seed != 0 ? (uint64_t)seed : seed_from_clock();
    j->next_group_id = 1u; /* 0 reserved for receiver root */
    j->cursor_x = j->config.scene_width  * 0.5f;
    j->cursor_y = j->config.scene_height * 0.5f;
    j->first_tick_done = false;
    j->last_event_ms = 0u;
    j->next_event_delay_ms =
        j->config.event_interval_ms_min +
        ((j->config.event_interval_ms_max - j->config.event_interval_ms_min) / 2u);
    return YETTY_OK(yetty_yjungle_ptr, j);
}

void yetty_yjungle_destroy(struct yetty_yjungle *j)
{
    if (!j) {
        return;
    }
    for (uint32_t i = 0; i < j->chain_len; i++) {
        segment_free(&j->chain[i]);
    }
    free(j->chain);
    free(j);
}

struct yetty_ycore_void_result yetty_yjungle_set_scene_size(
    struct yetty_yjungle *j, float scene_width, float scene_height)
{
    if (!j) {
        return YETTY_ERR(yetty_ycore_void, "yjungle: NULL");
    }
    if (scene_width  < 16.0f) scene_width  = 16.0f;
    if (scene_height < 16.0f) scene_height = 16.0f;
    j->config.scene_width  = scene_width;
    j->config.scene_height = scene_height;
    return YETTY_OK_VOID();
}

const struct yetty_yjungle_config *yetty_yjungle_config_get(const struct yetty_yjungle *j)
{
    return j ? &j->config : NULL;
}

/*=============================================================================
 * Tick — drive one frame
 *===========================================================================*/

/* Pick the next event delay; called after each event fires. */
static void schedule_next_event(struct yetty_yjungle *j, uint64_t now_ms)
{
    j->last_event_ms = now_ms;
    uint32_t span = j->config.event_interval_ms_max - j->config.event_interval_ms_min;
    j->next_event_delay_ms =
        j->config.event_interval_ms_min + (span ? rng_uint(j, span + 1u) : 0u);
}

/* Extend the chain by one segment at the tail. Caller has already
 * reserved capacity. Emits the new GROUP into `buf` and updates
 * `*z_order_counter`. */
static struct yetty_ycore_void_result do_extend(struct yetty_yjungle *j,
                                                 struct yetty_ydraw_draw_list *buf,
                                                 uint32_t *z_order_counter)
{
    float sx = j->cursor_x;
    float sy = j->cursor_y;
    float ex, ey;
    random_next_point(j, sx, sy, &ex, &ey);

    struct yjungle_segment *seg = &j->chain[j->chain_len];
    memset(seg, 0, sizeof(*seg));
    generate_subtree_into(j, seg, sx, sy, ex, ey, 0u);
    j->chain_len++;
    j->cursor_x = ex;
    j->cursor_y = ey;

    return emit_segment_subtree(buf, seg, z_order_counter);
}

/* Replace a randomly-picked existing segment in place. The new segment
 * keeps the old (start, end); only its tree/primitive content changes.
 * Emits DELETE(old_id) followed by GROUP(new_id). */
static struct yetty_ycore_void_result do_replace(struct yetty_yjungle *j,
                                                  struct yetty_ydraw_draw_list *buf,
                                                  uint32_t *z_order_counter)
{
    uint32_t idx = rng_uint(j, j->chain_len);
    struct yjungle_segment *seg = &j->chain[idx];

    /* Snapshot the endpoints — these stay so the chain neighbours
     * remain connected. */
    float sx = seg->start_x;
    float sy = seg->start_y;
    float ex = seg->end_x;
    float ey = seg->end_y;
    uint32_t old_id = seg->group_id;

    /* Emit DELETE(old_id) at the envelope root. */
    struct yetty_ycore_void_result dr = yetty_ydraw_draw_list_add_cmd_delete(buf, old_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "yjungle: DELETE old segment");

    /* Free the old subtree and build a fresh one in place. */
    segment_free(seg);
    memset(seg, 0, sizeof(*seg));
    generate_subtree_into(j, seg, sx, sy, ex, ey, 0u);

    return emit_segment_subtree(buf, seg, z_order_counter);
}

struct yetty_ycore_void_result yetty_yjungle_tick(struct yetty_yjungle *j,
                                                   struct yetty_ydraw_draw_list *buf,
                                                   uint64_t now_ms)
{
    if (!j) {
        return YETTY_ERR(yetty_ycore_void, "yjungle_tick: NULL jungle");
    }
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "yjungle_tick: NULL buf");
    }

    yetty_ydraw_draw_list_clear(buf);
    yetty_ydraw_draw_list_set_scene_bounds(buf, 0.0f, 0.0f,
                                            j->config.scene_width,
                                            j->config.scene_height);

    /* z_order is local to this envelope. Each new envelope is a delta
     * — its prims layer on top of what's already on the canvas, so the
     * receiver's z_order field only needs to be unique within the
     * envelope (and prefer newer prims on conflicts). */
    uint32_t z_order = 0u;

    if (!j->first_tick_done) {
        /* First tick: full-redraw envelope. CMD_ZERO clears any prior
         * scene-canvas state, then we emit GROUP(...) for every initial
         * chain segment. */
        struct yetty_ycore_void_result zr = yetty_ydraw_draw_list_add_cmd_zero(buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, zr, "yjungle_tick: add_cmd_zero");

        struct yetty_ycore_void_result rr =
            chain_reserve(j, j->config.initial_chain_length);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "yjungle_tick: initial reserve");

        for (uint32_t i = 0; i < j->config.initial_chain_length; i++) {
            struct yetty_ycore_void_result er = do_extend(j, buf, &z_order);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "yjungle_tick: initial extend");
        }
        j->first_tick_done = true;
        schedule_next_event(j, now_ms);
        return YETTY_OK_VOID();
    }

    /* Subsequent ticks: only emit if it's time for an event. */
    if (now_ms - j->last_event_ms < (uint64_t)j->next_event_delay_ms) {
        return YETTY_OK_VOID();
    }

    /* Decide event type. */
    bool can_extend = (j->chain_len < j->config.max_chain_length);
    bool can_replace = (j->chain_len > 0u);
    bool be_extend;
    if (can_extend && !can_replace) {
        be_extend = true;
    } else if (!can_extend && can_replace) {
        be_extend = false;
    } else if (!can_extend && !can_replace) {
        /* chain_len == 0 AND already at cap (== 0) — shouldn't happen
         * for sane configs; just skip. */
        schedule_next_event(j, now_ms);
        return YETTY_OK_VOID();
    } else {
        be_extend = (rng_f01(j) < j->config.extend_probability);
    }

    if (be_extend) {
        struct yetty_ycore_void_result rr = chain_reserve(j, j->chain_len + 1u);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "yjungle_tick: extend reserve");
        struct yetty_ycore_void_result er = do_extend(j, buf, &z_order);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "yjungle_tick: extend");
    } else {
        struct yetty_ycore_void_result rr = do_replace(j, buf, &z_order);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "yjungle_tick: replace");
    }

    schedule_next_event(j, now_ms);
    return YETTY_OK_VOID();
}
