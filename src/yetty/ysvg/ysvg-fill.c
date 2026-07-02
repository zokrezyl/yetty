/*
 * ysvg-fill.c — ear-clipping triangulation for polygon fills.
 *
 * The ydraw SDF primitive set can stroke arbitrary polylines but has no
 * arbitrary-polygon fill. To paint a filled <path>/<polygon> we flatten it to
 * a polyline (ysvg-path.c) and triangulate the resulting simple polygon here,
 * then the paint pass emits one filled SDF triangle per facet.
 *
 * Ear clipping handles convex and concave simple polygons in O(n^2). It does
 * NOT resolve holes: each subpath is triangulated on its own, so a subpath
 * that encodes a counter (the inside of an "O", a donut hole) fills solid.
 * That is an accepted approximation for the terminal figure use case — the
 * overwhelming majority of real-world fills are hole-free blobs (icon shapes,
 * table cells, logo bars) which this renders exactly.
 */

#include "ysvg-internal.h"

#include <math.h>
#include <stdlib.h>

/* Polygons flattened past this many vertices are skipped: ear clipping is
 * O(n^2) and beyond a few thousand points the cost outweighs the value of a
 * pixel-accurate fill. Real assets stay far below this. */
#define YSVG_FILL_MAX_VERTS 4096

/* Twice the signed area of triangle (a, b, c). Positive when a→b→c turns
 * counter-clockwise in SVG's y-down user space is actually clockwise on
 * screen, but orientation is normalised below so only the sign matters
 * relative to the whole polygon. */
static float ysvg_fill_cross(const struct yetty_ysvg_point *a, const struct yetty_ysvg_point *b,
                             const struct yetty_ysvg_point *c)
{
    return (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
}

static int ysvg_fill_points_near(const struct yetty_ysvg_point *a, const struct yetty_ysvg_point *b)
{
    return fabsf(a->x - b->x) < 1e-6f && fabsf(a->y - b->y) < 1e-6f;
}

/* Strict-interior test: true only when `p` lies strictly inside the
 * counter-clockwise triangle (a, b, c). Vertices shared with the ear or lying
 * on an edge do not count, so they never wrongly block an otherwise valid
 * ear. */
static int ysvg_fill_point_in_tri(const struct yetty_ysvg_point *p,
                                  const struct yetty_ysvg_point *a,
                                  const struct yetty_ysvg_point *b,
                                  const struct yetty_ysvg_point *c)
{
    return ysvg_fill_cross(a, b, p) > 1e-7f && ysvg_fill_cross(b, c, p) > 1e-7f &&
           ysvg_fill_cross(c, a, p) > 1e-7f;
}

static float ysvg_fill_signed_area(const struct yetty_ysvg_point *pts, const uint32_t *ring,
                                   size_t count)
{
    float sum = 0.0f;
    for (size_t i = 0; i < count; i++) {
        const struct yetty_ysvg_point *a = &pts[ring[i]];
        const struct yetty_ysvg_point *b = &pts[ring[(i + 1) % count]];
        sum += a->x * b->y - b->x * a->y;
    }
    return sum * 0.5f;
}

size_t yetty_ysvg_triangulate(const struct yetty_ysvg_point *pts, size_t n, uint32_t **out_indices)
{
    *out_indices = NULL;
    if (!pts || n < 3 || n > YSVG_FILL_MAX_VERTS) {
        return 0;
    }

    /* Compact the ring: drop consecutive coincident points (curve flattening
     * and explicit "Z" closes both introduce duplicates) that would otherwise
     * spawn zero-area ears and stall the clip. */
    uint32_t *ring = malloc(n * sizeof(*ring));
    if (!ring) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        if (count > 0 && ysvg_fill_points_near(&pts[ring[count - 1]], &pts[i])) {
            continue;
        }
        ring[count++] = (uint32_t)i;
    }
    if (count > 1 && ysvg_fill_points_near(&pts[ring[0]], &pts[ring[count - 1]])) {
        count--;
    }
    if (count < 3) {
        free(ring);
        return 0;
    }

    /* Normalise to counter-clockwise so the convex-corner test has one sign. */
    if (ysvg_fill_signed_area(pts, ring, count) < 0.0f) {
        for (size_t i = 0; i < count / 2; i++) {
            uint32_t tmp = ring[i];
            ring[i] = ring[count - 1 - i];
            ring[count - 1 - i] = tmp;
        }
    }

    uint32_t *next = malloc(count * sizeof(*next));
    uint32_t *prev = malloc(count * sizeof(*prev));
    uint32_t *tris = malloc((count - 2) * 3 * sizeof(*tris));
    if (!next || !prev || !tris) {
        free(ring);
        free(next);
        free(prev);
        free(tris);
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        next[i] = (uint32_t)((i + 1) % count);
        prev[i] = (uint32_t)((i + count - 1) % count);
    }

    size_t tri_count = 0;
    size_t remaining = count;
    size_t cur = 0;
    /* Consecutive non-ear vertices; once we cycle the whole remaining ring
     * without clipping, the polygon is degenerate/self-intersecting — bail
     * with what we have rather than loop forever. */
    size_t fails = 0;
    while (remaining > 2 && fails <= remaining) {
        size_t i0 = prev[cur];
        size_t i1 = cur;
        size_t i2 = next[cur];
        const struct yetty_ysvg_point *a = &pts[ring[i0]];
        const struct yetty_ysvg_point *b = &pts[ring[i1]];
        const struct yetty_ysvg_point *c = &pts[ring[i2]];
        int is_ear = 0;
        if (ysvg_fill_cross(a, b, c) > 1e-7f) { /* convex corner */
            is_ear = 1;
            for (size_t k = next[i2]; k != i0; k = next[k]) {
                if (ysvg_fill_point_in_tri(&pts[ring[k]], a, b, c)) {
                    is_ear = 0;
                    break;
                }
            }
        }
        if (is_ear) {
            tris[tri_count * 3 + 0] = ring[i0];
            tris[tri_count * 3 + 1] = ring[i1];
            tris[tri_count * 3 + 2] = ring[i2];
            tri_count++;
            next[i0] = (uint32_t)i2;
            prev[i2] = (uint32_t)i0;
            remaining--;
            cur = i0;
            fails = 0;
        } else {
            cur = i2;
            fails++;
        }
    }

    free(ring);
    free(next);
    free(prev);
    if (tri_count == 0) {
        free(tris);
        return 0;
    }
    *out_indices = tris;
    return tri_count;
}
