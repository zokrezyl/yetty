/*
 * ysvg-path.c — SVG path "d" mini-language flattener.
 *
 * Produces a sequence of struct yetty_ysvg_subpath (polylines + closed flag).
 *
 * Path commands handled (case = relative when lower):
 *   M/m  moveto
 *   L/l  lineto
 *   H/h  horizontal lineto
 *   V/v  vertical lineto
 *   C/c  cubic Bézier
 *   S/s  smooth cubic Bézier (mirror previous control point)
 *   Q/q  quadratic Bézier
 *   T/t  smooth quadratic Bézier (mirror previous control point)
 *   A/a  elliptical arc
 *   Z/z  closepath
 *
 * Bézier subdivision: adaptive — recurse while the chord-flatness
 * exceeds tolerance. Arcs are converted to a series of cubic Béziers
 * (one per quarter sweep) per the SVG appendix algorithm.
 *
 * polyline/polygon `points` lists land in path_from_points which is a
 * trivial number-sequence reader.
 */

#include "ysvg-internal.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*=============================================================================
 * Sub-path builder
 *===========================================================================*/

struct ysvg_pb {
    struct yetty_ysvg_path *path;
    struct yetty_ysvg_subpath *cur;
    size_t cur_cap;
    int oom;
};

static struct yetty_ysvg_subpath *pb_new_subpath(struct ysvg_pb *pb)
{
    if (pb->path->sub_count == pb->path->sub_cap) {
        size_t nc = pb->path->sub_cap ? pb->path->sub_cap * 2 : 4;
        struct yetty_ysvg_subpath *na = realloc(pb->path->subs, nc * sizeof(*na));
        if (!na) {
            pb->oom = 1;
            return NULL;
        }
        pb->path->subs = na;
        pb->path->sub_cap = nc;
    }
    struct yetty_ysvg_subpath *sp = &pb->path->subs[pb->path->sub_count++];
    sp->points = NULL;
    sp->count = 0;
    sp->closed = 0;
    pb->cur = sp;
    pb->cur_cap = 0;
    return sp;
}

static int pb_emit(struct ysvg_pb *pb, float x, float y)
{
    if (!pb->cur) {
        if (!pb_new_subpath(pb)) return -1;
    }
    if (pb->cur->count == pb->cur_cap) {
        size_t nc = pb->cur_cap ? pb->cur_cap * 2 : 8;
        struct yetty_ysvg_point *np = realloc(pb->cur->points, nc * sizeof(*np));
        if (!np) {
            pb->oom = 1;
            return -1;
        }
        pb->cur->points = np;
        pb->cur_cap = nc;
    }
    pb->cur->points[pb->cur->count].x = x;
    pb->cur->points[pb->cur->count].y = y;
    pb->cur->count++;
    return 0;
}

/*=============================================================================
 * Bézier flattening (recursive adaptive subdivision)
 *===========================================================================*/

static int flatten_cubic(struct ysvg_pb *pb, float x0, float y0, float x1, float y1, float x2,
                         float y2, float x3, float y3, float tol, int depth)
{
    /* Approximate flatness via control-point deviation from the chord. */
    float dx = x3 - x0;
    float dy = y3 - y0;
    float d1 = fabsf((x1 - x3) * dy - (y1 - y3) * dx);
    float d2 = fabsf((x2 - x3) * dy - (y2 - y3) * dx);
    float chord = dx * dx + dy * dy;
    if (chord < 1e-20f) {
        return pb_emit(pb, x3, y3);
    }
    float lim = tol * sqrtf(chord);
    if (depth >= 18 || (d1 + d2) <= lim) {
        return pb_emit(pb, x3, y3);
    }
    /* de Casteljau subdivision at t=0.5 */
    float x01 = (x0 + x1) * 0.5f, y01 = (y0 + y1) * 0.5f;
    float x12 = (x1 + x2) * 0.5f, y12 = (y1 + y2) * 0.5f;
    float x23 = (x2 + x3) * 0.5f, y23 = (y2 + y3) * 0.5f;
    float x012 = (x01 + x12) * 0.5f, y012 = (y01 + y12) * 0.5f;
    float x123 = (x12 + x23) * 0.5f, y123 = (y12 + y23) * 0.5f;
    float xc = (x012 + x123) * 0.5f, yc = (y012 + y123) * 0.5f;
    if (flatten_cubic(pb, x0, y0, x01, y01, x012, y012, xc, yc, tol, depth + 1) < 0) return -1;
    return flatten_cubic(pb, xc, yc, x123, y123, x23, y23, x3, y3, tol, depth + 1);
}

