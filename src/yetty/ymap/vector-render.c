/*
 * vector-render.c — vector tiles → native SDF/MSDF drawables.
 *
 * The vector counterpart of openstreet.c's raster composite: fetches the
 * Mapbox-Vector-Tile (shortbread schema) tiles covering the viewport,
 * decodes them (vector-tile.c) and emits the map as first-class ydraw
 * drawables instead of pixels:
 *
 *   polygons (ocean, water, land cover, buildings) → ear-clipped
 *     YETTY_YSDF_TRIANGLE fills,
 *   linestrings (streets, rails, waterways, boundaries) → per-segment
 *     YETTY_YSDF_SEGMENT strokes with per-class width/color,
 *   labels (street_labels, place_labels) → MSDF text runs
 *     (yetty_ydraw_drawable_list_add_text, terminal default font).
 *
 * Everything is clipped to the viewport rect: Liang–Barsky for segments,
 * Sutherland–Hodgman for polygon rings (then triangulated), point checks
 * for labels — tile buffers would otherwise paint into the surrounding
 * text. Tile-border label duplicates are avoided by emitting a label only
 * when its anchor lies inside its tile's core extent.
 *
 * Like the raster path, per-tile failures are best-effort BY DESIGN: a
 * tile that fails to download or parse leaves a hole and the render
 * proceeds.
 *
 * Known v1 simplifications: polygon holes are skipped (islands in lakes
 * fill as water), street labels render horizontally at the line midpoint,
 * and no street casings/dashes.
 */

#include <yetty/ymap/engine.h>

#include "tile-fetch.h"
#include "vector-tile.h"

#include <yetty/yplatform/paths.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ytrace/ytrace.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OSM_VECTOR_TILE_SIZE 256.0f
#define OSM_VECTOR_DEFAULT_URL "https://vector.openstreetmap.org/shortbread_v1/%u/%u/%u.mvt"
#define OSM_VECTOR_MAX_VIEWPORT_PX 4096u
#define OSM_VECTOR_MAX_TILES 64u
/* Emission cap — a dense city viewport stays well under this; a corrupt
 * tile cannot balloon the envelope. Counted across every prim kind. */
#define OSM_VECTOR_MAX_PRIMS 400000u
/* Consecutive polyline points closer than this (px) are merged. */
#define OSM_VECTOR_DECIMATE_PX 0.75f
/* Clipped polygon rings are capped here before triangulation. */
#define OSM_VECTOR_MAX_RING_POINTS 8192u
/* Rings larger than this are clipped into square patches before
 * triangulation so triangle AABBs stay bucket-local (see
 * emit_polygon_feature). */
#define OSM_FILL_PATCH_PX 128u

/* Z bands — fixed per class so layering is stable regardless of tile
 * arrival order. */
enum osm_vector_z_band {
    OSM_Z_BACKGROUND = 0,
    OSM_Z_LAND = 10,
    OSM_Z_WATER = 20,
    OSM_Z_BUILDINGS = 30,
    /* Cased roads: every casing under every fill, classes stacked
     * minor → major inside each band — the osm-carto layering that makes
     * junctions read as connected surfaces instead of overlapping
     * strokes. */
    OSM_Z_CASING_MINOR = 40,
    OSM_Z_CASING_MID = 41,
    OSM_Z_CASING_MAJOR = 42,
    OSM_Z_FILL_MINOR = 45,
    OSM_Z_FILL_MID = 46,
    OSM_Z_FILL_MAJOR = 47,
    OSM_Z_RAIL = 50,
    OSM_Z_RAIL_DASH = 51,
    OSM_Z_BOUNDARY = 60,
    OSM_Z_STREET_LABELS = 100,
    OSM_Z_PLACE_LABELS = 110,
};

/*=============================================================================
 * Styling — shortbread schema, palette borrowed from osm-carto.
 *===========================================================================*/

enum osm_line_render {
    OSM_LINE_SOLID = 0, /* fill stroke only */
    OSM_LINE_CASED,     /* casing stroke under the fill stroke */
    OSM_LINE_DASHED,    /* dashes of the fill color, no casing */
    OSM_LINE_RAIL,      /* solid dark base + white dash ladder on top */
};

struct osm_line_style {
    const char *kind;
    uint32_t color; /* 0xAARRGGBB */
    float width_px;
    uint32_t z_band;
    enum osm_line_render render;
    uint32_t casing_color;
    float casing_extra_px; /* added to width for the casing stroke */
    float dash_px;
    float gap_px;
    uint32_t min_zoom; /* kind hidden below this zoom — declutters low zooms */
};

/* Streets layer (also carries rail kinds), osm-carto palette: cased
 * majors in warm tones, white minors with gray casings, dashed paths,
 * ladder rails. NULL-kind row terminates. */
static const struct osm_line_style *street_style_for_kind(const char *kind)
{
    static const struct osm_line_style styles[] = {
        {"motorway", 0xffe892a2u, 6.0f, OSM_Z_FILL_MAJOR, OSM_LINE_CASED, 0xffc24e6bu, 2.0f, 0, 0,
         0},
        {"trunk", 0xfff9b29cu, 5.5f, OSM_Z_FILL_MAJOR, OSM_LINE_CASED, 0xffcf6649u, 2.0f, 0, 0, 0},
        {"primary", 0xfffcd6a4u, 5.0f, OSM_Z_FILL_MAJOR, OSM_LINE_CASED, 0xffb88a32u, 2.0f, 0, 0,
         0},
        {"secondary", 0xfff7fabfu, 4.5f, OSM_Z_FILL_MID, OSM_LINE_CASED, 0xff9aa11fu, 1.8f, 0, 0,
         9},
        {"tertiary", 0xffffffffu, 4.0f, OSM_Z_FILL_MID, OSM_LINE_CASED, 0xffa8a8a8u, 1.8f, 0, 0,
         10},
        {"residential", 0xffffffffu, 3.0f, OSM_Z_FILL_MINOR, OSM_LINE_CASED, 0xffbbbbbbu, 1.6f, 0,
         0, 12},
        {"unclassified", 0xffffffffu, 3.0f, OSM_Z_FILL_MINOR, OSM_LINE_CASED, 0xffbbbbbbu, 1.6f, 0,
         0, 12},
        {"living_street", 0xffededecu, 3.0f, OSM_Z_FILL_MINOR, OSM_LINE_CASED, 0xffc5c5c5u, 1.5f, 0,
         0, 13},
        {"pedestrian", 0xffdddde9u, 2.5f, OSM_Z_FILL_MINOR, OSM_LINE_CASED, 0xffa9a9bcu, 1.4f, 0, 0,
         13},
        {"service", 0xffffffffu, 1.8f, OSM_Z_FILL_MINOR, OSM_LINE_CASED, 0xffccccccu, 1.2f, 0, 0,
         14},
        {"track", 0xffac8331u, 1.2f, OSM_Z_FILL_MINOR, OSM_LINE_DASHED, 0, 0, 6.0f, 4.0f, 14},
        {"footway", 0xfffa8072u, 1.0f, OSM_Z_FILL_MINOR, OSM_LINE_DASHED, 0, 0, 3.5f, 3.0f, 14},
        {"path", 0xfffa8072u, 1.0f, OSM_Z_FILL_MINOR, OSM_LINE_DASHED, 0, 0, 3.5f, 3.0f, 14},
        {"cycleway", 0xff9393eau, 1.0f, OSM_Z_FILL_MINOR, OSM_LINE_DASHED, 0, 0, 5.0f, 3.0f, 14},
        {"steps", 0xfffa8072u, 1.6f, OSM_Z_FILL_MINOR, OSM_LINE_DASHED, 0, 0, 1.5f, 1.5f, 14},
        {"rail", 0xff707070u, 1.8f, OSM_Z_RAIL, OSM_LINE_RAIL, 0, 0, 6.0f, 6.0f, 9},
        {"light_rail", 0xff6e6e6eu, 1.4f, OSM_Z_RAIL, OSM_LINE_RAIL, 0, 0, 4.0f, 4.0f, 13},
        {"subway", 0xff999999u, 1.4f, OSM_Z_RAIL, OSM_LINE_DASHED, 0, 0, 5.0f, 3.0f, 13},
        {"tram", 0xff6b6b6bu, 1.0f, OSM_Z_RAIL, OSM_LINE_SOLID, 0, 0, 0, 0, 13},
        {"narrow_gauge", 0xff707070u, 1.2f, OSM_Z_RAIL, OSM_LINE_RAIL, 0, 0, 5.0f, 5.0f, 12},
        {NULL, 0, 0.0f, 0, OSM_LINE_SOLID, 0, 0, 0, 0, 0},
    };
    if (!kind) {
        return NULL;
    }
    for (size_t i = 0; styles[i].kind; i++) {
        if (strcmp(styles[i].kind, kind) == 0) {
            return &styles[i];
        }
    }
    return NULL;
}

