/*
 * 10_glyph_animate — actually animate yfont glyph-shaders on screen
 * through the bridge.
 *
 * Pipeline per glyph:
 *   load _util.wgsl + glyph body + wrap with vs/fs (time uniform)
 *   CreateShaderModule → CreateRenderPipeline → GetBindGroupLayout
 *   → CreateBindGroup against a 16-byte uniform buffer
 *
 * Per frame:
 *   QueueWriteBuffer(ubo, time)
 *   CreateCommandEncoder → BeginRenderPass(Clear) → SetPipeline
 *   → SetBindGroup → Draw(3) → End → ReleaseRenderPass
 *   CopyTextureToBuffer → Finish → QueueSubmit
 *   BufferMapAsync (WaitAny in the dispatch fires the trampoline)
 *   BufferReadMappedRange (server-fills, REPLY ships bytes back)
 *   BufferUnmap
 *   yetty_yrdawn_client_present_frame
 *   Release CommandBuffer + Encoder
 *
 * Cycles through a handful of visually distinct glyphs; each runs for
 * FRAMES_PER_GLYPH frames. Press 'q' in the host yetty to exit.
 *
 * Run via:  ./yetty -e demo-yrdawn-10-glyph-animate
 * Trace at: /tmp/yrdawn-demo-10-glyph-animate.trace
 */
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

enum { W = 256, H = 256 };
enum { FRAMES_PER_GLYPH = 90, FRAME_INTERVAL_MS = 33 };

static const char *const GLYPH_FILES[] = {
    "0x0000-spinner.wgsl",
    "0x0001-pulse.wgsl",
    "0x0002-wave.wgsl",
    "0x0007-plasma.wgsl",
    "0x0008-ripple.wgsl",
    "0x000b-sparkle.wgsl",
    "0x000c-star.wgsl",
    "0x001a-radar-sweep.wgsl",
    "0x001c-dna-helix.wgsl",
    "0x001d-equalizer.wgsl",
    "0x0021-rain.wgsl",
    "0x0022-orbit.wgsl",
    "0x0024-matrix-rain.wgsl",
    "0x0025-metaballs.wgsl",
    "0x0029-vinyl.wgsl",
};
enum { GLYPH_COUNT = sizeof(GLYPH_FILES) / sizeof(GLYPH_FILES[0]) };

static int s_adapter_done, s_device_done;
static uint32_t s_adapter_status, s_device_status;
static int s_map_done;
static uint32_t s_map_payload;

static void on_adapter(void *u, uint32_t st, uint32_t mid, const uint8_t *b, size_t bl)
{ (void)u;(void)mid;(void)b;(void)bl; s_adapter_status = st; s_adapter_done = 1; }
static void on_device(void *u, uint32_t st, uint32_t mid, const uint8_t *b, size_t bl)
{ (void)u;(void)mid;(void)b;(void)bl; s_device_status = st; s_device_done = 1; }
static void on_map(void *u, uint32_t st, uint32_t mid, const uint8_t *b, size_t bl)
{
    (void)u; (void)mid; (void)st;
    s_map_payload = (bl >= sizeof(uint32_t) && b) ? *(const uint32_t *)b : 0xFFFFFFFFu;
    s_map_done = 1;
}

static char *slurp(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1u);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len) *out_len = got;
    return buf;
}

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
    static const char wrapper[] =
        "struct U { time: f32, _p0: f32, _p1: f32, _p2: f32 };\n"
        "@group(0) @binding(0) var<uniform> u: U;\n"
        "struct VsOut { @builtin(position) pos: vec4<f32>, @location(0) uv: vec2<f32> };\n"
        "@vertex fn vs_main(@builtin(vertex_index) vid: u32) -> VsOut {\n"
        "  var p = array<vec2<f32>,3>(vec2<f32>(-1.0,-3.0),"
        "                              vec2<f32>(-1.0, 1.0),"
        "                              vec2<f32>( 3.0, 1.0));\n"
        "  var o: VsOut; o.pos = vec4<f32>(p[vid],0.0,1.0);"
        "  o.uv = p[vid] * 0.5 + vec2<f32>(0.5,0.5); return o;\n"
        "}\n"
        "@fragment fn fs_main(in: VsOut) -> @location(0) vec4<f32> {\n"
        "  let fg = vec3<f32>(0.42, 0.66, 0.57);\n"
        "  let bg = vec3<f32>(0.04, 0.06, 0.08);\n"
        "  let rgb = shader_glyph_%u(in.uv, u.time, fg, bg, in.uv * 256.0);\n"
        "  return vec4<f32>(rgb, 1.0);\n"
        "}\n";
    size_t ul = strlen(util_src), bl = strlen(body_src);
    size_t cap = ul + bl + sizeof(wrapper) + 64;
    char *buf = (char *)malloc(cap);
    int n = snprintf(buf, cap, "%s\n%s\n", util_src, body_src);
    char tail[2048];
    int m = snprintf(tail, sizeof(tail), wrapper, (unsigned)glyph_id);
    memcpy(buf + n, tail, (size_t)m + 1);
    if (out_len) *out_len = (size_t)(n + m);
    return buf;
}

