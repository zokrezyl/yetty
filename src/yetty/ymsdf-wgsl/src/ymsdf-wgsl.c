/*
 * ymsdf-wgsl.c - GPU MSDF CDB generator.
 *
 * Mirrors yetty-poc/src/yetty/msdf-wgsl/src/msdf-wgsl.cpp but ported to C.
 * Pipeline:
 *   FreeType outline → metadata + points GPU buffers
 *     → one compute dispatch over the whole atlas (a tile → glyph table
 *       tells each 8×8 workgroup which glyph it renders)
 *     → RGBA8 storage texture → readback → CDB writer.
 *
 * Atlas is shelf-packed at 8192px wide. The texture grows in height when
 * the cursor walks off the bottom; old contents are copied across via
 * texture-to-texture copy.
 *
 * Async waits register their callbacks in WaitAnyOnly mode and block in
 * wgpuInstanceWaitAny until the future completes — no polling, no sleep.
 * This blocks the calling thread but keeps the API simple — the CDB
 * generator is invoked once per font on cache miss, which is fine to
 * block on. The caller's WGPUInstance must be created with the
 * WGPUInstanceFeatureName_TimedWaitAny feature.
 */

#include <yetty/ymsdf-wgsl/ymsdf-wgsl.h>
#include <yetty/ycdb/ycdb.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yplatform/time.h>

#include <webgpu/webgpu.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#define FT_SCALE 64.0f

/*=============================================================================
 * Tiny dynamic vectors. The decompose callbacks need to grow per-glyph
 * metadata/points, and we don't want to pull libstd_vector into C. These are
 * deliberately minimal — no shrink, no copy, freed at end of glyph.
 *===========================================================================*/

struct u32_vec {
    uint32_t *data;
    size_t size;
    size_t cap;
};

struct f32_vec {
    float *data;
    size_t size;
    size_t cap;
};

struct u8_vec {
    uint8_t *data;
    size_t size;
    size_t cap;
};

static int u32_vec_push(struct u32_vec *v, uint32_t x)
{
    if (v->size == v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 16;
        uint32_t *nd = realloc(v->data, nc * sizeof(uint32_t));
        if (!nd) {
            return -1;
        }
        v->data = nd;
        v->cap = nc;
    }
    v->data[v->size++] = x;
    return 0;
}

static int f32_vec_push(struct f32_vec *v, float x)
{
    if (v->size == v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 16;
        float *nd = realloc(v->data, nc * sizeof(float));
        if (!nd) {
            return -1;
        }
        v->data = nd;
        v->cap = nc;
    }
    v->data[v->size++] = x;
    return 0;
}

static int u8_vec_reserve(struct u8_vec *v, size_t n)
{
    if (v->cap >= n) {
        return 0;
    }
    uint8_t *nd = realloc(v->data, n);
    if (!nd) {
        return -1;
    }
    v->data = nd;
    v->cap = n;
    return 0;
}

static void u32_vec_free(struct u32_vec *v)
{
    free(v->data);
    v->data = NULL;
    v->size = v->cap = 0;
}
static void f32_vec_free(struct f32_vec *v)
{
    free(v->data);
    v->data = NULL;
    v->size = v->cap = 0;
}
static void u8_vec_free(struct u8_vec *v)
{
    free(v->data);
    v->data = NULL;
    v->size = v->cap = 0;
}

/*=============================================================================
 * FreeType outline → packed buffers (mirrors poc serializer namespace).
 *
 * Metadata layout per glyph:
 *   [0]                         contour_count
 *   for each contour:
 *     [+0] winding   (0 = CW, 2 = CCW)
 *     [+1] segment_count
 *     for each segment:
 *       [+0] color   (always WHITE for now — pure SDF mode)
 *       [+1] npoints (2 = line, 3 = quadratic)
 *
 * Points layout: tightly packed (x, y, x, y, ...) floats. The FIRST point of
 * a contour is followed by (npoints-1) points per segment, sharing endpoints
 * with the next segment's first.
 *===========================================================================*/

enum {
    COLOR_BLACK = 0,
    COLOR_RED = 1,
    COLOR_GREEN = 2,
    COLOR_BLUE = 4,
    COLOR_YELLOW = 3,  /* RED|GREEN */
    COLOR_MAGENTA = 5, /* RED|BLUE */
    COLOR_CYAN = 6,    /* GREEN|BLUE */
    COLOR_WHITE = 7,
};

struct glyph_ctx {
    struct u32_vec metadata;
    struct f32_vec points;
    int current_contour_index; /* -1 until first moveTo */
    float last_x, last_y;
    int oom; /* sticky out-of-memory flag */
};

static void glyph_ctx_close_contour(struct glyph_ctx *ctx);

static int decompose_move_to(const FT_Vector *to, void *user)
{
    struct glyph_ctx *ctx = user;

    glyph_ctx_close_contour(ctx);

    ctx->current_contour_index++;
    ctx->metadata.data[0]++;                   /* contour count */
    if (u32_vec_push(&ctx->metadata, 1) < 0 || /* winding placeholder */
        u32_vec_push(&ctx->metadata, 0) < 0) { /* segment count */
        ctx->oom = 1;
        return 0;
    }

    float x = to->x / FT_SCALE;
    float y = to->y / FT_SCALE;
    if (f32_vec_push(&ctx->points, x) < 0 || f32_vec_push(&ctx->points, y) < 0) {
        ctx->oom = 1;
        return 0;
    }
    ctx->last_x = x;
    ctx->last_y = y;
    return 0;
}

/* Find the index of the current contour's segment_count cell in metadata. */
static size_t current_seg_count_idx(struct glyph_ctx *ctx)
{
    size_t idx = 1;
    for (int i = 0; i < ctx->current_contour_index; i++) {
        size_t nseg = ctx->metadata.data[idx + 1];
        idx += 2 + nseg * 2;
    }
    return idx + 1;
}

static int decompose_line_to(const FT_Vector *to, void *user)
{
    struct glyph_ctx *ctx = user;
    if (ctx->current_contour_index < 0) {
        return 0;
    }

    ctx->metadata.data[current_seg_count_idx(ctx)]++;
    if (u32_vec_push(&ctx->metadata, COLOR_WHITE) < 0 || u32_vec_push(&ctx->metadata, 2) < 0) {
        ctx->oom = 1;
        return 0;
    }

    float x = to->x / FT_SCALE;
    float y = to->y / FT_SCALE;
    if (f32_vec_push(&ctx->points, x) < 0 || f32_vec_push(&ctx->points, y) < 0) {
        ctx->oom = 1;
        return 0;
    }
    ctx->last_x = x;
    ctx->last_y = y;
    return 0;
}

static int decompose_conic_to(const FT_Vector *ctrl, const FT_Vector *to, void *user)
{
    struct glyph_ctx *ctx = user;
    if (ctx->current_contour_index < 0) {
        return 0;
    }

    ctx->metadata.data[current_seg_count_idx(ctx)]++;
    if (u32_vec_push(&ctx->metadata, COLOR_WHITE) < 0 || u32_vec_push(&ctx->metadata, 3) < 0) {
        ctx->oom = 1;
        return 0;
    }

    if (f32_vec_push(&ctx->points, ctrl->x / FT_SCALE) < 0 ||
        f32_vec_push(&ctx->points, ctrl->y / FT_SCALE) < 0 ||
        f32_vec_push(&ctx->points, to->x / FT_SCALE) < 0 ||
        f32_vec_push(&ctx->points, to->y / FT_SCALE) < 0) {
        ctx->oom = 1;
        return 0;
    }
    ctx->last_x = to->x / FT_SCALE;
    ctx->last_y = to->y / FT_SCALE;
    return 0;
}

static int decompose_cubic_to(const FT_Vector *ctrl1, const FT_Vector *ctrl2, const FT_Vector *to,
                              void *user)
{
    /* TTF cubics aren't supported by the shader; degrade to a single conic
     * using ctrl1 as the control point. Same compromise as the poc. */
    (void)ctrl2;
    return decompose_conic_to(ctrl1, to, user);
}

/* Locate the offset of the current contour's first point in `points`. */
static size_t current_contour_point_offset(struct glyph_ctx *ctx)
{
    size_t off = 0;
    size_t midx = 1;
    for (int i = 0; i < ctx->current_contour_index; i++) {
        size_t nseg = ctx->metadata.data[midx + 1];
        midx += 2;
        for (size_t j = 0; j < nseg; j++) {
            size_t np = ctx->metadata.data[midx + 1];
            off += (np - 1) * 2;
            midx += 2;
        }
        off += 2;
    }
    return off;
}