/* Roads narrow as the camera pulls back — constant pixel widths look
 * cartoonish below city zoom. */
static float street_width_zoom_factor(uint32_t zoom)
{
    if (zoom >= 13) {
        return 1.0f;
    }
    if (zoom == 12) {
        return 0.8f;
    }
    if (zoom == 11) {
        return 0.62f;
    }
    return 0.45f;
}

/* The casing band sits exactly one fill-band step below; relies on the
 * CASING/FILL enum spacing above. */
static uint32_t casing_band_for_fill_band(uint32_t fill_band)
{
    return fill_band - (OSM_Z_FILL_MINOR - OSM_Z_CASING_MINOR);
}

static const struct osm_line_style *water_line_style_for_kind(const char *kind)
{
    static const struct osm_line_style styles[] = {
        {"river", 0xffaad3dfu, 3.0f, OSM_Z_WATER, OSM_LINE_SOLID, 0, 0, 0, 0, 0},
        {"canal", 0xffaad3dfu, 2.0f, OSM_Z_WATER, OSM_LINE_SOLID, 0, 0, 0, 0, 9},
        {"stream", 0xffaad3dfu, 1.2f, OSM_Z_WATER, OSM_LINE_SOLID, 0, 0, 0, 0, 12},
        {"ditch", 0xffaad3dfu, 1.0f, OSM_Z_WATER, OSM_LINE_SOLID, 0, 0, 0, 0, 14},
        {NULL, 0, 0.0f, 0, OSM_LINE_SOLID, 0, 0, 0, 0, 0},
    };
    if (!kind) {
        return NULL;
    }
    for (size_t i = 0; styles[i].kind; i++) {
        if (strcmp(styles[i].kind, kind) == 0) {
            return &styles[i];
        }
    }
    return NULL;
}

/* Land-cover fills (shortbread "land" layer). 0 = skip the kind. */
static uint32_t land_fill_for_kind(const char *kind)
{
    static const struct {
        const char *kind;
        uint32_t color;
    } fills[] = {
        {"forest", 0xffadd19eu},
        {"wood", 0xffadd19eu},
        {"grass", 0xffcdebb0u},
        {"meadow", 0xffcdebb0u},
        {"park", 0xffc8faccu},
        {"garden", 0xffcdebb0u},
        {"village_green", 0xffcdebb0u},
        {"recreation_ground", 0xffcdebb0u},
        {"cemetery", 0xffaacbafu},
        {"allotments", 0xffc9e1bfu},
        {"heath", 0xffd6d99fu},
        {"scrub", 0xffc8d7abu},
        {"golf_course", 0xffcdebb0u},
        {"farmland", 0xffeef0d5u},
        {"orchard", 0xffaedfa3u},
        {"vineyard", 0xffaedfa3u},
        {"sand", 0xfffff1bau},
        {"beach", 0xfffff1bau},
        {NULL, 0},
    };
    if (!kind) {
        return 0;
    }
    for (size_t i = 0; fills[i].kind; i++) {
        if (strcmp(fills[i].kind, kind) == 0) {
            return fills[i].color;
        }
    }
    return 0;
}

static float place_label_size_for_kind(const char *kind)
{
    if (!kind) {
        return 0.0f;
    }
    if (strcmp(kind, "city") == 0) {
        return 16.0f;
    }
    if (strcmp(kind, "town") == 0) {
        return 13.0f;
    }
    if (strcmp(kind, "village") == 0 || strcmp(kind, "suburb") == 0 ||
        strcmp(kind, "quarter") == 0) {
        return 11.0f;
    }
    if (strcmp(kind, "neighbourhood") == 0 || strcmp(kind, "hamlet") == 0) {
        return 10.0f;
    }
    return 0.0f;
}

/*=============================================================================
 * Geometry helpers — clipping + triangulation
 *===========================================================================*/

struct osm_point {
    float x;
    float y;
};

/* Liang–Barsky segment clip against [0,w]×[0,h]. Returns 0 when the
 * segment is fully outside. */
static int clip_segment(float *x0, float *y0, float *x1, float *y1, float clip_w, float clip_h)
{
    float dx = *x1 - *x0;
    float dy = *y1 - *y0;
    float t_enter = 0.0f;
    float t_exit = 1.0f;
    float edge_p[4] = {-dx, dx, -dy, dy};
    float edge_q[4] = {*x0 - 0.0f, clip_w - *x0, *y0 - 0.0f, clip_h - *y0};
    for (int edge = 0; edge < 4; edge++) {
        if (edge_p[edge] == 0.0f) {
            if (edge_q[edge] < 0.0f) {
                return 0;
            }
            continue;
        }
        float t = edge_q[edge] / edge_p[edge];
        if (edge_p[edge] < 0.0f) {
            if (t > t_exit) {
                return 0;
            }
            if (t > t_enter) {
                t_enter = t;
            }
        } else {
            if (t < t_enter) {
                return 0;
            }
            if (t < t_exit) {
                t_exit = t;
            }
        }
    }
    float clipped_x0 = *x0 + t_enter * dx;
    float clipped_y0 = *y0 + t_enter * dy;
    float clipped_x1 = *x0 + t_exit * dx;
    float clipped_y1 = *y0 + t_exit * dy;
    *x0 = clipped_x0;
    *y0 = clipped_y0;
    *x1 = clipped_x1;
    *y1 = clipped_y1;
    return 1;
}

/* One Sutherland–Hodgman pass against a single half-plane.
 * edge_axis: 0 = x, 1 = y; keep_greater: keep points >= bound. */
