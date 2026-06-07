/*
 * ylottie-path.c — flatten Lottie shape geometry into polylines.
 *
 *   - "sh" bezier shapes: vertices `v` with vertex-relative in/out tangents
 *     `i`/`o` and a `c` closed flag → adaptive cubic-bezier subdivision
 *     (same algorithm as ysvg-path.c).
 *   - "sr" polystar shapes: analytic star / regular-polygon vertices.
 *
 * Bezier shapes are animatable; ylottie_path_eval picks the keyframe value
 * active at the frame (no vertex interpolation — exact at keyframe times,
 * held between them).
 */

#include "ylottie-internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*=============================================================================
 * Polyline buffer
 *===========================================================================*/

int yetty_ylottie_polyline_push(struct yetty_ylottie_polyline *pl, float x, float y)
{
    if (pl->count == pl->cap) {
        size_t ncap = pl->cap ? pl->cap * 2 : 16;
        struct yetty_ylottie_point *np = realloc(pl->points, ncap * sizeof(*np));
        if (!np) {
            return -1;
        }
        pl->points = np;
        pl->cap = ncap;
    }
    pl->points[pl->count].x = x;
    pl->points[pl->count].y = y;
    pl->count++;
    return 0;
}

void yetty_ylottie_polyline_destroy(struct yetty_ylottie_polyline *pl)
{
    if (!pl) {
        return;
    }
    free(pl->points);
    pl->points = NULL;
    pl->count = pl->cap = 0;
    pl->closed = false;
}

/*=============================================================================
 * Cubic bezier subdivision
 *===========================================================================*/

static int flatten_cubic(struct yetty_ylottie_polyline *pl, float x0, float y0, float x1, float y1,
                         float x2, float y2, float x3, float y3, float tol, int depth)
{
    float dx = x3 - x0;
    float dy = y3 - y0;
    float d1 = fabsf((x1 - x3) * dy - (y1 - y3) * dx);
    float d2 = fabsf((x2 - x3) * dy - (y2 - y3) * dx);
    float chord = dx * dx + dy * dy;
    if (chord < 1e-20f) {
        return yetty_ylottie_polyline_push(pl, x3, y3);
    }
    float lim = tol * sqrtf(chord);
    if (depth >= 18 || (d1 + d2) <= lim) {
        return yetty_ylottie_polyline_push(pl, x3, y3);
    }
    float x01 = (x0 + x1) * 0.5f, y01 = (y0 + y1) * 0.5f;
    float x12 = (x1 + x2) * 0.5f, y12 = (y1 + y2) * 0.5f;
    float x23 = (x2 + x3) * 0.5f, y23 = (y2 + y3) * 0.5f;
    float x012 = (x01 + x12) * 0.5f, y012 = (y01 + y12) * 0.5f;
    float x123 = (x12 + x23) * 0.5f, y123 = (y12 + y23) * 0.5f;
    float xc = (x012 + x123) * 0.5f, yc = (y012 + y123) * 0.5f;
    if (flatten_cubic(pl, x0, y0, x01, y01, x012, y012, xc, yc, tol, depth + 1) < 0) {
        return -1;
    }
    return flatten_cubic(pl, xc, yc, x123, y123, x23, y23, x3, y3, tol, depth + 1);
}

/* Read a 2D point (an [x,y] array node) at `index` of `arr`. */
static int read_xy(const struct yetty_ylottie_json *arr, size_t index, float *x, float *y)
{
    const struct yetty_ylottie_json *p = yetty_ylottie_json_at(arr, index);
    if (!p || p->type != YETTY_YLOTTIE_JSON_ARRAY) {
        return -1;
    }
    *x = (float)yetty_ylottie_json_num_at(p, 0, 0.0);
    *y = (float)yetty_ylottie_json_num_at(p, 1, 0.0);
    return 0;
}

/*=============================================================================
 * Bezier shape flattening
 *===========================================================================*/

int yetty_ylottie_bezier_flatten(struct yetty_ylottie_polyline *out,
                                 const struct yetty_ylottie_json *shape_value, float tolerance)
{
    memset(out, 0, sizeof(*out));
    if (!shape_value) {
        return 1;
    }
    const struct yetty_ylottie_json *v = yetty_ylottie_json_get(shape_value, "v");
    const struct yetty_ylottie_json *in = yetty_ylottie_json_get(shape_value, "i");
    const struct yetty_ylottie_json *outt = yetty_ylottie_json_get(shape_value, "o");
    if (!v || v->type != YETTY_YLOTTIE_JSON_ARRAY) {
        return 1;
    }
    size_t n = v->child_count;
    if (n == 0) {
        return 1;
    }
    const struct yetty_ylottie_json *c = yetty_ylottie_json_get(shape_value, "c");
    bool closed = c && c->type == YETTY_YLOTTIE_JSON_BOOL && c->boolean;

    float vx0, vy0;
    if (read_xy(v, 0, &vx0, &vy0) != 0) {
        return 1;
    }
    if (yetty_ylottie_polyline_push(out, vx0, vy0) < 0) {
        yetty_ylottie_polyline_destroy(out);
        return 0;
    }

    size_t segs = closed ? n : (n - 1);
    for (size_t k = 0; k < segs; k++) {
        size_t k0 = k;
        size_t k1 = (k + 1) % n;
        float p0x, p0y, p3x, p3y;
        if (read_xy(v, k0, &p0x, &p0y) != 0 || read_xy(v, k1, &p3x, &p3y) != 0) {
            break;
        }
        float ox = 0, oy = 0, ix = 0, iy = 0;
        if (outt) {
            read_xy(outt, k0, &ox, &oy);
        }
        if (in) {
            read_xy(in, k1, &ix, &iy);
        }
        /* Tangents are vertex-relative. */
        float c1x = p0x + ox, c1y = p0y + oy;
        float c2x = p3x + ix, c2y = p3y + iy;
        if (flatten_cubic(out, p0x, p0y, c1x, c1y, c2x, c2y, p3x, p3y, tolerance, 0) < 0) {
            yetty_ylottie_polyline_destroy(out);
            return 0;
        }
    }
    out->closed = closed;
    return 1;
}

