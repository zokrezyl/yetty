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
 * after the children finish — see drawable-list.h. This stresses scene-
 * canvas's process_group_body recursive parser.
 */

#include <yetty/yjungle/yjungle.h>

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <yetty/yplatform/time.h>
#include <yetty/ytrace/ytrace.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Same 22-shape table as yzoo's emit_shape, so the visual feel is
 * consistent across the two tools. */
#define YJUNGLE_SHAPE_COUNT 22
#define YJUNGLE_TWO_PI 6.28318530717958647692f

/*=============================================================================
 * Segment tree
 *===========================================================================*/

struct yjungle_segment {
    uint32_t group_id; /* wire id; monotonic, never reused */
    float start_x, start_y;
    float end_x, end_y;
    uint32_t depth;

    /* If is_group: children[] is the chained sub-segments; primitive
     * fields below are unused. If !is_group: this is a leaf primitive
     * and children[] is empty. */
    bool is_group;
    struct yjungle_segment *children;
    uint32_t children_count;

    /* Primitive parameters (leaf only). */
    int shape_choice;
    uint32_t color;
    float stroke_width;
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
    bool first_tick_done;
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
        r1 = c;
        g1 = x;
        b1 = 0.0f;
    } else if (hue < 120.0f) {
        r1 = x;
        g1 = c;
        b1 = 0.0f;
    } else if (hue < 180.0f) {
        r1 = 0.0f;
        g1 = c;
        b1 = x;
    } else if (hue < 240.0f) {
        r1 = 0.0f;
        g1 = x;
        b1 = c;
    } else if (hue < 300.0f) {
        r1 = x;
        g1 = 0.0f;
        b1 = c;
    } else {
        r1 = c;
        g1 = 0.0f;
        b1 = x;
    }

    uint8_t r = (uint8_t)((r1 + m) * 255.0f);
    uint8_t g = (uint8_t)((g1 + m) * 255.0f);
    uint8_t b = (uint8_t)((b1 + m) * 255.0f);
    /* ABGR byte order — matches the pipeline yzoo writes into. */
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

/*=============================================================================
 * Pick the next chain endpoint — uniformly random over the whole canvas
 * (with a small off-canvas margin so the receiver occasionally sees
 * partially-off-screen segments). The previous step-based random walk
 * made segments tiny; the user wants the chain to span the full area.
 *===========================================================================*/

static void random_next_point(struct yetty_yjungle *j, float sx, float sy, float *ex, float *ey)
{
    (void)sx;
    (void)sy;
    const struct yetty_yjungle_config *cfg = &j->config;
    float xmin = -cfg->off_canvas_margin;
    float ymin = -cfg->off_canvas_margin;
    float xmax = cfg->scene_width + cfg->off_canvas_margin;
    float ymax = cfg->scene_height + cfg->off_canvas_margin;
    *ex = rng_range(j, xmin, xmax);
    *ey = rng_range(j, ymin, ymax);
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

static void generate_subtree_into(struct yetty_yjungle *j, struct yjungle_segment *seg, float sx,
                                  float sy, float ex, float ey, uint32_t depth)
{
    seg->group_id = j->next_group_id++;
    seg->start_x = sx;
    seg->start_y = sy;
    seg->end_x = ex;
    seg->end_y = ey;
    seg->depth = depth;
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
    if (lo < 2u) {
        lo = 2u;
    }
    if (hi < lo) {
        hi = lo;
    }
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
        perp_y = dx / span_len;
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
        generate_subtree_into(j, &seg->children[i], prev_x, prev_y, child_ex, child_ey, depth + 1u);
        prev_x = child_ex;
        prev_y = child_ey;
    }
}

