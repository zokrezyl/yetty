/*
 * demo-shaders.c — definitions for the built-in yshadertoy gallery.
 * See <yetty/yshadertoy/demo-shaders.h>. Adapted from the shader-glyph
 * fractal/complex set; inputs rewired to the yshadertoy uniforms.
 */
#include <yetty/yshadertoy/demo-shaders.h>

/* Mandelbrot — glyphs/0xeffd-mandelbrot.wgsl. Mouse pans the centre;
 * time oscillates the zoom. */
static const char SHADER_MANDELBROT[] =
    "fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,\n"
    "             iTime: f32, iMouse: vec4<f32>) -> vec4<f32> {\n"
    "    let screenSize = iResolution.xy;\n"
    "    let screenUV = fragCoord / screenSize;\n"
    "    let mouseUV = clamp(iMouse.xy / screenSize, vec2<f32>(0.0), vec2<f32>(1.0));\n"
    "    let centerX = -0.5 + (mouseUV.x - 0.5);\n"
    "    let centerY = (mouseUV.y - 0.5);\n"
    "    let zoomLevel = 2.0 + 1.5 * sin(iTime * 0.2);\n"
    "    let aspect = screenSize.x / screenSize.y;\n"
    "    let c = vec2<f32>((screenUV.x - 0.5) * zoomLevel * aspect + centerX,\n"
    "                      (screenUV.y - 0.5) * zoomLevel + centerY);\n"
    "    var z = vec2<f32>(0.0, 0.0);\n"
    "    var iterations = 0u;\n"
    "    let maxIterations = 100u;\n"
    "    for (var i = 0u; i < maxIterations; i++) {\n"
    "        z = vec2<f32>(z.x * z.x - z.y * z.y + c.x, 2.0 * z.x * z.y + c.y);\n"
    "        if (dot(z, z) > 4.0) { break; }\n"
    "        iterations++;\n"
    "    }\n"
    "    if (iterations == maxIterations) { return vec4<f32>(0.0, 0.0, 0.1, 1.0); }\n"
    "    let smooth_iter = f32(iterations) - log2(log2(dot(z, z)));\n"
    "    let t = smooth_iter / f32(maxIterations);\n"
    "    let col = vec3<f32>(0.5 + 0.5 * sin(3.0 + t * 6.28318 * 2.0),\n"
    "                        0.5 + 0.5 * sin(3.0 + t * 6.28318 * 2.0 + 2.094),\n"
    "                        0.5 + 0.5 * sin(3.0 + t * 6.28318 * 2.0 + 4.188));\n"
    "    return vec4<f32>(col, 1.0);\n"
    "}\n";

/* Julia set — glyphs/0xeffc-julia.wgsl. Mouse sets the Julia constant. */
static const char SHADER_JULIA[] =
    "fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,\n"
    "             iTime: f32, iMouse: vec4<f32>) -> vec4<f32> {\n"
    "    let screenSize = iResolution.xy;\n"
    "    let screenUV = fragCoord / screenSize;\n"
    "    let mouseUV = clamp(iMouse.xy / screenSize, vec2<f32>(0.0), vec2<f32>(1.0));\n"
    "    let c = vec2<f32>((mouseUV.x - 0.5) * 1.6, (mouseUV.y - 0.5) * 1.6);\n"
    "    let zoomLevel = 2.5 + 0.5 * sin(iTime * 0.3);\n"
    "    let aspect = screenSize.x / screenSize.y;\n"
    "    var z = vec2<f32>((screenUV.x - 0.5) * zoomLevel * aspect,\n"
    "                      (screenUV.y - 0.5) * zoomLevel);\n"
    "    var iterations = 0u;\n"
    "    let maxIterations = 80u;\n"
    "    for (var i = 0u; i < maxIterations; i++) {\n"
    "        z = vec2<f32>(z.x * z.x - z.y * z.y + c.x, 2.0 * z.x * z.y + c.y);\n"
    "        if (dot(z, z) > 4.0) { break; }\n"
    "        iterations++;\n"
    "    }\n"
    "    if (iterations == maxIterations) { return vec4<f32>(0.1, 0.0, 0.0, 1.0); }\n"
    "    let smooth_iter = f32(iterations) - log2(log2(dot(z, z)));\n"
    "    let t = smooth_iter / f32(maxIterations);\n"
    "    let col = vec3<f32>(0.5 + 0.5 * sin(t * 6.28318 * 3.0),\n"
    "                        0.5 + 0.5 * sin(t * 6.28318 * 3.0 + 1.0),\n"
    "                        0.5 + 0.5 * sin(t * 6.28318 * 3.0 + 2.5));\n"
    "    return vec4<f32>(col, 1.0);\n"
    "}\n";

/* Voronoi distances — glyphs/0xeff9-voronoi.wgsl (Inigo Quilez, MIT).
 * Helper functions kept verbatim. */