static int flatten_quad(struct ysvg_pb *pb, float x0, float y0, float x1, float y1, float x2,
                        float y2, float tol, int depth)
{
    float dx = x2 - x0, dy = y2 - y0;
    float d = fabsf((x1 - x2) * dy - (y1 - y2) * dx);
    float chord = dx * dx + dy * dy;
    if (chord < 1e-20f) return pb_emit(pb, x2, y2);
    float lim = tol * sqrtf(chord);
    if (depth >= 18 || d <= lim) return pb_emit(pb, x2, y2);
    float x01 = (x0 + x1) * 0.5f, y01 = (y0 + y1) * 0.5f;
    float x12 = (x1 + x2) * 0.5f, y12 = (y1 + y2) * 0.5f;
    float xc = (x01 + x12) * 0.5f, yc = (y01 + y12) * 0.5f;
    if (flatten_quad(pb, x0, y0, x01, y01, xc, yc, tol, depth + 1) < 0) return -1;
    return flatten_quad(pb, xc, yc, x12, y12, x2, y2, tol, depth + 1);
}

/*=============================================================================
 * Elliptical arc → cubic Béziers (per SVG 1.1 Appendix F)
 *===========================================================================*/

static void arc_endpoint_to_center(float x1, float y1, float x2, float y2, int large, int sweep,
                                   float rx, float ry, float phi, float *cx, float *cy,
                                   float *theta1, float *delta)
{
    float cos_phi = cosf(phi), sin_phi = sinf(phi);
    float dx2 = (x1 - x2) * 0.5f, dy2 = (y1 - y2) * 0.5f;
    float x1p = cos_phi * dx2 + sin_phi * dy2;
    float y1p = -sin_phi * dx2 + cos_phi * dy2;

    float rxs = rx * rx;
    float rys = ry * ry;
    float x1ps = x1p * x1p;
    float y1ps = y1p * y1p;
    float lambda = x1ps / rxs + y1ps / rys;
    if (lambda > 1.0f) {
        float s = sqrtf(lambda);
        rx *= s;
        ry *= s;
        rxs = rx * rx;
        rys = ry * ry;
    }
    float sign = (large == sweep) ? -1.0f : 1.0f;
    float num = rxs * rys - rxs * y1ps - rys * x1ps;
    if (num < 0.0f) num = 0.0f;
    float den = rxs * y1ps + rys * x1ps;
    float coef = (den > 0.0f) ? (sign * sqrtf(num / den)) : 0.0f;
    float cxp = coef * (rx * y1p / ry);
    float cyp = coef * -(ry * x1p / rx);

    *cx = cos_phi * cxp - sin_phi * cyp + (x1 + x2) * 0.5f;
    *cy = sin_phi * cxp + cos_phi * cyp + (y1 + y2) * 0.5f;

    float ux = (x1p - cxp) / rx;
    float uy = (y1p - cyp) / ry;
    float vx = (-x1p - cxp) / rx;
    float vy = (-y1p - cyp) / ry;
    float n = sqrtf(ux * ux + uy * uy);
    float p = ux;
    *theta1 = (uy < 0.0f ? -1.0f : 1.0f) * acosf(p / n);

    n = sqrtf((ux * ux + uy * uy) * (vx * vx + vy * vy));
    p = ux * vx + uy * vy;
    float dp = p / n;
    if (dp > 1.0f) dp = 1.0f;
    if (dp < -1.0f) dp = -1.0f;
    float d = (ux * vy - uy * vx < 0.0f ? -1.0f : 1.0f) * acosf(dp);
    if (!sweep && d > 0.0f) d -= 2.0f * (float)M_PI;
    if (sweep && d < 0.0f) d += 2.0f * (float)M_PI;
    *delta = d;
}