static uint32_t clip_polygon_edge(const struct osm_point *in, uint32_t in_count,
                                  struct osm_point *out, uint32_t out_cap, int edge_axis,
                                  float bound, int keep_greater)
{
    uint32_t out_count = 0;
    for (uint32_t i = 0; i < in_count; i++) {
        struct osm_point current = in[i];
        struct osm_point previous = in[(i + in_count - 1) % in_count];
        float current_coord = edge_axis ? current.y : current.x;
        float previous_coord = edge_axis ? previous.y : previous.x;
        int current_inside = keep_greater ? (current_coord >= bound) : (current_coord <= bound);
        int previous_inside = keep_greater ? (previous_coord >= bound) : (previous_coord <= bound);

        if (current_inside != previous_inside) {
            float t = (bound - previous_coord) / (current_coord - previous_coord);
            if (out_count < out_cap) {
                out[out_count].x = previous.x + t * (current.x - previous.x);
                out[out_count].y = previous.y + t * (current.y - previous.y);
                out_count++;
            }
        }
        if (current_inside && out_count < out_cap) {
            out[out_count++] = current;
        }
    }
    return out_count;
}

/* Clip a ring against an arbitrary rect. Returns the clipped point count
 * in `work_a` (the caller's primary scratch buffer). */
static uint32_t clip_polygon_rect(struct osm_point *work_a, struct osm_point *work_b,
                                  uint32_t point_count, uint32_t cap, float min_x, float min_y,
                                  float max_x, float max_y)
{
    point_count = clip_polygon_edge(work_a, point_count, work_b, cap, 0, min_x, 1);
    point_count = clip_polygon_edge(work_b, point_count, work_a, cap, 0, max_x, 0);
    point_count = clip_polygon_edge(work_a, point_count, work_b, cap, 1, min_y, 1);
    point_count = clip_polygon_edge(work_b, point_count, work_a, cap, 1, max_y, 0);
    return point_count;
}

static float ring_signed_area(const struct osm_point *points, uint32_t count)
{
    float area = 0.0f;
    for (uint32_t i = 0; i < count; i++) {
        const struct osm_point *a = &points[i];
        const struct osm_point *b = &points[(i + 1) % count];
        area += a->x * b->y - b->x * a->y;
    }
    return area * 0.5f;
}

static int point_in_triangle(const struct osm_point *p, const struct osm_point *a,
                             const struct osm_point *b, const struct osm_point *c)
{
    float cross_ab = (b->x - a->x) * (p->y - a->y) - (b->y - a->y) * (p->x - a->x);
    float cross_bc = (c->x - b->x) * (p->y - b->y) - (c->y - b->y) * (p->x - b->x);
    float cross_ca = (a->x - c->x) * (p->y - c->y) - (a->y - c->y) * (p->x - c->x);
    int has_negative = (cross_ab < 0.0f) || (cross_bc < 0.0f) || (cross_ca < 0.0f);
    int has_positive = (cross_ab > 0.0f) || (cross_bc > 0.0f) || (cross_ca > 0.0f);
    return !(has_negative && has_positive);
}

/*=============================================================================
 * Emission context
 *===========================================================================*/

/* Style tables above are written in natural 0xAARRGGBB. The SDF wire
 * stores RGBA bytes little-endian — word 0xAABBGGRR (the shader's
 * yetty_ysdf_unpack_color maps the LSB to red) — so every color crossing
 * into add_cmd_add_* gets its R and B lanes swapped here. Grays are
 * swap-invariant, which is how the mismatch stayed invisible in other
 * producers; the Danube rendering khaki instead of water-blue is what
 * exposed it. */
static uint32_t osm_color_to_wire(uint32_t argb)
{
    return (argb & 0xff00ff00u) | ((argb & 0x00ff0000u) >> 16) | ((argb & 0x000000ffu) << 16);
}

struct osm_label_candidate {
    char *text; /* owned copy — outlives the per-tile parse */
    float x;
    float y;
    float font_size;
    float char_spacing; /* cartographic letter-spacing for place names */
    uint32_t color;
    uint32_t z_band;
    int priority; /* lower places first (city < town < ... < minor street) */
};

struct osm_vector_emit {
    struct yetty_ydraw_drawable_list *list;
    float viewport_w;
    float viewport_h;
    uint32_t zoom;
    uint32_t prim_count;
    struct osm_label_candidate *labels;
    uint32_t label_count;
    uint32_t label_capacity;
};

static int emit_budget_left(struct osm_vector_emit *emit)
{
    return emit->prim_count < OSM_VECTOR_MAX_PRIMS;
}

static struct yetty_ycore_void_result emit_segment(struct osm_vector_emit *emit, float x0, float y0,
                                                   float x1, float y1, uint32_t color, float width,
                                                   uint32_t z_band)
{
    if (!emit_budget_left(emit)) {
        return YETTY_OK_VOID();
    }
    if (!clip_segment(&x0, &y0, &x1, &y1, emit->viewport_w, emit->viewport_h)) {
        return YETTY_OK_VOID();
    }
    struct yetty_ysdf_segment geometry = {.start_x = x0, .start_y = y0, .end_x = x1, .end_y = y1};
    struct yetty_ycore_void_result add_res = yetty_ydraw_drawable_list_add_cmd_add_segment(
        emit->list, /*id=*/0, z_band, 0, osm_color_to_wire(color), width, &geometry);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_res, "osm vector: add segment");
    emit->prim_count++;
    return YETTY_OK_VOID();
}

/* Adjacent SDF triangles each anti-alias their edges, so a shared edge
 * gets ~half coverage from both sides and composites as a visible seam —
 * the whole triangulation skeleton shows through large fills. Stroking
 * every triangle's edges in its own fill color covers the AA falloff
 * from both sides; the overlap is invisible (same color, opaque) and
 * works for arbitrarily thin ear-clip slivers, where centroid-based
 * inflation barely moves the long edges. */
#define OSM_TRIANGLE_SEAM_STROKE_PX 1.2f

/* Ear-clip triangulate `points` (a simple, possibly concave ring) and
 * emit each triangle as a filled SDF prim. Degenerate input is skipped
 * silently; a ring the clipper mangled gives up after a bounded number
 * of passes rather than looping. */
