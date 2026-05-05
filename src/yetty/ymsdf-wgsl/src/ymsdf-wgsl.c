/*
 * ymsdf-wgsl.c - GPU MSDF CDB generator.
 *
 * Mirrors yetty-poc/src/yetty/msdf-wgsl/src/msdf-wgsl.cpp but ported to C.
 * Pipeline:
 *   FreeType outline → metadata + points GPU buffers
 *     → per-glyph compute dispatch → RGBA32Float storage texture
 *     → readback → RGBA8 conversion → CDB writer.
 *
 * Atlas is shelf-packed at 8192px wide. The texture grows in height when
 * the cursor walks off the bottom; old contents are copied across via
 * texture-to-texture copy.
 *
 * Async waits poll wgpuInstanceProcessEvents in a tight sleep loop. This
 * blocks the calling thread but keeps the API simple — the CDB generator
 * is invoked once per font on cache miss, which is fine to block on.
 */

#include <yetty/ymsdf-wgsl/ymsdf-wgsl.h>
#include <yetty/ycdb/ycdb.h>
#include <yetty/ytrace/ytrace.h>

#include <webgpu/webgpu.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
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

static void u32_vec_free(struct u32_vec *v) { free(v->data); v->data = NULL; v->size = v->cap = 0; }
static void f32_vec_free(struct f32_vec *v) { free(v->data); v->data = NULL; v->size = v->cap = 0; }
static void u8_vec_free(struct u8_vec *v)  { free(v->data); v->data = NULL; v->size = v->cap = 0; }

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
    COLOR_BLACK   = 0,
    COLOR_RED     = 1,
    COLOR_GREEN   = 2,
    COLOR_BLUE    = 4,
    COLOR_YELLOW  = 3, /* RED|GREEN */
    COLOR_MAGENTA = 5, /* RED|BLUE */
    COLOR_CYAN    = 6, /* GREEN|BLUE */
    COLOR_WHITE   = 7,
};

struct glyph_ctx {
    struct u32_vec metadata;
    struct f32_vec points;
    int current_contour_index;  /* -1 until first moveTo */
    float last_x, last_y;
    int oom;                    /* sticky out-of-memory flag */
};

static void glyph_ctx_close_contour(struct glyph_ctx *ctx);