/* Append a closing line segment if the contour didn't end at its start. */
static void glyph_ctx_close_contour(struct glyph_ctx *ctx)
{
    if (ctx->current_contour_index < 0) {
        return;
    }
    size_t start = current_contour_point_offset(ctx);
    if (start + 1 >= ctx->points.size) {
        return;
    }
    float sx = ctx->points.data[start];
    float sy = ctx->points.data[start + 1];

    if (fabsf(ctx->last_x - sx) <= 1e-6f && fabsf(ctx->last_y - sy) <= 1e-6f) {
        return;
    }
    /* Increment seg count for current contour, append closing line. */
    ctx->metadata.data[current_seg_count_idx(ctx)]++;
    if (u32_vec_push(&ctx->metadata, COLOR_WHITE) < 0 || u32_vec_push(&ctx->metadata, 2) < 0 ||
        f32_vec_push(&ctx->points, sx) < 0 || f32_vec_push(&ctx->points, sy) < 0) {
        ctx->oom = 1;
        return;
    }
    ctx->last_x = sx;
    ctx->last_y = sy;
}

/* Shoelace winding for a closed polygon over the points array. Returns
 * positive for CCW, negative for CW. */
static float compute_winding_signed(const float *points, size_t start_off, size_t point_count)
{
    float total = 0;
    for (size_t i = 0; i < point_count * 2; i += 2) {
        size_t j = (i + 2) % (point_count * 2);
        float x0 = points[start_off + i];
        float y0 = points[start_off + i + 1];
        float x1 = points[start_off + j];
        float y1 = points[start_off + j + 1];
        total += (x1 - x0) * (y1 + y0);
    }
    return total;
}

static void glyph_ctx_compute_windings(struct glyph_ctx *ctx)
{
    size_t point_idx = 0;
    size_t midx = 1;
    uint32_t ncontours = ctx->metadata.data[0];

    for (uint32_t i = 0; i < ncontours; i++) {
        size_t contour_start = point_idx;
        size_t winding_idx = midx++;
        uint32_t nseg = ctx->metadata.data[midx++];

        size_t cpoints = 0;
        for (uint32_t j = 0; j < nseg; j++) {
            midx++; /* skip color */
            uint32_t np = ctx->metadata.data[midx++];
            cpoints += (np - 1);
        }
        cpoints++; /* closing point shared with start */

        if (contour_start * 2 + cpoints * 2 > ctx->points.size) {
            return;
        }
        float w = compute_winding_signed(ctx->points.data, contour_start * 2, cpoints);
        ctx->metadata.data[winding_idx] = w > 0 ? 2 : 0;
        point_idx += cpoints;
    }
}

/*=============================================================================
 * Edge coloring — port of msdfgen's edgeColoringSimple.
 *
 * Without this, every segment is COLOR_WHITE, every channel sees every
 * segment, and median3 produces a single-channel SDF (corners come out
 * rounded, plus sign discontinuities at sharp corners leak across the
 * whole field). Proper MSDF assigns each segment one of YELLOW/MAGENTA/
 * CYAN such that adjacent runs across a corner share exactly one channel
 * — at the corner that shared channel stays "on" and median3 picks it.
 *===========================================================================*/

static void normalize_dir(float *vx, float *vy)
{
    float d = sqrtf((*vx) * (*vx) + (*vy) * (*vy));
    if (d > 1e-12f) {
        *vx /= d;
        *vy /= d;
    }
}

static int is_corner(float ax, float ay, float bx, float by, float cross_threshold)
{
    /* msdfgen: corner iff dot(a,b) <= 0 OR |cross(a,b)| > sin(threshold). */
    float dotp = ax * bx + ay * by;
    if (dotp <= 0.0f) {
        return 1;
    }
    float crossp = ax * by - ay * bx;
    if (crossp < 0.0f) {
        crossp = -crossp;
    }
    return crossp > cross_threshold ? 1 : 0;
}

static uint32_t advance_color(uint32_t cur, uint32_t banned)
{
    /* msdfgen's switchColor reduced to its YELLOW/MAGENTA/CYAN cycle: pick the
     * next of the three non-WHITE 2-channel colors that isn't `cur` and isn't
     * `banned` (banned!=0 only for the wrap-around between the last and first
     * run, to keep the loop alternating). */
    static const uint32_t cycle[3] = {COLOR_YELLOW, COLOR_MAGENTA, COLOR_CYAN};
    int idx_cur = 0;
    for (int i = 0; i < 3; i++) {
        if (cycle[i] == cur) {
            idx_cur = i;
            break;
        }
    }
    uint32_t next1 = cycle[(idx_cur + 1) % 3];
    uint32_t next2 = cycle[(idx_cur + 2) % 3];
    if (banned != 0 && next1 == banned) {
        return next2;
    }
    return next1;
}

/* Walks the metadata array contour-by-contour, identifies corners using each
 * segment's begin/end tangent direction, then writes the proper YELLOW/
 * MAGENTA/CYAN color into each segment's color slot. Mirrors the structure of
 * glyph_ctx_compute_windings so the metadata indexing stays in sync. */
static void apply_edge_coloring(struct glyph_ctx *ctx)
{
    /* msdfgen's default: angleThreshold = 3.0 rad ⇒ crossThreshold = sin(3.0). */
    const float cross_threshold = 0.14112f; /* sinf(3.0f) */

    size_t midx = 1; /* skip contour count */
    size_t point_idx = 0;
    uint32_t ncontours = ctx->metadata.data[0];

    /* Scratch buffer reused across contours for begin/end tangent dirs. */
    struct seg_dir {
        float bx, by;     /* begin direction (normalized) */
        float ex, ey;     /* end direction (normalized) */
        size_t color_idx; /* metadata slot to write the assigned color into */
    };
    struct seg_dir *segs = NULL;
    size_t segs_cap = 0;

    for (uint32_t ci = 0; ci < ncontours; ci++) {
        midx++; /* skip winding */
        uint32_t nseg = ctx->metadata.data[midx++];
        if (nseg == 0) {
            point_idx++; /* the lone moveTo point */
            continue;
        }
        if (nseg > segs_cap) {
            struct seg_dir *nb = realloc(segs, nseg * sizeof(*nb));
            if (!nb) {
                free(segs);
                return; /* leave previous WHITE coloring on OOM */
            }
            segs = nb;
            segs_cap = nseg;
        }

        size_t seg_meta = midx;
        size_t seg_pt = point_idx;
        for (uint32_t s = 0; s < nseg; s++) {
            uint32_t color_slot = (uint32_t)midx;
            uint32_t color = ctx->metadata.data[midx++];
            (void)color;
            uint32_t np = ctx->metadata.data[midx++];

            float p0x = ctx->points.data[point_idx * 2 + 0];
            float p0y = ctx->points.data[point_idx * 2 + 1];
            if (np == 2u) {
                float p1x = ctx->points.data[(point_idx + 1) * 2 + 0];
                float p1y = ctx->points.data[(point_idx + 1) * 2 + 1];
                float dx = p1x - p0x;
                float dy = p1y - p0y;
                normalize_dir(&dx, &dy);
                segs[s].bx = dx;
                segs[s].by = dy;
                segs[s].ex = dx;
                segs[s].ey = dy;
            } else {
                /* Quadratic — begin tangent = p1-p0, end tangent = p2-p1.
                 * Falls back to the chord when a control point coincides
                 * with an endpoint. */
                float p1x = ctx->points.data[(point_idx + 1) * 2 + 0];
                float p1y = ctx->points.data[(point_idx + 1) * 2 + 1];
                float p2x = ctx->points.data[(point_idx + 2) * 2 + 0];
                float p2y = ctx->points.data[(point_idx + 2) * 2 + 1];
                float bdx = p1x - p0x;
                float bdy = p1y - p0y;
                if (bdx * bdx + bdy * bdy < 1e-20f) {
                    bdx = p2x - p0x;
                    bdy = p2y - p0y;
                }
                float edx = p2x - p1x;
                float edy = p2y - p1y;
                if (edx * edx + edy * edy < 1e-20f) {
                    edx = p2x - p0x;
                    edy = p2y - p0y;
                }
                normalize_dir(&bdx, &bdy);
                normalize_dir(&edx, &edy);
                segs[s].bx = bdx;
                segs[s].by = bdy;
                segs[s].ex = edx;
                segs[s].ey = edy;
            }
            segs[s].color_idx = color_slot;
            point_idx += np - 1;
        }

        /* Identify corners: index i is a corner when the previous segment's
         * end tangent is non-collinear with seg[i]'s begin tangent. */
        int corner_count = 0;
        int corners[256];
        if (nseg <= sizeof(corners) / sizeof(corners[0])) {
            for (uint32_t s = 0; s < nseg; s++) {
                uint32_t prev = (s == 0u) ? (nseg - 1u) : (s - 1u);
                if (is_corner(segs[prev].ex, segs[prev].ey, segs[s].bx, segs[s].by,
                              cross_threshold)) {
                    corners[corner_count++] = (int)s;
                }
            }
        } else {
            /* Pathological glyph (>256 segments per contour). Fall back to
             * everything-WHITE — better than reading off the stack. */
            corner_count = 0;
        }

        if (corner_count == 0) {
            /* Smooth contour — single channel pattern produces a regular SDF
             * but that's the right behaviour for a teardrop/circle. */
            for (uint32_t s = 0; s < nseg; s++) {
                ctx->metadata.data[segs[s].color_idx] = COLOR_WHITE;
            }
        } else if (corner_count == 1) {
            /* Teardrop case from msdfgen — split the contour into 3 runs at
             * the single corner, alternating colors, with a WHITE middle run
             * when there are enough segments to give it dedicated room. */
            uint32_t colors[3] = {COLOR_MAGENTA, COLOR_WHITE, COLOR_YELLOW};
            int corner = corners[0];
            for (uint32_t s = 0; s < nseg; s++) {
                int rel = ((int)s - corner + (int)nseg) % (int)nseg;
                int phase;
                if (nseg >= 3u) {
                    phase = (rel * 3 + (int)nseg / 2) / (int)nseg;
                } else {
                    phase = rel; /* nseg=1 or 2 — no middle run */
                }
                if (phase < 0) {
                    phase = 0;
                }
                if (phase > 2) {
                    phase = 2;
                }
                ctx->metadata.data[segs[s].color_idx] = colors[phase];
            }
        } else {
            /* ≥2 corners — cycle YELLOW/MAGENTA/CYAN, switching at each corner.
             * The last switch bans the initial color so the run wrapping back
             * around to the start of the loop differs from the first run. */
            uint32_t color = COLOR_YELLOW;
            uint32_t initial_color = color;
            int spline = 0;
            int start = corners[0];
            int m = (int)nseg;
            for (int i = 0; i < m; i++) {
                int index = (start + i) % m;
                if (spline + 1 < corner_count && corners[spline + 1] == index) {
                    spline++;
                    uint32_t banned = (spline == corner_count - 1) ? initial_color : 0;
                    color = advance_color(color, banned);
                }
                ctx->metadata.data[segs[index].color_idx] = color;
            }
        }

        (void)seg_meta;
        (void)seg_pt;
        point_idx++; /* closing point shared with start */
    }

    free(segs);
}