static struct yetty_ycore_void_result emit_polygon_fill(struct osm_vector_emit *emit,
                                                        const struct osm_point *points,
                                                        uint32_t point_count, uint32_t color,
                                                        uint32_t z_band)
{
    if (point_count < 3 || !emit_budget_left(emit)) {
        return YETTY_OK_VOID();
    }
    float area = ring_signed_area(points, point_count);
    if (fabsf(area) < 0.5f) {
        return YETTY_OK_VOID(); /* sub-pixel sliver */
    }
    int winding_positive = area > 0.0f;

    uint32_t *indices = malloc(point_count * sizeof(uint32_t));
    if (!indices) {
        return YETTY_ERR(yetty_ycore_void, "osm vector: triangulation index alloc failed");
    }
    for (uint32_t i = 0; i < point_count; i++) {
        indices[i] = i;
    }

    uint32_t remaining = point_count;
    uint32_t stuck_guard = 0;
    struct yetty_ycore_void_result result = YETTY_OK_VOID();

    while (remaining > 3 && stuck_guard < remaining) {
        int ear_found = 0;
        for (uint32_t i = 0; i < remaining && emit_budget_left(emit); i++) {
            const struct osm_point *prev = &points[indices[(i + remaining - 1) % remaining]];
            const struct osm_point *curr = &points[indices[i]];
            const struct osm_point *next = &points[indices[(i + 1) % remaining]];

            float cross = (curr->x - prev->x) * (next->y - prev->y) -
                          (curr->y - prev->y) * (next->x - prev->x);
            int convex = winding_positive ? (cross > 0.0f) : (cross < 0.0f);
            if (!convex) {
                continue;
            }
            int contains_other = 0;
            for (uint32_t j = 0; j < remaining; j++) {
                uint32_t candidate = indices[j];
                if (candidate == indices[i] ||
                    candidate == indices[(i + remaining - 1) % remaining] ||
                    candidate == indices[(i + 1) % remaining]) {
                    continue;
                }
                if (point_in_triangle(&points[candidate], prev, curr, next)) {
                    contains_other = 1;
                    break;
                }
            }
            if (contains_other) {
                continue;
            }

            struct yetty_ysdf_triangle geometry = {
                .vertex_a_x = prev->x,
                .vertex_a_y = prev->y,
                .vertex_b_x = curr->x,
                .vertex_b_y = curr->y,
                .vertex_c_x = next->x,
                .vertex_c_y = next->y,
            };
            result = yetty_ydraw_drawable_list_add_cmd_add_triangle(
                emit->list, /*id=*/0, z_band, osm_color_to_wire(color), osm_color_to_wire(color),
                OSM_TRIANGLE_SEAM_STROKE_PX, &geometry);
            if (YETTY_IS_ERR(result)) {
                free(indices);
                return YETTY_ERR(yetty_ycore_void, "osm vector: add triangle", result);
            }
            emit->prim_count++;

            /* Remove the ear vertex. */
            for (uint32_t j = i; j + 1 < remaining; j++) {
                indices[j] = indices[j + 1];
            }
            remaining--;
            ear_found = 1;
            stuck_guard = 0;
            break;
        }
        if (!ear_found) {
            stuck_guard++; /* degenerate ring — give up below */
            break;
        }
    }

    if (remaining == 3 && emit_budget_left(emit)) {
        struct yetty_ysdf_triangle geometry = {
            .vertex_a_x = points[indices[0]].x,
            .vertex_a_y = points[indices[0]].y,
            .vertex_b_x = points[indices[1]].x,
            .vertex_b_y = points[indices[1]].y,
            .vertex_c_x = points[indices[2]].x,
            .vertex_c_y = points[indices[2]].y,
        };
        result = yetty_ydraw_drawable_list_add_cmd_add_triangle(
            emit->list, /*id=*/0, z_band, osm_color_to_wire(color), osm_color_to_wire(color),
            OSM_TRIANGLE_SEAM_STROKE_PX, &geometry);
        if (YETTY_IS_ERR(result)) {
            free(indices);
            return YETTY_ERR(yetty_ycore_void, "osm vector: add final triangle", result);
        }
        emit->prim_count++;
    }
    free(indices);
    return YETTY_OK_VOID();
}

/* Street kinds worth a name, by importance. -1 = never labeled
 * (service ways, footpaths, rails). At z13 and below only ranks up to
 * secondary are collected — minor-street names are z14+ detail. */
static int street_label_rank(const char *kind)
{
    if (!kind) {
        return -1;
    }
    static const char *const ranked[] = {"motorway",     "trunk",         "primary",
                                         "secondary",    "tertiary",      "residential",
                                         "unclassified", "living_street", "pedestrian"};
    for (size_t i = 0; i < sizeof(ranked) / sizeof(ranked[0]); i++) {
        if (strcmp(ranked[i], kind) == 0) {
            return (int)i;
        }
    }
    return -1;
}

#define OSM_STREET_LABEL_MAX_RANK_LOW_ZOOM 3 /* ..secondary below z14 */
#define OSM_VECTOR_MAX_LABEL_CANDIDATES 8192u
#define OSM_VECTOR_MAX_PLACED_LABELS 96u
#define OSM_LABEL_COLLISION_PAD_PX 6.0f
/* Same advance estimate emit_text uses for centering. */
#define OSM_LABEL_ADVANCE_EM 0.55f

static struct yetty_ycore_void_result push_label_candidate(struct osm_vector_emit *emit, float x,
                                                           float y, const char *text,
                                                           float font_size, float char_spacing,
                                                           uint32_t color, uint32_t z_band,
                                                           int priority)
{
    if (!text || !text[0] || emit->label_count >= OSM_VECTOR_MAX_LABEL_CANDIDATES) {
        return YETTY_OK_VOID();
    }
    if (emit->label_count + 1 > emit->label_capacity) {
        uint32_t new_capacity = emit->label_capacity ? emit->label_capacity * 2 : 256;
        struct osm_label_candidate *grown =
            realloc(emit->labels, new_capacity * sizeof(struct osm_label_candidate));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "osm vector: label pool alloc failed");
        }
        emit->labels = grown;
        emit->label_capacity = new_capacity;
    }
    char *text_copy = strdup(text);
    if (!text_copy) {
        return YETTY_ERR(yetty_ycore_void, "osm vector: label text alloc failed");
    }
    emit->labels[emit->label_count++] = (struct osm_label_candidate){
        .text = text_copy,
        .x = x,
        .y = y,
        .font_size = font_size,
        .char_spacing = char_spacing,
        .color = color,
        .z_band = z_band,
        .priority = priority,
    };
    return YETTY_OK_VOID();
}

static float label_width_estimate(const char *text, float font_size, float char_spacing)
{
    size_t text_len = strlen(text);
    /* The default mono-ish font advance is ~0.55 em. */
    float width = (float)text_len * font_size * OSM_LABEL_ADVANCE_EM;
    if (text_len > 1) {
        width += char_spacing * (float)(text_len - 1);
    }
    return width;
}

static struct yetty_ycore_void_result emit_text(struct osm_vector_emit *emit, float x, float y,
                                                const char *text, float font_size,
                                                float char_spacing, uint32_t color, uint32_t z_band)
{
    if (!text || !text[0] || !emit_budget_left(emit)) {
        return YETTY_OK_VOID();
    }
    float anchor_x = x - label_width_estimate(text, font_size, char_spacing) * 0.5f;
    if (anchor_x < 0.0f) {
        anchor_x = 0.0f;
    }
    if (x < 0.0f || x > emit->viewport_w || y < font_size || y > emit->viewport_h) {
        return YETTY_OK_VOID();
    }
    size_t text_len = strlen(text);
    struct yetty_ycore_buffer view = {
        .data = (uint8_t *)(uintptr_t)text, .capacity = text_len, .size = text_len};
    struct yetty_ycore_void_result add_res = yetty_ydraw_drawable_list_add_text_full(
        emit->list, anchor_x, y, &view, font_size, color, z_band, -1, 0.0f, char_spacing, 0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_res, "osm vector: add text");
    emit->prim_count++;
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Per-feature emission
 *===========================================================================*/

struct osm_tile_transform {
    float offset_x; /* viewport px of the tile's north-west corner */
    float offset_y;
    float scale; /* tile units → viewport px (256 / extent) */
    uint32_t extent;
};

static struct osm_point transform_point(const struct osm_tile_transform *transform,
                                        struct yetty_ymap_vt_point point)
{
    struct osm_point out = {
        .x = transform->offset_x + point.x * transform->scale,
        .y = transform->offset_y + point.y * transform->scale,
    };
    return out;
}

static struct yetty_ycore_void_result emit_polyline(struct osm_vector_emit *emit,
                                                    const struct osm_tile_transform *transform,
                                                    const struct yetty_ymap_vt_ring *ring,
                                                    uint32_t color, float width, uint32_t z_band)
{
    if (ring->point_count < 2) {
        return YETTY_OK_VOID();
    }
    struct osm_point previous = transform_point(transform, ring->points[0]);
    for (uint32_t i = 1; i < ring->point_count; i++) {
        struct osm_point current = transform_point(transform, ring->points[i]);
        float dx = current.x - previous.x;
        float dy = current.y - previous.y;
        /* Decimate sub-pixel steps — except the final point, which must
         * land exactly so polylines don't visibly shorten. */
        if (i + 1 < ring->point_count &&
            dx * dx + dy * dy < OSM_VECTOR_DECIMATE_PX * OSM_VECTOR_DECIMATE_PX) {
            continue;
        }
        struct yetty_ycore_void_result seg_res =
            emit_segment(emit, previous.x, previous.y, current.x, current.y, color, width, z_band);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, seg_res, "osm vector: polyline segment");
        previous = current;
    }
    return YETTY_OK_VOID();
}

