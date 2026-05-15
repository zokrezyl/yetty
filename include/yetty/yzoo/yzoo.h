#ifndef YETTY_YZOO_YZOO_H
#define YETTY_YZOO_YZOO_H

/*
 * yzoo — animated control-point "zoo": points spawn around the scene centre,
 * drift outward (exponential growth), connect to nearest neighbours, and get
 * culled at the viewport edge. Connections render as line-segment-approximated
 * quadratic beziers, straight segments, or one of 22 SDF shapes at the
 * connection midpoint.
 *
 * Port of yetty-poc/src/yetty/ydraw-zoo/zoo-renderer.cpp (C++ → C). The
 * renderer is a pure producer of ypaint primitives — no IO, no GPU, no
 * terminal interaction. The frontend tool drives time, owns the buffer, and
 * ships it out via OSC.
 */

#include <stdbool.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yzoo;

YETTY_YRESULT_DECLARE(yetty_yzoo_ptr, struct yetty_yzoo *);

struct yetty_yzoo_config {
    uint32_t target_points;       /* live control points to maintain */
    uint32_t target_conns_per_cp; /* desired connections per CP */
    float growth_rate;            /* exponential outward drift rate */
    float spawn_radius_min;       /* initial radius range when spawning */
    float spawn_radius_max;
    float cp_marker_size;     /* base CP marker radius (grows with age) */
    float max_connection_dist; /* drop connections beyond this */
    float stroke_min;          /* connection stroke-width range */
    float stroke_max;
    float curve_ratio;         /* fraction of connections rendered as curves */
    uint32_t bezier_segments;  /* line-segment count per bezier approximation */
    uint32_t bg_color;         /* 0xAARRGGBB; advisory — frontend may ignore */
    float scene_width;
    float scene_height;
};

struct yetty_yzoo_config yetty_yzoo_config_default(void);

/* `seed = 0` → seed from CLOCK_MONOTONIC. */
struct yetty_yzoo_ptr_result yetty_yzoo_create(const struct yetty_yzoo_config *config,
                                               uint32_t seed);

void yetty_yzoo_destroy(struct yetty_yzoo *zoo);

struct yetty_ycore_void_result yetty_yzoo_set_scene_size(struct yetty_yzoo *zoo,
                                                         float scene_width, float scene_height);

/* Spawn/cull/connect, then write all primitives for the current time into
 * `buf`. The buffer is cleared and its scene bounds are set to
 * (0, 0, scene_width, scene_height). */
struct yetty_ycore_void_result yetty_yzoo_render(struct yetty_yzoo *zoo,
                                                 struct yetty_ydraw_core_buffer *buf, float time);

const struct yetty_yzoo_config *yetty_yzoo_config_get(const struct yetty_yzoo *zoo);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YZOO_YZOO_H */