/* Returns 0 on success (and leaves ctx populated), 1 if the glyph is empty
 * (no contours or no points), -1 on a real error. Caller frees ctx. */
static int serialize_glyph(FT_Face face, uint32_t codepoint, struct glyph_ctx *ctx)
{
    if (FT_Load_Char(face, codepoint, FT_LOAD_NO_SCALE) != 0) {
        return -1;
    }

    if (u32_vec_push(&ctx->metadata, 0) < 0) { /* contour count */
        return -1;
    }
    ctx->current_contour_index = -1;

    FT_Outline_Funcs funcs = {
        .move_to = decompose_move_to,
        .line_to = decompose_line_to,
        .conic_to = decompose_conic_to,
        .cubic_to = decompose_cubic_to,
        .shift = 0,
        .delta = 0,
    };
    if (FT_Outline_Decompose(&face->glyph->outline, &funcs, ctx) != 0) {
        return -1;
    }
    if (ctx->oom) {
        return -1;
    }
    glyph_ctx_close_contour(ctx);

    if (ctx->metadata.data[0] == 0 || ctx->points.size == 0) {
        return 1;
    }
    glyph_ctx_compute_windings(ctx);
    apply_edge_coloring(ctx);
    return 0;
}

struct bounds {
    float min_x, min_y, max_x, max_y;
    int empty;
};

static struct bounds get_glyph_bounds(FT_Face face, uint32_t codepoint)
{
    struct bounds b = {0, 0, 0, 0, 1};
    if (FT_Load_Char(face, codepoint, FT_LOAD_NO_SCALE) != 0) {
        return b;
    }
    FT_BBox bbox;
    FT_Outline_Get_CBox(&face->glyph->outline, &bbox);
    if (bbox.xMin == bbox.xMax || bbox.yMin == bbox.yMax) {
        return b;
    }
    b.min_x = bbox.xMin / FT_SCALE;
    b.min_y = bbox.yMin / FT_SCALE;
    b.max_x = bbox.xMax / FT_SCALE;
    b.max_y = bbox.yMax / FT_SCALE;
    b.empty = 0;
    return b;
}

/*=============================================================================
 * Atlas — shelf packing on the CPU (prepare stage) and, once the layout is
 * final, one RGBA8Unorm storage texture of exactly that size (render stage).
 * Glyphs sit on the 8-pixel tile grid so an 8×8 compute tile never straddles
 * two of them — the one-dispatch tile table relies on that.
 *===========================================================================*/

enum {
    ATLAS_TILE = 8,     /* compute workgroup tile edge; glyph placement grid */
    ATLAS_WIDTH = 8192, /* fixed width; the height follows the font */
};

/* Round `n` up to the tile grid. */
static int align_to_tile(int n)
{
    return (n + ATLAS_TILE - 1) / ATLAS_TILE * ATLAS_TILE;
}

/* CPU-side shelf packer. No GPU objects — the texture is created from the
 * finished layout in the render stage. */
struct atlas_layout {
    int width;
    int height;
    int row_height;
    int cursor_x;
    int cursor_y;
    size_t glyph_count;
};

static void atlas_layout_init(struct atlas_layout *a, int width)
{
    memset(a, 0, sizeof(*a));
    a->width = width;
}

/* Every glyph starts on an 8-pixel boundary in both axes and the next one
 * starts a whole tile after it ends. The +1 keeps the one-pixel gap between
 * neighbours the shelf packer always had. */
static int atlas_layout_allocate(struct atlas_layout *a, int w, int h, int *out_x, int *out_y)
{
    int stride_w = align_to_tile(w + 1);
    if (stride_w > a->width) {
        return -1;
    }
    if (a->cursor_x + stride_w > a->width) {
        a->cursor_x = 0;
        a->cursor_y += align_to_tile(a->row_height + 1);
        a->row_height = 0;
    }
    if (a->cursor_y + h + 1 > a->height) {
        a->height = a->cursor_y + h + 1;
    }
    *out_x = a->cursor_x;
    *out_y = a->cursor_y;
    a->cursor_x += stride_w;
    if (h > a->row_height) {
        a->row_height = h;
    }
    a->glyph_count++;
    return 0;
}

/* Close the last shelf and snap the height to the tile grid. */
static void atlas_layout_finish(struct atlas_layout *a)
{
    a->height = align_to_tile(a->height);
}

/* The storage texture for a finished layout. */
struct atlas_texture {
    WGPUTexture texture;
    WGPUTextureView view;
};

static void atlas_texture_cleanup(struct atlas_texture *t)
{
    if (t->view) {
        wgpuTextureViewRelease(t->view);
        t->view = NULL;
    }
    if (t->texture) {
        wgpuTextureDestroy(t->texture);
        wgpuTextureRelease(t->texture);
        t->texture = NULL;
    }
}