/* Dashed polyline: the dash phase runs continuously across vertices so
 * the pattern flows through bends instead of restarting per segment. */
static struct yetty_ycore_void_result emit_polyline_dashed(
    struct osm_vector_emit *emit, const struct osm_tile_transform *transform,
    const struct yetty_ymap_vt_ring *ring, uint32_t color, float width, uint32_t z_band,
    float dash_px, float gap_px)
{
    if (ring->point_count < 2 || dash_px <= 0.0f) {
        return YETTY_OK_VOID();
    }
    float period = dash_px + gap_px;
    float phase = 0.0f; /* distance into the current period */
    struct osm_point previous = transform_point(transform, ring->points[0]);

    for (uint32_t i = 1; i < ring->point_count; i++) {
        struct osm_point current = transform_point(transform, ring->points[i]);
        float delta_x = current.x - previous.x;
        float delta_y = current.y - previous.y;
        float segment_len = sqrtf(delta_x * delta_x + delta_y * delta_y);
        if (segment_len < 0.01f) {
            continue;
        }
        float unit_x = delta_x / segment_len;
        float unit_y = delta_y / segment_len;

        float walked = 0.0f;
        while (walked < segment_len) {
            float in_period = fmodf(phase + walked, period);
            if (in_period < dash_px) {
                float dash_left = dash_px - in_period;
                float draw_len =
                    dash_left < segment_len - walked ? dash_left : segment_len - walked;
                struct yetty_ycore_void_result dash_res =
                    emit_segment(emit, previous.x + unit_x * walked, previous.y + unit_y * walked,
                                 previous.x + unit_x * (walked + draw_len),
                                 previous.y + unit_y * (walked + draw_len), color, width, z_band);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, dash_res, "osm vector: dash segment");
                walked += draw_len;
            } else {
                walked += period - in_period; /* skip the gap remainder */
            }
        }
        phase = fmodf(phase + segment_len, period);
        previous = current;
    }
    return YETTY_OK_VOID();
}

/* One street feature, styled: casing under fill, dash patterns, rail
 * ladders. `width_factor` is the zoom narrowing. */
