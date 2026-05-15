/*
 * yzoo.c — animated control-point zoo, drawn as ydraw SDF prims.
 *
 * Port of yetty-poc/src/yetty/ydraw-zoo/zoo-renderer.cpp.
 *
 * Notable differences from the C++ original:
 *  - splitmix64 PRNG instead of std::mt19937 (matches ymaze.c).
 *  - Curve connections (type 0) approximated as a polyline of straight
 *    segments — the new ysdf API has no quadratic-bezier primitive.
 *  - Shape connections (type 2) draw from the 22 ysdf shapes that exist in
 *    the new codebase, not the C++ ydraw 43-shape table.
 *  - bg_color stays in the config but is not painted by the renderer (the
 *    layer underneath provides the background); same convention as ymaze.
 */

#include <yetty/yzoo/yzoo.h>

#include <yetty/ydraw-core/buffer.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <yetty/yplatform/time.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Tunables and clamps (mirror the C++ argv parser bounds).
 *===========================================================================*/

#define YZOO_TARGET_POINTS_MIN 2u
#define YZOO_TARGET_POINTS_MAX 100u
#define YZOO_CONNS_MIN 1u
#define YZOO_CONNS_MAX 10u
#define YZOO_GROWTH_MIN 0.05f
#define YZOO_GROWTH_MAX 3.0f
#define YZOO_MAX_DIST_MIN 20.0f
#define YZOO_MAX_DIST_MAX 600.0f
#define YZOO_MARKER_MIN 0.5f
#define YZOO_MARKER_MAX 20.0f
#define YZOO_STROKE_MIN_LO 0.1f
#define YZOO_STROKE_MAX_HI 20.0f

#define YZOO_BEZIER_SEGMENTS_MIN 1u
#define YZOO_BEZIER_SEGMENTS_MAX 32u

/* Number of shape variants available in the new ysdf API for type-2 conns. */
#define YZOO_SHAPE_COUNT 22

#define YZOO_TWO_PI 6.28318530717958647692f

/*=============================================================================
 * Internal state
 *===========================================================================*/

struct yzoo_control_point {
    float angle;
    float spawn_time;
    float base_radius;
    float anim_phase;
    float anim_freq;
    uint32_t color;
};

struct yzoo_connection {
    uint32_t cp_a;
    uint32_t cp_b;
    int type; /* 0 = curve, 1 = line, 2 = shape */
    int shape_choice;
    float curvature;
    float curve_anim_phase;
    float curve_anim_freq;
    float stroke_width;
    uint32_t color;
};

struct yetty_yzoo {
    struct yetty_yzoo_config config;

    uint64_t rng_state;

    struct yzoo_control_point *cps;
    uint32_t cp_len;
    uint32_t cp_cap;

    struct yzoo_connection *conns;
    uint32_t conn_len;
    uint32_t conn_cap;

    bool initialized;
};

/*=============================================================================
 * splitmix64 PRNG — same as ymaze.c.
 *===========================================================================*/

static uint64_t yzoo_rng_next(struct yetty_yzoo *z)
{
    uint64_t v = (z->rng_state += 0x9E3779B97F4A7C15ULL);
    v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
    v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
    return v ^ (v >> 31);
}

static float yzoo_rng_float01(struct yetty_yzoo *z)
{
    /* Top 24 bits → [0, 1). */
    return (float)(yzoo_rng_next(z) >> 40) * (1.0f / 16777216.0f);
}

static float yzoo_rng_range(struct yetty_yzoo *z, float lo, float hi)
{
    return lo + (hi - lo) * yzoo_rng_float01(z);
}

static uint32_t yzoo_rng_uint(struct yetty_yzoo *z, uint32_t bound)
{
    if (bound <= 1) {
        return 0;
    }
    return (uint32_t)(yzoo_rng_next(z) % bound);
}

static uint64_t yzoo_seed_from_clock(void)
{
    double t = yetty_yplatform_ytime_monotonic_sec();
    uint64_t s = (uint64_t)(t * 1.0e9);
    if (s == 0) {
        s = 1;
    }
    return s;
}

/*=============================================================================
 * Color helpers — HSL → AARRGGBB (matches the C++ randomColor).
 *===========================================================================*/

