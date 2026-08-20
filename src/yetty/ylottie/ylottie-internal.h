#ifndef YETTY_YLOTTIE_INTERNAL_H
#define YETTY_YLOTTIE_INTERNAL_H

/*
 * Internal definitions shared across the ylottie translation units.
 *
 *   ylottie-json.c   minimal recursive-descent JSON parser → DOM tree
 *   ylottie-prop.c   Lottie property evaluation at a frame (keyframe easing),
 *                    affine-transform math, colour helpers
 *   ylottie-path.c   flatten a Lottie cubic-bezier shape ("sh") to polylines;
 *                    compute polystar vertices
 *   ylottie-paint.c  walk layers + shape groups, emit ydraw primitives + text
 *   ylottie.c        glues the pipeline together; owns the public entry point
 *
 * Memory model: one root yetty_ylottie_json owns every node and string via a
 * bump arena. Destroying the root frees everything. We operate directly on
 * the JSON DOM rather than building a second typed model — the DOM already is
 * the structured tree, and Lottie's `{a,k}` property objects are read on the
 * fly by ylottie-prop.c.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ylottie/ylottie.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * JSON DOM (ylottie-json.c)
 *
 * Children of arrays and objects are a singly-linked list (first_child /
 * next_sibling), same shape as the ysvg node tree. Object members carry a
 * `key`; array elements have key == NULL.
 *===========================================================================*/

enum yetty_ylottie_json_type {
    YETTY_YLOTTIE_JSON_NULL = 0,
    YETTY_YLOTTIE_JSON_BOOL,
    YETTY_YLOTTIE_JSON_NUMBER,
    YETTY_YLOTTIE_JSON_STRING,
    YETTY_YLOTTIE_JSON_ARRAY,
    YETTY_YLOTTIE_JSON_OBJECT,
};

struct yetty_ylottie_json {
    enum yetty_ylottie_json_type type;

    /* Scalar payloads. */
    double number;
    bool boolean;
    const char *string; /* NUL-terminated, arena-owned (string values only) */
    size_t string_len;

    /* Object-member key (NULL for array elements / the root). */
    const char *key;
    size_t key_len;

    /* Container children. */
    struct yetty_ylottie_json *first_child;
    struct yetty_ylottie_json *last_child;
    struct yetty_ylottie_json *next_sibling;
    size_t child_count;
};

/* The arena owner. Holds the root value plus the chunk list backing every
 * node and string. */
struct yetty_ylottie_doc {
    struct yetty_ylottie_json *root;
    struct yetty_ylottie_json_chunk *chunks;
};

YETTY_YRESULT_DECLARE(yetty_ylottie_doc_ptr, struct yetty_ylottie_doc *);

/* Parse JSON source into a doc. On error returns a result with a NULL doc and
 * a populated error (with a byte offset in the message). */
struct yetty_ylottie_doc_ptr_result yetty_ylottie_json_parse(const char *src, size_t len);

void yetty_ylottie_doc_destroy(struct yetty_ylottie_doc *doc);

/* Accessors — all NULL-safe on the node argument. */

/* Object member lookup by key (linear). NULL if not an object or absent. */
const struct yetty_ylottie_json *yetty_ylottie_json_get(const struct yetty_ylottie_json *obj,
                                                        const char *key);

/* Array element by index (linear). NULL if not an array or out of range. */
const struct yetty_ylottie_json *yetty_ylottie_json_at(const struct yetty_ylottie_json *arr,
                                                       size_t index);

/* Number with fallback when the node is missing or not a number. */
double yetty_ylottie_json_num(const struct yetty_ylottie_json *node, double fallback);

/* Number at object key / array index, with fallback. */
double yetty_ylottie_json_num_at(const struct yetty_ylottie_json *node, size_t index,
                                 double fallback);
double yetty_ylottie_json_num_key(const struct yetty_ylottie_json *obj, const char *key,
                                  double fallback);

/*=============================================================================
 * Affine transform (column-major 2×3), identical convention to ysvg.
 *
 *   [ a c e ]   [ x ]
 *   [ b d f ] · [ y ]
 *   [ 0 0 1 ]   [ 1 ]
 *===========================================================================*/

struct yetty_ylottie_xform {
    float a, b, c, d, e, f;
};

void yetty_ylottie_xform_identity(struct yetty_ylottie_xform *m);
void yetty_ylottie_xform_multiply(struct yetty_ylottie_xform *out,
                                  const struct yetty_ylottie_xform *l,
                                  const struct yetty_ylottie_xform *r);
void yetty_ylottie_xform_point(const struct yetty_ylottie_xform *m, float x, float y, float *ox,
                               float *oy);
/* Average uniform scale magnitude — used to scale stroke widths / radii. */
float yetty_ylottie_xform_avg_scale(const struct yetty_ylottie_xform *m);
/* True when the matrix has no rotation/skew (pure translate + scale). */
bool yetty_ylottie_xform_axis_aligned(const struct yetty_ylottie_xform *m);