static struct yetty_ycore_void_result emit_street_feature(
    struct osm_vector_emit *emit, const struct osm_tile_transform *transform,
    const struct yetty_ymap_vt_feature *feature, const struct osm_line_style *style,
    float width_factor)
{
    float width = style->width_px * width_factor;
    for (uint32_t ring_index = 0; ring_index < feature->ring_count; ring_index++) {
        const struct yetty_ymap_vt_ring *ring = &feature->rings[ring_index];
        struct yetty_ycore_void_result ring_res = YETTY_OK_VOID();
        switch (style->render) {
        case OSM_LINE_CASED:
            ring_res = emit_polyline(emit, transform, ring, style->casing_color,
                                     width + style->casing_extra_px * width_factor,
                                     casing_band_for_fill_band(style->z_band));
            if (YETTY_IS_OK(ring_res)) {
                ring_res = emit_polyline(emit, transform, ring, style->color, width, style->z_band);
            }
            break;
        case OSM_LINE_DASHED:
            ring_res = emit_polyline_dashed(emit, transform, ring, style->color, width,
                                            style->z_band, style->dash_px, style->gap_px);
            break;
        case OSM_LINE_RAIL:
            /* Dark base with a white dash ladder on top — the classic
             * railway signature. */
            ring_res = emit_polyline(emit, transform, ring, style->color, width, style->z_band);
            if (YETTY_IS_OK(ring_res)) {
                ring_res = emit_polyline_dashed(emit, transform, ring, 0xfffafafau, width * 0.6f,
                                                OSM_Z_RAIL_DASH, style->dash_px, style->gap_px);
            }
            break;
        case OSM_LINE_SOLID:
        default:
            ring_res = emit_polyline(emit, transform, ring, style->color, width, style->z_band);
            break;
        }
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ring_res, "osm vector: street ring");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_polygon_feature(
    struct osm_vector_emit *emit, const struct osm_tile_transform *transform,
    const struct yetty_ymap_vt_feature *feature, uint32_t color, uint32_t z_band)
{
    /* Holes are skipped in v1: find the dominant winding (the largest
     * |area| ring is an exterior by the MVT spec) and emit only rings
     * wound the same way. */
    float dominant_area = 0.0f;
    for (uint32_t ring_index = 0; ring_index < feature->ring_count; ring_index++) {
        const struct yetty_ymap_vt_ring *ring = &feature->rings[ring_index];
        if (ring->point_count < 3) {
            continue;
        }
        float area = 0.0f;
        for (uint32_t i = 0; i < ring->point_count; i++) {
            const struct yetty_ymap_vt_point *a = &ring->points[i];
            const struct yetty_ymap_vt_point *b = &ring->points[(i + 1) % ring->point_count];
            area += a->x * b->y - b->x * a->y;
        }
        if (fabsf(area) > fabsf(dominant_area)) {
            dominant_area = area;
        }
    }

    struct osm_point *work_a = NULL;
    struct osm_point *work_b = NULL;
    struct osm_point *work_base = NULL;
    struct yetty_ycore_void_result result = YETTY_OK_VOID();

    for (uint32_t ring_index = 0; ring_index < feature->ring_count; ring_index++) {
        const struct yetty_ymap_vt_ring *ring = &feature->rings[ring_index];
        if (ring->point_count < 3 || ring->point_count > OSM_VECTOR_MAX_RING_POINTS) {
            continue;
        }
        float area = 0.0f;
        for (uint32_t i = 0; i < ring->point_count; i++) {
            const struct yetty_ymap_vt_point *a = &ring->points[i];
            const struct yetty_ymap_vt_point *b = &ring->points[(i + 1) % ring->point_count];
            area += a->x * b->y - b->x * a->y;
        }
        if ((area > 0.0f) != (dominant_area > 0.0f)) {
            continue; /* hole */
        }

        /* Transform, then clip to the viewport (+ slack for intersection
         * points the clip inserts). `base` keeps the viewport-clipped ring
         * intact across the patch loop below. */
        uint32_t cap = ring->point_count * 2 + 8;
        struct osm_point *grown_a = realloc(work_a, cap * sizeof(struct osm_point));
        struct osm_point *grown_b = realloc(work_b, cap * sizeof(struct osm_point));
        struct osm_point *grown_base = realloc(work_base, cap * sizeof(struct osm_point));
        if (grown_a) {
            work_a = grown_a;
        }
        if (grown_b) {
            work_b = grown_b;
        }
        if (grown_base) {
            work_base = grown_base;
        }
        if (!grown_a || !grown_b || !grown_base) {
            result = YETTY_ERR(yetty_ycore_void, "osm vector: clip scratch alloc failed");
            break;
        }
        for (uint32_t i = 0; i < ring->point_count; i++) {
            work_a[i] = transform_point(transform, ring->points[i]);
        }
        uint32_t clipped_count = clip_polygon_rect(work_a, work_b, ring->point_count, cap, 0.0f,
                                                   0.0f, emit->viewport_w, emit->viewport_h);
        if (clipped_count < 3) {
            continue;
        }

        /* Triangle AABBs must stay LOCAL: the spatial buckets register a
         * prim in every cell its AABB overlaps, and ear-clip fans from a
         * large ring produce sliver triangles spanning hundreds of px —
         * each occupying hundreds of cells and blowing the shader's
         * per-cell loop cap (which silently drops whatever lands after,
         * labels first). Rings bigger than one patch are clipped into
         * OSM_FILL_PATCH_PX windows and triangulated per patch; patch
         * borders are invisible (same color + edge stroke). */
        float ring_min_x = work_a[0].x;
        float ring_min_y = work_a[0].y;
        float ring_max_x = work_a[0].x;
        float ring_max_y = work_a[0].y;
        for (uint32_t i = 1; i < clipped_count; i++) {
            ring_min_x = work_a[i].x < ring_min_x ? work_a[i].x : ring_min_x;
            ring_min_y = work_a[i].y < ring_min_y ? work_a[i].y : ring_min_y;
            ring_max_x = work_a[i].x > ring_max_x ? work_a[i].x : ring_max_x;
            ring_max_y = work_a[i].y > ring_max_y ? work_a[i].y : ring_max_y;
        }
        if (ring_max_x - ring_min_x <= (float)OSM_FILL_PATCH_PX * 1.25f &&
            ring_max_y - ring_min_y <= (float)OSM_FILL_PATCH_PX * 1.25f) {
            result = emit_polygon_fill(emit, work_a, clipped_count, color, z_band);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            continue;
        }

        memcpy(work_base, work_a, clipped_count * sizeof(struct osm_point));
        uint32_t base_count = clipped_count;
        for (float patch_y = ring_min_y; patch_y < ring_max_y && YETTY_IS_OK(result);
             patch_y += (float)OSM_FILL_PATCH_PX) {
            for (float patch_x = ring_min_x; patch_x < ring_max_x && YETTY_IS_OK(result);
                 patch_x += (float)OSM_FILL_PATCH_PX) {
                memcpy(work_a, work_base, base_count * sizeof(struct osm_point));
                uint32_t patch_count = clip_polygon_rect(
                    work_a, work_b, base_count, cap, patch_x, patch_y,
                    patch_x + (float)OSM_FILL_PATCH_PX, patch_y + (float)OSM_FILL_PATCH_PX);
                if (patch_count >= 3) {
                    result = emit_polygon_fill(emit, work_a, patch_count, color, z_band);
                }
            }
        }
        if (YETTY_IS_ERR(result)) {
            break;
        }
    }
    free(work_a);
    free(work_b);
    free(work_base);
    return result;
}

/* One decoded tile → drawables. `core_only_labels`: a label is emitted
 * only when its anchor is inside [0, extent) so buffer copies from the
 * neighbouring tile don't double it. */
static struct yetty_ycore_void_result emit_tile(struct osm_vector_emit *emit,
                                                const struct yetty_ymap_vt_tile *tile,
                                                const struct osm_tile_transform *transform_template)
{
    for (uint32_t layer_index = 0; layer_index < tile->layer_count; layer_index++) {
        const struct yetty_ymap_vt_layer *layer = &tile->layers[layer_index];
        if (!layer->name) {
            continue;
        }
        struct osm_tile_transform transform = *transform_template;
        transform.extent = layer->extent;
        transform.scale = OSM_VECTOR_TILE_SIZE / (float)layer->extent;

        int is_ocean = strcmp(layer->name, "ocean") == 0;
        int is_water_polygons = strcmp(layer->name, "water_polygons") == 0;
        int is_water_lines = strcmp(layer->name, "water_lines") == 0;
        int is_land = strcmp(layer->name, "land") == 0;
        int is_buildings = strcmp(layer->name, "buildings") == 0;
        int is_streets = strcmp(layer->name, "streets") == 0;
        int is_boundaries = strcmp(layer->name, "boundaries") == 0;
        int is_street_labels = strcmp(layer->name, "street_labels") == 0;
        int is_place_labels = strcmp(layer->name, "place_labels") == 0;
        if (!is_ocean && !is_water_polygons && !is_water_lines && !is_land && !is_buildings &&
            !is_streets && !is_boundaries && !is_street_labels && !is_place_labels) {
            continue;
        }

        for (uint32_t feature_index = 0; feature_index < layer->feature_count; feature_index++) {
            const struct yetty_ymap_vt_feature *feature = &layer->features[feature_index];
            struct yetty_ycore_void_result feature_res = YETTY_OK_VOID();

            if ((is_ocean || is_water_polygons) && feature->geom_type == YETTY_YMAP_VT_POLYGON) {
                feature_res =
                    emit_polygon_feature(emit, &transform, feature, 0xffaad3dfu, OSM_Z_WATER);
            } else if (is_land && feature->geom_type == YETTY_YMAP_VT_POLYGON) {
                uint32_t fill = land_fill_for_kind(feature->kind);
                if (fill) {
                    feature_res = emit_polygon_feature(emit, &transform, feature, fill, OSM_Z_LAND);
                }
            } else if (is_buildings && feature->geom_type == YETTY_YMAP_VT_POLYGON) {
                feature_res =
                    emit_polygon_feature(emit, &transform, feature, 0xffd9d0c9u, OSM_Z_BUILDINGS);
            } else if (is_water_lines && feature->geom_type == YETTY_YMAP_VT_LINESTRING) {
                const struct osm_line_style *style = water_line_style_for_kind(feature->kind);
                if (style && emit->zoom >= style->min_zoom) {
                    for (uint32_t ring = 0; ring < feature->ring_count; ring++) {
                        feature_res = emit_polyline(emit, &transform, &feature->rings[ring],
                                                    style->color, style->width_px, style->z_band);
                        if (YETTY_IS_ERR(feature_res)) {
                            break;
                        }
                    }
                }
            } else if (is_streets && feature->geom_type == YETTY_YMAP_VT_LINESTRING) {
                const struct osm_line_style *style = street_style_for_kind(feature->kind);
                if (style && emit->zoom >= style->min_zoom) {
                    feature_res = emit_street_feature(emit, &transform, feature, style,
                                                      street_width_zoom_factor(emit->zoom));
                }
            } else if (is_boundaries && feature->geom_type == YETTY_YMAP_VT_LINESTRING) {
                for (uint32_t ring = 0; ring < feature->ring_count; ring++) {
                    feature_res =
                        emit_polyline_dashed(emit, &transform, &feature->rings[ring], 0xff9e7bb5u,
                                             1.3f, OSM_Z_BOUNDARY, 7.0f, 4.0f);
                    if (YETTY_IS_ERR(feature_res)) {
                        break;
                    }
                }
            } else if (is_street_labels && feature->name &&
                       feature->geom_type == YETTY_YMAP_VT_LINESTRING && feature->ring_count > 0) {
                int rank = street_label_rank(feature->kind);
                if (rank >= 0 && (emit->zoom >= 14 || rank <= OSM_STREET_LABEL_MAX_RANK_LOW_ZOOM)) {
                    const struct yetty_ymap_vt_ring *ring = &feature->rings[0];
                    if (ring->point_count > 0) {
                        struct yetty_ymap_vt_point mid = ring->points[ring->point_count / 2];
                        if (mid.x >= 0.0f && mid.x < (float)layer->extent && mid.y >= 0.0f &&
                            mid.y < (float)layer->extent) {
                            struct osm_point anchor = transform_point(&transform, mid);
                            feature_res = push_label_candidate(
                                emit, anchor.x, anchor.y, feature->name, 9.0f, 0.0f, 0xff4a4a4au,
                                OSM_Z_STREET_LABELS, 10 + rank);
                        }
                    }
                }
            } else if (is_place_labels && feature->name &&
                       feature->geom_type == YETTY_YMAP_VT_POINT && feature->ring_count > 0 &&
                       feature->rings[0].point_count > 0) {
                float font_size = place_label_size_for_kind(feature->kind);
                struct yetty_ymap_vt_point anchor_point = feature->rings[0].points[0];
                if (font_size > 0.0f && anchor_point.x >= 0.0f &&
                    anchor_point.x < (float)layer->extent && anchor_point.y >= 0.0f &&
                    anchor_point.y < (float)layer->extent) {
                    struct osm_point anchor = transform_point(&transform, anchor_point);
                    /* Bigger font = more important place = lower priority value.
                     * Letter-spacing scales with importance — the classic
                     * cartographic treatment for settlement names. */
                    float char_spacing = font_size >= 16.0f   ? 2.0f
                                         : font_size >= 13.0f ? 1.2f
                                                              : 0.5f;
                    feature_res = push_label_candidate(
                        emit, anchor.x, anchor.y, feature->name, font_size, char_spacing,
                        0xff30303cu, OSM_Z_PLACE_LABELS, (int)(20.0f - font_size));
                }
            }
            YETTY_RETURN_IF_ERR(yetty_ycore_void, feature_res, "osm vector: feature emit");
        }
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Label placement — collected candidates from every tile compete here.
 * Priority order (places before streets, majors before minors), greedy
 * collision culling against already-placed boxes, and one label per
 * distinct name per render (kills the per-tile duplicates of long
 * streets and the per-feature repeats of split ways). Without this pass
 * a z13 city center draws hundreds of overprinting names — unreadable.
 *===========================================================================*/

static int label_priority_compare(const void *first, const void *second)
{
    const struct osm_label_candidate *a = first;
    const struct osm_label_candidate *b = second;
    if (a->priority != b->priority) {
        return a->priority < b->priority ? -1 : 1;
    }
    /* Stable-ish tiebreak so placement doesn't flicker between renders. */
    if (a->y != b->y) {
        return a->y < b->y ? -1 : 1;
    }
    return a->x < b->x ? -1 : (a->x > b->x ? 1 : 0);
}

struct osm_label_box {
    float min_x;
    float min_y;
    float max_x;
    float max_y;
    const char *text; /* borrowed from the candidate */
};

static struct yetty_ycore_void_result place_labels(struct osm_vector_emit *emit)
{
    if (emit->label_count == 0) {
        return YETTY_OK_VOID();
    }
    qsort(emit->labels, emit->label_count, sizeof(struct osm_label_candidate),
          label_priority_compare);

    struct osm_label_box *placed =
        malloc(OSM_VECTOR_MAX_PLACED_LABELS * sizeof(struct osm_label_box));
    if (!placed) {
        return YETTY_ERR(yetty_ycore_void, "osm vector: placement scratch alloc failed");
    }
    uint32_t placed_count = 0;
    struct yetty_ycore_void_result result = YETTY_OK_VOID();

    for (uint32_t i = 0; i < emit->label_count && placed_count < OSM_VECTOR_MAX_PLACED_LABELS;
         i++) {
        const struct osm_label_candidate *candidate = &emit->labels[i];
        float half_width =
            label_width_estimate(candidate->text, candidate->font_size, candidate->char_spacing) *
            0.5f;
        struct osm_label_box box = {
            .min_x = candidate->x - half_width - OSM_LABEL_COLLISION_PAD_PX,
            .min_y = candidate->y - candidate->font_size - OSM_LABEL_COLLISION_PAD_PX,
            .max_x = candidate->x + half_width + OSM_LABEL_COLLISION_PAD_PX,
            .max_y = candidate->y + OSM_LABEL_COLLISION_PAD_PX,
            .text = candidate->text,
        };
        if (box.max_x < 0.0f || box.min_x > emit->viewport_w || box.max_y < 0.0f ||
            box.min_y > emit->viewport_h) {
            continue;
        }
        int blocked = 0;
        for (uint32_t j = 0; j < placed_count; j++) {
            const struct osm_label_box *other = &placed[j];
            if (strcmp(other->text, candidate->text) == 0) {
                blocked = 1; /* one label per name per render */
                break;
            }
            if (box.min_x < other->max_x && box.max_x > other->min_x && box.min_y < other->max_y &&
                box.max_y > other->min_y) {
                blocked = 1;
                break;
            }
        }
        if (blocked) {
            continue;
        }
        result = emit_text(emit, candidate->x, candidate->y, candidate->text, candidate->font_size,
                           candidate->char_spacing, candidate->color, candidate->z_band);
        if (YETTY_IS_ERR(result)) {
            break;
        }
        placed[placed_count++] = box;
    }
    free(placed);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result, "osm vector: label placement emit");
    ydebug("ymap vector: placed %u/%u labels", placed_count, emit->label_count);
    return YETTY_OK_VOID();
}

static void free_label_candidates(struct osm_vector_emit *emit)
{
    for (uint32_t i = 0; i < emit->label_count; i++) {
        free(emit->labels[i].text);
    }
    free(emit->labels);
    emit->labels = NULL;
    emit->label_count = 0;
    emit->label_capacity = 0;
}

/*=============================================================================
 * Public entry
 *===========================================================================*/

struct yetty_ydraw_drawable_list_result yetty_ymap_render_vector(
    const struct yetty_ymap_config *config)
{
    if (!config) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymap vector: config is NULL");
    }
    if (config->zoom > YETTY_YMAP_MAX_VECTOR_ZOOM) {
        return YETTY_ERR(yetty_ydraw_drawable_list,
                         "ymap vector: zoom exceeds vector maximum (14)");
    }
    if (config->width_px == 0 || config->height_px == 0 ||
        config->width_px > OSM_VECTOR_MAX_VIEWPORT_PX ||
        config->height_px > OSM_VECTOR_MAX_VIEWPORT_PX) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymap vector: viewport size out of range");
    }
    if (config->latitude < -85.06 || config->latitude > 85.06 || config->longitude < -180.0 ||
        config->longitude > 180.0) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymap vector: lat/lon out of range");
    }

    const char *url_template =
        config->tile_url_template ? config->tile_url_template : OSM_VECTOR_DEFAULT_URL;
    char default_cache_root[1024];
    const char *cache_root = config->cache_dir;
    if (!cache_root) {
        snprintf(default_cache_root, sizeof(default_cache_root), "%s/osm-vector-tiles",
                 yetty_yplatform_get_cache_dir());
        cache_root = default_cache_root;
    }

    double center_pixel_x = 0.0;
    double center_pixel_y = 0.0;
    yetty_ymap_lonlat_to_global_pixel(config->longitude, config->latitude, config->zoom,
                                      &center_pixel_x, &center_pixel_y);
    int64_t origin_x = (int64_t)llround(center_pixel_x) - (int64_t)config->width_px / 2;
    int64_t origin_y = (int64_t)llround(center_pixel_y) - (int64_t)config->height_px / 2;

    int64_t tile_count = (int64_t)1 << config->zoom;
    int64_t tile_size = (int64_t)OSM_VECTOR_TILE_SIZE;
    int64_t tile_x_first =
        origin_x >= 0 ? origin_x / tile_size : (origin_x - (tile_size - 1)) / tile_size;
    int64_t tile_y_first =
        origin_y >= 0 ? origin_y / tile_size : (origin_y - (tile_size - 1)) / tile_size;
    int64_t tile_x_last = (origin_x + (int64_t)config->width_px - 1) / tile_size;
    int64_t tile_y_last = (origin_y + (int64_t)config->height_px - 1) / tile_size;
    int64_t tiles_total = (tile_x_last - tile_x_first + 1) * (tile_y_last - tile_y_first + 1);
    if (tiles_total > (int64_t)OSM_VECTOR_MAX_TILES) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymap vector: viewport covers too many tiles");
    }

    struct yetty_ydraw_drawable_list_config list_config = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = (float)config->width_px,
        .scene_max_y = (float)config->height_px,
    };
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(&list_config);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, list_res, "ymap vector: list create");

    struct osm_vector_emit emit = {
        .list = list_res.value,
        .viewport_w = (float)config->width_px,
        .viewport_h = (float)config->height_px,
        .zoom = config->zoom,
        .prim_count = 0,
    };

    /* Land-colored base under everything (osm-carto background). */
    {
        struct yetty_ysdf_box background = {
            .center_x = emit.viewport_w * 0.5f,
            .center_y = emit.viewport_h * 0.5f,
            .half_width = emit.viewport_w * 0.5f,
            .half_height = emit.viewport_h * 0.5f,
            .corner_radius = 0.0f,
        };
        struct yetty_ycore_void_result bg_res = yetty_ydraw_drawable_list_add_cmd_add_box(
            emit.list, /*id=*/0, OSM_Z_BACKGROUND, osm_color_to_wire(0xfff2efe9u), 0, 0.0f,
            &background);
        if (YETTY_IS_ERR(bg_res)) {
            yetty_ydraw_drawable_list_destroy(emit.list);
            return YETTY_ERR(yetty_ydraw_drawable_list, "ymap vector: background", bg_res);
        }
    }

    /* Collect the covering tile set (wrapped duplicates deduped), fetch
     * everything in one parallel batch, then decode + emit per tile. */
    struct osm_emit_slot {
        int64_t tile_x; /* unwrapped — transform position */
        int64_t tile_y;
        uint32_t request_index;
    };
    struct osm_emit_slot *slots = malloc((size_t)tiles_total * sizeof(struct osm_emit_slot));
    struct yetty_ymap_tile_request *requests =
        calloc((size_t)tiles_total, sizeof(struct yetty_ymap_tile_request));
    if (!slots || !requests) {
        free(slots);
        free(requests);
        yetty_ydraw_drawable_list_destroy(emit.list);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymap vector: tile set alloc failed");
    }
    uint32_t slot_count = 0;
    uint32_t request_count = 0;
    for (int64_t tile_y = tile_y_first; tile_y <= tile_y_last; tile_y++) {
        if (tile_y < 0 || tile_y >= tile_count) {
            continue;
        }
        for (int64_t tile_x = tile_x_first; tile_x <= tile_x_last; tile_x++) {
            uint32_t wrapped_x = (uint32_t)(((tile_x % tile_count) + tile_count) % tile_count);
            uint32_t request_index = request_count;
            for (uint32_t i = 0; i < request_count; i++) {
                if (requests[i].tile_x == wrapped_x && requests[i].tile_y == (uint32_t)tile_y) {
                    request_index = i;
                    break;
                }
            }
            if (request_index == request_count) {
                requests[request_count].tile_x = wrapped_x;
                requests[request_count].tile_y = (uint32_t)tile_y;
                request_count++;
            }
            slots[slot_count++] = (struct osm_emit_slot){
                .tile_x = tile_x, .tile_y = tile_y, .request_index = request_index};
        }
    }

    const char *tile_extension = config->tile_file_extension ? config->tile_file_extension : "mvt";
    struct yetty_ycore_void_result fetch_res = yetty_ymap_tiles_fetch(
        url_template, cache_root, tile_extension, config->zoom, requests, request_count);
    if (YETTY_IS_ERR(fetch_res)) {
        free(slots);
        free(requests);
        yetty_ydraw_drawable_list_destroy(emit.list);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymap vector: batch fetch", fetch_res);
    }

    uint32_t tiles_rendered = 0;
    struct yetty_ycore_void_result emit_result = YETTY_OK_VOID();
    for (uint32_t i = 0; i < slot_count && YETTY_IS_OK(emit_result); i++) {
        const struct osm_emit_slot *slot = &slots[i];
        const struct yetty_ymap_tile_request *request = &requests[slot->request_index];
        if (!request->bytes) {
            continue; /* best-effort — hole stays background-colored */
        }
        struct yetty_ymap_vt_tile tile = {0};
        struct yetty_ycore_void_result parse_res =
            yetty_ymap_vt_parse(request->bytes, request->len, &tile);
        if (YETTY_IS_ERR(parse_res)) {
            ywarn("ymap vector: tile %u/%u/%lld parse failed: %s", config->zoom, request->tile_x,
                  (long long)slot->tile_y, parse_res.error.msg);
            yetty_ycore_error_destroy(parse_res.error);
            continue;
        }
        struct osm_tile_transform transform = {
            .offset_x = (float)(slot->tile_x * tile_size - origin_x),
            .offset_y = (float)(slot->tile_y * tile_size - origin_y),
            .scale = 1.0f,  /* per-layer: 256 / extent */
            .extent = 4096, /* per-layer override */
        };
        emit_result = emit_tile(&emit, &tile, &transform);
        yetty_ymap_vt_free(&tile);
        if (YETTY_IS_OK(emit_result)) {
            tiles_rendered++;
        }
    }
    for (uint32_t i = 0; i < request_count; i++) {
        free(requests[i].bytes);
    }
    free(requests);
    free(slots);

    if (YETTY_IS_OK(emit_result) && tiles_rendered > 0) {
        emit_result = place_labels(&emit);
    }
    free_label_candidates(&emit);

    if (YETTY_IS_ERR(emit_result)) {
        yetty_ydraw_drawable_list_destroy(emit.list);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymap vector: emit", emit_result);
    }
    if (tiles_rendered == 0) {
        yetty_ydraw_drawable_list_destroy(emit.list);
        return YETTY_ERR(yetty_ydraw_drawable_list,
                         "ymap vector: no tile could be fetched — offline and cold cache?");
    }
    if (!emit_budget_left(&emit)) {
        ywarn("ymap vector: prim budget (%u) exhausted — map truncated", OSM_VECTOR_MAX_PRIMS);
    }
    ydebug("ymap vector: %u tiles, %u prims at z%u", tiles_rendered, emit.prim_count, config->zoom);
    return YETTY_OK(yetty_ydraw_drawable_list, emit.list);
}