static int atlas_texture_create(struct atlas_texture *t, WGPUDevice device,
                                const struct atlas_layout *layout)
{
    memset(t, 0, sizeof(*t));
    WGPUTextureDescriptor desc = {0};
    desc.dimension = WGPUTextureDimension_2D;
    desc.size = (WGPUExtent3D){(uint32_t)layout->width, (uint32_t)layout->height, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc;
    t->texture = wgpuDeviceCreateTexture(device, &desc);
    if (!t->texture) {
        return -1;
    }
    WGPUTextureViewDescriptor vdesc = {0};
    vdesc.format = WGPUTextureFormat_RGBA8Unorm;
    vdesc.dimension = WGPUTextureViewDimension_2D;
    vdesc.mipLevelCount = 1;
    vdesc.arrayLayerCount = 1;
    t->view = wgpuTextureCreateView(t->texture, &vdesc);
    if (!t->view) {
        atlas_texture_cleanup(t);
        return -1;
    }
    return 0;
}

/*=============================================================================
 * Shader path discovery — same fallback chain as poc.
 *===========================================================================*/

static int file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

static char *read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[sz] = '\0';
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

static void exe_dir(char *out, size_t out_size)
{
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)out_size);
    if (n == 0 || n >= out_size) {
        snprintf(out, out_size, ".");
        return;
    }
    out[n] = '\0';
    char *slash = strrchr(out, '\\');
    if (!slash) {
        slash = strrchr(out, '/');
    }
    if (slash) {
        *slash = '\0';
    } else {
        snprintf(out, out_size, ".");
    }
#else
    ssize_t n = readlink("/proc/self/exe", out, out_size - 1);
    if (n <= 0) {
        snprintf(out, out_size, ".");
        return;
    }
    out[n] = '\0';
    char *slash = strrchr(out, '/');
    if (slash) {
        *slash = '\0';
    } else {
        snprintf(out, out_size, ".");
    }
#endif
}

/* Returns malloc'd path to a readable shader, or NULL. */
static char *find_shader_path(const char *user_path)
{
    if (user_path && file_exists(user_path)) {
        return strdup(user_path);
    }
    char ed[4096];
    exe_dir(ed, sizeof(ed));

    char p[8192];
    snprintf(p, sizeof(p), "%s/msdf_gen.wgsl", ed);
    if (file_exists(p)) {
        return strdup(p);
    }
    snprintf(p, sizeof(p), "%s/shaders/msdf_gen.wgsl", ed);
    if (file_exists(p)) {
        return strdup(p);
    }
    if (file_exists("shaders/msdf_gen.wgsl")) {
        return strdup("shaders/msdf_gen.wgsl");
    }
    return NULL;
}

/*=============================================================================
 * Async wait helper — register the callback in WaitAnyOnly mode, then
 * block in wgpuInstanceWaitAny until the future completes. The callback
 * fires on this thread from inside the wait call, so no atomics and no
 * polling are needed. Requires an instance created with
 * WGPUInstanceFeatureName_TimedWaitAny (non-zero timeouts fail otherwise).
 *===========================================================================*/

static void map_done_callback(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1,
                              void *userdata2)
{
    (void)message;
    (void)userdata2;
    *(WGPUMapAsyncStatus *)userdata1 = status;
}

static int wait_buffer_map(WGPUBuffer buffer, WGPUMapMode mode, size_t size, WGPUInstance instance)
{
    WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
    WGPUBufferMapCallbackInfo callback_info = {0};
    callback_info.mode = WGPUCallbackMode_WaitAnyOnly;
    callback_info.callback = map_done_callback;
    callback_info.userdata1 = &status;

    WGPUFutureWaitInfo wait_info = {0};
    wait_info.future = wgpuBufferMapAsync(buffer, mode, 0, size, callback_info);

    WGPUWaitStatus wait_status = wgpuInstanceWaitAny(instance, 1, &wait_info, UINT64_MAX);
    if (wait_status != WGPUWaitStatus_Success) {
        ywarn("ymsdf-wgsl: WaitAny for buffer map failed (%d)", (int)wait_status);
        return -1;
    }
    return status == WGPUMapAsyncStatus_Success ? 0 : -1;
}

/*=============================================================================
 * Compute pipeline — created once per generate() call and torn down at the
 * end. The shader source is read from disk; the bind group layout is fixed:
 *   binding 0: storage<read> per-glyph parameters, one entry per glyph
 *   binding 1: storage<read> metadata
 *   binding 2: storage<read> points
 *   binding 3: storage texture write-only RGBA8Unorm (the atlas)
 *   binding 4: storage<read> tile → glyph table (one u32 per 8×8 tile)
 *===========================================================================*/

struct compute {
    WGPUDevice device;
    WGPUInstance instance;
    WGPUShaderModule module;
    WGPUBindGroupLayout bgl;
    WGPUComputePipeline pipeline;
};