static int flatten_arc(struct ysvg_pb *pb, float x0, float y0, float rx, float ry, float phi_deg,
                       int large, int sweep, float x2, float y2, float tol)
{
    if (rx == 0.0f || ry == 0.0f) {
        /* SVG says: treat as straight line. */
        return pb_emit(pb, x2, y2);
    }
    if (rx < 0) rx = -rx;
    if (ry < 0) ry = -ry;
    float phi = phi_deg * (float)(M_PI / 180.0);
    float cx, cy, theta1, delta;
    arc_endpoint_to_center(x0, y0, x2, y2, large, sweep, rx, ry, phi, &cx, &cy, &theta1, &delta);
    int segs = (int)ceilf(fabsf(delta) / (float)(M_PI * 0.5f));
    if (segs < 1) segs = 1;
    float dtheta = delta / (float)segs;
    float t = 8.0f / 3.0f * sinf(dtheta * 0.25f) * sinf(dtheta * 0.25f) / sinf(dtheta * 0.5f);
    float cos_phi = cosf(phi), sin_phi = sinf(phi);

    float theta = theta1;
    float px = x0, py = y0;
    for (int i = 0; i < segs; i++) {
        float theta_next = theta + dtheta;
        float cos_t = cosf(theta), sin_t = sinf(theta);
        float cos_tn = cosf(theta_next), sin_tn = sinf(theta_next);

        /* Point on ellipse at theta_next */
        float qxn = cos_phi * rx * cos_tn - sin_phi * ry * sin_tn + cx;
        float qyn = sin_phi * rx * cos_tn + cos_phi * ry * sin_tn + cy;

        /* Tangent vectors */
        float dxs = cos_phi * (-rx * sin_t) - sin_phi * (ry * cos_t);
        float dys = sin_phi * (-rx * sin_t) + cos_phi * (ry * cos_t);
        float dxe = cos_phi * (-rx * sin_tn) - sin_phi * (ry * cos_tn);
        float dye = sin_phi * (-rx * sin_tn) + cos_phi * (ry * cos_tn);

        float c1x = px + t * dxs, c1y = py + t * dys;
        float c2x = qxn - t * dxe, c2y = qyn - t * dye;

        if (flatten_cubic(pb, px, py, c1x, c1y, c2x, c2y, qxn, qyn, tol, 0) < 0) return -1;
        px = qxn;
        py = qyn;
        theta = theta_next;
    }
    return 0;
}

/*=============================================================================
 * "d" attribute parser
 *===========================================================================*/

static int ysvg_path_is_wsp(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == ',';
}

static size_t ysvg_path_skip_wsp(const char *s, size_t len, size_t pos)
{
    while (pos < len && ysvg_path_is_wsp((unsigned char)s[pos])) pos++;
    return pos;
}

static int ysvg_path_read_num(const char *s, size_t len, size_t *pos, float *out)
{
    *pos = ysvg_path_skip_wsp(s, len, *pos);
    return yetty_ysvg_parse_number(s, len, pos, out);
}

static int ysvg_path_read_flag(const char *s, size_t len, size_t *pos, int *out)
{
    *pos = ysvg_path_skip_wsp(s, len, *pos);
    if (*pos >= len) return 0;
    if (s[*pos] == '0' || s[*pos] == '1') {
        *out = (s[*pos] == '1');
        (*pos)++;
        return 1;
    }
    return 0;
}