/*=============================================================================
 * Primitive emission. Every leaf segment carries an SDF shape whose two
 * anchor points are exactly the segment's (sx, sy) start and (ex, ey)
 * end — that way consecutive chain segments visibly meet at shared
 * endpoints regardless of which shape was rolled.
 *
 * Most SDF shapes (circle, pentagon, …) are radially defined and don't
 * naturally have two anchor points. For those we emit a "bead" at the
 * start anchor PLUS a stroked segment along the spine; the bead radius
 * is small so adjacent chain neighbours' beads at the shared endpoint
 * overlap and the chain looks continuous. The segment+stroke spine
 * carries the connection itself.
 *
 * Variant assignment (modulo YJUNGLE_SHAPE_COUNT):
 *   0   segment (just a line)
 *   1   capsule (rounded thick line)
 *   2   thin rotated box aligned to spine
 *   3   triangle with 2 verts at endpoints + perpendicular peak
 *   4-N spine line + bead-of-shape-X at start
 *===========================================================================*/

static struct yetty_ycore_void_result emit_bead_at(struct yetty_ydraw_drawable_list *buf,
                                                   uint32_t z_order, int variant, float ax,
                                                   float ay, float r, uint32_t color, float scene_w,
                                                   float scene_h);

/* yjungle authors prims in the widget's client-area coordinate system —
 * (0,0) to (scene_width, scene_height). random_next_point used to pick
 * endpoints with an off-canvas margin so chain segments could spill
 * out — that's incompatible with "the receiver doesn't clip". Every
 * emit point now checks its full AABB and skips anything that would
 * leave the box. */
static inline float yj_min2(float a, float b)
{
    return a < b ? a : b;
}
static inline float yj_max2(float a, float b)
{
    return a > b ? a : b;
}
static inline float yj_min3(float a, float b, float c)
{
    return yj_min2(yj_min2(a, b), c);
}
static inline float yj_max3(float a, float b, float c)
{
    return yj_max2(yj_max2(a, b), c);
}

static int yjungle_aabb_in_bounds(float x0, float y0, float x1, float y1, float scene_w,
                                  float scene_h)
{
    if (x0 < 0.0f || y0 < 0.0f) {
        return 0;
    }
    if (x1 > scene_w || y1 > scene_h) {
        return 0;
    }
    return 1;
}