static int compute_init(struct compute *c, WGPUDevice device, WGPUInstance instance,
                        const char *shader_path)
{
    memset(c, 0, sizeof(*c));
    c->device = device;
    c->instance = instance;

    char *path = find_shader_path(shader_path);
    if (!path) {
        yerror("ymsdf-wgsl: msdf_gen.wgsl not found (tried user path, exe dir, "
               "exe dir/shaders, ./shaders)");
        return -1;
    }
    size_t shader_len = 0;
    char *shader_src = read_file(path, &shader_len);
    if (!shader_src) {
        yerror("ymsdf-wgsl: read shader '%s' failed", path);
        free(path);
        return -1;
    }
    ydebug("ymsdf-wgsl: shader '%s' (%zu bytes)", path, shader_len);
    free(path);

    WGPUShaderSourceWGSL wgsl = {0};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code.data = shader_src;
    wgsl.code.length = shader_len;
    WGPUShaderModuleDescriptor mdesc = {0};
    mdesc.nextInChain = &wgsl.chain;
    c->module = wgpuDeviceCreateShaderModule(device, &mdesc);
    free(shader_src);
    if (!c->module) {
        yerror("ymsdf-wgsl: shader module creation failed");
        return -1;
    }

    WGPUBindGroupLayoutEntry e[5] = {0};
    e[0].binding = 0;
    e[0].visibility = WGPUShaderStage_Compute;
    e[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    e[1].binding = 1;
    e[1].visibility = WGPUShaderStage_Compute;
    e[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    e[2].binding = 2;
    e[2].visibility = WGPUShaderStage_Compute;
    e[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    e[3].binding = 3;
    e[3].visibility = WGPUShaderStage_Compute;
    e[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    e[3].storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
    e[3].storageTexture.viewDimension = WGPUTextureViewDimension_2D;
    e[4].binding = 4;
    e[4].visibility = WGPUShaderStage_Compute;
    e[4].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

    WGPUBindGroupLayoutDescriptor ldesc = {0};
    ldesc.entryCount = 5;
    ldesc.entries = e;
    c->bgl = wgpuDeviceCreateBindGroupLayout(device, &ldesc);
    if (!c->bgl) {
        yerror("ymsdf-wgsl: bind group layout failed");
        return -1;
    }

    WGPUPipelineLayoutDescriptor pldesc = {0};
    pldesc.bindGroupLayoutCount = 1;
    pldesc.bindGroupLayouts = &c->bgl;
    WGPUPipelineLayout plo = wgpuDeviceCreatePipelineLayout(device, &pldesc);
    if (!plo) {
        yerror("ymsdf-wgsl: pipeline layout failed");
        return -1;
    }

    WGPUComputePipelineDescriptor pdesc = {0};
    pdesc.layout = plo;
    pdesc.compute.module = c->module;
    static const char entry[] = "main";
    pdesc.compute.entryPoint.data = entry;
    pdesc.compute.entryPoint.length = sizeof(entry) - 1;
    c->pipeline = wgpuDeviceCreateComputePipeline(device, &pdesc);
    wgpuPipelineLayoutRelease(plo);
    if (!c->pipeline) {
        yerror("ymsdf-wgsl: compute pipeline creation failed");
        return -1;
    }
    return 0;
}

static void compute_cleanup(struct compute *c)
{
    if (c->pipeline) {
        wgpuComputePipelineRelease(c->pipeline);
        c->pipeline = NULL;
    }
    if (c->bgl) {
        wgpuBindGroupLayoutRelease(c->bgl);
        c->bgl = NULL;
    }
    if (c->module) {
        wgpuShaderModuleRelease(c->module);
        c->module = NULL;
    }
}

/*=============================================================================
 * Per-glyph dispatch + atlas readback + CDB write.
 *===========================================================================*/

/* Mirrors the WGSL uniform struct in msdf_gen.wgsl. Order/size matters. */
struct glyph_uniforms {
    uint32_t atlas_offset[2];
    uint32_t glyph_size[2];
    float translate[2];
    float scale;
    float range;
    uint32_t meta_offset;
    uint32_t point_offset;
    float glyph_height;
    uint32_t _padding;
};

/* Per-glyph data assembled during dispatch and serialised to the CDB.
 * Pixel-space metrics are stored as floats to preserve sub-pixel precision —
 * downstream (msdf-font.c → expand_text_drawable_list_to_glyphs) reads them straight
 * out of the CDB header and any truncation here translates 1:1 to wrong
 * glyph layout on screen. */
struct emit_glyph {
    uint32_t codepoint;
    uint32_t atlas_x, atlas_y; /* position inside this font's atlas (px) */
    uint32_t atlas_w, atlas_h; /* glyph bitmap dimensions inside atlas (px) */
    /* Pixel-space metrics at config->font_size — what the CDB consumer
     * expects. Computed once from FreeType's font-unit values. */
    float bearing_x_px;
    float bearing_y_px;
    float size_x_px;
    float size_y_px;
    float advance_px;
    /* Compute-shader dispatch state. */
    uint32_t meta_offset;
    uint32_t point_offset;
    struct bounds b; /* glyph bounds in font units (for the shader uniform) */
};

/* Push a codepoint if not already present (linear dedupe over `out`).
 * Returns -1 on alloc failure, 0 otherwise. */
static int push_unique(struct u32_vec *out, uint32_t cp)
{
    for (uint32_t j = 0; j < out->size; j++) {
        if (out->data[j] == cp) {
            return 0;
        }
    }
    return u32_vec_push(out, cp);
}

/* Walk every cmap available on the face and collect the codepoints it
 * maps. FreeType picks ONE charmap as `face->charmap` at FT_New_Face
 * time (Unicode preferred). For PDF symbol fonts that only carry a
 * Microsoft-Symbol (3,0) or Mac-Roman (1,0) cmap, the default selection
 * may leave `face->charmap` NULL — `FT_Get_First_Char` then returns 0
 * immediately and the charset comes back empty.
 *
 * Iterate over `face->charmaps[]` instead: select each cmap, walk it,
 * dedupe via push_unique. Restore the original selection on exit.
 *
 * Microsoft Symbol (platform=3, encoding=0) cmaps encode glyphs in the
 * PUA range 0xF020-0xF0FF — the high byte 0xF0 is a fixed prefix and the
 * low byte is the real character. PDF /ToUnicode maps these glyphs back
 * to plain Latin codepoints (e.g. GID for "-" lives at U+F02D in the
 * cmap but the producer emits U+002D in text). For each MS-Symbol entry
 * also register the stripped codepoint so the CDB has both keys
 * pointing at the same MSDF render. FreeType's MS-Symbol cmap honours
 * the stripped codepoint too: FT_Get_Char_Index(face, 0x002D) returns
 * the same GID as 0xF02D when the symbol cmap is active. */
static int collect_codepoints(FT_Face face, struct u32_vec *out)
{
    FT_CharMap saved = face->charmap;

    for (FT_Int i = 0; i < face->num_charmaps; i++) {
        FT_CharMap cm = face->charmaps[i];
        if (FT_Set_Charmap(face, cm) != 0) {
            continue;
        }
        int is_ms_symbol = (cm->platform_id == 3 && cm->encoding_id == 0);
        ydebug("ymsdf-wgsl: cmap[%d] plat=%u enc=%u symbol=%d", (int)i, (unsigned)cm->platform_id,
               (unsigned)cm->encoding_id, is_ms_symbol);

        FT_UInt gid;
        FT_ULong cp = FT_Get_First_Char(face, &gid);
        while (gid != 0) {
            if (push_unique(out, (uint32_t)cp) < 0) {
                if (saved) {
                    FT_Set_Charmap(face, saved);
                }
                return -1;
            }
            if (is_ms_symbol && cp >= 0xF000 && cp <= 0xF0FF) {
                ydebug("ymsdf-wgsl:   ms-symbol cp=0x%lX -> stripped 0x%02lX", (unsigned long)cp,
                       (unsigned long)(cp & 0xFFu));
                if (push_unique(out, (uint32_t)(cp & 0xFFu)) < 0) {
                    if (saved) {
                        FT_Set_Charmap(face, saved);
                    }
                    return -1;
                }
            }
            cp = FT_Get_Next_Char(face, cp, &gid);
        }
    }

    if (saved) {
        FT_Set_Charmap(face, saved);
    }
    return 0;
}

static struct yetty_ycore_void_result write_cdb_file(const char *cdb_path,
                                                     const struct emit_glyph *emits,
                                                     size_t emit_count, const uint8_t *atlas_rgba8,
                                                     int atlas_w, int atlas_h)
{
    (void)atlas_h;
    struct yetty_ycdb_writer_result wr = yetty_ycdb_writer_create(cdb_path);
    if (YETTY_IS_ERR(wr)) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: open CDB for write", wr);
    }
    struct yetty_ycdb_writer *w = wr.value;

    struct u8_vec val = {0};
    for (size_t i = 0; i < emit_count; i++) {
        const struct emit_glyph *g = &emits[i];
        size_t pix_bytes = (size_t)g->atlas_w * g->atlas_h * 4;
        size_t need = sizeof(struct yetty_ymsdf_wgsl_glyph_header) + pix_bytes;
        if (u8_vec_reserve(&val, need) < 0) {
            u8_vec_free(&val);
            {
                struct yetty_ycore_void_result drop_r = yetty_ycdb_writer_finish(w);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "drop: yetty_ycdb_writer_finish");
            }
            return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: alloc value");
        }

        /* Convert FreeType font-units → display-pixels at config->font_size.
         * The poc does the same scaling at write time, but we already have
         * the bounds at hand and stash advance in font-units; scale below. */
        struct yetty_ymsdf_wgsl_glyph_header h = {0};
        h.codepoint = g->codepoint;
        h.width = (uint16_t)g->atlas_w;
        h.height = (uint16_t)g->atlas_h;
        h.bearing_x = g->bearing_x_px;
        h.bearing_y = g->bearing_y_px;
        h.size_x = g->size_x_px;
        h.size_y = g->size_y_px;
        h.advance = g->advance_px;
        memcpy(val.data, &h, sizeof(h));

        for (uint32_t y = 0; y < g->atlas_h; y++) {
            const uint8_t *src =
                atlas_rgba8 + ((size_t)(g->atlas_y + y) * atlas_w + g->atlas_x) * 4;
            uint8_t *dst = val.data + sizeof(h) + (size_t)y * g->atlas_w * 4;
            memcpy(dst, src, (size_t)g->atlas_w * 4);
        }

        uint32_t key = g->codepoint;
        struct yetty_ycore_void_result ar =
            yetty_ycdb_writer_add(w, &key, sizeof(key), val.data, need);
        if (YETTY_IS_ERR(ar)) {
            u8_vec_free(&val);
            {
                struct yetty_ycore_void_result drop_r = yetty_ycdb_writer_finish(w);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "drop: yetty_ycdb_writer_finish");
            }
            return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: CDB add", ar);
        }
    }
    u8_vec_free(&val);
    return yetty_ycdb_writer_finish(w);
}

/*=============================================================================
 * Public API — a generation is staged, so a batch of fonts can overlap the
 * CPU work of one with the GPU pass of another (see ymsdf/ensure.c):
 *   prepare   CPU only, any thread: FreeType outlines → packed buffers,
 *             atlas layout, per-glyph parameters, tile table.
 *   submit    the device's thread: texture, uploads, ONE dispatch over the
 *             atlas + the copy-out, submitted.
 *   readback  the device's thread: wait for that copy-out, read the atlas.
 *   write     any thread: the CDB file.
 * The compute pipeline is a separate object so a batch compiles it once.
 * config_generate() runs everything back to back.
 *===========================================================================*/

struct yetty_ymsdf_wgsl_job {
    char *ttf_path;
    char *cdb_path;
    char *shader_path; /* may be NULL */
    WGPUDevice device;
    WGPUInstance instance;
    float scale;
    float range;

    /* prepare → */
    struct emit_glyph *emits;
    size_t emit_count;
    struct u32_vec combined_meta;
    struct f32_vec combined_pts;
    struct atlas_layout layout;
    struct glyph_uniforms *slots; /* one per rendered glyph */
    size_t slot_count;
    uint32_t *tile_slots; /* tiles_x * tiles_y */
    size_t tiles_x;
    size_t tiles_y;

    /* submit → (released by readback) */
    struct atlas_texture texture;
    WGPUBuffer meta_buf;
    WGPUBuffer pts_buf;
    WGPUBuffer slots_buf;
    WGPUBuffer tiles_buf;
    WGPUBuffer staging; /* the atlas copied out, 256-byte row pitch */
    WGPUBindGroup bind_group;
    int submitted;

    /* readback → */
    uint8_t *rgba8; /* layout.width * layout.height * 4, NULL if nothing rendered */

    double time_start;
    double time_prepared;
    double time_submitted;
    double time_rendered;
};

/* Pipeline object: the compiled compute pipeline + layout for one device. */
struct yetty_ymsdf_wgsl_pipeline {
    struct compute comp;
};

struct yetty_ymsdf_wgsl_pipeline_ptr_result yetty_ymsdf_wgsl_pipeline_create(
    void *device, void *instance, const char *shader_path)
{
    if (!device || !instance) {
        return YETTY_ERR(yetty_ymsdf_wgsl_pipeline_ptr, "ymsdf-wgsl: device and instance required");
    }
    struct yetty_ymsdf_wgsl_pipeline *pipeline = calloc(1, sizeof(*pipeline));
    if (!pipeline) {
        return YETTY_ERR(yetty_ymsdf_wgsl_pipeline_ptr, "ymsdf-wgsl: alloc pipeline");
    }
    if (compute_init(&pipeline->comp, (WGPUDevice)device, (WGPUInstance)instance, shader_path) <
        0) {
        free(pipeline);
        return YETTY_ERR(yetty_ymsdf_wgsl_pipeline_ptr, "ymsdf-wgsl: compute pipeline init failed");
    }
    return YETTY_OK(yetty_ymsdf_wgsl_pipeline_ptr, pipeline);
}

void yetty_ymsdf_wgsl_pipeline_destroy(struct yetty_ymsdf_wgsl_pipeline *pipeline)
{
    if (!pipeline) {
        return;
    }
    compute_cleanup(&pipeline->comp);
    free(pipeline);
}

static void release_buffer(WGPUBuffer *buffer)
{
    if (*buffer) {
        wgpuBufferDestroy(*buffer);
        wgpuBufferRelease(*buffer);
        *buffer = NULL;
    }
}

/* Drop everything a submit put on the device. */
static void job_release_device_state(struct yetty_ymsdf_wgsl_job *job)
{
    if (job->bind_group) {
        wgpuBindGroupRelease(job->bind_group);
        job->bind_group = NULL;
    }
    release_buffer(&job->slots_buf);
    release_buffer(&job->tiles_buf);
    release_buffer(&job->meta_buf);
    release_buffer(&job->pts_buf);
    release_buffer(&job->staging);
    atlas_texture_cleanup(&job->texture);
    job->submitted = 0;
}

void yetty_ymsdf_wgsl_job_destroy(struct yetty_ymsdf_wgsl_job *job)
{
    if (!job) {
        return;
    }
    job_release_device_state(job);
    free(job->rgba8);
    free(job->tile_slots);
    free(job->slots);
    u32_vec_free(&job->combined_meta);
    f32_vec_free(&job->combined_pts);
    free(job->emits);
    free(job->shader_path);
    free(job->cdb_path);
    free(job->ttf_path);
    free(job);
}

static char *dup_string(const char *text)
{
    size_t len = strlen(text);
    char *copy = malloc(len + 1);
    if (copy) {
        memcpy(copy, text, len + 1);
    }
    return copy;
}

/* Every prepare failure path: close FreeType, drop the half-built job. */
static struct yetty_ymsdf_wgsl_job_ptr_result prepare_fail(struct yetty_ymsdf_wgsl_job *job,
                                                           FT_Library lib, FT_Face face,
                                                           const char *message)
{
    if (face) {
        FT_Done_Face(face);
    }
    if (lib) {
        FT_Done_FreeType(lib);
    }
    yetty_ymsdf_wgsl_job_destroy(job);
    return YETTY_ERR(yetty_ymsdf_wgsl_job_ptr, message);
}

struct yetty_ymsdf_wgsl_job_ptr_result yetty_ymsdf_wgsl_job_prepare(
    const struct yetty_ymsdf_wgsl_config *config)
{
    if (!config || !config->ttf_path || !config->cdb_path || !config->device || !config->instance) {
        return YETTY_ERR(yetty_ymsdf_wgsl_job_ptr, "ymsdf-wgsl: invalid config");
    }

    struct yetty_ymsdf_wgsl_job *job = calloc(1, sizeof(*job));
    if (!job) {
        return YETTY_ERR(yetty_ymsdf_wgsl_job_ptr, "ymsdf-wgsl: alloc job");
    }
    job->time_start = yetty_yplatform_ytime_monotonic_sec();
    job->device = (WGPUDevice)config->device;
    job->instance = (WGPUInstance)config->instance;
    job->ttf_path = dup_string(config->ttf_path);
    job->cdb_path = dup_string(config->cdb_path);
    job->shader_path = config->shader_path ? dup_string(config->shader_path) : NULL;
    if (!job->ttf_path || !job->cdb_path || (config->shader_path && !job->shader_path)) {
        return prepare_fail(job, NULL, NULL, "ymsdf-wgsl: alloc job paths");
    }
    float font_size = config->font_size > 0 ? config->font_size : 32.0f;
    float pixel_range = config->pixel_range > 0 ? config->pixel_range : 4.0f;

    /* FreeType */
    FT_Library lib = NULL;
    if (FT_Init_FreeType(&lib) != 0) {
        return prepare_fail(job, NULL, NULL, "ymsdf-wgsl: FT_Init_FreeType failed");
    }
    FT_Face face = NULL;
    if (FT_New_Face(lib, job->ttf_path, 0, &face) != 0) {
        return prepare_fail(job, lib, NULL, "ymsdf-wgsl: FT_New_Face failed");
    }
    float upem = (float)face->units_per_EM;
    if (upem <= 0.0f) {
        return prepare_fail(job, lib, face, "ymsdf-wgsl: invalid units_per_EM");
    }
    job->scale = font_size * 64.0f / upem;
    job->range = pixel_range;
    float scale = job->scale;
    float range = job->range;
    /* Font-units → pixel scale at font_size. Used to convert every glyph's
     * bearing/size/advance for the CDB header. */
    float us = font_size / upem;

    /* Codepoints */
    struct u32_vec cps = {0};
    if (collect_codepoints(face, &cps) < 0) {
        u32_vec_free(&cps);
        return prepare_fail(job, lib, face, "ymsdf-wgsl: collect codepoints failed");
    }
    if (cps.size == 0) {
        u32_vec_free(&cps);
        return prepare_fail(job, lib, face, "ymsdf-wgsl: empty charset");
    }
    ydebug("ymsdf-wgsl: %s has %zu codepoints", job->ttf_path, cps.size);

    /* Per-glyph serialization, atlas layout, emit list. */
    job->emits = calloc(cps.size, sizeof(struct emit_glyph));
    if (!job->emits) {
        u32_vec_free(&cps);
        return prepare_fail(job, lib, face, "ymsdf-wgsl: alloc emits");
    }
    atlas_layout_init(&job->layout, ATLAS_WIDTH);
    int padding = (int)ceilf(range);

    for (size_t i = 0; i < cps.size; i++) {
        uint32_t cp = cps.data[i];
        struct glyph_ctx gc = {0};
        gc.current_contour_index = -1;
        int sr = serialize_glyph(face, cp, &gc);
        if (sr < 0) {
            u32_vec_free(&gc.metadata);
            f32_vec_free(&gc.points);
            continue;
        }
        if (sr == 1) {
            /* Empty (e.g. space). No bitmap, no bearings — just the
             * advance, matching how CPU msdfgen leaves an empty entry. */
            u32_vec_free(&gc.metadata);
            f32_vec_free(&gc.points);
            FT_Load_Char(face, cp, FT_LOAD_NO_SCALE);
            struct emit_glyph *e = &job->emits[job->emit_count++];
            e->codepoint = cp;
            e->atlas_w = 0;
            e->atlas_h = 0;
            e->bearing_x_px = 0.0f;
            e->bearing_y_px = 0.0f;
            e->size_x_px = 0.0f;
            e->size_y_px = 0.0f;
            e->advance_px = (float)face->glyph->metrics.horiAdvance * us;
            continue;
        }
        struct bounds b = get_glyph_bounds(face, cp);
        if (b.empty) {
            u32_vec_free(&gc.metadata);
            f32_vec_free(&gc.points);
            continue;
        }

        float bw = b.max_x - b.min_x;
        float bh = b.max_y - b.min_y;
        int aw = (int)ceilf(bw * scale) + padding * 2;
        int ah = (int)ceilf(bh * scale) + padding * 2;
        int ax, ay;
        if (atlas_layout_allocate(&job->layout, aw, ah, &ax, &ay) < 0) {
            ywarn("ymsdf-wgsl: atlas full at U+%04X", cp);
            u32_vec_free(&gc.metadata);
            f32_vec_free(&gc.points);
            continue;
        }

        struct emit_glyph *e = &job->emits[job->emit_count++];
        e->codepoint = cp;
        e->atlas_x = (uint32_t)ax;
        e->atlas_y = (uint32_t)ay;
        e->atlas_w = (uint32_t)aw;
        e->atlas_h = (uint32_t)ah;
        /* CDB metadata convention (matches the CPU msdfgen path in
         * src/yetty/ymsdf-gen/ymsdf-gen.cpp — same downstream consumer in
         * msdf-font.c expects identical semantics):
         *
         *   size_x / size_y  = the FULL atlas-bitmap dimensions, including
         *                      the MSDF pixel-range padding on every side.
         *   bearing_x        = (glyph-contour left in pixels) - padding
         *                      so cursor_x + bearing_x lands at the
         *                      bitmap's LEFT edge (not the glyph contour).
         *   bearing_y        = (glyph-contour top in pixels) + padding
         *                      so y - bearing_y lands at the bitmap's TOP.
         *   advance          = horizontal advance in pixels. */
        e->size_x_px = (float)aw;
        e->size_y_px = (float)ah;
        e->bearing_x_px = b.min_x * FT_SCALE * us - (float)padding;
        e->bearing_y_px = b.max_y * FT_SCALE * us + (float)padding;
        e->advance_px = (float)face->glyph->metrics.horiAdvance * us;
        e->meta_offset = (uint32_t)job->combined_meta.size;
        e->point_offset = (uint32_t)(job->combined_pts.size / 2);
        e->b = b;

        int oom = 0;
        for (size_t k = 0; k < gc.metadata.size && !oom; k++) {
            oom = u32_vec_push(&job->combined_meta, gc.metadata.data[k]) < 0;
        }
        for (size_t k = 0; k < gc.points.size && !oom; k++) {
            oom = f32_vec_push(&job->combined_pts, gc.points.data[k]) < 0;
        }
        u32_vec_free(&gc.metadata);
        f32_vec_free(&gc.points);
        if (oom) {
            u32_vec_free(&cps);
            return prepare_fail(job, lib, face, "ymsdf-wgsl: alloc combined buffers");
        }
    }
    u32_vec_free(&cps);
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    face = NULL;
    lib = NULL;

    if (job->emit_count == 0) {
        return prepare_fail(job, NULL, NULL, "ymsdf-wgsl: no glyphs emitted");
    }
    atlas_layout_finish(&job->layout);

    /* Every rendered glyph's parameters, packed, plus the tile → glyph table
     * the single dispatch reads: each 8×8 workgroup looks up its tile's slot
     * and renders that glyph's pixels. */
    for (size_t i = 0; i < job->emit_count; i++) {
        if (job->emits[i].atlas_w != 0 && job->emits[i].atlas_h != 0) {
            job->slot_count++;
        }
    }
    job->tiles_x = (size_t)job->layout.width / ATLAS_TILE;
    job->tiles_y = (size_t)job->layout.height / ATLAS_TILE;
    size_t tile_count = job->tiles_x * job->tiles_y;
    job->slots = calloc(job->slot_count ? job->slot_count : 1, sizeof(*job->slots));
    job->tile_slots = calloc(tile_count ? tile_count : 1, sizeof(*job->tile_slots));
    if (!job->slots || !job->tile_slots) {
        return prepare_fail(job, NULL, NULL, "ymsdf-wgsl: alloc glyph table");
    }

    float padding_glyph_space = range / scale;
    size_t slot = 0;
    for (size_t i = 0; i < job->emit_count; i++) {
        const struct emit_glyph *e = &job->emits[i];
        if (e->atlas_w == 0 || e->atlas_h == 0) {
            continue; /* empty glyph — no compute needed */
        }
        struct glyph_uniforms *u = &job->slots[slot];
        u->atlas_offset[0] = e->atlas_x;
        u->atlas_offset[1] = e->atlas_y;
        u->glyph_size[0] = e->atlas_w;
        u->glyph_size[1] = e->atlas_h;
        u->translate[0] = padding_glyph_space - e->b.min_x;
        /* Pin the ink TOP exactly `padding` px below the bitmap top —
         * bearing_y_px assumes it. Anchoring the bottom let the ceil()
         * slack of atlas_h land at the top, giving each glyph a 0..1 px
         * vertical jitter on the rendered baseline (same fix as the CPU
         * generator). Identity when bh*scale is integral. */
        u->translate[1] = e->b.max_y - ((float)e->atlas_h - (float)padding) / scale;
        u->scale = scale;
        /* The compute shader measures distances in glyph space and
         * normalizes by u.range, so u.range is in GLYPH units. The font
         * shaders assume the field spans `range` OUTPUT pixels — convert
         * (matches the CPU generator; the two only coincided for 2048-upm
         * fonts at size 32, where scale == 1.0). */
        u->range = range / scale;
        u->meta_offset = e->meta_offset;
        u->point_offset = e->point_offset;
        u->glyph_height = (float)e->atlas_h;

        /* Every tile the glyph's rectangle touches belongs to it (the
         * packer keeps neighbours a whole tile apart). */
        size_t tile_x0 = e->atlas_x / ATLAS_TILE;
        size_t tile_x1 = (e->atlas_x + e->atlas_w - 1) / ATLAS_TILE;
        size_t tile_y0 = e->atlas_y / ATLAS_TILE;
        size_t tile_y1 = (e->atlas_y + e->atlas_h - 1) / ATLAS_TILE;
        for (size_t ty = tile_y0; ty <= tile_y1 && ty < job->tiles_y; ty++) {
            for (size_t tx = tile_x0; tx <= tile_x1 && tx < job->tiles_x; tx++) {
                job->tile_slots[ty * job->tiles_x + tx] = (uint32_t)(slot + 1);
            }
        }
        slot++;
    }

    job->time_prepared = yetty_yplatform_ytime_monotonic_sec();
    ydebug("ymsdf-wgsl: %s prepared %zu glyphs (%zu rendered, atlas %dx%d) in %.1f ms",
           job->ttf_path, job->emit_count, job->slot_count, job->layout.width, job->layout.height,
           (job->time_prepared - job->time_start) * 1000.0);
    return YETTY_OK(yetty_ymsdf_wgsl_job_ptr, job);
}

static WGPUBuffer upload_storage_buffer(WGPUDevice device, WGPUQueue queue, const void *data,
                                        size_t size)
{
    WGPUBufferDescriptor desc = {0};
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    desc.size = size;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &desc);
    if (buffer) {
        wgpuQueueWriteBuffer(queue, buffer, 0, data, size);
    }
    return buffer;
}

