#ifndef YETTY_YSHADERTOY_PRIM_H
#define YETTY_YSHADERTOY_PRIM_H

/*
 * yshadertoy prim — Shadertoy-style shader as a ydraw complex prim.
 *
 * The low-level sibling of the yshadertoy *figure* (figure.h): instead of
 * the positioned-figure compositor wire, the shader rides the ordinary
 * YDRAW_BIN drawable-list channel as a complex prim (same tier as yplot /
 * yimage / yvideo), so it anchors at the cursor and scrolls with the
 * terminal content. ycat's shadertoy handler emits it; the scrolling
 * canvas registers the receiving factory.
 *
 * Wire payload layout (after the u32 type_id | u32 payload_size header):
 *   f32 bounds_x, bounds_y, bounds_w, bounds_h   (standard complex AABB)
 *   u32 flags                                    (reserved, 0)
 *   u32 wgsl_len                                 (byte length, unpadded)
 *   u8  wgsl[wgsl_len]                           (padded to a u32 boundary)
 *
 * The WGSL text is the user shader only — a `mainImage` function with the
 * same contract the yshadertoy figure uses:
 *
 *   fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,
 *                iTime: f32, iMouse: vec4<f32>) -> vec4<f32> { ... }
 *
 * fragCoord is in pixels, origin at the bottom-left of the prim's rect
 * (Shadertoy convention). The receiving factory wraps it with a
 * fullscreen-triangle vertex shader and a bounds-clipping fragment shader,
 * then compiles a per-instance pipeline (the payload IS the shader, so no
 * factory-shared pipeline exists for this type).
 *
 * Split:
 *   - serializer + drawable-list builder (this header's first half) are
 *     CPU-only — lib yetty_yshadertoy_core, safe for client tools (ycat).
 *   - the factory (second half) pulls WebGPU — lib yetty_yshadertoy_prim,
 *     linked by the scrolling canvas.
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Complex-prim type id — see the tier ranges in ydraw-core/complex.h.
 * 0x80000003..6 are yplot / yimage / ymesh / yvideo. */
#define YETTY_YSHADERTOY_PRIM_TYPE_ID 0x80000007u

/*=============================================================================
 * Client side — wire serializer + drawable-list builder (CPU-only).
 *===========================================================================*/

struct yetty_yshadertoy_prim_config {
    float bounds_x;
    float bounds_y;
    float bounds_w;
    float bounds_h;
};

/* Total record size (header + payload) for a shader of wgsl_len bytes. */
size_t yetty_yshadertoy_prim_serialized_size(size_t wgsl_len);

/* Pack config + shader text into `out`. Returns bytes written. */
struct yetty_ycore_size_result yetty_yshadertoy_prim_serialize(
    const struct yetty_yshadertoy_prim_config *config, const char *wgsl_source, size_t wgsl_len,
    uint8_t *out, size_t out_capacity);

/* Build a drawable list holding one yshadertoy prim. Pass wgsl_len == 0 to
 * ship the receiver's built-in default shader (animated gradient). Returned
 * list ownership transfers to the caller. */
struct yetty_ydraw_drawable_list_result yetty_yshadertoy_prim_render(
    const char *wgsl_source, size_t wgsl_len, const struct yetty_yshadertoy_prim_config *config);

/*=============================================================================
 * Server side — concrete factory (GPU; lib yetty_yshadertoy_prim).
 *===========================================================================*/

struct yetty_ydraw_concrete_factory;

/* Mint the factory for registration with a complex factory. Unlike the
 * sibling prim types there is no shared pipeline — each instance compiles
 * its own from the wire's WGSL text. */
struct yetty_ydraw_concrete_factory *yetty_yshadertoy_prim_factory_create(void);

void yetty_yshadertoy_prim_factory_destroy(struct yetty_ydraw_concrete_factory *self);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YSHADERTOY_PRIM_H */
