/* GENERATED — do not edit. */
/* Public interface for regular class(es) `figure` (module: yshadertoy).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YSHADERTOY_FIGURE_H
#define YETTY_YCLASSGEN_YSHADERTOY_FIGURE_H

#include <yetty/yclass/class.h>
#include <yetty/yshadertoy/methods.h>

struct yetty_yclass_ptr_result yetty_yshadertoy_figure_class_get(void);

/* Header-destined content for the generated figure.h (skipped by the real build, which takes it from that header). */
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * yshadertoy figure — a Shadertoy-style shader rendered as a compositor
 * figure. Subclass of yetty_yfigure_figure. Paints its whole rect with a
 * single full-screen quad whose fragment shader runs a user-supplied
 * WGSL `mainImage(...)`, with the usual Shadertoy inputs injected as
 * uniforms (iResolution, iTime, iTimeDelta, iFrame, iMouse).
 *
 * Unlike yterm's shader-glyph figure, this is decoupled from the text
 * grid: it takes its shader text as an argument. Created either directly
 * via yetty_yshadertoy_create() or minted from the figure
 * registry under YETTY_YFIGURE_KIND_YSHADERTOY (the ygui yshadertoy
 * widget path), in which case the shader text arrives as the
 * CREATE_CHILD init payload via the figure's process_bytes.
 *
 * Contract for the user shader text (WGSL):
 *   fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,
 *                iTime: f32, iMouse: vec4<f32>) -> vec4<f32> { ... }
 *
 * fragCoord is in pixels, origin bottom-left of the figure rect.
 * iTimeDelta / iFrame are also available as uniforms.yshadertoy_iTimeDelta
 * and uniforms.yshadertoy_iFrame.
 */

struct yetty_yshadertoy_figure;

YETTY_YRESULT_DECLARE(yetty_yshadertoy_figure_ptr, struct yetty_yshadertoy_figure *);

/* Create a yshadertoy figure directly (in-process). `shader_src` is the
 * user WGSL mainImage body; pass NULL to start with the default gradient
 * (set the real source later via _set_source). `rect` is absolute pixel
 * space within the parent container. The figure reaches the GPU, event
 * loop (for its anim timer + repaint nudge) and config through `context`.
 *
 * (Named _create on the module, not the class, so it doesn't collide
 * with the codegen-emitted yclass object accessor of the same shape.) */
struct yetty_yshadertoy_figure_ptr_result yetty_yshadertoy_create(
    struct yetty_ycore_rectangle rect, const char *shader_src, size_t shader_len,
    const struct yetty_context *context);

/* Upcast. Stable pointer. */
struct yetty_yfigure_figure *yetty_yshadertoy_as_figure(struct yetty_yshadertoy_figure *f);

/* Replace the shader text and force a recompile on the next render. */
struct yetty_ycore_void_result yetty_yshadertoy_set_source(struct yetty_yshadertoy_figure *f,
                                                           const char *shader_src,
                                                           size_t shader_len);

/* Register the yshadertoy factory under YETTY_YFIGURE_KIND_YSHADERTOY.
 * No args bundle: the registry hands the host context to the factory at
 * mint time. Call from yframework's register_figure_factories (the same
 * place ymgui/yrdawn register), once per host registry. */
struct yetty_ycore_void_result yetty_yshadertoy_register_factory(
    struct yetty_yfigure_registry *registry);

#ifdef __cplusplus
}
#endif

#endif