/* Row pitch of the copied-out atlas: WebGPU wants 256-byte multiples. */
static size_t atlas_staging_pitch(const struct atlas_layout *layout)
{
    size_t bytes_per_row = (size_t)layout->width * 4;
    return (bytes_per_row + 255) & ~(size_t)255;
}

struct yetty_ycore_void_result yetty_ymsdf_wgsl_job_submit(
    struct yetty_ymsdf_wgsl_job *job, struct yetty_ymsdf_wgsl_pipeline *pipeline)
{
    if (!job || !job->emits) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: submit before prepare");
    }
    if (!pipeline) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: submit needs a pipeline");
    }
    if (job->submitted) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: job already submitted");
    }
    double time_begin = yetty_yplatform_ytime_monotonic_sec();
    if (job->slot_count == 0 || job->combined_meta.size == 0 || job->combined_pts.size == 0) {
        /* A font with no outline data at all: nothing to dispatch, every
         * glyph is an empty entry in the CDB. */
        job->time_submitted = time_begin;
        job->time_rendered = time_begin;
        return YETTY_OK_VOID();
    }

    WGPUDevice device = job->device;
    WGPUQueue queue = wgpuDeviceGetQueue(device);

    if (atlas_texture_create(&job->texture, device, &job->layout) < 0) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: atlas texture create failed");
    }
    job->meta_buf = upload_storage_buffer(device, queue, job->combined_meta.data,
                                          job->combined_meta.size * sizeof(uint32_t));
    job->pts_buf = upload_storage_buffer(device, queue, job->combined_pts.data,
                                         job->combined_pts.size * sizeof(float));
    job->slots_buf =
        upload_storage_buffer(device, queue, job->slots, job->slot_count * sizeof(*job->slots));
    job->tiles_buf = upload_storage_buffer(device, queue, job->tile_slots,
                                           job->tiles_x * job->tiles_y * sizeof(*job->tile_slots));
    WGPUBufferDescriptor sdesc = {0};
    sdesc.size = atlas_staging_pitch(&job->layout) * (size_t)job->layout.height;
    sdesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    job->staging = wgpuDeviceCreateBuffer(device, &sdesc);
    if (!job->meta_buf || !job->pts_buf || !job->slots_buf || !job->tiles_buf || !job->staging) {
        job_release_device_state(job);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: device buffers failed");
    }

    WGPUBindGroupEntry be[5] = {0};
    be[0].binding = 0;
    be[0].buffer = job->slots_buf;
    be[0].size = job->slot_count * sizeof(*job->slots);
    be[1].binding = 1;
    be[1].buffer = job->meta_buf;
    be[1].size = job->combined_meta.size * sizeof(uint32_t);
    be[2].binding = 2;
    be[2].buffer = job->pts_buf;
    be[2].size = job->combined_pts.size * sizeof(float);
    be[3].binding = 3;
    be[3].textureView = job->texture.view;
    be[4].binding = 4;
    be[4].buffer = job->tiles_buf;
    be[4].size = job->tiles_x * job->tiles_y * sizeof(*job->tile_slots);
    WGPUBindGroupDescriptor bgd = {0};
    bgd.layout = pipeline->comp.bgl;
    bgd.entryCount = 5;
    bgd.entries = be;
    job->bind_group = wgpuDeviceCreateBindGroup(device, &bgd);
    if (!job->bind_group) {
        job_release_device_state(job);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: bind group create failed");
    }

    /* One dispatch over the whole atlas, then the copy-out, in one submit:
     * mapping the staging buffer later waits for exactly this font's work. */
    WGPUCommandEncoderDescriptor edesc = {0};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device, &edesc);
    WGPUComputePassDescriptor pdesc = {0};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(enc, &pdesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline->comp.pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, job->bind_group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, (uint32_t)job->tiles_x, (uint32_t)job->tiles_y,
                                             1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);

    WGPUTexelCopyTextureInfo src = {0};
    src.texture = job->texture.texture;
    WGPUTexelCopyBufferInfo dst = {0};
    dst.buffer = job->staging;
    dst.layout.bytesPerRow = (uint32_t)atlas_staging_pitch(&job->layout);
    dst.layout.rowsPerImage = (uint32_t)job->layout.height;
    WGPUExtent3D ext = {(uint32_t)job->layout.width, (uint32_t)job->layout.height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);

    WGPUCommandBufferDescriptor cdesc = {0};
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cdesc);
    wgpuQueueSubmit(queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);
    job->submitted = 1;
    job->time_submitted = yetty_yplatform_ytime_monotonic_sec();
    ydebug("ymsdf-wgsl: %s submitted %zu glyphs over %zux%zu tiles in %.1f ms", job->ttf_path,
           job->slot_count, job->tiles_x, job->tiles_y,
           (job->time_submitted - time_begin) * 1000.0);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ymsdf_wgsl_job_readback(struct yetty_ymsdf_wgsl_job *job)
{
    if (!job || !job->emits) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: readback before prepare");
    }
    if (!job->submitted) {
        if (job->slot_count == 0) {
            return YETTY_OK_VOID(); /* nothing was rendered, nothing to read */
        }
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: readback before submit");
    }
    double time_begin = yetty_yplatform_ytime_monotonic_sec();

    size_t pitch = atlas_staging_pitch(&job->layout);
    size_t bytes_per_row = (size_t)job->layout.width * 4;
    size_t total = pitch * (size_t)job->layout.height;
    /* The map completes once the copy-out — and so the dispatch before it —
     * has executed; only this font's work is waited for. */
    if (wait_buffer_map(job->staging, WGPUMapMode_Read, total, job->instance) < 0) {
        job_release_device_state(job);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: atlas readback map failed");
    }
    double time_mapped = yetty_yplatform_ytime_monotonic_sec();
    const uint8_t *mapped = wgpuBufferGetConstMappedRange(job->staging, 0, total);
    if (!mapped) {
        wgpuBufferUnmap(job->staging);
        job_release_device_state(job);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: atlas readback range failed");
    }
    job->rgba8 = malloc((size_t)job->layout.width * job->layout.height * 4);
    if (!job->rgba8) {
        wgpuBufferUnmap(job->staging);
        job_release_device_state(job);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: alloc atlas readback");
    }
    /* Drop the 256-byte row padding of the copy layout. */
    for (int y = 0; y < job->layout.height; y++) {
        memcpy(job->rgba8 + (size_t)y * bytes_per_row, mapped + (size_t)y * pitch, bytes_per_row);
    }
    wgpuBufferUnmap(job->staging);
    job_release_device_state(job);

    job->time_rendered = yetty_yplatform_ytime_monotonic_sec();
    ydebug("ymsdf-wgsl: %s read back: gpu wait %.1f ms, copy %.1f ms", job->ttf_path,
           (time_mapped - time_begin) * 1000.0, (job->time_rendered - time_mapped) * 1000.0);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ymsdf_wgsl_job_render(
    struct yetty_ymsdf_wgsl_job *job, struct yetty_ymsdf_wgsl_pipeline *pipeline)
{
    struct yetty_ycore_void_result submitted = yetty_ymsdf_wgsl_job_submit(job, pipeline);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, submitted, "ymsdf-wgsl: submit failed");
    return yetty_ymsdf_wgsl_job_readback(job);
}