static int decompose_move_to(const FT_Vector *to, void *user)
{
    struct glyph_ctx *ctx = user;

    glyph_ctx_close_contour(ctx);

    ctx->current_contour_index++;
    ctx->metadata.data[0]++;  /* contour count */
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
    if (u32_vec_push(&ctx->metadata, COLOR_WHITE) < 0 ||
        u32_vec_push(&ctx->metadata, 2) < 0) {
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
    if (u32_vec_push(&ctx->metadata, COLOR_WHITE) < 0 ||
        u32_vec_push(&ctx->metadata, 3) < 0) {
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

static int decompose_cubic_to(const FT_Vector *ctrl1, const FT_Vector *ctrl2,
                              const FT_Vector *to, void *user)
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
    if (u32_vec_push(&ctx->metadata, COLOR_WHITE) < 0 ||
        u32_vec_push(&ctx->metadata, 2) < 0 ||
        f32_vec_push(&ctx->points, sx) < 0 ||
        f32_vec_push(&ctx->points, sy) < 0) {
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
            uint32_t color = ctx->metadata.data[midx++]; (void)color;
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
                if (is_corner(segs[prev].ex, segs[prev].ey,
                              segs[s].bx, segs[s].by, cross_threshold)) {
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
                if (phase < 0) phase = 0;
                if (phase > 2) phase = 2;
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
 * Atlas — a single RGBA32Float storage texture grown via texture-to-texture
 * copy as the shelf packer walks off the bottom.
 *===========================================================================*/

struct atlas {
    WGPUDevice device;
    WGPUTexture texture;
    WGPUTextureView view;
    int width;
    int height;
    int row_height;
    int cursor_x;
    int cursor_y;
    size_t glyph_count;
};

static void atlas_cleanup(struct atlas *a)
{
    if (a->view) {
        wgpuTextureViewRelease(a->view);
        a->view = NULL;
    }
    if (a->texture) {
        wgpuTextureDestroy(a->texture);
        wgpuTextureRelease(a->texture);
        a->texture = NULL;
    }
}

static int atlas_resize(struct atlas *a, int new_height)
{
    if (new_height <= a->height) {
        return 0;
    }

    WGPUTextureDescriptor desc = {0};
    desc.dimension = WGPUTextureDimension_2D;
    desc.size = (WGPUExtent3D){(uint32_t)a->width, (uint32_t)new_height, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.format = WGPUTextureFormat_RGBA32Float;
    desc.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding |
                 WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    WGPUTexture nt = wgpuDeviceCreateTexture(a->device, &desc);
    if (!nt) {
        return -1;
    }

    WGPUTextureViewDescriptor vdesc = {0};
    vdesc.format = WGPUTextureFormat_RGBA32Float;
    vdesc.dimension = WGPUTextureViewDimension_2D;
    vdesc.mipLevelCount = 1;
    vdesc.arrayLayerCount = 1;
    WGPUTextureView nv = wgpuTextureCreateView(nt, &vdesc);
    if (!nv) {
        wgpuTextureDestroy(nt);
        wgpuTextureRelease(nt);
        return -1;
    }

    /* Carry over old contents. */
    if (a->texture && a->height > 0) {
        WGPUCommandEncoderDescriptor edesc = {0};
        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(a->device, &edesc);
        WGPUTexelCopyTextureInfo src = {0};
        src.texture = a->texture;
        WGPUTexelCopyTextureInfo dst = {0};
        dst.texture = nt;
        WGPUExtent3D ext = {(uint32_t)a->width, (uint32_t)a->height, 1};
        wgpuCommandEncoderCopyTextureToTexture(enc, &src, &dst, &ext);
        WGPUCommandBufferDescriptor cdesc = {0};
        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cdesc);
        WGPUQueue q = wgpuDeviceGetQueue(a->device);
        wgpuQueueSubmit(q, 1, &cb);
        wgpuCommandBufferRelease(cb);
        wgpuCommandEncoderRelease(enc);
    }

    atlas_cleanup(a);
    a->texture = nt;
    a->view = nv;
    a->height = new_height;
    return 0;
}

static int atlas_init(struct atlas *a, WGPUDevice device, int width)
{
    memset(a, 0, sizeof(*a));
    a->device = device;
    a->width = width;
    a->cursor_x = 1;
    a->cursor_y = 1;
    /* Lazy initial allocation — first allocate() forces resize. */
    return 0;
}

static int atlas_allocate(struct atlas *a, int w, int h, int *out_x, int *out_y)
{
    if (a->cursor_x + w + 1 > a->width) {
        a->cursor_x = 1;
        a->cursor_y += a->row_height + 1;
        a->row_height = 0;
    }
    if (a->cursor_y + h + 1 > a->height) {
        int doubled = a->height * 2;
        int needed = a->cursor_y + h + 64;
        int nh = doubled > needed ? doubled : needed;
        if (atlas_resize(a, nh) < 0) {
            return -1;
        }
    }
    *out_x = a->cursor_x;
    *out_y = a->cursor_y;
    a->cursor_x += w + 1;
    if (h > a->row_height) {
        a->row_height = h;
    }
    a->glyph_count++;
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
 * Async wait helpers — poll wgpuInstanceProcessEvents until the callback
 * fires. Same pattern as poc; blocking but simple, no coroutines required.
 *===========================================================================*/

static void usleep_short(void)
{
    struct timespec ts = {0, 100000}; /* 100us */
    nanosleep(&ts, NULL);
}

static void queue_done_cb(WGPUQueueWorkDoneStatus status, WGPUStringView msg, void *u1, void *u2)
{
    (void)status;
    (void)msg;
    (void)u2;
    atomic_store((atomic_int *)u1, 1);
}

static void wait_queue_done(WGPUQueue queue, WGPUInstance instance)
{
    atomic_int done = 0;
    WGPUQueueWorkDoneCallbackInfo cb = {0};
    cb.mode = WGPUCallbackMode_AllowSpontaneous;
    cb.callback = queue_done_cb;
    cb.userdata1 = &done;
    wgpuQueueOnSubmittedWorkDone(queue, cb);
    while (!atomic_load(&done)) {
        if (instance) {
            wgpuInstanceProcessEvents(instance);
        }
        usleep_short();
    }
}

static void map_done_cb(WGPUMapAsyncStatus status, WGPUStringView msg, void *u1, void *u2)
{
    (void)msg;
    atomic_store((atomic_int *)u2, (int)status);
    atomic_store((atomic_int *)u1, 1);
}

static int wait_buffer_map(WGPUBuffer buf, WGPUMapMode mode, size_t size, WGPUInstance instance)
{
    atomic_int done = 0;
    atomic_int status = (int)WGPUMapAsyncStatus_Error;
    WGPUBufferMapCallbackInfo cb = {0};
    cb.mode = WGPUCallbackMode_AllowSpontaneous;
    cb.callback = map_done_cb;
    cb.userdata1 = &done;
    cb.userdata2 = &status;
    wgpuBufferMapAsync(buf, mode, 0, size, cb);
    while (!atomic_load(&done)) {
        if (instance) {
            wgpuInstanceProcessEvents(instance);
        }
        usleep_short();
    }
    return atomic_load(&status) == WGPUMapAsyncStatus_Success ? 0 : -1;
}

/*=============================================================================
 * Compute pipeline — created once per generate() call and torn down at the
 * end. The shader source is read from disk; the bind group layout is fixed:
 *   binding 0: uniform (per-glyph parameters)
 *   binding 1: storage<read> metadata
 *   binding 2: storage<read> points
 *   binding 3: storage texture write-only RGBA32Float
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

    WGPUBindGroupLayoutEntry e[4] = {0};
    e[0].binding = 0;
    e[0].visibility = WGPUShaderStage_Compute;
    e[0].buffer.type = WGPUBufferBindingType_Uniform;
    e[1].binding = 1;
    e[1].visibility = WGPUShaderStage_Compute;
    e[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    e[2].binding = 2;
    e[2].visibility = WGPUShaderStage_Compute;
    e[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    e[3].binding = 3;
    e[3].visibility = WGPUShaderStage_Compute;
    e[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    e[3].storageTexture.format = WGPUTextureFormat_RGBA32Float;
    e[3].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor ldesc = {0};
    ldesc.entryCount = 4;
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
 * downstream (msdf-font.c → expand_text_span_to_glyphs) reads them straight
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

static int collect_codepoints(FT_Face face, struct u32_vec *out)
{
    FT_UInt gid;
    FT_ULong cp = FT_Get_First_Char(face, &gid);
    while (gid != 0) {
        if (u32_vec_push(out, (uint32_t)cp) < 0) {
            return -1;
        }
        cp = FT_Get_Next_Char(face, cp, &gid);
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
            yetty_ycdb_writer_finish(w);
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
            const uint8_t *src = atlas_rgba8 + ((size_t)(g->atlas_y + y) * atlas_w + g->atlas_x) * 4;
            uint8_t *dst = val.data + sizeof(h) + (size_t)y * g->atlas_w * 4;
            memcpy(dst, src, (size_t)g->atlas_w * 4);
        }

        uint32_t key = g->codepoint;
        struct yetty_ycore_void_result ar =
            yetty_ycdb_writer_add(w, &key, sizeof(key), val.data, need);
        if (YETTY_IS_ERR(ar)) {
            u8_vec_free(&val);
            yetty_ycdb_writer_finish(w);
            return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: CDB add", ar);
        }
    }
    u8_vec_free(&val);
    return yetty_ycdb_writer_finish(w);
}

/* Read the RGBA32Float atlas back to a malloc'd RGBA8 buffer.
 * Returns NULL on error. */
static uint8_t *atlas_readback_rgba8(struct atlas *a, WGPUInstance instance)
{
    if (!a->texture || a->width == 0 || a->height == 0) {
        return NULL;
    }
    size_t bpp = 16; /* RGBA32Float */
    size_t bytes_per_row = (size_t)a->width * bpp;
    size_t aligned = (bytes_per_row + 255) & ~(size_t)255;
    size_t total = aligned * (size_t)a->height;

    WGPUBufferDescriptor bdesc = {0};
    bdesc.size = total;
    bdesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer staging = wgpuDeviceCreateBuffer(a->device, &bdesc);
    if (!staging) {
        return NULL;
    }

    WGPUCommandEncoderDescriptor edesc = {0};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(a->device, &edesc);
    WGPUTexelCopyTextureInfo src = {0};
    src.texture = a->texture;
    WGPUTexelCopyBufferInfo dst = {0};
    dst.buffer = staging;
    dst.layout.bytesPerRow = (uint32_t)aligned;
    dst.layout.rowsPerImage = (uint32_t)a->height;
    WGPUExtent3D ext = {(uint32_t)a->width, (uint32_t)a->height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);

    WGPUCommandBufferDescriptor cdesc = {0};
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cdesc);
    WGPUQueue q = wgpuDeviceGetQueue(a->device);
    wgpuQueueSubmit(q, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);

    wait_queue_done(q, instance);

    if (wait_buffer_map(staging, WGPUMapMode_Read, total, instance) < 0) {
        wgpuBufferRelease(staging);
        return NULL;
    }
    const float *fp = wgpuBufferGetConstMappedRange(staging, 0, total);
    if (!fp) {
        wgpuBufferUnmap(staging);
        wgpuBufferRelease(staging);
        return NULL;
    }

    uint8_t *out = malloc((size_t)a->width * a->height * 4);
    if (!out) {
        wgpuBufferUnmap(staging);
        wgpuBufferRelease(staging);
        return NULL;
    }
    for (int y = 0; y < a->height; y++) {
        const float *row = fp + ((size_t)y * aligned / sizeof(float));
        uint8_t *drow = out + (size_t)y * a->width * 4;
        for (int x = 0; x < a->width; x++) {
            const float *px = row + (size_t)x * 4;
            for (int c = 0; c < 4; c++) {
                float v = px[c] * 255.0f;
                if (v < 0.0f) {
                    v = 0.0f;
                } else if (v > 255.0f) {
                    v = 255.0f;
                }
                drow[x * 4 + c] = (uint8_t)v;
            }
        }
    }

    wgpuBufferUnmap(staging);
    wgpuBufferDestroy(staging);
    wgpuBufferRelease(staging);
    return out;
}

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ymsdf_wgsl_config_generate(
    const struct yetty_ymsdf_wgsl_config *config)
{
    if (!config || !config->ttf_path || !config->cdb_path || !config->device ||
        !config->instance) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: invalid config");
    }

    WGPUDevice device = (WGPUDevice)config->device;
    WGPUInstance instance = (WGPUInstance)config->instance;
    float font_size = config->font_size > 0 ? config->font_size : 32.0f;
    float pixel_range = config->pixel_range > 0 ? config->pixel_range : 4.0f;

    /* FreeType */
    FT_Library lib = NULL;
    if (FT_Init_FreeType(&lib) != 0) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: FT_Init_FreeType failed");
    }
    FT_Face face = NULL;
    if (FT_New_Face(lib, config->ttf_path, 0, &face) != 0) {
        FT_Done_FreeType(lib);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: FT_New_Face failed");
    }
    float upem = (float)face->units_per_EM;
    if (upem <= 0.0f) {
        FT_Done_Face(face);
        FT_Done_FreeType(lib);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: invalid units_per_EM");
    }
    float scale = font_size * 64.0f / upem;
    float range = pixel_range;
    /* Font-units → pixel scale at config->font_size. Used to convert every
     * glyph's bearing/size/advance for the CDB header. */
    float us = font_size / upem;

    /* Compute pipeline */
    struct compute comp;
    if (compute_init(&comp, device, instance, config->shader_path) < 0) {
        FT_Done_Face(face);
        FT_Done_FreeType(lib);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: compute pipeline init failed");
    }

    /* Atlas */
    struct atlas atlas;
    atlas_init(&atlas, device, 8192);

    /* Codepoints */
    struct u32_vec cps = {0};
    if (collect_codepoints(face, &cps) < 0) {
        u32_vec_free(&cps);
        atlas_cleanup(&atlas);
        compute_cleanup(&comp);
        FT_Done_Face(face);
        FT_Done_FreeType(lib);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: collect codepoints failed");
    }
    if (cps.size == 0) {
        u32_vec_free(&cps);
        atlas_cleanup(&atlas);
        compute_cleanup(&comp);
        FT_Done_Face(face);
        FT_Done_FreeType(lib);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: empty charset");
    }
    ydebug("ymsdf-wgsl: %s has %zu codepoints", config->ttf_path, cps.size);

    /* Per-glyph serialization, atlas allocation, emit list. */
    struct emit_glyph *emits = calloc(cps.size, sizeof(struct emit_glyph));
    struct u32_vec combined_meta = {0};
    struct f32_vec combined_pts = {0};
    if (!emits) {
        u32_vec_free(&cps);
        atlas_cleanup(&atlas);
        compute_cleanup(&comp);
        FT_Done_Face(face);
        FT_Done_FreeType(lib);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: alloc emits");
    }
    size_t emit_count = 0;
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
            struct emit_glyph *e = &emits[emit_count++];
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
        if (atlas_allocate(&atlas, aw, ah, &ax, &ay) < 0) {
            ywarn("ymsdf-wgsl: atlas full at U+%04X", cp);
            u32_vec_free(&gc.metadata);
            f32_vec_free(&gc.points);
            continue;
        }

        struct emit_glyph *e = &emits[emit_count++];
        e->codepoint = cp;
        e->atlas_x = (uint32_t)ax;
        e->atlas_y = (uint32_t)ay;
        e->atlas_w = (uint32_t)aw;
        e->atlas_h = (uint32_t)ah;
        /* CDB metadata convention (matches the CPU msdfgen path in
         * src/yetty/ymsdf-gen/ymsdf-gen.cpp:135-140 — same downstream
         * consumer in msdf-font.c expects identical semantics):
         *
         *   size_x / size_y  = the FULL atlas-bitmap dimensions, including
         *                      the MSDF pixel-range padding on every side.
         *                      The shader treats this as the glyph render
         *                      rectangle and samples the entire region.
         *   bearing_x        = (glyph-contour left in pixels) - padding
         *                      so cursor_x + bearing_x lands at the
         *                      bitmap's LEFT edge (not the glyph contour).
         *   bearing_y        = (glyph-contour top in pixels) + padding
         *                      so y - bearing_y lands at the bitmap's TOP.
         *   advance          = horizontal advance in pixels.
         *
         * Earlier I stored the glyph-contour dims here, which made the
         * shader sample only the inner glyph-contour-sized sub-rectangle
         * of the actual bitmap — every glyph rendered shifted by
         * `padding` and clipped at the right/bottom. */
        e->size_x_px = (float)aw;
        e->size_y_px = (float)ah;
        e->bearing_x_px = b.min_x * FT_SCALE * us - (float)padding;
        e->bearing_y_px = b.max_y * FT_SCALE * us + (float)padding;
        e->advance_px = (float)face->glyph->metrics.horiAdvance * us;
        e->meta_offset = (uint32_t)combined_meta.size;
        e->point_offset = (uint32_t)(combined_pts.size / 2);
        e->b = b;

        for (size_t k = 0; k < gc.metadata.size; k++) {
            if (u32_vec_push(&combined_meta, gc.metadata.data[k]) < 0) {
                emit_count = 0;
                break;
            }
        }
        for (size_t k = 0; k < gc.points.size; k++) {
            if (f32_vec_push(&combined_pts, gc.points.data[k]) < 0) {
                emit_count = 0;
                break;
            }
        }
        u32_vec_free(&gc.metadata);
        f32_vec_free(&gc.points);
    }

    if (emit_count == 0) {
        free(emits);
        u32_vec_free(&combined_meta);
        f32_vec_free(&combined_pts);
        u32_vec_free(&cps);
        atlas_cleanup(&atlas);
        compute_cleanup(&comp);
        FT_Done_Face(face);
        FT_Done_FreeType(lib);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: no glyphs emitted");
    }
    ydebug("ymsdf-wgsl: serialized %zu/%zu glyphs", emit_count, cps.size);

    /* Upload combined buffers. Two storage buffers shared across all dispatches. */
    WGPUQueue queue = wgpuDeviceGetQueue(device);
    WGPUBuffer meta_buf = NULL;
    WGPUBuffer pts_buf = NULL;
    if (combined_meta.size > 0) {
        WGPUBufferDescriptor d = {0};
        d.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        d.size = combined_meta.size * sizeof(uint32_t);
        meta_buf = wgpuDeviceCreateBuffer(device, &d);
        wgpuQueueWriteBuffer(queue, meta_buf, 0, combined_meta.data, d.size);
    }
    if (combined_pts.size > 0) {
        WGPUBufferDescriptor d = {0};
        d.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        d.size = combined_pts.size * sizeof(float);
        pts_buf = wgpuDeviceCreateBuffer(device, &d);
        wgpuQueueWriteBuffer(queue, pts_buf, 0, combined_pts.data, d.size);
    }

    /* Per-glyph dispatch — uniform buffer + bind group per glyph, all
     * recorded into one command encoder, single submit. */
    WGPUCommandEncoderDescriptor edesc = {0};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device, &edesc);

    WGPUBuffer *uniform_bufs = calloc(emit_count, sizeof(WGPUBuffer));
    WGPUBindGroup *bgroups = calloc(emit_count, sizeof(WGPUBindGroup));
    if (!uniform_bufs || !bgroups) {
        free(uniform_bufs);
        free(bgroups);
        if (meta_buf) {
            wgpuBufferDestroy(meta_buf);
            wgpuBufferRelease(meta_buf);
        }
        if (pts_buf) {
            wgpuBufferDestroy(pts_buf);
            wgpuBufferRelease(pts_buf);
        }
        wgpuCommandEncoderRelease(enc);
        free(emits);
        u32_vec_free(&combined_meta);
        f32_vec_free(&combined_pts);
        u32_vec_free(&cps);
        atlas_cleanup(&atlas);
        compute_cleanup(&comp);
        FT_Done_Face(face);
        FT_Done_FreeType(lib);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: alloc per-glyph arrays");
    }

    float padding_glyph_space = range / scale;

    for (size_t i = 0; i < emit_count; i++) {
        const struct emit_glyph *e = &emits[i];
        if (e->atlas_w == 0 || e->atlas_h == 0) {
            continue; /* empty glyph — no compute needed */
        }
        struct glyph_uniforms u = {0};
        u.atlas_offset[0] = e->atlas_x;
        u.atlas_offset[1] = e->atlas_y;
        u.glyph_size[0] = e->atlas_w;
        u.glyph_size[1] = e->atlas_h;
        u.translate[0] = padding_glyph_space - e->b.min_x;
        u.translate[1] = e->b.min_y - padding_glyph_space;
        u.scale = scale;
        u.range = range;
        u.meta_offset = e->meta_offset;
        u.point_offset = e->point_offset;
        u.glyph_height = (float)e->atlas_h;

        WGPUBufferDescriptor ud = {0};
        ud.size = sizeof(u);
        ud.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        WGPUBuffer ub = wgpuDeviceCreateBuffer(device, &ud);
        wgpuQueueWriteBuffer(queue, ub, 0, &u, sizeof(u));

        WGPUBindGroupEntry be[4] = {0};
        be[0].binding = 0;
        be[0].buffer = ub;
        be[0].size = sizeof(u);
        be[1].binding = 1;
        be[1].buffer = meta_buf;
        be[1].size = combined_meta.size * sizeof(uint32_t);
        be[2].binding = 2;
        be[2].buffer = pts_buf;
        be[2].size = combined_pts.size * sizeof(float);
        be[3].binding = 3;
        be[3].textureView = atlas.view;

        WGPUBindGroupDescriptor bgd = {0};
        bgd.layout = comp.bgl;
        bgd.entryCount = 4;
        bgd.entries = be;
        WGPUBindGroup bg = wgpuDeviceCreateBindGroup(device, &bgd);
        uniform_bufs[i] = ub;
        bgroups[i] = bg;

        WGPUComputePassDescriptor pdesc = {0};
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(enc, &pdesc);
        wgpuComputePassEncoderSetPipeline(pass, comp.pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bg, 0, NULL);
        uint32_t wgx = (e->atlas_w + 7) / 8;
        uint32_t wgy = (e->atlas_h + 7) / 8;
        wgpuComputePassEncoderDispatchWorkgroups(pass, wgx, wgy, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    }

    WGPUCommandBufferDescriptor cdesc = {0};
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cdesc);
    wgpuQueueSubmit(queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);

    wait_queue_done(queue, instance);

    /* Per-glyph cleanup */
    for (size_t i = 0; i < emit_count; i++) {
        if (bgroups[i]) {
            wgpuBindGroupRelease(bgroups[i]);
        }
        if (uniform_bufs[i]) {
            wgpuBufferDestroy(uniform_bufs[i]);
            wgpuBufferRelease(uniform_bufs[i]);
        }
    }
    free(bgroups);
    free(uniform_bufs);
    if (meta_buf) {
        wgpuBufferDestroy(meta_buf);
        wgpuBufferRelease(meta_buf);
    }
    if (pts_buf) {
        wgpuBufferDestroy(pts_buf);
        wgpuBufferRelease(pts_buf);
    }

    /* Atlas readback + CDB write. */
    uint8_t *rgba8 = atlas_readback_rgba8(&atlas, instance);
    if (!rgba8) {
        free(emits);
        u32_vec_free(&combined_meta);
        f32_vec_free(&combined_pts);
        u32_vec_free(&cps);
        atlas_cleanup(&atlas);
        compute_cleanup(&comp);
        FT_Done_Face(face);
        FT_Done_FreeType(lib);
        return YETTY_ERR(yetty_ycore_void, "ymsdf-wgsl: atlas readback failed");
    }

    /* Pixel-space metrics were already computed when each glyph was emitted
     * (above), so we go straight to the CDB writer. */

    struct yetty_ycore_void_result wres =
        write_cdb_file(config->cdb_path, emits, emit_count, rgba8, atlas.width, atlas.height);

    free(rgba8);
    free(emits);
    u32_vec_free(&combined_meta);
    f32_vec_free(&combined_pts);
    u32_vec_free(&cps);
    atlas_cleanup(&atlas);
    compute_cleanup(&comp);
    FT_Done_Face(face);
    FT_Done_FreeType(lib);

    if (YETTY_IS_ERR(wres)) {
        return wres;
    }
    ydebug("ymsdf-wgsl: wrote %s (%zu glyphs)", config->cdb_path, emit_count);
    return YETTY_OK_VOID();
}