static uint32_t yzoo_random_color(struct yetty_yzoo *z)
{
    float hue = yzoo_rng_range(z, 0.0f, 360.0f);
    float sat = yzoo_rng_range(z, 0.6f, 1.0f);
    float lit = yzoo_rng_range(z, 0.45f, 0.65f);

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

    /* The C++ code packs as 0xFF000000 | (B<<16) | (G<<8) | R — i.e. ABGR
     * byte order. Keep that so the visual output matches the original. */
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static uint32_t yzoo_blend_colors(uint32_t a, uint32_t b)
{
    uint32_t r = ((a & 0xFFu) + (b & 0xFFu)) / 2u;
    uint32_t g = (((a >> 8) & 0xFFu) + ((b >> 8) & 0xFFu)) / 2u;
    uint32_t bl = (((a >> 16) & 0xFFu) + ((b >> 16) & 0xFFu)) / 2u;
    return 0xFF000000u | (bl << 16) | (g << 8) | r;
}

/*=============================================================================
 * Dynamic-array growth helpers.
 *===========================================================================*/

static struct yetty_ycore_void_result yzoo_cps_reserve(struct yetty_yzoo *z, uint32_t need)
{
    if (need <= z->cp_cap) {
        return YETTY_OK_VOID();
    }
    uint32_t nc = z->cp_cap ? z->cp_cap : 16u;
    while (nc < need) {
        nc *= 2u;
    }
    struct yzoo_control_point *np = realloc(z->cps, nc * sizeof(*np));
    if (!np) {
        return YETTY_ERR(yetty_ycore_void, "yzoo: cps realloc failed");
    }
    z->cps = np;
    z->cp_cap = nc;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yzoo_conns_reserve(struct yetty_yzoo *z, uint32_t need)
{
    if (need <= z->conn_cap) {
        return YETTY_OK_VOID();
    }
    uint32_t nc = z->conn_cap ? z->conn_cap : 32u;
    while (nc < need) {
        nc *= 2u;
    }
    struct yzoo_connection *np = realloc(z->conns, nc * sizeof(*np));
    if (!np) {
        return YETTY_ERR(yetty_ycore_void, "yzoo: conns realloc failed");
    }
    z->conns = np;
    z->conn_cap = nc;
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Control-point lifecycle.
 *===========================================================================*/

struct yzoo_vec2 {
    float x;
    float y;
};

static struct yzoo_vec2 yzoo_cp_position(const struct yetty_yzoo *z,
                                         const struct yzoo_control_point *cp, float time)
{
    float center_x = z->config.scene_width * 0.5f;
    float center_y = z->config.scene_height * 0.5f;
    float age = time - cp->spawn_time;
    if (age < 0.0f) {
        age = 0.0f;
    }
    float scale = expf(z->config.growth_rate * age);
    float dist = cp->base_radius * scale;
    struct yzoo_vec2 p = {
        center_x + dist * cosf(cp->angle),
        center_y + dist * sinf(cp->angle),
    };
    return p;
}

static struct yetty_ycore_void_result yzoo_spawn_cp(struct yetty_yzoo *z, float time)
{
    struct yetty_ycore_void_result rr = yzoo_cps_reserve(z, z->cp_len + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "yzoo: spawn cp");

    struct yzoo_control_point *cp = &z->cps[z->cp_len++];
    cp->angle = yzoo_rng_range(z, 0.0f, YZOO_TWO_PI);
    cp->spawn_time = time;
    cp->base_radius = yzoo_rng_range(z, z->config.spawn_radius_min, z->config.spawn_radius_max);
    cp->anim_phase = yzoo_rng_range(z, 0.0f, YZOO_TWO_PI);
    cp->anim_freq = yzoo_rng_range(z, 0.8f, 2.5f);
    cp->color = yzoo_random_color(z);
    return YETTY_OK_VOID();
}

/* Drop control point `idx`; drop any connection touching it; renumber
 * surviving connections so later indices shift down by one. Mirror of the
 * C++ removeControlPoint. */
static void yzoo_remove_cp(struct yetty_yzoo *z, uint32_t idx)
{
    if (idx >= z->cp_len) {
        return;
    }

    /* Remove connections referencing idx in-place. */
    uint32_t w = 0;
    for (uint32_t r = 0; r < z->conn_len; r++) {
        if (z->conns[r].cp_a == idx || z->conns[r].cp_b == idx) {
            continue;
        }
        if (w != r) {
            z->conns[w] = z->conns[r];
        }
        w++;
    }
    z->conn_len = w;

    /* Renumber: any reference > idx shifts down. */
    for (uint32_t i = 0; i < z->conn_len; i++) {
        if (z->conns[i].cp_a > idx) {
            z->conns[i].cp_a--;
        }
        if (z->conns[i].cp_b > idx) {
            z->conns[i].cp_b--;
        }
    }

    /* Remove cp[idx] in-place. */
    if (idx + 1 < z->cp_len) {
        memmove(&z->cps[idx], &z->cps[idx + 1],
                (z->cp_len - idx - 1) * sizeof(*z->cps));
    }
    z->cp_len--;
}

/*=============================================================================
 * Connection lifecycle.
 *===========================================================================*/

static struct yetty_ycore_void_result yzoo_update_connections(struct yetty_yzoo *z, float time)
{
    /* Drop connections whose endpoints have drifted too far apart, plus any
     * with stale indices. Walk in-place. */
    uint32_t w = 0;
    for (uint32_t r = 0; r < z->conn_len; r++) {
        const struct yzoo_connection *c = &z->conns[r];
        if (c->cp_a >= z->cp_len || c->cp_b >= z->cp_len) {
            continue;
        }
        struct yzoo_vec2 pa = yzoo_cp_position(z, &z->cps[c->cp_a], time);
        struct yzoo_vec2 pb = yzoo_cp_position(z, &z->cps[c->cp_b], time);
        float dx = pb.x - pa.x;
        float dy = pb.y - pa.y;
        if (sqrtf(dx * dx + dy * dy) > z->config.max_connection_dist) {
            continue;
        }
        if (w != r) {
            z->conns[w] = z->conns[r];
        }
        w++;
    }
    z->conn_len = w;

    /* Tally per-cp connection counts. */
    uint32_t *count = NULL;
    if (z->cp_len > 0) {
        count = calloc(z->cp_len, sizeof(*count));
        if (!count) {
            return YETTY_ERR(yetty_ycore_void, "yzoo: count alloc failed");
        }
        for (uint32_t i = 0; i < z->conn_len; i++) {
            count[z->conns[i].cp_a]++;
            count[z->conns[i].cp_b]++;
        }
    }

    /* Connect under-connected cps to nearest neighbour. O(N²) over cp_len
     * — same as C++; cp_len is bounded by target_points <= 100. */
    for (uint32_t i = 0; i < z->cp_len; i++) {
        if (count[i] >= z->config.target_conns_per_cp) {
            continue;
        }
        struct yzoo_vec2 pi = yzoo_cp_position(z, &z->cps[i], time);
        float best_dist = z->config.max_connection_dist;
        int best_j = -1;

        for (uint32_t j = 0; j < z->cp_len; j++) {
            if (j == i) {
                continue;
            }
            if (count[j] >= z->config.target_conns_per_cp) {
                continue;
            }

            bool already = false;
            for (uint32_t k = 0; k < z->conn_len; k++) {
                if ((z->conns[k].cp_a == i && z->conns[k].cp_b == j) ||
                    (z->conns[k].cp_a == j && z->conns[k].cp_b == i)) {
                    already = true;
                    break;
                }
            }
            if (already) {
                continue;
            }

            struct yzoo_vec2 pj = yzoo_cp_position(z, &z->cps[j], time);
            float dx = pj.x - pi.x;
            float dy = pj.y - pi.y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist < best_dist) {
                best_dist = dist;
                best_j = (int)j;
            }
        }

        if (best_j < 0) {
            continue;
        }

        struct yetty_ycore_void_result rr = yzoo_conns_reserve(z, z->conn_len + 1);
        if (YETTY_IS_ERR(rr)) {
            free(count);
            return rr;
        }

        struct yzoo_connection *c = &z->conns[z->conn_len++];
        c->cp_a = i;
        c->cp_b = (uint32_t)best_j;

        float r = yzoo_rng_float01(z);
        float seg_thresh = z->config.curve_ratio + (1.0f - z->config.curve_ratio) * 0.5f;
        if (r < z->config.curve_ratio) {
            c->type = 0;
        } else if (r < seg_thresh) {
            c->type = 1;
        } else {
            c->type = 2;
        }
        c->shape_choice = (int)yzoo_rng_uint(z, YZOO_SHAPE_COUNT);
        c->curvature = yzoo_rng_range(z, -0.8f, 0.8f);
        c->curve_anim_phase = yzoo_rng_range(z, 0.0f, YZOO_TWO_PI);
        c->curve_anim_freq = yzoo_rng_range(z, 0.3f, 1.2f);
        c->stroke_width = yzoo_rng_range(z, z->config.stroke_min, z->config.stroke_max);
        c->color = yzoo_blend_colors(z->cps[i].color, z->cps[(uint32_t)best_j].color);

        count[i]++;
        count[(uint32_t)best_j]++;
    }

    free(count);
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Shape primitive emission — 22 ysdf shapes.
 *
 * `choice` is taken modulo YZOO_SHAPE_COUNT before we get here. The
 * stroke_color/stroke_width slots stay zero; we paint via fill.
 *===========================================================================*/

static void yzoo_emit_shape(struct yetty_ydraw_core_buffer *buf, uint32_t z_order, int choice,
                            float cx, float cy, float size, uint32_t color)
{
    switch (choice) {
    case 0: {
        struct yetty_ysdf_circle g = {cx, cy, size};
        yetty_ysdf_add_circle(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 1: {
        struct yetty_ysdf_box g = {cx, cy, size, size * 0.8f, size * 0.15f};
        yetty_ysdf_add_box(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 2: {
        struct yetty_ysdf_segment g = {cx - size, cy, cx + size, cy};
        /* Segments paint via stroke. */
        yetty_ysdf_add_segment(buf, z_order, 0u, color, size * 0.15f, &g);
        break;
    }
    case 3: {
        struct yetty_ysdf_triangle g = {
            cx, cy - size,
            cx - size, cy + size * 0.7f,
            cx + size, cy + size * 0.7f,
        };
        yetty_ysdf_add_triangle(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 4: {
        struct yetty_ysdf_ellipse g = {cx, cy, size, size * 0.6f};
        yetty_ysdf_add_ellipse(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 5: {
        struct yetty_ysdf_arc g = {cx, cy, 0.866f, 0.5f, size, size * 0.2f};
        yetty_ysdf_add_arc(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 6: {
        struct yetty_ysdf_rounded_box g = {
            cx, cy, size, size * 0.7f,
            size * 0.2f, size * 0.2f, size * 0.2f, size * 0.2f,
        };
        yetty_ysdf_add_rounded_box(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 7: {
        struct yetty_ysdf_rhombus g = {cx, cy, size, size * 1.4f};
        yetty_ysdf_add_rhombus(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 8: {
        struct yetty_ysdf_pentagon g = {cx, cy, size};
        yetty_ysdf_add_pentagon(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 9: {
        struct yetty_ysdf_hexagon g = {cx, cy, size};
        yetty_ysdf_add_hexagon(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 10: {
        struct yetty_ysdf_star g = {cx, cy, size, 5.0f, 2.5f};
        yetty_ysdf_add_star(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 11: {
        struct yetty_ysdf_pie g = {cx, cy, 0.866f, 0.5f, size};
        yetty_ysdf_add_pie(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 12: {
        struct yetty_ysdf_ring g = {cx, cy, 0.866f, 0.5f, size, size * 0.2f};
        yetty_ysdf_add_ring(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 13: {
        struct yetty_ysdf_heart g = {cx, cy, size};
        yetty_ysdf_add_heart(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 14: {
        struct yetty_ysdf_cross g = {cx, cy, size, size * 0.3f, size * 0.1f};
        yetty_ysdf_add_cross(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 15: {
        struct yetty_ysdf_rounded_x g = {cx, cy, size, size * 0.2f};
        yetty_ysdf_add_rounded_x(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 16: {
        struct yetty_ysdf_capsule g = {cx - size * 0.7f, cy, cx + size * 0.7f, cy, size * 0.3f};
        /* Capsule has thickness already; paint via fill. */
        yetty_ysdf_add_capsule(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 17: {
        struct yetty_ysdf_moon g = {cx, cy, size * 0.5f, size, size * 0.8f};
        yetty_ysdf_add_moon(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 18: {
        struct yetty_ysdf_egg g = {cx, cy, size, size * 0.6f};
        yetty_ysdf_add_egg(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 19: {
        struct yetty_ysdf_octogon g = {cx, cy, size};
        yetty_ysdf_add_octogon(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 20: {
        struct yetty_ysdf_hexagram g = {cx, cy, size};
        yetty_ysdf_add_hexagram(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    case 21: {
        struct yetty_ysdf_pentagram g = {cx, cy, size};
        yetty_ysdf_add_pentagram(buf, z_order, color, 0u, 0.0f, &g);
        break;
    }
    default:
        break;
    }
}

static uint32_t yzoo_emit_curve(struct yetty_ydraw_core_buffer *buf, uint32_t z_order, float p0x,
                                float p0y, float cx, float cy, float p1x, float p1y, float width,
                                uint32_t color, uint32_t n_segs)
{
    float prev_x = p0x;
    float prev_y = p0y;
    for (uint32_t i = 1; i <= n_segs; i++) {
        float t = (float)i / (float)n_segs;
        float omt = 1.0f - t;
        float x = omt * omt * p0x + 2.0f * omt * t * cx + t * t * p1x;
        float y = omt * omt * p0y + 2.0f * omt * t * cy + t * t * p1y;
        struct yetty_ysdf_segment g = {prev_x, prev_y, x, y};
        yetty_ysdf_add_segment(buf, z_order++, 0u, color, width, &g);
        prev_x = x;
        prev_y = y;
    }
    return z_order;
}

/*=============================================================================
 * Frame builder.
 *===========================================================================*/

static struct yetty_ycore_void_result yzoo_build_prims(struct yetty_yzoo *z,
                                                       struct yetty_ydraw_core_buffer *buf,
                                                       float time)
{
    uint32_t zo = 0;

    /* Connections: curves / lines / shapes. */
    for (uint32_t i = 0; i < z->conn_len; i++) {
        const struct yzoo_connection *c = &z->conns[i];
        if (c->cp_a >= z->cp_len || c->cp_b >= z->cp_len) {
            continue;
        }
        struct yzoo_vec2 pa = yzoo_cp_position(z, &z->cps[c->cp_a], time);
        struct yzoo_vec2 pb = yzoo_cp_position(z, &z->cps[c->cp_b], time);

        float curv = c->curvature + 0.15f * sinf(time * c->curve_anim_freq + c->curve_anim_phase);

        float breathe = 0.6f + 0.4f * sinf(time * 1.5f + c->curve_anim_phase);
        uint8_t alpha = (uint8_t)(breathe * 200.0f);
        uint32_t color = (c->color & 0x00FFFFFFu) | ((uint32_t)alpha << 24);

        switch (c->type) {
        case 0: {
            float dx = pb.x - pa.x;
            float dy = pb.y - pa.y;
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 0.001f) {
                break;
            }
            float perp_x = -dy / len;
            float perp_y = dx / len;
            float mid_x = (pa.x + pb.x) * 0.5f;
            float mid_y = (pa.y + pb.y) * 0.5f;
            float ctrl_x = mid_x + perp_x * curv * len * 0.5f;
            float ctrl_y = mid_y + perp_y * curv * len * 0.5f;
            zo = yzoo_emit_curve(buf, zo, pa.x, pa.y, ctrl_x, ctrl_y, pb.x, pb.y, c->stroke_width,
                                 color, z->config.bezier_segments);
            break;
        }
        case 1: {
            struct yetty_ysdf_segment g = {pa.x, pa.y, pb.x, pb.y};
            yetty_ysdf_add_segment(buf, zo++, 0u, color, c->stroke_width, &g);
            break;
        }
        case 2: {
            float mid_x = (pa.x + pb.x) * 0.5f;
            float mid_y = (pa.y + pb.y) * 0.5f;
            float dx = pb.x - pa.x;
            float dy = pb.y - pa.y;
            float dist = sqrtf(dx * dx + dy * dy);
            float size = dist * 0.12f;
            if (size < 3.0f) {
                size = 3.0f;
            }
            yzoo_emit_shape(buf, zo++, c->shape_choice, mid_x, mid_y, size, color);
            break;
        }
        default:
            break;
        }
    }

    /* Control-point markers: a small breathing circle, drawn on top. */
    for (uint32_t i = 0; i < z->cp_len; i++) {
        const struct yzoo_control_point *cp = &z->cps[i];
        struct yzoo_vec2 p = yzoo_cp_position(z, cp, time);

        float breathe = 0.7f + 0.3f * sinf(time * cp->anim_freq + cp->anim_phase);
        uint8_t alpha = (uint8_t)(breathe * 255.0f);
        uint32_t color = (cp->color & 0x00FFFFFFu) | ((uint32_t)alpha << 24);

        float age = time - cp->spawn_time;
        if (age < 0.0f) {
            age = 0.0f;
        }
        float scale = expf(z->config.growth_rate * age);
        float marker_size = z->config.cp_marker_size * scale;
        if (marker_size > 12.0f) {
            marker_size = 12.0f;
        }

        struct yetty_ysdf_circle g = {p.x, p.y, marker_size};
        yetty_ysdf_add_circle(buf, zo++, color, 0u, 0.0f, &g);
    }

    return YETTY_OK_VOID();
}

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_yzoo_config yetty_yzoo_config_default(void)
{
    struct yetty_yzoo_config cfg = {
        .target_points = 20u,
        .target_conns_per_cp = 3u,
        .growth_rate = 0.30f,
        .spawn_radius_min = 20.0f,
        .spawn_radius_max = 140.0f,
        .cp_marker_size = 3.0f,
        .max_connection_dist = 280.0f,
        .stroke_min = 0.5f,
        .stroke_max = 2.5f,
        .curve_ratio = 0.80f,
        .bezier_segments = 12u,
        .bg_color = 0xFF1A1A2Eu,
        .scene_width = 600.0f,
        .scene_height = 400.0f,
    };
    return cfg;
}

static float yzoo_clamp_f(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static uint32_t yzoo_clamp_u(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static struct yetty_yzoo_config yzoo_clamp_config(struct yetty_yzoo_config c)
{
    c.target_points = yzoo_clamp_u(c.target_points, YZOO_TARGET_POINTS_MIN, YZOO_TARGET_POINTS_MAX);
    c.target_conns_per_cp =
        yzoo_clamp_u(c.target_conns_per_cp, YZOO_CONNS_MIN, YZOO_CONNS_MAX);
    c.growth_rate = yzoo_clamp_f(c.growth_rate, YZOO_GROWTH_MIN, YZOO_GROWTH_MAX);
    c.max_connection_dist =
        yzoo_clamp_f(c.max_connection_dist, YZOO_MAX_DIST_MIN, YZOO_MAX_DIST_MAX);
    c.cp_marker_size = yzoo_clamp_f(c.cp_marker_size, YZOO_MARKER_MIN, YZOO_MARKER_MAX);
    c.stroke_min = yzoo_clamp_f(c.stroke_min, YZOO_STROKE_MIN_LO, YZOO_STROKE_MAX_HI);
    c.stroke_max = yzoo_clamp_f(c.stroke_max, c.stroke_min, YZOO_STROKE_MAX_HI);
    c.curve_ratio = yzoo_clamp_f(c.curve_ratio, 0.0f, 1.0f);
    c.bezier_segments =
        yzoo_clamp_u(c.bezier_segments, YZOO_BEZIER_SEGMENTS_MIN, YZOO_BEZIER_SEGMENTS_MAX);
    if (c.spawn_radius_min < 0.0f) {
        c.spawn_radius_min = 0.0f;
    }
    if (c.spawn_radius_max < c.spawn_radius_min) {
        float t = c.spawn_radius_max;
        c.spawn_radius_max = c.spawn_radius_min;
        c.spawn_radius_min = t;
    }
    if (c.scene_width < 1.0f) {
        c.scene_width = 1.0f;
    }
    if (c.scene_height < 1.0f) {
        c.scene_height = 1.0f;
    }
    return c;
}

struct yetty_yzoo_ptr_result yetty_yzoo_create(const struct yetty_yzoo_config *config,
                                               uint32_t seed)
{
    struct yetty_yzoo *z = calloc(1, sizeof(*z));
    if (!z) {
        return YETTY_ERR(yetty_yzoo_ptr, "yzoo: calloc failed");
    }
    z->config = yzoo_clamp_config(config ? *config : yetty_yzoo_config_default());
    z->rng_state = seed != 0 ? (uint64_t)seed : yzoo_seed_from_clock();
    z->initialized = false;
    return YETTY_OK(yetty_yzoo_ptr, z);
}

void yetty_yzoo_destroy(struct yetty_yzoo *zoo)
{
    if (!zoo) {
        return;
    }
    free(zoo->cps);
    free(zoo->conns);
    free(zoo);
}

struct yetty_ycore_void_result yetty_yzoo_set_scene_size(struct yetty_yzoo *zoo, float scene_width,
                                                         float scene_height)
{
    if (!zoo) {
        return YETTY_ERR(yetty_ycore_void, "yzoo: zoo is NULL");
    }
    if (scene_width < 1.0f) {
        scene_width = 1.0f;
    }
    if (scene_height < 1.0f) {
        scene_height = 1.0f;
    }
    zoo->config.scene_width = scene_width;
    zoo->config.scene_height = scene_height;
    return YETTY_OK_VOID();
}

const struct yetty_yzoo_config *yetty_yzoo_config_get(const struct yetty_yzoo *zoo)
{
    return zoo ? &zoo->config : NULL;
}

struct yetty_ycore_void_result yetty_yzoo_render(struct yetty_yzoo *zoo,
                                                 struct yetty_ydraw_core_buffer *buf, float time)
{
    if (!zoo) {
        return YETTY_ERR(yetty_ycore_void, "yzoo_render: zoo is NULL");
    }
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "yzoo_render: buf is NULL");
    }

    /* First frame: seed initial control points with staggered ages so the
     * scene starts already populated instead of empty-then-popping. */
    if (!zoo->initialized) {
        for (uint32_t i = 0; i < zoo->config.target_points; i++) {
            float t = time - yzoo_rng_range(zoo, 0.0f, 3.0f);
            struct yetty_ycore_void_result rr = yzoo_spawn_cp(zoo, t);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "yzoo_render: initial spawn");
        }
        zoo->initialized = true;
    }

    /* Cull control points whose marker has fully drifted off-screen (the
     * comparison uses the post-growth marker margin, matching the C++). */
    for (int32_t i = (int32_t)zoo->cp_len - 1; i >= 0; i--) {
        struct yzoo_control_point *cp = &zoo->cps[i];
        struct yzoo_vec2 p = yzoo_cp_position(zoo, cp, time);
        float age = time - cp->spawn_time;
        if (age < 0.0f) {
            age = 0.0f;
        }
        float scale = expf(zoo->config.growth_rate * age);
        float margin = zoo->config.cp_marker_size * scale * 2.0f;
        if (p.x - margin > zoo->config.scene_width || p.x + margin < 0.0f ||
            p.y - margin > zoo->config.scene_height || p.y + margin < 0.0f) {
            yzoo_remove_cp(zoo, (uint32_t)i);
        }
    }

    /* Spawn replacements up to target. */
    while (zoo->cp_len < zoo->config.target_points) {
        struct yetty_ycore_void_result rr = yzoo_spawn_cp(zoo, time);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "yzoo_render: refill spawn");
    }

    /* Maintain connection network. */
    struct yetty_ycore_void_result ur = yzoo_update_connections(zoo, time);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ur, "yzoo_render: update_connections");

    yetty_ydraw_core_buffer_clear(buf);

    /* Prepend CMD_ZERO so the receiver clears its canvas + resets the cursor
     * in the SAME envelope that carries this frame's prims — without it,
     * older frames' primitives accumulate on screen between our send and
     * any separate clear envelope's arrival. Same pattern as ygui. */
    struct yetty_ycore_void_result zr = yetty_ydraw_core_buffer_add_cmd_zero(buf);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, zr, "yzoo_render: add_cmd_zero");

    yetty_ydraw_core_buffer_set_scene_bounds(buf, 0.0f, 0.0f, zoo->config.scene_width,
                                              zoo->config.scene_height);

    struct yetty_ycore_void_result br = yzoo_build_prims(zoo, buf, time);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "yzoo_render: build_prims");

    return YETTY_OK_VOID();
}