/* Per-glyph GPU resources. Rebuilt when the demo cycles to the next
 * shader; the texture/view/buffers stay across cycles because their
 * shape doesn't depend on the shader. */
struct glyph_rt {
    uint64_t module;
    uint64_t pipeline;
    uint64_t bgl;
    uint64_t bg;
};

static int load_glyph(struct yetty_yrdawn_client *c, uint64_t device, uint64_t ubo,
                      const char *util_src, const char *file_name,
                      struct glyph_rt *out, FILE *trace)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", YETTY_GLYPH_SHADERS_DIR, file_name);
    size_t body_len = 0;
    char *body = slurp(path, &body_len);
    if (!body) { if (trace) fprintf(trace, "10: slurp %s failed\n", file_name); return 0; }
    uint32_t gid = 0;
    if (!find_glyph_id(body, &gid)) { free(body); return 0; }
    size_t wgsl_len = 0;
    char *wgsl = build_wgsl(util_src, body, gid, &wgsl_len);
    free(body);

    WGPUShaderSourceWGSL src_wgsl = {0};
    src_wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    src_wgsl.code = (WGPUStringView){wgsl, wgsl_len};
    WGPUShaderModuleDescriptor smd = {0};
    smd.nextInChain = &src_wgsl.chain;
    smd.label = (WGPUStringView){file_name, strlen(file_name)};
    out->module = yrdawn_client_wgpuDeviceCreateShaderModule(c, device, &smd);
    free(wgsl);

    WGPUColorTargetState target = {0};
    target.format = WGPUTextureFormat_RGBA8Unorm;
    target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs = {0};
    fs.module = (WGPUShaderModule)out->module;
    fs.entryPoint = (WGPUStringView){"fs_main", 7};
    fs.targetCount = 1;
    fs.targets = &target;
    WGPURenderPipelineDescriptor pd = {0};
    pd.vertex.module = (WGPUShaderModule)out->module;
    pd.vertex.entryPoint = (WGPUStringView){"vs_main", 7};
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.multisample.count = 1;
    pd.multisample.mask = 0xFFFFFFFFu;
    pd.fragment = &fs;
    out->pipeline = yrdawn_client_wgpuDeviceCreateRenderPipeline(c, device, &pd);

    out->bgl = yrdawn_client_wgpuRenderPipelineGetBindGroupLayout(c, out->pipeline, 0);
    WGPUBindGroupEntry entry = {0};
    entry.binding = 0;
    entry.buffer = (WGPUBuffer)ubo;
    entry.offset = 0;
    entry.size = 16;
    WGPUBindGroupDescriptor bgd = {0};
    bgd.layout = (WGPUBindGroupLayout)out->bgl;
    bgd.entryCount = 1;
    bgd.entries = &entry;
    out->bg = yrdawn_client_wgpuDeviceCreateBindGroup(c, device, &bgd);

    if (trace) fprintf(trace, "10: loaded %s gid=0x%04x module=%lu pipeline=%lu\n",
                       file_name, gid, (unsigned long)out->module,
                       (unsigned long)out->pipeline);
    return 1;
}

static void unload_glyph(struct yetty_yrdawn_client *c, struct glyph_rt *g)
{
    if (g->bg)       (void)yrdawn_client_wgpuBindGroupRelease(c, g->bg);
    if (g->bgl)      (void)yrdawn_client_wgpuBindGroupLayoutRelease(c, g->bgl);
    if (g->pipeline) (void)yrdawn_client_wgpuRenderPipelineRelease(c, g->pipeline);
    if (g->module)   (void)yrdawn_client_wgpuShaderModuleRelease(c, g->module);
    memset(g, 0, sizeof(*g));
}

/* Render one frame at `time`, read pixels back, present. Returns 1 on
 * success, 0 if the round-trip failed. */
