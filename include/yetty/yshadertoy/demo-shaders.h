/*
 * demo-shaders.h — a small built-in gallery of Shadertoy-style WGSL
 * shaders, adapted from yetty's shader-glyph set (the fractal / complex
 * ones, src/yetty/yshaders/glyphs/0xeff*.wgsl) with their inputs rewired
 * to the yshadertoy uniforms: pixelPos -> fragCoord,
 * globals.screen{Width,Height} -> iResolution.xy, mousePos -> iMouse.xy,
 * time -> iTime.
 *
 * Shared by the ygui yshadertoy demo (demo/ygui/41_yshadertoy) and the
 * ygreeter "Shadertoy" tab so the same gallery is defined exactly once.
 * Each entry's `wgsl` defines:
 *   fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,
 *                iTime: f32, iMouse: vec4<f32>) -> vec4<f32>
 */
#ifndef YETTY_YSHADERTOY_DEMO_SHADERS_H
#define YETTY_YSHADERTOY_DEMO_SHADERS_H

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yshadertoy_demo_shader {
    const char *label;
    const char *wgsl;
};

/* NUL-terminated label per entry; `count` entries total. Stable for the
 * lifetime of the process (static storage). */
extern const struct yetty_yshadertoy_demo_shader yetty_yshadertoy_demo_shaders[];
extern const int yetty_yshadertoy_demo_shader_count;

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YSHADERTOY_DEMO_SHADERS_H */
