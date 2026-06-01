/*
 * ylottie-prop.c — Lottie property evaluation, affine-transform math, colour.
 *
 * A Lottie animatable property is an object { "a": 0|1, "k": ... }:
 *   a == 0 → k is the static value (number, or array of numbers).
 *   a == 1 → k is an array of keyframes { t, s, [e], i, o, [h] }.
 *
 * Keyframe interpolation honours the per-keyframe cubic-bezier easing handles
 * (o = out tangent of the segment start, i = in tangent), solved by bisecting
 * the bezier's x(u) = t' then reading y(u). When both `e` (older Bodymovin)
 * and the next keyframe's `s` (newer) are absent the segment is held.
 *
 * Spatial position tangents (`to`/`ti`) are not applied — position is lerped
 * linearly between keyframes. This is exact AT keyframe times (the common
 * poster-frame case) and only diverges on curved in-between motion. Easing
 * handles given per-dimension are reduced to their first component.
 */

#include "ylottie-internal.h"

#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define YLOTTIE_MAX_DIM 8

/*=============================================================================
 * Affine transform
 *===========================================================================*/

void yetty_ylottie_xform_identity(struct yetty_ylottie_xform *m)
{
    m->a = 1.0f;
    m->b = 0.0f;
    m->c = 0.0f;
    m->d = 1.0f;
    m->e = 0.0f;
    m->f = 0.0f;
}

void yetty_ylottie_xform_multiply(struct yetty_ylottie_xform *out,
                                  const struct yetty_ylottie_xform *l,
                                  const struct yetty_ylottie_xform *r)
{
    /* out = l · r (r applied to the point first, then l). */
    struct yetty_ylottie_xform t;
    t.a = l->a * r->a + l->c * r->b;
    t.b = l->b * r->a + l->d * r->b;
    t.c = l->a * r->c + l->c * r->d;
    t.d = l->b * r->c + l->d * r->d;
    t.e = l->a * r->e + l->c * r->f + l->e;
    t.f = l->b * r->e + l->d * r->f + l->f;
    *out = t;
}

void yetty_ylottie_xform_point(const struct yetty_ylottie_xform *m, float x, float y, float *ox,
                               float *oy)
{
    *ox = m->a * x + m->c * y + m->e;
    *oy = m->b * x + m->d * y + m->f;
}

float yetty_ylottie_xform_avg_scale(const struct yetty_ylottie_xform *m)
{
    float sx = sqrtf(m->a * m->a + m->b * m->b);
    float sy = sqrtf(m->c * m->c + m->d * m->d);
    return (sx + sy) * 0.5f;
}

bool yetty_ylottie_xform_axis_aligned(const struct yetty_ylottie_xform *m)
{
    return fabsf(m->b) < 1e-6f && fabsf(m->c) < 1e-6f;
}

static struct yetty_ylottie_xform xform_translate(float tx, float ty)
{
    struct yetty_ylottie_xform m = {1.0f, 0.0f, 0.0f, 1.0f, tx, ty};
    return m;
}

static struct yetty_ylottie_xform xform_scale(float sx, float sy)
{
    struct yetty_ylottie_xform m = {sx, 0.0f, 0.0f, sy, 0.0f, 0.0f};
    return m;
}

static struct yetty_ylottie_xform xform_rotate(float deg)
{
    float r = deg * (float)(M_PI / 180.0);
    float cs = cosf(r), sn = sinf(r);
    /* y-down coordinate system: +deg rotates clockwise, matching AE/Lottie. */
    struct yetty_ylottie_xform m = {cs, sn, -sn, cs, 0.0f, 0.0f};
    return m;
}

/* Skew about an axis. Rare in practice (sk = 0 in typical output); decomposed
 * as rotate(axis) · shearX(tan(skew)) · rotate(-axis). */
static struct yetty_ylottie_xform xform_skew(float skew_deg, float axis_deg)
{
    if (fabsf(skew_deg) < 1e-6f) {
        struct yetty_ylottie_xform id;
        yetty_ylottie_xform_identity(&id);
        return id;
    }
    float t = tanf(skew_deg * (float)(M_PI / 180.0));
    struct yetty_ylottie_xform shear = {1.0f, 0.0f, t, 1.0f, 0.0f, 0.0f};
    struct yetty_ylottie_xform rot = xform_rotate(axis_deg);
    struct yetty_ylottie_xform inv = xform_rotate(-axis_deg);
    struct yetty_ylottie_xform tmp;
    yetty_ylottie_xform_multiply(&tmp, &rot, &shear);
    yetty_ylottie_xform_multiply(&tmp, &tmp, &inv);
    return tmp;
}

/*=============================================================================
 * Keyframe easing
 *===========================================================================*/

/* Read an easing-handle component that may be a scalar or a per-dimension
 * array (we take element 0). */