static int render_frame(struct yetty_yrdawn_client *c, struct yetty_yrdawn_canvas *canvas,
                        uint64_t instance, uint64_t device, uint64_t queue, uint64_t view,
                        uint64_t tex, uint64_t ubo, uint64_t readback,
                        const struct glyph_rt *g, float time,
                        uint8_t *pixels, size_t pixel_count)
{
    (void)instance;
    float ubo_data[4] = { time, 0, 0, 0 };
    (void)yrdawn_client_wgpuQueueWriteBuffer(c, queue, ubo, 0, ubo_data, sizeof(ubo_data));

    uint64_t enc = yrdawn_client_wgpuDeviceCreateCommandEncoder(c, device);
    WGPURenderPassColorAttachment att = {0};
    att.view = (WGPUTextureView)view;
    att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    att.loadOp = WGPULoadOp_Clear;
    att.storeOp = WGPUStoreOp_Store;
    att.clearValue = (WGPUColor){0.0, 0.0, 0.0, 1.0};
    WGPURenderPassDescriptor rp = {0};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &att;
    uint64_t pass = yrdawn_client_wgpuCommandEncoderBeginRenderPass(c, enc, &rp);
    (void)yrdawn_client_wgpuRenderPassEncoderSetPipeline(c, pass, g->pipeline);
    (void)yrdawn_client_wgpuRenderPassEncoderSetBindGroup(c, pass, 0, g->bg, 0, NULL);
    (void)yrdawn_client_wgpuRenderPassEncoderDraw(c, pass, 3, 1, 0, 0);
    (void)yrdawn_client_wgpuRenderPassEncoderEnd(c, pass);
    (void)yrdawn_client_wgpuRenderPassEncoderRelease(c, pass);

    WGPUTexelCopyTextureInfo src = {0};
    src.texture = (WGPUTexture)tex;
    src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst = {0};
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = W * 4;
    dst.layout.rowsPerImage = H;
    dst.buffer = (WGPUBuffer)readback;
    WGPUExtent3D copy_size = { W, H, 1 };
    (void)yrdawn_client_wgpuCommandEncoderCopyTextureToBuffer(c, enc, &src, &dst, &copy_size);

    uint64_t cb = yrdawn_client_wgpuCommandEncoderFinish(c, enc);
    uint64_t cbs[1] = { cb };
    (void)yrdawn_client_wgpuQueueSubmit(c, queue, 1, (WGPUCommandBuffer const *)cbs);

    s_map_done = 0;
    s_map_payload = 0;
    (void)yrdawn_client_wgpuBufferMapAsync(c, readback, WGPUMapMode_Read, 0,
                                          pixel_count, on_map, NULL);
    /* Server's dispatch ran WaitAny synchronously, so the REPLY is
     * already on the wire — pump once to deliver it. */
    /* Round-trip needs the BULK reply for the mapped pixels — under the
     * figure-tree wire that's a few extra envelopes per frame. Give it
     * up to ~500ms before declaring the frame failed. */
    for (int i = 0; i < 500 && !s_map_done; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(1);
    }
    int ok = (s_map_done && s_map_payload == 1);
    if (ok) {
        (void)yrdawn_client_wgpuBufferReadMappedRange(c, readback, 0, pixels, pixel_count);
        (void)yrdawn_client_wgpuBufferUnmap(c, readback);
        struct yetty_ycore_void_result pr =
            yetty_yrdawn_canvas_present_frame(canvas, W, H, pixels, pixel_count);
        if (pr.ok != 1) ok = 0;
    }

    (void)yrdawn_client_wgpuCommandBufferRelease(c, cb);
    (void)yrdawn_client_wgpuCommandEncoderRelease(c, enc);
    return ok;
}

