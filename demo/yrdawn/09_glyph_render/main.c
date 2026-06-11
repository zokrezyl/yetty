/*
 * 09_glyph_render — actually run a yfont glyph-shader through the
 * bridge, animate it, and stream pixels back into yetty.
 *
 * Pipeline (every call goes over the OSC bridge):
 *   wgpuCreateInstance → RequestAdapter → RequestDevice → GetQueue
 *   CreateShaderModule (WGSL = _util + glyph body + vs/fs wrapper)
 *   CreateTexture (rgba8, RENDER_ATTACHMENT | COPY_SRC)
 *   CreateTextureView
 *   CreateBuffer (uniforms, COPY_DST | UNIFORM)
 *   CreateBuffer (readback, COPY_DST | MAP_READ)
 *   CreateRenderPipeline (auto layout, vs_main / fs_main, rgba8 target)
 *   per frame:
 *     QueueWriteBuffer(uniforms, time)
 *     CreateCommandEncoder → BeginRenderPass (clear) → SetPipeline
 *     → SetBindGroup(0, ubo_group) → Draw(3,1,0,0) → End
 *     → CopyTextureToBuffer → Finish → QueueSubmit
 *     BufferMapAsync(READ) — async wait
 *     BufferReadMappedRange (server fills bytes, REPLY ships them back)
 *     BufferUnmap
 *     yetty_yrdawn_client_present_frame
 *
 * Run via:  ./yetty -e demo-yrdawn-09-glyph-render
 * Trace at: /tmp/yrdawn-demo-09-glyph-render.trace
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <webgpu/webgpu.h>
#include <yetty/yrdawn/client.h>

#include "common.h"

#ifndef YETTY_GLYPH_SHADERS_DIR
#error "YETTY_GLYPH_SHADERS_DIR must be defined by CMake"
#endif

enum { W = 256, H = 256 };

static int s_adapter_done, s_device_done;
static uint32_t s_adapter_status, s_device_status;
static int s_map_done;
static uint32_t s_map_status;

static void on_adapter(void *u, uint32_t st, uint32_t mid, const uint8_t *b, size_t bl)
{
    (void)u;
    (void)mid;
    (void)b;
    (void)bl;
    s_adapter_status = st;
    s_adapter_done = 1;
}
static void on_device(void *u, uint32_t st, uint32_t mid, const uint8_t *b, size_t bl)
{
    (void)u;
    (void)mid;
    (void)b;
    (void)bl;
    s_device_status = st;
    s_device_done = 1;
}
static uint32_t s_map_payload;
static void on_map(void *u, uint32_t st, uint32_t mid, const uint8_t *b, size_t bl)
{
    (void)u;
    (void)mid;
    s_map_status = st;
    s_map_payload = (bl >= sizeof(uint32_t) && b) ? *(const uint32_t *)b : 0xFFFFFFFFu;
    s_map_done = 1;
}

static char *slurp(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1u);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len) {
        *out_len = got;
    }
    return buf;
}

/* Wrap _util + glyph body with a vertex/fragment program that reads
 * `time` from a uniform buffer at @group(0) @binding(0). Same shape
 * yetty's shader-glyph-layer uses internally (minus the cell grid),
 * proving the wrapping is equivalent. */
static char *build_wgsl(const char *util_src, const char *body_src, uint32_t glyph_id,
                        size_t *out_len)
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
        "  let fg = vec3<f32>(0.42, 0.66, 0.57);\n" // BRAND_ACCENT
        "  let bg = vec3<f32>(0.04, 0.06, 0.08);\n" // BRAND_BG
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
    if (out_len) {
        *out_len = (size_t)(n + m);
    }
    return buf;
}