static float handle_component(const struct yetty_ylottie_json *handle, const char *axis,
                              float fallback)
{
    const struct yetty_ylottie_json *v = yetty_ylottie_json_get(handle, axis);
    if (!v) {
        return fallback;
    }
    if (v->type == YETTY_YLOTTIE_JSON_NUMBER) {
        return (float)v->number;
    }
    if (v->type == YETTY_YLOTTIE_JSON_ARRAY) {
        return (float)yetty_ylottie_json_num_at(v, 0, fallback);
    }
    return fallback;
}

static float bezier_axis(float u, float p1, float p2)
{
    float omu = 1.0f - u;
    return 3.0f * omu * omu * u * p1 + 3.0f * omu * u * u * p2 + u * u * u;
}

/* Cubic-bezier ease: maps a linear fraction `t` in [0,1] through the segment's
 * easing handles. P0 = (0,0), P1 = (ox,oy), P2 = (ix,iy), P3 = (1,1). */
static float bezier_ease(float t, float ox, float oy, float ix, float iy)
{
    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t >= 1.0f) {
        return 1.0f;
    }
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 30; i++) {
        float mid = 0.5f * (lo + hi);
        float x = bezier_axis(mid, ox, ix);
        if (x < t) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    float u = 0.5f * (lo + hi);
    return bezier_axis(u, oy, iy);
}

/*=============================================================================
 * Value reading
 *===========================================================================*/

static size_t read_value(const struct yetty_ylottie_json *value, float *out, size_t dim)
{
    for (size_t i = 0; i < dim; i++) {
        out[i] = 0.0f;
    }
    if (!value) {
        return 0;
    }
    if (value->type == YETTY_YLOTTIE_JSON_NUMBER) {
        out[0] = (float)value->number;
        return 1;
    }
    if (value->type == YETTY_YLOTTIE_JSON_ARRAY) {
        size_t i = 0;
        for (const struct yetty_ylottie_json *e = value->first_child; e && i < dim;
             e = e->next_sibling) {
            out[i++] = (float)yetty_ylottie_json_num(e, 0.0);
        }
        return i;
    }
    return 0;
}

static bool is_keyframe_array(const struct yetty_ylottie_json *k)
{
    return k && k->type == YETTY_YLOTTIE_JSON_ARRAY && k->first_child &&
           k->first_child->type == YETTY_YLOTTIE_JSON_OBJECT;
}

size_t yetty_ylottie_prop_eval(const struct yetty_ylottie_json *prop, float frame, float *out,
                               size_t dim)
{
    for (size_t i = 0; i < dim; i++) {
        out[i] = 0.0f;
    }
    if (!prop) {
        return 0;
    }
    const struct yetty_ylottie_json *k = yetty_ylottie_json_get(prop, "k");
    if (!k) {
        return 0;
    }
    if (!is_keyframe_array(k)) {
        return read_value(k, out, dim); /* static */
    }

    size_t n = k->child_count;
    if (n == 0) {
        return 0;
    }
    const struct yetty_ylottie_json *first = yetty_ylottie_json_at(k, 0);
    if (n == 1) {
        return read_value(yetty_ylottie_json_get(first, "s"), out, dim);
    }

    float first_t = (float)yetty_ylottie_json_num_key(first, "t", 0.0);
    if (frame <= first_t) {
        return read_value(yetty_ylottie_json_get(first, "s"), out, dim);
    }

    /* Find segment [seg, seg+1] with kf[seg].t <= frame; clamp to the last
     * real segment so frames past the end hold the final value. */
    size_t seg = 0;
    for (size_t i = 0; i + 1 < n; i++) {
        float ti = (float)yetty_ylottie_json_num_key(yetty_ylottie_json_at(k, i), "t", 0.0);
        if (ti <= frame) {
            seg = i;
        }
    }
    if (seg > n - 2) {
        seg = n - 2;
    }

    const struct yetty_ylottie_json *kf0 = yetty_ylottie_json_at(k, seg);
    const struct yetty_ylottie_json *kf1 = yetty_ylottie_json_at(k, seg + 1);
    float t0 = (float)yetty_ylottie_json_num_key(kf0, "t", 0.0);
    float t1 = (float)yetty_ylottie_json_num_key(kf1, "t", 0.0);

    const struct yetty_ylottie_json *start_v = yetty_ylottie_json_get(kf0, "s");
    const struct yetty_ylottie_json *end_v = yetty_ylottie_json_get(kf0, "e");
    if (!end_v) {
        end_v = yetty_ylottie_json_get(kf1, "s");
    }
    if (!end_v) {
        end_v = start_v;
    }

    /* Hold keyframe: step, no interpolation. */
    if (yetty_ylottie_json_num_key(kf0, "h", 0.0) != 0.0) {
        return read_value(start_v, out, dim);
    }

    float local = (t1 > t0) ? (frame - t0) / (t1 - t0) : 0.0f;
    if (local < 0.0f) {
        local = 0.0f;
    }
    if (local > 1.0f) {
        local = 1.0f;
    }
    float ox = handle_component(yetty_ylottie_json_get(kf0, "o"), "x", 0.0f);
    float oy = handle_component(yetty_ylottie_json_get(kf0, "o"), "y", 0.0f);
    float ix = handle_component(yetty_ylottie_json_get(kf0, "i"), "x", 1.0f);
    float iy = handle_component(yetty_ylottie_json_get(kf0, "i"), "y", 1.0f);
    float factor = bezier_ease(local, ox, oy, ix, iy);

    float a[YLOTTIE_MAX_DIM];
    float b[YLOTTIE_MAX_DIM];
    size_t cap = dim < YLOTTIE_MAX_DIM ? dim : YLOTTIE_MAX_DIM;
    size_t na = read_value(start_v, a, cap);
    size_t nb = read_value(end_v, b, cap);
    size_t count = na > nb ? na : nb;
    for (size_t i = 0; i < count && i < dim; i++) {
        out[i] = a[i] + (b[i] - a[i]) * factor;
    }
    return count;
}