const struct yetty_ylottie_json *yetty_ylottie_path_eval(const struct yetty_ylottie_json *prop,
                                                         float frame)
{
    if (!prop) {
        return NULL;
    }
    const struct yetty_ylottie_json *k = yetty_ylottie_json_get(prop, "k");
    if (!k) {
        return NULL;
    }
    /* Static object form: k is the bezier value directly. */
    if (yetty_ylottie_json_get(k, "v")) {
        return k;
    }
    if (k->type != YETTY_YLOTTIE_JSON_ARRAY || !k->first_child) {
        return NULL;
    }
    /* Static wrapped form: k = [ {v,i,o,c} ]. */
    if (yetty_ylottie_json_get(k->first_child, "v")) {
        return k->first_child;
    }
    /* Keyframed form: pick the keyframe whose time bounds `frame` from below. */
    const struct yetty_ylottie_json *chosen = k->first_child;
    for (const struct yetty_ylottie_json *e = k->first_child; e; e = e->next_sibling) {
        if ((float)yetty_ylottie_json_num_key(e, "t", 0.0) <= frame) {
            chosen = e;
        }
    }
    const struct yetty_ylottie_json *sv = yetty_ylottie_json_get(chosen, "s");
    if (!sv) {
        return NULL;
    }
    if (yetty_ylottie_json_get(sv, "v")) {
        return sv;
    }
    if (sv->type == YETTY_YLOTTIE_JSON_ARRAY) {
        return sv->first_child; /* s = [ bezier ] */
    }
    return NULL;
}

/*=============================================================================
 * Polystar
 *===========================================================================*/

int yetty_ylottie_polystar_build(struct yetty_ylottie_polyline *out,
                                 const struct yetty_ylottie_json *star, float frame)
{
    memset(out, 0, sizeof(*out));
    if (!star) {
        return 1;
    }
    float pos[2] = {0, 0};
    yetty_ylottie_prop_eval(yetty_ylottie_json_get(star, "p"), frame, pos, 2);
    float cx = pos[0], cy = pos[1];
    float rotation = yetty_ylottie_prop_eval_scalar(yetty_ylottie_json_get(star, "r"), frame, 0.0f);
    float pts_f = yetty_ylottie_prop_eval_scalar(yetty_ylottie_json_get(star, "pt"), frame, 5.0f);
    float outer = yetty_ylottie_prop_eval_scalar(yetty_ylottie_json_get(star, "or"), frame, 0.0f);
    float star_type =
        yetty_ylottie_prop_eval_scalar(yetty_ylottie_json_get(star, "sy"), frame, 1.0f);

    int num_pts = (int)lroundf(pts_f);
    if (num_pts < 2 || outer <= 0.0f) {
        return 1;
    }
    /* Lottie polystar points up at rotation 0. */
    float start = (rotation - 90.0f) * (float)(M_PI / 180.0);

    if (star_type >= 1.5f) {
        /* Regular polygon (sy == 2). */
        float step = (float)(2.0 * M_PI) / (float)num_pts;
        for (int k = 0; k < num_pts; k++) {
            float ang = start + (float)k * step;
            if (yetty_ylottie_polyline_push(out, cx + outer * cosf(ang), cy + outer * sinf(ang)) <
                0) {
                yetty_ylottie_polyline_destroy(out);
                return 0;
            }
        }
    } else {
        /* Star (sy == 1): alternate outer / inner radius. */
        float inner =
            yetty_ylottie_prop_eval_scalar(yetty_ylottie_json_get(star, "ir"), frame, outer * 0.5f);
        float step = (float)M_PI / (float)num_pts;
        int total = num_pts * 2;
        for (int k = 0; k < total; k++) {
            float radius = (k % 2 == 0) ? outer : inner;
            float ang = start + (float)k * step;
            if (yetty_ylottie_polyline_push(out, cx + radius * cosf(ang), cy + radius * sinf(ang)) <
                0) {
                yetty_ylottie_polyline_destroy(out);
                return 0;
            }
        }
    }
    out->closed = true;
    return 1;
}