struct yetty_ycore_void_result yetty_ymsdf_wgsl_job_write(struct yetty_ymsdf_wgsl_job *job)
{
    if (!job || !job->emits) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: write before prepare");
    }
    if (job->slot_count > 0 && !job->rgba8) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: write before render");
    }
    double time_begin = yetty_yplatform_ytime_monotonic_sec();
    struct yetty_ycore_void_result wres =
        write_cdb_file(job->cdb_path, job->emits, job->emit_count, job->rgba8, job->layout.width,
                       job->layout.height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wres, "ymsdf-wgsl: CDB write failed");
    double time_written = yetty_yplatform_ytime_monotonic_sec();
    ydebug("ymsdf-wgsl: wrote %s (%zu glyphs) in %.1f ms; %.1f ms since prepare began",
           job->cdb_path, job->emit_count, (time_written - time_begin) * 1000.0,
           (time_written - job->time_start) * 1000.0);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ymsdf_wgsl_config_generate(
    const struct yetty_ymsdf_wgsl_config *config)
{
    if (!config) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: invalid config");
    }
    struct yetty_ymsdf_wgsl_pipeline_ptr_result pipeline_res =
        yetty_ymsdf_wgsl_pipeline_create(config->device, config->instance, config->shader_path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pipeline_res, "ymsdf-wgsl: pipeline failed");
    struct yetty_ymsdf_wgsl_pipeline *pipeline = pipeline_res.value;

    struct yetty_ymsdf_wgsl_job_ptr_result prepared = yetty_ymsdf_wgsl_job_prepare(config);
    if (YETTY_IS_ERR(prepared)) {
        yetty_ymsdf_wgsl_pipeline_destroy(pipeline);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: prepare failed", prepared);
    }
    struct yetty_ymsdf_wgsl_job *job = prepared.value;

    struct yetty_ycore_void_result rendered = yetty_ymsdf_wgsl_job_render(job, pipeline);
    if (YETTY_IS_ERR(rendered)) {
        yetty_ymsdf_wgsl_job_destroy(job);
        yetty_ymsdf_wgsl_pipeline_destroy(pipeline);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: render failed", rendered);
    }
    struct yetty_ycore_void_result written = yetty_ymsdf_wgsl_job_write(job);
    yetty_ymsdf_wgsl_job_destroy(job);
    yetty_ymsdf_wgsl_pipeline_destroy(pipeline);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, written, "ymsdf-wgsl: write failed");
    return YETTY_OK_VOID();
}