int main(void)
{
    demo_raw_stdin();
    FILE *trace = demo_trace_open("10-glyph-animate");
#define LOG(...) do { if (trace) fprintf(trace, __VA_ARGS__); } while (0)

    struct yetty_yrdawn_client *c = NULL;
    struct yetty_yrdawn_canvas *canvas =
        demo_bringup_single_canvas(/*figure_id=*/1, (float)W, (float)H, trace, &c);
    if (!canvas) {
        LOG("10: bringup failed\n");
        return 1;
    }
    LOG("10: connected=%d\n", yetty_yrdawn_canvas_connected(canvas));

    uint64_t instance = yrdawn_client_wgpuCreateInstance(c);
    uint64_t adapter  = yrdawn_client_wgpuInstanceRequestAdapter(c, instance, on_adapter, NULL);
    for (int i = 0; i < 300 && !s_adapter_done; ++i) {
        (void)yetty_yrdawn_client_pump(c); demo_sleep_ms(10);
    }
    if (s_adapter_status != 0) { LOG("10: adapter failed\n"); goto cleanup; }
    uint64_t device = yrdawn_client_wgpuAdapterRequestDevice(c, adapter, on_device, NULL);
    for (int i = 0; i < 300 && !s_device_done; ++i) {
        (void)yetty_yrdawn_client_pump(c); demo_sleep_ms(10);
    }
    if (s_device_status != 0) { LOG("10: device failed\n"); goto cleanup; }
    uint64_t queue = yrdawn_client_wgpuDeviceGetQueue(c, device);

    /* Persistent GPU resources — texture/view/uniform/readback don't
     * change across glyphs. */
    WGPUTextureDescriptor td = {0};
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    td.dimension = WGPUTextureDimension_2D;
    td.size.width = W; td.size.height = H; td.size.depthOrArrayLayers = 1;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1; td.sampleCount = 1;
    uint64_t tex = yrdawn_client_wgpuDeviceCreateTexture(c, device, &td);

    WGPUTextureViewDescriptor tvd = {0};
    tvd.format = WGPUTextureFormat_RGBA8Unorm;
    tvd.dimension = WGPUTextureViewDimension_2D;
    tvd.baseArrayLayer = 0; tvd.arrayLayerCount = 1;
    tvd.baseMipLevel = 0; tvd.mipLevelCount = 1;
    tvd.aspect = WGPUTextureAspect_All;
    uint64_t view = yrdawn_client_wgpuTextureCreateView(c, tex, &tvd);

    WGPUBufferDescriptor ubd = {0};
    ubd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
    ubd.size = 16;
    uint64_t ubo = yrdawn_client_wgpuDeviceCreateBuffer(c, device, &ubd);

    const size_t pixel_count = (size_t)W * H * 4u;
    WGPUBufferDescriptor rbd = {0};
    rbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    rbd.size = pixel_count;
    uint64_t readback = yrdawn_client_wgpuDeviceCreateBuffer(c, device, &rbd);

    size_t util_n = 0;
    char *util = slurp(YETTY_GLYPH_SHADERS_DIR "/_util.wgsl", &util_n);
    if (!util) { LOG("10: util slurp failed\n"); goto cleanup_buffers; }

    uint8_t *pixels = (uint8_t *)malloc(pixel_count);
    if (!pixels) { free(util); goto cleanup_buffers; }

    int total_frames = 0;
    for (int gi = 0; gi < GLYPH_COUNT && !demo_quit_flag; ++gi) {
        struct glyph_rt g = {0};
        if (!load_glyph(c, device, ubo, util, GLYPH_FILES[gi], &g, trace)) {
            LOG("10: load %s failed, skipping\n", GLYPH_FILES[gi]);
            continue;
        }
        int glyph_frames = 0;
        for (int f = 0; f < FRAMES_PER_GLYPH && !demo_quit_flag; ++f) {
            float t = (float)f * 0.05f;
            if (!render_frame(c, canvas, instance, device, queue, view, tex, ubo,
                              readback, &g, t, pixels, pixel_count)) {
                LOG("10: glyph %s frame %d failed\n", GLYPH_FILES[gi], f);
                break;
            }
            ++total_frames;
            ++glyph_frames;
            demo_sleep_ms(FRAME_INTERVAL_MS);
        }
        LOG("10: %s — %d frames\n", GLYPH_FILES[gi], glyph_frames);
        unload_glyph(c, &g);
    }
    LOG("10: %d frames presented across %d glyphs\n", total_frames, GLYPH_COUNT);

    free(pixels);
    free(util);

cleanup_buffers:
    (void)yrdawn_client_wgpuBufferRelease(c, readback);
    (void)yrdawn_client_wgpuBufferRelease(c, ubo);
    (void)yrdawn_client_wgpuTextureViewRelease(c, view);
    (void)yrdawn_client_wgpuTextureRelease(c, tex);
    (void)yrdawn_client_wgpuQueueRelease(c, queue);
    (void)yrdawn_client_wgpuDeviceRelease(c, device);
cleanup:
    (void)yrdawn_client_wgpuAdapterRelease(c, adapter);
    (void)yrdawn_client_wgpuInstanceRelease(c, instance);
    (void)yetty_yrdawn_canvas_destroy(canvas);
    (void)yetty_yrdawn_client_destroy(c);
    LOG("10: done\n");
    if (trace) fclose(trace);
    return 0;
}