void yetty_ysvg_path_destroy(struct yetty_ysvg_path *p)
{
    if (!p) return;
    for (size_t i = 0; i < p->sub_count; i++) {
        free(p->subs[i].points);
    }
    free(p->subs);
    p->subs = NULL;
    p->sub_count = p->sub_cap = 0;
}

int yetty_ysvg_path_flatten(struct yetty_ysvg_path *out, const char *d, size_t len, float tolerance)
{
    memset(out, 0, sizeof(*out));
    if (!d || len == 0) return 1;

    struct ysvg_pb pb = {.path = out};
    float cx = 0, cy = 0;   /* current point */
    float sx = 0, sy = 0;   /* subpath start */
    float pcx = 0, pcy = 0; /* previous cubic control */
    float pqx = 0, pqy = 0; /* previous quadratic control */
    char prev_cmd = 0;
    char cmd = 0;
    size_t i = 0;
    int have_current = 0;
    int last_was_cubic = 0;
    int last_was_quad = 0;

    while (i < len) {
        i = ysvg_path_skip_wsp(d, len, i);
        if (i >= len) break;
        char c = d[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            cmd = c;
            i++;
        } else if (!cmd) {
            return 0;
        } else {
            /* implicit repeated command — moveto becomes lineto */
            if (cmd == 'M') cmd = 'L';
            else if (cmd == 'm') cmd = 'l';
        }

        int relative = (cmd >= 'a' && cmd <= 'z');
        char op = (char)(relative ? (cmd - ('a' - 'A')) : cmd);

        switch (op) {
        case 'M': {
            float x, y;
            if (!ysvg_path_read_num(d, len, &i, &x)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &y)) return 0;
            if (relative && have_current) {
                x += cx;
                y += cy;
            }
            pb_new_subpath(&pb);
            if (pb.oom) return 0;
            if (pb_emit(&pb, x, y) < 0) return 0;
            cx = sx = x;
            cy = sy = y;
            have_current = 1;
            last_was_cubic = last_was_quad = 0;
            break;
        }
        case 'L': {
            float x, y;
            if (!ysvg_path_read_num(d, len, &i, &x)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &y)) return 0;
            if (relative) {
                x += cx;
                y += cy;
            }
            if (pb_emit(&pb, x, y) < 0) return 0;
            cx = x;
            cy = y;
            last_was_cubic = last_was_quad = 0;
            break;
        }
        case 'H': {
            float x;
            if (!ysvg_path_read_num(d, len, &i, &x)) return 0;
            if (relative) x += cx;
            if (pb_emit(&pb, x, cy) < 0) return 0;
            cx = x;
            last_was_cubic = last_was_quad = 0;
            break;
        }
        case 'V': {
            float y;
            if (!ysvg_path_read_num(d, len, &i, &y)) return 0;
            if (relative) y += cy;
            if (pb_emit(&pb, cx, y) < 0) return 0;
            cy = y;
            last_was_cubic = last_was_quad = 0;
            break;
        }
        case 'C': {
            float x1, y1, x2, y2, x, y;
            if (!ysvg_path_read_num(d, len, &i, &x1)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &y1)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &x2)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &y2)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &x)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &y)) return 0;
            if (relative) {
                x1 += cx; y1 += cy;
                x2 += cx; y2 += cy;
                x += cx; y += cy;
            }
            if (flatten_cubic(&pb, cx, cy, x1, y1, x2, y2, x, y, tolerance, 0) < 0) return 0;
            pcx = x2;
            pcy = y2;
            cx = x;
            cy = y;
            last_was_cubic = 1;
            last_was_quad = 0;
            break;
        }
        case 'S': {
            float x2, y2, x, y;
            if (!ysvg_path_read_num(d, len, &i, &x2)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &y2)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &x)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &y)) return 0;
            if (relative) {
                x2 += cx; y2 += cy;
                x += cx; y += cy;
            }
            float x1, y1;
            if (last_was_cubic) {
                x1 = 2.0f * cx - pcx;
                y1 = 2.0f * cy - pcy;
            } else {
                x1 = cx;
                y1 = cy;
            }
            if (flatten_cubic(&pb, cx, cy, x1, y1, x2, y2, x, y, tolerance, 0) < 0) return 0;
            pcx = x2;
            pcy = y2;
            cx = x;
            cy = y;
            last_was_cubic = 1;
            last_was_quad = 0;
            break;
        }
        case 'Q': {
            float x1, y1, x, y;
            if (!ysvg_path_read_num(d, len, &i, &x1)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &y1)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &x)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &y)) return 0;
            if (relative) {
                x1 += cx; y1 += cy;
                x += cx; y += cy;
            }
            if (flatten_quad(&pb, cx, cy, x1, y1, x, y, tolerance, 0) < 0) return 0;
            pqx = x1;
            pqy = y1;
            cx = x;
            cy = y;
            last_was_quad = 1;
            last_was_cubic = 0;
            break;
        }
        case 'T': {
            float x, y;
            if (!ysvg_path_read_num(d, len, &i, &x)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &y)) return 0;
            if (relative) {
                x += cx;
                y += cy;
            }
            float x1, y1;
            if (last_was_quad) {
                x1 = 2.0f * cx - pqx;
                y1 = 2.0f * cy - pqy;
            } else {
                x1 = cx;
                y1 = cy;
            }
            if (flatten_quad(&pb, cx, cy, x1, y1, x, y, tolerance, 0) < 0) return 0;
            pqx = x1;
            pqy = y1;
            cx = x;
            cy = y;
            last_was_quad = 1;
            last_was_cubic = 0;
            break;
        }
        case 'A': {
            float rx, ry, phi, x, y;
            int large = 0, sweep = 0;
            if (!ysvg_path_read_num(d, len, &i, &rx)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &ry)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &phi)) return 0;
            if (!ysvg_path_read_flag(d, len, &i, &large)) return 0;
            if (!ysvg_path_read_flag(d, len, &i, &sweep)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &x)) return 0;
            if (!ysvg_path_read_num(d, len, &i, &y)) return 0;
            if (relative) {
                x += cx;
                y += cy;
            }
            if (flatten_arc(&pb, cx, cy, rx, ry, phi, large, sweep, x, y, tolerance) < 0)
                return 0;
            cx = x;
            cy = y;
            last_was_cubic = last_was_quad = 0;
            break;
        }
        case 'Z':
            if (pb.cur && pb.cur->count > 0) {
                pb.cur->closed = 1;
            }
            cx = sx;
            cy = sy;
            pb.cur = NULL; /* next emit starts a new subpath */
            last_was_cubic = last_was_quad = 0;
            break;
        default:
            return 0;
        }
        prev_cmd = cmd;
        (void)prev_cmd;
    }
    if (pb.oom) {
        yetty_ysvg_path_destroy(out);
        return 0;
    }
    return 1;
}

int yetty_ysvg_path_from_points(struct yetty_ysvg_path *out, const char *pts, size_t len, bool closed)
{
    memset(out, 0, sizeof(*out));
    if (!pts || len == 0) return 1;
    struct ysvg_pb pb = {.path = out};
    if (!pb_new_subpath(&pb)) return 0;
    size_t i = 0;
    int first = 1;
    while (i < len) {
        float x, y;
        if (!yetty_ysvg_parse_number(pts, len, &i, &x)) break;
        if (!yetty_ysvg_parse_number(pts, len, &i, &y)) break;
        if (pb_emit(&pb, x, y) < 0) {
            yetty_ysvg_path_destroy(out);
            return 0;
        }
        first = 0;
    }
    if (pb.cur && closed) pb.cur->closed = 1;
    (void)first;
    return 1;
}