int main(void)
{
    demo_raw_stdin();
    FILE *trace = demo_trace_open("09-glyph-render");
#define LOG(...)                                                                                   \
    do {                                                                                           \
        if (trace)                                                                                 \
            fprintf(trace, __VA_ARGS__);                                                           \
    } while (0)

    struct yetty_yrdawn_client *c = NULL;
    struct yetty_yrdawn_canvas *canvas =
        demo_bringup_single_canvas(/*figure_id=*/1, (float)W, (float)H, trace, &c);
    if (!canvas) {
        LOG("09: bringup failed\n");
        return 1;
    }
    LOG("09: connected=%d\n", yetty_yrdawn_canvas_connected(canvas));

    uint64_t instance = yrdawn_client_wgpuCreateInstance(c);
    uint64_t adapter = yrdawn_client_wgpuInstanceRequestAdapter(c, instance, on_adapter, NULL);
    for (int i = 0; i < 300 && !s_adapter_done; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }
    if (s_adapter_status != 0) {
        LOG("09: adapter failed\n");
        goto cleanup;
    }
    uint64_t device = yrdawn_client_wgpuAdapterRequestDevice(c, adapter, on_device, NULL);
    for (int i = 0; i < 300 && !s_device_done; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }
    if (s_device_status != 0) {
        LOG("09: device failed\n");
        goto cleanup;
    }
    uint64_t queue = yrdawn_client_wgpuDeviceGetQueue(c, device);
    LOG("09: device=%lu queue=%lu\n", (unsigned long)device, (unsigned long)queue);

    /* Pick one glyph shader to render: the spinner. */
    size_t util_n = 0, body_n = 0, wgsl_n = 0;
    char *util = slurp(YETTY_GLYPH_SHADERS_DIR "/_util.wgsl", &util_n);
    char *body = slurp(YETTY_GLYPH_SHADERS_DIR "/0x0000-spinner.wgsl", &body_n);
    char *wgsl = build_wgsl(util, body, 0, &wgsl_n);
    free(util);
    free(body);
    LOG("09: WGSL composed (%zu bytes)\n", wgsl_n);

    WGPUShaderSourceWGSL src_wgsl = {0};
    src_wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    src_wgsl.code = (WGPUStringView){wgsl, wgsl_n};
    WGPUShaderModuleDescriptor smd = {0};
    smd.nextInChain = &src_wgsl.chain;
    uint64_t module = yrdawn_client_wgpuDeviceCreateShaderModule(c, device, &smd);
    LOG("09: shader_module=%lu\n", (unsigned long)module);
    free(wgsl);

    /* Render texture + view + readback + uniform buffers. */
    WGPUTextureDescriptor td = {0};
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    td.dimension = WGPUTextureDimension_2D;
    td.size.width = W;
    td.size.height = H;
    td.size.depthOrArrayLayers = 1;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    uint64_t tex = yrdawn_client_wgpuDeviceCreateTexture(c, device, &td);

    WGPUTextureViewDescriptor tvd = {0};
    tvd.format = WGPUTextureFormat_RGBA8Unorm;
    tvd.dimension = WGPUTextureViewDimension_2D;
    tvd.baseArrayLayer = 0;
    tvd.arrayLayerCount = 1;
    tvd.baseMipLevel = 0;
    tvd.mipLevelCount = 1;
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
    LOG("09: tex=%lu view=%lu ubo=%lu readback=%lu\n", (unsigned long)tex, (unsigned long)view,
        (unsigned long)ubo, (unsigned long)readback);

    /* Render pipeline. Auto layout (pipeline.layout = 0) — Dawn reflects
     * the bind groups from the shader. */
    WGPUColorTargetState target = {0};
    target.format = WGPUTextureFormat_RGBA8Unorm;
    target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs = {0};
    fs.module = (WGPUShaderModule)module;
    fs.entryPoint = (WGPUStringView){"fs_main", 7};
    fs.targetCount = 1;
    fs.targets = &target;
    WGPURenderPipelineDescriptor pd = {0};
    pd.vertex.module = (WGPUShaderModule)module;
    pd.vertex.entryPoint = (WGPUStringView){"vs_main", 7};
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.multisample.count = 1;
    pd.multisample.mask = 0xFFFFFFFFu;
    pd.fragment = &fs;
    uint64_t pipeline = yrdawn_client_wgpuDeviceCreateRenderPipeline(c, device, &pd);
    LOG("09: pipeline=%lu\n", (unsigned long)pipeline);

    /* Bind group for the uniform — Dawn lays out @group(0) @binding(0)
     * on the pipeline automatically; we ask it for the layout. */
    uint64_t bgl = yrdawn_client_wgpuRenderPipelineGetBindGroupLayout(c, pipeline, 0);
    WGPUBindGroupEntry entry = {0};
    entry.binding = 0;
    entry.buffer = (WGPUBuffer)ubo;
    entry.offset = 0;
    entry.size = 16;
    WGPUBindGroupDescriptor bgd = {0};
    bgd.layout = (WGPUBindGroupLayout)bgl;
    bgd.entryCount = 1;
    bgd.entries = &entry;
    uint64_t bg = yrdawn_client_wgpuDeviceCreateBindGroup(c, device, &bgd);
    LOG("09: bgl=%lu bg=%lu\n", (unsigned long)bgl, (unsigned long)bg);

    /* Render exactly one frame. (Animation = wrap this in a loop with
     * different `time` values and call present_frame each iteration.) */
    float ubo_data[4] = {0.35f, 0.0f, 0.0f, 0.0f}; /* time, pad×3 */
    (void)yrdawn_client_wgpuQueueWriteBuffer(c, queue, ubo, 0, ubo_data, sizeof(ubo_data));

    uint64_t encoder = yrdawn_client_wgpuDeviceCreateCommandEncoder(c, device);
    WGPURenderPassColorAttachment att = {0};
    att.view = (WGPUTextureView)view;
    att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    att.loadOp = WGPULoadOp_Clear;
    att.storeOp = WGPUStoreOp_Store;
    att.clearValue = (WGPUColor){0.0, 0.0, 0.0, 1.0};
    WGPURenderPassDescriptor rp = {0};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &att;
    uint64_t pass = yrdawn_client_wgpuCommandEncoderBeginRenderPass(c, encoder, &rp);
    (void)yrdawn_client_wgpuRenderPassEncoderSetPipeline(c, pass, pipeline);
    (void)yrdawn_client_wgpuRenderPassEncoderSetBindGroup(c, pass, 0, bg, 0, NULL);
    (void)yrdawn_client_wgpuRenderPassEncoderDraw(c, pass, 3, 1, 0, 0);
    (void)yrdawn_client_wgpuRenderPassEncoderEnd(c, pass);
    (void)yrdawn_client_wgpuRenderPassEncoderRelease(c, pass);

    WGPUTexelCopyTextureInfo src = {0};
    src.texture = (WGPUTexture)tex;
    src.mipLevel = 0;
    src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst = {0};
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = W * 4;
    dst.layout.rowsPerImage = H;
    dst.buffer = (WGPUBuffer)readback;
    WGPUExtent3D copy_size = {W, H, 1};
    (void)yrdawn_client_wgpuCommandEncoderCopyTextureToBuffer(c, encoder, &src, &dst, &copy_size);

    uint64_t cb = yrdawn_client_wgpuCommandEncoderFinish(c, encoder);
    uint64_t cbs[1] = {cb};
    (void)yrdawn_client_wgpuQueueSubmit(c, queue, 1, (WGPUCommandBuffer const *)cbs);
    LOG("09: submitted; awaiting MapAsync\n");

    /* Map the readback buffer, copy bytes back over the bridge, present.
     * Tick the bridge's WGPU instance every iteration — without that
     * Dawn never fires the MapAsync callback (the bridge runs its own
     * Dawn instance, separate from yetty's main one). */
    (void)yrdawn_client_wgpuBufferMapAsync(c, readback, WGPUMapMode_Read, 0, pixel_count, on_map,
                                           NULL);
    for (int i = 0; i < 500 && !s_map_done; ++i) {
        (void)yrdawn_client_wgpuDeviceTick(c, device);
        (void)yrdawn_client_wgpuInstanceProcessEvents(c, instance);
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }
    LOG("09: map reply_status=%u map_async_status=%u (done=%d)\n", s_map_status, s_map_payload,
        s_map_done);
    if (s_map_status != 0 || s_map_payload != 1) {
        goto cleanup_pipeline;
    }

    uint8_t *pixels = (uint8_t *)malloc(pixel_count);
    memset(pixels, 0xAA, pixel_count); /* poison so a no-op ReadMappedRange is obvious. */
    WGPUStatus rs = yrdawn_client_wgpuBufferReadMappedRange(c, readback, 0, pixels, pixel_count);
    LOG("09: ReadMappedRange status=%u (%zu bytes)\n", (unsigned)rs, pixel_count);
    (void)yrdawn_client_wgpuBufferUnmap(c, readback);

    /* Pixel sanity: a few spot samples + min/max/mean per channel.
     * If the GPU never wrote, we still see 0xAA from the poison. If
     * the pipeline ran but the shader emitted black, we see (0,0,0).
     * A live spinner gives a clear-color floor (BRAND_BG ≈ 10,16,20)
     * with mint arc pixels (≈107,168,146) standing out in the max. */
    {
        uint32_t mn[4] = {255, 255, 255, 255}, mx[4] = {0, 0, 0, 0};
        uint64_t sum[4] = {0, 0, 0, 0};
        size_t px = (size_t)W * H;
        for (size_t i = 0; i < px; ++i) {
            for (int k = 0; k < 4; ++k) {
                uint8_t v = pixels[i * 4u + k];
                if (v < mn[k]) {
                    mn[k] = v;
                }
                if (v > mx[k]) {
                    mx[k] = v;
                }
                sum[k] += v;
            }
        }
        LOG("09: pixels R[min=%u max=%u mean=%lu] G[min=%u max=%u mean=%lu] "
            "B[min=%u max=%u mean=%lu] A[min=%u max=%u mean=%lu]\n",
            mn[0], mx[0], (unsigned long)(sum[0] / px), mn[1], mx[1], (unsigned long)(sum[1] / px),
            mn[2], mx[2], (unsigned long)(sum[2] / px), mn[3], mx[3], (unsigned long)(sum[3] / px));
        LOG("09: sample center pixel rgba=(%u,%u,%u,%u) corner=(%u,%u,%u,%u)\n",
            pixels[(H / 2 * W + W / 2) * 4 + 0], pixels[(H / 2 * W + W / 2) * 4 + 1],
            pixels[(H / 2 * W + W / 2) * 4 + 2], pixels[(H / 2 * W + W / 2) * 4 + 3], pixels[0],
            pixels[1], pixels[2], pixels[3]);
    }

    struct yetty_ycore_void_result pr =
        yetty_yrdawn_canvas_present_frame(canvas, W, H, pixels, pixel_count);
    if (pr.ok != 1) {
        LOG("09: present_frame error: %s\n", pr.error.msg);
    } else {
        LOG("09: presented %dx%d frame from real Dawn render\n", W, H);
    }
    free(pixels);

cleanup_pipeline:
    (void)yrdawn_client_wgpuBindGroupRelease(c, bg);
    (void)yrdawn_client_wgpuBindGroupLayoutRelease(c, bgl);
    (void)yrdawn_client_wgpuCommandBufferRelease(c, cb);
    (void)yrdawn_client_wgpuCommandEncoderRelease(c, encoder);
    (void)yrdawn_client_wgpuRenderPipelineRelease(c, pipeline);
    (void)yrdawn_client_wgpuBufferRelease(c, readback);
    (void)yrdawn_client_wgpuBufferRelease(c, ubo);
    (void)yrdawn_client_wgpuTextureViewRelease(c, view);
    (void)yrdawn_client_wgpuTextureRelease(c, tex);
    (void)yrdawn_client_wgpuShaderModuleRelease(c, module);
    (void)yrdawn_client_wgpuQueueRelease(c, queue);
    (void)yrdawn_client_wgpuDeviceRelease(c, device);
cleanup:
    (void)yrdawn_client_wgpuAdapterRelease(c, adapter);
    (void)yrdawn_client_wgpuInstanceRelease(c, instance);
    /* Hold the connection open so yetty's render thread paints the
     * frame before we drop the PTY. Press 'q' to exit immediately. */
    for (int i = 0; i < 200 && !demo_quit_flag; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }
    (void)yetty_yrdawn_canvas_destroy(canvas);
    (void)yetty_yrdawn_client_destroy(c);
    LOG("09: done\n");
    if (trace) {
        fclose(trace);
    }
    return 0;
}
