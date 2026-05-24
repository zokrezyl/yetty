/*
 * 08_glyph_shaders — compile every yfont glyph-shader through the
 * bridge.
 *
 * Reads the .wgsl files under src/yetty/yfont/glyph-shaders/, wraps
 * each one in a self-contained vertex + fragment program, and feeds
 * the source to wgpuDeviceCreateShaderModule via the bridge. The
 * server-side dispatcher calls the real Dawn entry point — if the
 * WGSL compiles, we get back a non-zero shader-module handle.
 *
 * Exercises:
 *   - WGPUShaderModuleDescriptor.nextInChain → WGPUShaderSourceWGSL
 *     (chain encoding with length-prefixed sType payloads)
 *   - large stringview transmit (each wrapped shader is ~1–4 KB)
 *   - many sequential CMD/REPLY round-trips
 *
 * Run via:  ./yetty -e demo-yrdawn-08-glyph-shaders
 * Trace at: /tmp/yrdawn-demo-08-glyph-shaders.trace
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <webgpu/webgpu.h>
#include <yetty/yrdawn/client.h>
#include <yetty/yrdawn/methods.gen.h>

#include "common.h"

#ifndef YETTY_GLYPH_SHADERS_DIR
#error "YETTY_GLYPH_SHADERS_DIR must be defined by CMake"
#endif

static int s_adapter_done, s_device_done;
static uint32_t s_adapter_status, s_device_status;

static void on_adapter(void *u, uint32_t status, uint32_t mid,
                       const uint8_t *body, size_t body_len)
{
    (void)u; (void)mid; (void)body; (void)body_len;
    s_adapter_status = status;
    s_adapter_done = 1;
}

static void on_device(void *u, uint32_t status, uint32_t mid,
                      const uint8_t *body, size_t body_len)
{
    (void)u; (void)mid; (void)body; (void)body_len;
    s_device_status = status;
    s_device_done = 1;
}

static char *slurp(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1u);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len) *out_len = got;
    return buf;
}

/* Wrap one glyph body (shader_glyph_N) into a self-contained module:
 * fullscreen-triangle vertex + fragment that calls the body. The util
 * lib is inlined because glyph bodies under glyph-shaders/ may call
 * util_*. Time + colors are passed via push-style uniforms.
 *
 * Note: each glyph body has its own local id encoded in the function
 * name (shader_glyph_0, shader_glyph_1, …). Discover the id from the
 * body source so we can wire the fragment shader's call site. */
static int find_glyph_id(const char *body, uint32_t *out_id)
{
    const char *p = strstr(body, "fn shader_glyph_");
    if (!p) return 0;
    p += strlen("fn shader_glyph_");
    char *end = NULL;
    unsigned long v = strtoul(p, &end, 10);
    if (!end || end == p) return 0;
    *out_id = (uint32_t)v;
    return 1;
}

static char *build_wgsl(const char *util_src, const char *body_src,
                        uint32_t glyph_id, size_t *out_len)
{
    /* The glyph bodies sit inside yetty's shader-glyph-layer, which
     * binds a giant `uniforms` block with grid_size, cell_size, time,
     * zoom, etc. The bodies are free to reach into that block as
     * globals — see e.g. 0x00f5-butterfly-flock.wgsl. Standalone
     * compilation has to satisfy those references or Dawn rejects
     * the module (and the layer's uncaptured-error callback FATAL-
     * exits yetty). We declare a private-scope `uniforms` that
     * satisfies every field the bodies actually reach for; default-
     * initialised to zero is fine — this demo only compiles, it
     * doesn't run the pipeline. */
    static const char wrapper[] =
        "struct ShaderGlyphUniforms {\n"
        "    shader_glyph_grid_size: vec2<f32>,\n"
        "    shader_glyph_cell_size: vec2<f32>,\n"
        "};\n"
        "var<private> uniforms: ShaderGlyphUniforms;\n"
        "struct Uniforms {\n"
        "    time: f32,\n"
        "    _pad0: f32, _pad1: f32, _pad2: f32,\n"
        "    fg: vec3<f32>, _pad3: f32,\n"
        "    bg: vec3<f32>, _pad4: f32,\n"
        "};\n"
        "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
        "\n"
        "struct VsOut {\n"
        "    @builtin(position) pos: vec4<f32>,\n"
        "    @location(0) uv: vec2<f32>,\n"
        "};\n"
        "\n"
        "@vertex\n"
        "fn vs_main(@builtin(vertex_index) vid: u32) -> VsOut {\n"
        "    var p = array<vec2<f32>, 3>(\n"
        "        vec2<f32>(-1.0, -3.0),\n"
        "        vec2<f32>(-1.0,  1.0),\n"
        "        vec2<f32>( 3.0,  1.0),\n"
        "    );\n"
        "    var out: VsOut;\n"
        "    out.pos = vec4<f32>(p[vid], 0.0, 1.0);\n"
        "    out.uv  = (p[vid] * 0.5 + vec2<f32>(0.5, 0.5));\n"
        "    return out;\n"
        "}\n"
        "\n"
        "@fragment\n"
        "fn fs_main(in: VsOut) -> @location(0) vec4<f32> {\n"
        "    let pixel_pos = in.uv * 32.0;\n"
        "    let rgb = shader_glyph_%u(in.uv, u.time, u.fg, u.bg, pixel_pos);\n"
        "    return vec4<f32>(rgb, 1.0);\n"
        "}\n";

    size_t util_len = strlen(util_src);
    size_t body_len = strlen(body_src);
    size_t cap = util_len + body_len + sizeof(wrapper) + 64;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    int n = snprintf(buf, cap, "%s\n%s\n", util_src, body_src);
    if (n < 0) { free(buf); return NULL; }
    /* Append the wrapper with the glyph_id wired into the call. */
    char tail[2048];
    int m = snprintf(tail, sizeof(tail), wrapper, (unsigned)glyph_id);
    if (m < 0 || (size_t)m >= sizeof(tail)) { free(buf); return NULL; }
    if ((size_t)(n + m) >= cap) { free(buf); return NULL; }
    memcpy(buf + n, tail, (size_t)m);
    buf[n + m] = '\0';
    if (out_len) *out_len = (size_t)(n + m);
    return buf;
}