/*=============================================================================
 * Property evaluation (ylottie-prop.c)
 *
 * A Lottie animatable property is an object `{ "a": 0|1, "k": ... }`:
 *   a == 0  → k is the static value (a number, or an array of numbers).
 *   a == 1  → k is an array of keyframes, each { t, s, [e], i, o, [h] }.
 * eval_prop writes up to `dim` components into `out` (zero-filled first) and
 * returns the number of components actually produced.
 *===========================================================================*/

/* Evaluate a multi-dimensional property (position, scale, anchor, colour, …)
 * at frame `frame`. `out` must hold at least `dim` floats. */
size_t yetty_ylottie_prop_eval(const struct yetty_ylottie_json *prop, float frame, float *out,
                               size_t dim);

/* Evaluate a scalar property (opacity, rotation, stroke width, …). */
float yetty_ylottie_prop_eval_scalar(const struct yetty_ylottie_json *prop, float frame,
                                     float fallback);

/* Build the local affine of a Lottie transform object (a layer `ks`, or a
 * shape-group `tr`) at `frame`. Components honoured: anchor `a`, position
 * `p`, scale `s` (percent), rotation `r` (degrees), skew `sk`/`sa`. */
void yetty_ylottie_transform_build(const struct yetty_ylottie_json *tr, float frame,
                                   struct yetty_ylottie_xform *out);

/* Opacity component (`o`, 0..100) of a transform object as a 0..1 factor;
 * 1.0 when absent. */
float yetty_ylottie_transform_opacity(const struct yetty_ylottie_json *tr, float frame);

/* Pack a Lottie colour (`c` property — [r,g,b] or [r,g,b,a] floats in 0..1)
 * evaluated at `frame`, combined with `alpha` (0..1), into the ABGR word
 * ydraw expects. Returns 0 (fully transparent) when prop is NULL. */
uint32_t yetty_ylottie_color_eval(const struct yetty_ylottie_json *color_prop, float frame,
                                  float alpha);

/* Pack normalized r,g,b,a (each 0..1) into the ABGR layout ydraw expects. */
uint32_t yetty_ylottie_rgba_to_abgr(float r, float g, float b, float a);

/*=============================================================================
 * Path flattening (ylottie-path.c)
 *===========================================================================*/

struct yetty_ylottie_point {
    float x, y;
};

struct yetty_ylottie_polyline {
    struct yetty_ylottie_point *points;
    size_t count;
    size_t cap;
    bool closed;
};

void yetty_ylottie_polyline_destroy(struct yetty_ylottie_polyline *pl);

/* Flatten a Lottie bezier shape VALUE (the object holding arrays `v`
 * (vertices), `i` (in tangents, vertex-relative), `o` (out tangents),
 * and bool `c` (closed)) into a polyline. `tolerance` is the max chord
 * deviation in user units. Returns 1 on success, 0 on OOM. A `sh` property
 * is animatable: pass the value already resolved for the frame (the static
 * `k`, or the active keyframe's `s`); the resolver lives in ylottie-path.c
 * via yetty_ylottie_path_eval below. */
int yetty_ylottie_bezier_flatten(struct yetty_ylottie_polyline *out,
                                 const struct yetty_ylottie_json *shape_value, float tolerance);

/* Resolve a shape ("sh") property `{a,k}` to the bezier value object active
 * at `frame` (no vertex interpolation between keyframes — the keyframe whose
 * time bounds `frame` from below is used; this is exact at keyframe times and
 * a hold elsewhere). Returns NULL when there is no usable value. */
const struct yetty_ylottie_json *yetty_ylottie_path_eval(const struct yetty_ylottie_json *prop,
                                                         float frame);

/* Append the analytic vertices of a polystar ("sr") to `out` as a closed
 * polyline. `star` is the polystar shape item; values are evaluated at
 * `frame`. Returns 1 on success, 0 on OOM. */
int yetty_ylottie_polystar_build(struct yetty_ylottie_polyline *out,
                                 const struct yetty_ylottie_json *star, float frame);

/* Grow-and-append one point. Returns 0 on success, -1 on OOM. Shared by the
 * flattener and the polystar builder. */
int yetty_ylottie_polyline_push(struct yetty_ylottie_polyline *pl, float x, float y);

/*=============================================================================
 * Paint pass (ylottie-paint.c)
 *===========================================================================*/

struct yetty_ylottie_paint_ctx {
    struct yetty_ydraw_drawable_list *buf;
    const struct yetty_ylottie_json *layers; /* composition `layers` array */
    float frame;                             /* frame being rendered */
    float default_font_size;
    /* Root transform: composition user space → display pixels. */
    struct yetty_ylottie_xform root_ctm;
    float user_to_pixel_scale;
};

struct yetty_ycore_void_result yetty_ylottie_paint(const struct yetty_ylottie_paint_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YLOTTIE_INTERNAL_H */