float yetty_ylottie_prop_eval_scalar(const struct yetty_ylottie_json *prop, float frame,
                                     float fallback)
{
    if (!prop) {
        return fallback;
    }
    float v[1] = {0.0f};
    size_t n = yetty_ylottie_prop_eval(prop, frame, v, 1);
    return n > 0 ? v[0] : fallback;
}

/*=============================================================================
 * Transform build
 *===========================================================================*/

void yetty_ylottie_transform_build(const struct yetty_ylottie_json *tr, float frame,
                                   struct yetty_ylottie_xform *out)
{
    yetty_ylottie_xform_identity(out);
    if (!tr) {
        return;
    }

    float anchor[3] = {0, 0, 0};
    float position[3] = {0, 0, 0};
    float scale[3] = {100, 100, 100};
    yetty_ylottie_prop_eval(yetty_ylottie_json_get(tr, "a"), frame, anchor, 3);
    yetty_ylottie_prop_eval(yetty_ylottie_json_get(tr, "p"), frame, position, 3);
    yetty_ylottie_prop_eval(yetty_ylottie_json_get(tr, "s"), frame, scale, 3);
    float rotation = yetty_ylottie_prop_eval_scalar(yetty_ylottie_json_get(tr, "r"), frame, 0.0f);
    float skew = yetty_ylottie_prop_eval_scalar(yetty_ylottie_json_get(tr, "sk"), frame, 0.0f);
    float skew_axis = yetty_ylottie_prop_eval_scalar(yetty_ylottie_json_get(tr, "sa"), frame, 0.0f);

    /* M = T(position) · R(rotation) · Skew · S(scale/100) · T(-anchor). */
    struct yetty_ylottie_xform m = xform_translate(position[0], position[1]);
    struct yetty_ylottie_xform rot = xform_rotate(rotation);
    yetty_ylottie_xform_multiply(&m, &m, &rot);
    if (fabsf(skew) > 1e-6f) {
        struct yetty_ylottie_xform sk = xform_skew(skew, skew_axis);
        yetty_ylottie_xform_multiply(&m, &m, &sk);
    }
    struct yetty_ylottie_xform sc = xform_scale(scale[0] * 0.01f, scale[1] * 0.01f);
    yetty_ylottie_xform_multiply(&m, &m, &sc);
    struct yetty_ylottie_xform anc = xform_translate(-anchor[0], -anchor[1]);
    yetty_ylottie_xform_multiply(&m, &m, &anc);
    *out = m;
}

float yetty_ylottie_transform_opacity(const struct yetty_ylottie_json *tr, float frame)
{
    if (!tr) {
        return 1.0f;
    }
    const struct yetty_ylottie_json *o = yetty_ylottie_json_get(tr, "o");
    if (!o) {
        return 1.0f;
    }
    float v = yetty_ylottie_prop_eval_scalar(o, frame, 100.0f) * 0.01f;
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }
    return v;
}

/*=============================================================================
 * Colour
 *===========================================================================*/

static float clamp01(float v)
{
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

uint32_t yetty_ylottie_rgba_to_abgr(float r, float g, float b, float a)
{
    uint32_t ri = (uint32_t)(clamp01(r) * 255.0f + 0.5f);
    uint32_t gi = (uint32_t)(clamp01(g) * 255.0f + 0.5f);
    uint32_t bi = (uint32_t)(clamp01(b) * 255.0f + 0.5f);
    uint32_t ai = (uint32_t)(clamp01(a) * 255.0f + 0.5f);
    return (ai << 24) | (bi << 16) | (gi << 8) | ri;
}

uint32_t yetty_ylottie_color_eval(const struct yetty_ylottie_json *color_prop, float frame,
                                  float alpha)
{
    if (!color_prop) {
        return 0;
    }
    float c[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    size_t n = yetty_ylottie_prop_eval(color_prop, frame, c, 4);
    float r = c[0], g = c[1], b = c[2];
    /* Older Bodymovin emitted 0..255 colour components; normalise those. */
    if (r > 1.0f || g > 1.0f || b > 1.0f) {
        r /= 255.0f;
        g /= 255.0f;
        b /= 255.0f;
    }
    float a = (n >= 4 ? c[3] : 1.0f) * alpha;
    return yetty_ylottie_rgba_to_abgr(r, g, b, a);
}