static struct yetty_ycore_void_result emit_primitive(struct yetty_ydraw_drawable_list *buf,
                                                     uint32_t z_order,
                                                     const struct yjungle_segment *seg,
                                                     float scene_w, float scene_h)
{
    float sx = seg->start_x, sy = seg->start_y;
    float ex = seg->end_x, ey = seg->end_y;
    float dx = ex - sx;
    float dy = ey - sy;
    float length = sqrtf(dx * dx + dy * dy);
    if (length < 1e-3f) {
        /* Degenerate — still emit a tiny circle at the endpoint so the
         * canvas isn't unaccounted-for. */
        struct yetty_ysdf_circle g = {sx, sy, 2.0f};
        return yetty_ydraw_drawable_list_add_cmd_add_circle(buf, 0, z_order, seg->color, 0u, 0.0f, &g);
    }
    float nx = dx / length; /* unit along spine */
    float ny = dy / length;
    float px = -ny; /* unit perpendicular */
    float py = nx;
    uint32_t color = seg->color;
    float stroke = seg->stroke_width;
    int choice = seg->shape_choice;
    if (choice < 0) {
        choice = 0;
    }
    choice %= YJUNGLE_SHAPE_COUNT;

    switch (choice) {
    case 0: {
        /* Plain stroked line, start→end. */
        float half = stroke * 0.5f;
        float ax0 = yj_min2(sx, ex) - half;
        float ay0 = yj_min2(sy, ey) - half;
        float ax1 = yj_max2(sx, ex) + half;
        float ay1 = yj_max2(sy, ey) + half;
        if (!yjungle_aabb_in_bounds(ax0, ay0, ax1, ay1, scene_w, scene_h)) {
            ydebug("yjungle: skip LINE aabb=(%.1f,%.1f .. %.1f,%.1f) scene=%.1fx%.1f", ax0, ay0,
                   ax1, ay1, scene_w, scene_h);
            return YETTY_OK_VOID();
        }
        struct yetty_ysdf_segment g = {sx, sy, ex, ey};
        return yetty_ydraw_drawable_list_add_cmd_add_segment(buf, 0, z_order, 0u, color, stroke, &g);
    }
    case 1: {
        /* Capsule = thick rounded line from start to end. */
        float radius = stroke * 1.5f;
        if (radius < 2.0f) {
            radius = 2.0f;
        }
        float ax0 = yj_min2(sx, ex) - radius;
        float ay0 = yj_min2(sy, ey) - radius;
        float ax1 = yj_max2(sx, ex) + radius;
        float ay1 = yj_max2(sy, ey) + radius;
        if (!yjungle_aabb_in_bounds(ax0, ay0, ax1, ay1, scene_w, scene_h)) {
            ydebug("yjungle: skip CAPSULE aabb=(%.1f,%.1f .. %.1f,%.1f) scene=%.1fx%.1f", ax0, ay0,
                   ax1, ay1, scene_w, scene_h);
            return YETTY_OK_VOID();
        }
        struct yetty_ysdf_capsule g = {sx, sy, ex, ey, radius};
        return yetty_ydraw_drawable_list_add_cmd_add_capsule(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 2: {
        /* Rotated thin box aligned along the spine. SDF box is
         * axis-aligned and has no rotation parameter, so we approximate
         * with a stroked segment of larger thickness — same chain
         * semantics but visually distinct from case 0. */
        float half = stroke * 1.5f;
        float ax0 = yj_min2(sx, ex) - half;
        float ay0 = yj_min2(sy, ey) - half;
        float ax1 = yj_max2(sx, ex) + half;
        float ay1 = yj_max2(sy, ey) + half;
        if (!yjungle_aabb_in_bounds(ax0, ay0, ax1, ay1, scene_w, scene_h)) {
            ydebug("yjungle: skip ROTBOX aabb=(%.1f,%.1f .. %.1f,%.1f) scene=%.1fx%.1f", ax0, ay0,
                   ax1, ay1, scene_w, scene_h);
            return YETTY_OK_VOID();
        }
        struct yetty_ysdf_segment g = {sx, sy, ex, ey};
        return yetty_ydraw_drawable_list_add_cmd_add_segment(buf, 0, z_order, 0u, color, stroke * 3.0f,
                                                         &g);
    }
    case 3: {
        /* Triangle: two verts at the chain endpoints, third vertex
         * offset perpendicular by ~25% of the spine length. */
        float off = length * 0.25f;
        float tx = (sx + ex) * 0.5f + px * off;
        float ty = (sy + ey) * 0.5f + py * off;
        float ax0 = yj_min3(sx, ex, tx);
        float ay0 = yj_min3(sy, ey, ty);
        float ax1 = yj_max3(sx, ex, tx);
        float ay1 = yj_max3(sy, ey, ty);
        if (!yjungle_aabb_in_bounds(ax0, ay0, ax1, ay1, scene_w, scene_h)) {
            ydebug("yjungle: skip TRI aabb=(%.1f,%.1f .. %.1f,%.1f) scene=%.1fx%.1f", ax0, ay0, ax1,
                   ay1, scene_w, scene_h);
            return YETTY_OK_VOID();
        }
        struct yetty_ysdf_triangle g = {sx, sy, ex, ey, tx, ty};
        return yetty_ydraw_drawable_list_add_cmd_add_triangle(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    default: {
        /* Spine + bead. Emit the spine first (lower z_order? no — the
         * receiver paints in submission order for ties, so the bead
         * landing AFTER the spine sits on top, which is what we want). */
        float half = stroke * 0.5f;
        float ax0 = yj_min2(sx, ex) - half;
        float ay0 = yj_min2(sy, ey) - half;
        float ax1 = yj_max2(sx, ex) + half;
        float ay1 = yj_max2(sy, ey) + half;
        if (!yjungle_aabb_in_bounds(ax0, ay0, ax1, ay1, scene_w, scene_h)) {
            ydebug("yjungle: skip SPINE aabb=(%.1f,%.1f .. %.1f,%.1f) scene=%.1fx%.1f", ax0, ay0,
                   ax1, ay1, scene_w, scene_h);
            return YETTY_OK_VOID();
        }
        struct yetty_ysdf_segment g = {sx, sy, ex, ey};
        struct yetty_ycore_void_result sr =
            yetty_ydraw_drawable_list_add_cmd_add_segment(buf, 0, z_order, 0u, color, stroke, &g);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yjungle: spine emit");
        /* Bead radius scales with stroke but stays small enough that
         * the next segment's bead at the shared endpoint visibly
         * overlaps — keeps the chain crisp. */
        float bead = stroke * 3.0f;
        if (bead < 5.0f) {
            bead = 5.0f;
        }
        if (bead > length * 0.4f) {
            bead = length * 0.4f;
        }
        return emit_bead_at(buf, z_order + 1u, choice, sx, sy, bead, color, scene_w, scene_h);
    }
    }
}

/* Bead = small shape placed at (ax, ay) with characteristic size r.
 * `variant` selects which SDF flavour. Variants are taken from the
 * choice value the segment was generated with, so each segment's bead
 * is stable across re-renders. */
static struct yetty_ycore_void_result emit_bead_at(struct yetty_ydraw_drawable_list *buf,
                                                   uint32_t z_order, int variant, float ax,
                                                   float ay, float r, uint32_t color, float scene_w,
                                                   float scene_h)
{
    /* All 18 bead variants fit inside a (1.5r × 1.5r) box centred at
     * (ax, ay) — rhombus/ellipse use up to 1.2r on one axis, the rest
     * stay within r. Use 1.5r as a safe over-approximation. */
    float ext = r * 1.5f;
    if (!yjungle_aabb_in_bounds(ax - ext, ay - ext, ax + ext, ay + ext, scene_w, scene_h)) {
        ydebug("yjungle: skip BEAD variant=%d pos=(%.1f,%.1f) r=%.1f scene=%.1fx%.1f", variant, ax,
               ay, r, scene_w, scene_h);
        return YETTY_OK_VOID();
    }
    switch (variant % 18) {
    case 0: {
        struct yetty_ysdf_circle g = {ax, ay, r};
        return yetty_ydraw_drawable_list_add_cmd_add_circle(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 1: {
        struct yetty_ysdf_box g = {ax, ay, r, r, r * 0.2f};
        return yetty_ydraw_drawable_list_add_cmd_add_box(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 2: {
        struct yetty_ysdf_ellipse g = {ax, ay, r * 1.2f, r * 0.8f};
        return yetty_ydraw_drawable_list_add_cmd_add_ellipse(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 3: {
        struct yetty_ysdf_rounded_box g = {ax, ay, r, r, r * 0.3f, r * 0.3f, r * 0.3f, r * 0.3f};
        return yetty_ydraw_drawable_list_add_cmd_add_rounded_box(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 4: {
        struct yetty_ysdf_rhombus g = {ax, ay, r, r * 1.2f};
        return yetty_ydraw_drawable_list_add_cmd_add_rhombus(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 5: {
        struct yetty_ysdf_pentagon g = {ax, ay, r};
        return yetty_ydraw_drawable_list_add_cmd_add_pentagon(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 6: {
        struct yetty_ysdf_hexagon g = {ax, ay, r};
        return yetty_ydraw_drawable_list_add_cmd_add_hexagon(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 7: {
        struct yetty_ysdf_star g = {ax, ay, r, 5.0f, 2.5f};
        return yetty_ydraw_drawable_list_add_cmd_add_star(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 8: {
        struct yetty_ysdf_pie g = {ax, ay, 0.866f, 0.5f, r};
        return yetty_ydraw_drawable_list_add_cmd_add_pie(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 9: {
        struct yetty_ysdf_ring g = {ax, ay, 0.866f, 0.5f, r, r * 0.25f};
        return yetty_ydraw_drawable_list_add_cmd_add_ring(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 10: {
        struct yetty_ysdf_heart g = {ax, ay, r};
        return yetty_ydraw_drawable_list_add_cmd_add_heart(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 11: {
        struct yetty_ysdf_cross g = {ax, ay, r, r * 0.3f, r * 0.1f};
        return yetty_ydraw_drawable_list_add_cmd_add_cross(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 12: {
        struct yetty_ysdf_rounded_x g = {ax, ay, r, r * 0.2f};
        return yetty_ydraw_drawable_list_add_cmd_add_rounded_x(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 13: {
        struct yetty_ysdf_moon g = {ax, ay, r * 0.5f, r, r * 0.8f};
        return yetty_ydraw_drawable_list_add_cmd_add_moon(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 14: {
        struct yetty_ysdf_egg g = {ax, ay, r, r * 0.6f};
        return yetty_ydraw_drawable_list_add_cmd_add_egg(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 15: {
        struct yetty_ysdf_octogon g = {ax, ay, r};
        return yetty_ydraw_drawable_list_add_cmd_add_octogon(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 16: {
        struct yetty_ysdf_hexagram g = {ax, ay, r};
        return yetty_ydraw_drawable_list_add_cmd_add_hexagram(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    case 17: {
        struct yetty_ysdf_pentagram g = {ax, ay, r};
        return yetty_ydraw_drawable_list_add_cmd_add_pentagram(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
    default: {
        struct yetty_ysdf_circle g = {ax, ay, r};
        return yetty_ydraw_drawable_list_add_cmd_add_circle(buf, 0, z_order, color, 0u, 0.0f, &g);
    }
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

static struct yetty_ycore_void_result emit_segment_subtree(struct yetty_ydraw_drawable_list *buf,
                                                           const struct yjungle_segment *seg,
                                                           uint32_t *z_order_counter, float scene_w,
                                                           float scene_h)
{
    struct yetty_ydraw_id_result br = yetty_ydraw_drawable_list_begin_group(buf, seg->group_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "yjungle: begin_group");
    uint32_t marker = br.value;

    if (seg->is_group) {
        for (uint32_t i = 0; i < seg->children_count; i++) {
            struct yetty_ycore_void_result cr =
                emit_segment_subtree(buf, &seg->children[i], z_order_counter, scene_w, scene_h);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "yjungle: child emit");
        }
    } else {
        struct yetty_ycore_void_result pr =
            emit_primitive(buf, (*z_order_counter)++, seg, scene_w, scene_h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "yjungle: primitive emit");
    }

    struct yetty_ycore_void_result er = yetty_ydraw_drawable_list_end_group(buf, marker);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "yjungle: end_group");
    return YETTY_OK_VOID();
}

/* Flat variant of emit_segment_subtree: walk the tree and emit each leaf
 * primitive directly, with no GROUP framing. Used by the full-redraw path
 * (yetty_yjungle_render) for consumers that paint a flat prim list each
 * frame (the ygui ydraw_embed widget) rather than accumulating GROUP/DELETE
 * deltas on a persistent canvas. */
static struct yetty_ycore_void_result emit_segment_flat(struct yetty_ydraw_drawable_list *buf,
                                                        const struct yjungle_segment *seg,
                                                        uint32_t *z_order_counter, float scene_w,
                                                        float scene_h)
{
    if (seg->is_group) {
        for (uint32_t i = 0; i < seg->children_count; i++) {
            struct yetty_ycore_void_result cr =
                emit_segment_flat(buf, &seg->children[i], z_order_counter, scene_w, scene_h);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "yjungle: flat child emit");
        }
        return YETTY_OK_VOID();
    }
    return emit_primitive(buf, (*z_order_counter)++, seg, scene_w, scene_h);
}

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_yjungle_config yetty_yjungle_config_default(void)
{
    struct yetty_yjungle_config cfg = {
        .scene_width = 800.0f,
        .scene_height = 600.0f,

        .step_min = 40.0f,
        .step_max = 140.0f,
        .off_canvas_margin = 60.0f,

        .max_depth = 3u,
        .group_prob_depth0 = 0.5f,
        .group_children_min = 2u,
        .group_children_max = 3u,

        .initial_chain_length = 8u,
        .max_chain_length = 30u,
        .extend_probability = 0.3f,

        .event_interval_ms_min = 500u,
        .event_interval_ms_max = 2500u,
    };
    return cfg;
}

static struct yetty_yjungle_config clamp_config(struct yetty_yjungle_config c)
{
    if (c.scene_width < 16.0f) {
        c.scene_width = 16.0f;
    }
    if (c.scene_height < 16.0f) {
        c.scene_height = 16.0f;
    }
    if (c.step_min < 1.0f) {
        c.step_min = 1.0f;
    }
    if (c.step_max < c.step_min) {
        c.step_max = c.step_min;
    }
    if (c.off_canvas_margin < 0.0f) {
        c.off_canvas_margin = 0.0f;
    }

    if (c.max_depth > 6u) {
        c.max_depth = 6u;
    }
    if (c.group_prob_depth0 < 0.0f) {
        c.group_prob_depth0 = 0.0f;
    }
    if (c.group_prob_depth0 > 1.0f) {
        c.group_prob_depth0 = 1.0f;
    }
    if (c.group_children_min < 2u) {
        c.group_children_min = 2u;
    }
    if (c.group_children_max < c.group_children_min) {
        c.group_children_max = c.group_children_min;
    }
    if (c.group_children_max > 6u) {
        c.group_children_max = 6u;
    }

    if (c.initial_chain_length < 1u) {
        c.initial_chain_length = 1u;
    }
    if (c.max_chain_length < c.initial_chain_length) {
        c.max_chain_length = c.initial_chain_length;
    }
    if (c.max_chain_length > 1000u) {
        c.max_chain_length = 1000u;
    }
    if (c.extend_probability < 0.0f) {
        c.extend_probability = 0.0f;
    }
    if (c.extend_probability > 1.0f) {
        c.extend_probability = 1.0f;
    }

    if (c.event_interval_ms_min < 10u) {
        c.event_interval_ms_min = 10u;
    }
    if (c.event_interval_ms_max < c.event_interval_ms_min) {
        c.event_interval_ms_max = c.event_interval_ms_min;
    }
    return c;
}

struct yetty_yjungle_ptr_result yetty_yjungle_create(const struct yetty_yjungle_config *config,
                                                     uint32_t seed)
{
    struct yetty_yjungle *j = calloc(1, sizeof(*j));
    if (!j) {
        return YETTY_ERR(yetty_yjungle_ptr, "yjungle: calloc failed");
    }
    j->config = clamp_config(config ? *config : yetty_yjungle_config_default());
    j->rng_state = seed != 0 ? (uint64_t)seed : seed_from_clock();
    j->next_group_id = 1u; /* 0 reserved for receiver root */
    j->cursor_x = j->config.scene_width * 0.5f;
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

struct yetty_ycore_void_result yetty_yjungle_set_scene_size(struct yetty_yjungle *j,
                                                            float scene_width, float scene_height)
{
    if (!j) {
        return YETTY_ERR(yetty_ycore_void, "yjungle: NULL");
    }
    if (scene_width < 16.0f) {
        scene_width = 16.0f;
    }
    if (scene_height < 16.0f) {
        scene_height = 16.0f;
    }
    j->config.scene_width = scene_width;
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
    j->next_event_delay_ms = j->config.event_interval_ms_min + (span ? rng_uint(j, span + 1u) : 0u);
}

/* Extend the chain by one segment at the tail. Caller has already
 * reserved capacity. Emits the new GROUP into `buf` and updates
 * `*z_order_counter`. */
static struct yetty_ycore_void_result do_extend(struct yetty_yjungle *j,
                                                struct yetty_ydraw_drawable_list *buf,
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

    return emit_segment_subtree(buf, seg, z_order_counter, j->config.scene_width,
                                j->config.scene_height);
}

/* Replace a randomly-picked existing segment in place. The new segment
 * keeps the old (start, end); only its tree/primitive content changes.
 * Emits DELETE(old_id) followed by GROUP(new_id). */
static struct yetty_ycore_void_result do_replace(struct yetty_yjungle *j,
                                                 struct yetty_ydraw_drawable_list *buf,
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
    struct yetty_ycore_void_result dr = yetty_ydraw_drawable_list_add_cmd_delete(buf, old_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "yjungle: DELETE old segment");

    /* Free the old subtree and build a fresh one in place. */
    segment_free(seg);
    memset(seg, 0, sizeof(*seg));
    generate_subtree_into(j, seg, sx, sy, ex, ey, 0u);

    return emit_segment_subtree(buf, seg, z_order_counter, j->config.scene_width,
                                j->config.scene_height);
}

struct yetty_ycore_void_result yetty_yjungle_tick(struct yetty_yjungle *j,
                                                  struct yetty_ydraw_drawable_list *buf,
                                                  uint64_t now_ms)
{
    if (!j) {
        return YETTY_ERR(yetty_ycore_void, "yjungle_tick: NULL jungle");
    }
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "yjungle_tick: NULL buf");
    }

    yetty_ydraw_drawable_list_clear(buf);
    yetty_ydraw_drawable_list_set_scene_bounds(buf, 0.0f, 0.0f, j->config.scene_width,
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
        struct yetty_ycore_void_result zr = yetty_ydraw_drawable_list_add_cmd_zero(buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, zr, "yjungle_tick: add_cmd_zero");

        struct yetty_ycore_void_result rr = chain_reserve(j, j->config.initial_chain_length);
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

struct yetty_ycore_void_result yetty_yjungle_render(struct yetty_yjungle *j,
                                                    struct yetty_ydraw_drawable_list *buf,
                                                    uint64_t now_ms)
{
    if (!j) {
        return YETTY_ERR(yetty_ycore_void, "yjungle_render: NULL jungle");
    }
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "yjungle_render: NULL buf");
    }

    /* Advance the simulation (mutates the chain). The tick writes its
     * incremental commands into a scratch buffer we throw away — only the
     * chain side effect matters here. */
    struct yetty_ydraw_drawable_list_result sr = yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yjungle_render: scratch create");
    struct yetty_ycore_void_result tr = yetty_yjungle_tick(j, sr.value, now_ms);
    yetty_ydraw_drawable_list_destroy(sr.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "yjungle_render: advance");

    /* Emit the whole current chain as a flat prim list, replacing the
     * buffer each frame (the full-redraw model the ydraw_embed widget
     * needs). */
    yetty_ydraw_drawable_list_clear(buf);
    yetty_ydraw_drawable_list_set_scene_bounds(buf, 0.0f, 0.0f, j->config.scene_width,
                                           j->config.scene_height);
    uint32_t z_order = 0u;
    for (uint32_t i = 0; i < j->chain_len; i++) {
        struct yetty_ycore_void_result er = emit_segment_flat(
            buf, &j->chain[i], &z_order, j->config.scene_width, j->config.scene_height);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "yjungle_render: segment");
    }
    return YETTY_OK_VOID();
}