static int cmp_name(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

int main(void)
{
    demo_raw_stdin();
    FILE *trace = demo_trace_open("08-glyph-shaders");
#define LOG(...) do { if (trace) fprintf(trace, __VA_ARGS__); } while (0)

    struct yetty_yrdawn_client *c = NULL;
    struct yetty_yrdawn_canvas *canvas = demo_bringup_single_canvas(/*figure_id=*/1, trace, &c);
    if (!canvas) {
        LOG("08_glyph_shaders: bringup failed\n");
        return 1;
    }
    LOG("08_glyph_shaders: connected=%d\n", yetty_yrdawn_canvas_connected(canvas));

    uint64_t instance = yrdawn_client_wgpuCreateInstance(c);
    uint64_t adapter  = yrdawn_client_wgpuInstanceRequestAdapter(c, instance, on_adapter, NULL);
    for (int i = 0; i < 300 && !s_adapter_done; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }
    if (s_adapter_status != 0) { LOG("adapter status=%u\n", s_adapter_status); goto cleanup; }
    uint64_t device = yrdawn_client_wgpuAdapterRequestDevice(c, adapter, on_device, NULL);
    for (int i = 0; i < 300 && !s_device_done; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }
    if (s_device_status != 0) { LOG("device status=%u\n", s_device_status); goto cleanup; }

    /* Load util.wgsl once — every glyph references util_* functions. */
    char util_path[1024];
    snprintf(util_path, sizeof(util_path), "%s/_util.wgsl", YETTY_GLYPH_SHADERS_DIR);
    size_t util_len = 0;
    char *util_src = slurp(util_path, &util_len);
    if (!util_src) {
        LOG("08_glyph_shaders: failed to read %s\n", util_path);
        goto cleanup_device;
    }
    LOG("08_glyph_shaders: loaded _util.wgsl (%zu bytes)\n", util_len);

    /* Enumerate glyph shader files (0xNNNN-name.wgsl). */
    DIR *d = opendir(YETTY_GLYPH_SHADERS_DIR);
    if (!d) {
        LOG("08_glyph_shaders: opendir %s failed\n", YETTY_GLYPH_SHADERS_DIR);
        free(util_src);
        goto cleanup_device;
    }
    char *names[256];
    int n_names = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && n_names < (int)(sizeof(names) / sizeof(names[0]))) {
        const char *nm = de->d_name;
        if (nm[0] != '0' || nm[1] != 'x') continue;
        size_t L = strlen(nm);
        if (L < 6 || strcmp(nm + L - 5, ".wgsl") != 0) continue;
        names[n_names++] = strdup(nm);
    }
    closedir(d);
    qsort(names, (size_t)n_names, sizeof(names[0]), cmp_name);
    LOG("08_glyph_shaders: %d shader files to compile\n", n_names);

    int ok = 0, fail = 0;
    for (int i = 0; i < n_names && !demo_quit_flag; ++i) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", YETTY_GLYPH_SHADERS_DIR, names[i]);
        size_t body_len = 0;
        char *body = slurp(path, &body_len);
        if (!body) { LOG("  [%2d] %s — slurp failed\n", i, names[i]); ++fail; continue; }

        uint32_t glyph_id = 0;
        if (!find_glyph_id(body, &glyph_id)) {
            LOG("  [%2d] %s — no shader_glyph_N body found\n", i, names[i]);
            free(body); ++fail; continue;
        }

        size_t wgsl_len = 0;
        char *wgsl = build_wgsl(util_src, body, glyph_id, &wgsl_len);
        free(body);
        if (!wgsl) { LOG("  [%2d] %s — wrap failed\n", i, names[i]); ++fail; continue; }

        WGPUShaderSourceWGSL src_wgsl = {0};
        src_wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        src_wgsl.code = (WGPUStringView){wgsl, wgsl_len};

        WGPUShaderModuleDescriptor desc = {0};
        desc.nextInChain = &src_wgsl.chain;
        desc.label = (WGPUStringView){names[i], strlen(names[i])};

        uint64_t mod = yrdawn_client_wgpuDeviceCreateShaderModule(c, device, &desc);
        if (mod) {
            ++ok;
            LOG("  [%2d] %s (id=0x%04x, %zu bytes WGSL) → module=%lu\n",
                i, names[i], glyph_id, wgsl_len, (unsigned long)mod);
            (void)yrdawn_client_wgpuShaderModuleRelease(c, mod);
        } else {
            ++fail;
            LOG("  [%2d] %s (id=0x%04x) → COMPILE FAILED\n", i, names[i], glyph_id);
        }
        free(wgsl);
    }
    LOG("08_glyph_shaders: ok=%d fail=%d total=%d\n", ok, fail, n_names);

    for (int i = 0; i < n_names; ++i) free(names[i]);
    free(util_src);

cleanup_device:
    (void)yrdawn_client_wgpuDeviceRelease(c, device);
cleanup:
    (void)yrdawn_client_wgpuAdapterRelease(c, adapter);
    (void)yrdawn_client_wgpuInstanceRelease(c, instance);
    (void)yetty_yrdawn_canvas_destroy(canvas);
    (void)yetty_yrdawn_client_destroy(c);
    LOG("08_glyph_shaders: done\n");
    if (trace) fclose(trace);
    return 0;
}