static const char SHADER_VORONOI[] =
    "fn voronoi_hash2(p: vec2<f32>) -> vec2<f32> {\n"
    "    return fract(sin(vec2<f32>(dot(p, vec2<f32>(127.1, 311.7)),\n"
    "                               dot(p, vec2<f32>(269.5, 183.3)))) * 43758.5453);\n"
    "}\n"
    "fn voronoi_calc(x: vec2<f32>, time: f32) -> vec3<f32> {\n"
    "    let ip = floor(x);\n"
    "    let fp = fract(x);\n"
    "    var mg: vec2<f32>;\n"
    "    var mr: vec2<f32>;\n"
    "    var md = 8.0;\n"
    "    for (var j = -1; j <= 1; j++) {\n"
    "        for (var i = -1; i <= 1; i++) {\n"
    "            let g = vec2<f32>(f32(i), f32(j));\n"
    "            var o = voronoi_hash2(ip + g);\n"
    "            o = 0.5 + 0.5 * sin(time + 6.2831 * o);\n"
    "            let r = g + o - fp;\n"
    "            let d = dot(r, r);\n"
    "            if (d < md) { md = d; mr = r; mg = g; }\n"
    "        }\n"
    "    }\n"
    "    md = 8.0;\n"
    "    for (var j = -2; j <= 2; j++) {\n"
    "        for (var i = -2; i <= 2; i++) {\n"
    "            let g = mg + vec2<f32>(f32(i), f32(j));\n"
    "            var o = voronoi_hash2(ip + g);\n"
    "            o = 0.5 + 0.5 * sin(time + 6.2831 * o);\n"
    "            let r = g + o - fp;\n"
    "            if (dot(mr - r, mr - r) > 0.00001) {\n"
    "                md = min(md, dot(0.5 * (mr + r), normalize(r - mr)));\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    return vec3<f32>(md, mr);\n"
    "}\n"
    "fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,\n"
    "             iTime: f32, iMouse: vec4<f32>) -> vec4<f32> {\n"
    "    let p = fragCoord / iResolution.x;\n"
    "    let c = voronoi_calc(8.0 * p, iTime);\n"
    "    var col = c.x * (0.5 + 0.5 * sin(64.0 * c.x)) * vec3<f32>(1.0);\n"
    "    col = mix(vec3<f32>(1.0, 0.6, 0.0), col, smoothstep(0.04, 0.07, c.x));\n"
    "    let dd = length(c.yz);\n"
    "    col = mix(vec3<f32>(1.0, 0.6, 0.1), col, smoothstep(0.0, 0.12, dd));\n"
    "    col = col + vec3<f32>(1.0, 0.6, 0.1) * (1.0 - smoothstep(0.0, 0.04, dd));\n"
    "    return vec4<f32>(col, 1.0);\n"
    "}\n";

/* Plasma — glyphs/0x0007-plasma.wgsl. */
static const char SHADER_PLASMA[] =
    "fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,\n"
    "             iTime: f32, iMouse: vec4<f32>) -> vec4<f32> {\n"
    "    let p = fragCoord * 0.02;\n"
    "    let t = iTime * 0.5;\n"
    "    var v = 0.0;\n"
    "    v += sin(p.x + t);\n"
    "    v += sin(p.y + t * 0.5);\n"
    "    v += sin(p.x + p.y + t * 0.3);\n"
    "    v += sin(sqrt(p.x * p.x + p.y * p.y) * 0.5 + t);\n"
    "    v = v / 4.0;\n"
    "    let col = vec3<f32>(sin(v * 3.14159) * 0.5 + 0.5,\n"
    "                        sin(v * 3.14159 + 2.094) * 0.5 + 0.5,\n"
    "                        sin(v * 3.14159 + 4.188) * 0.5 + 0.5);\n"
    "    return vec4<f32>(col, 1.0);\n"
    "}\n";

/* Spiral — a simple hand-written sanity shader. */
static const char SHADER_SPIRAL[] =
    "fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,\n"
    "             iTime: f32, iMouse: vec4<f32>) -> vec4<f32> {\n"
    "    let uv = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;\n"
    "    let d = length(uv);\n"
    "    let a = atan2(uv.y, uv.x);\n"
    "    let v = 0.5 + 0.5 * sin(a * 6.0 + iTime * 2.0 - d * 10.0);\n"
    "    let col = vec3<f32>(0.5 + 0.5 * cos(iTime + uv.x),\n"
    "                        0.5 + 0.5 * cos(iTime + uv.y + 2.0), v);\n"
    "    return vec4<f32>(col, 1.0);\n"
    "}\n";

const struct yetty_yshadertoy_demo_shader yetty_yshadertoy_demo_shaders[] = {
    {"Mandelbrot", SHADER_MANDELBROT}, {"Julia", SHADER_JULIA}, {"Voronoi", SHADER_VORONOI},
    {"Plasma", SHADER_PLASMA},         {"Spiral", SHADER_SPIRAL},
};

const int yetty_yshadertoy_demo_shader_count =
    (int)(sizeof(yetty_yshadertoy_demo_shaders) / sizeof(yetty_yshadertoy_demo_shaders[0]));
