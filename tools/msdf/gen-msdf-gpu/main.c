/*
 * yetty-ymsdf-gen-gpu — CLI tool for the GPU (WGSL compute) MSDF generator.
 *
 * Counterpart to tools/gen-msdf (CPU/msdfgen). Brings up a headless WGPU
 * instance + adapter + device (no surface, no window), then calls
 * yetty_ymsdf_wgsl_config_generate(). Output CDB is byte-compatible with
 * the CPU generator so cdb-diff can compare them directly.
 *
 * Usage: yetty-ymsdf-gen-gpu [options] <font.ttf> <output.cdb>
 */

#include <yetty/ymsdf-wgsl/ymsdf-wgsl.h>
#include <yetty/ywebgpu/limits.h>
#include <yetty/ywebgpu/request.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <webgpu/webgpu.h>

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [options] <font.ttf> <output.cdb>\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --size N        Font size in pixels (default: 32)\n");
    fprintf(stderr, "  --range N       MSDF pixel range (default: 4)\n");
    fprintf(stderr, "  --shader PATH   Path to msdf_gen.wgsl (auto-detected by default)\n");
}

static void usleep_short(void)
{
    struct timespec ts = {0, 100000}; /* 100us */
    nanosleep(&ts, NULL);
}

int main(int argc, char *argv[])
{
    const char *ttf_path = NULL;
    const char *cdb_path = NULL;
    const char *shader_path = NULL;
    float font_size = 32.0f;
    float pixel_range = 4.0f;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            font_size = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--range") == 0 && i + 1 < argc) {
            pixel_range = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--shader") == 0 && i + 1 < argc) {
            shader_path = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        } else if (!ttf_path) {
            ttf_path = argv[i];
        } else if (!cdb_path) {
            cdb_path = argv[i];
        }
    }

    if (!ttf_path || !cdb_path) {
        usage(argv[0]);
        return 1;
    }

    fprintf(stderr, "MSDF Generator (GPU)\n");
    fprintf(stderr, "  Font: %s\n", ttf_path);
    fprintf(stderr, "  Output: %s\n", cdb_path);
    fprintf(stderr, "  Size: %.0fpx\n", (double)font_size);
    fprintf(stderr, "  Pixel range: %.0f\n", (double)pixel_range);
    fprintf(stderr, "  Shader: %s\n", shader_path ? shader_path : "<auto>");

    /* WebGPU instance — no surface needed for compute-only workload. */
    WGPUInstance instance = wgpuCreateInstance(NULL);
    if (!instance) {
        fprintf(stderr, "wgpuCreateInstance failed\n");
        return 1;
    }

    /* Request adapter (headless). */
    WGPUAdapter adapter = NULL;
    int adapter_ready = 0;
    WGPURequestAdapterOptions adapter_opts = {0};
    adapter_opts.compatibleSurface = NULL;
    adapter_opts.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo a_cb = {0};
    a_cb.mode = WGPUCallbackMode_AllowSpontaneous;
    a_cb.callback = yetty_ywebgpu_adapter_request_callback;
    a_cb.userdata1 = &adapter;
    a_cb.userdata2 = &adapter_ready;
    wgpuInstanceRequestAdapter(instance, &adapter_opts, a_cb);
    while (!adapter_ready) {
        wgpuInstanceProcessEvents(instance);
        usleep_short();
    }
    if (!adapter) {
        fprintf(stderr, "Failed to acquire WGPU adapter\n");
        wgpuInstanceRelease(instance);
        return 1;
    }

    /* Request device. The default limits are fine — ymsdf-wgsl uses small
	 * uniform buffers and a 8192-wide RGBA32Float storage texture. */
    /* Request larger limits — the full nerd font atlas (8192 wide
	 * × thousands of glyphs at RGBA32Float) overshoots the default
	 * maxBufferSize at readback time. Use the same helper as yetty
	 * proper, which clamps to adapter caps. */
    WGPULimits limits;
    yetty_ywebgpu_fill_default_limits(adapter, NULL, &limits);

    WGPUDevice device = NULL;
    struct yetty_ywebgpu_device_request_state ds = {{0}, 0};
    WGPUDeviceDescriptor device_desc = {0};
    device_desc.requiredLimits = &limits;
    WGPURequestDeviceCallbackInfo d_cb = {0};
    d_cb.mode = WGPUCallbackMode_AllowSpontaneous;
    d_cb.callback = yetty_ywebgpu_device_request_callback;
    d_cb.userdata1 = &device;
    d_cb.userdata2 = &ds;
    wgpuAdapterRequestDevice(adapter, &device_desc, d_cb);
    while (!ds.ready) {
        wgpuInstanceProcessEvents(instance);
        usleep_short();
    }
    if (!device) {
        fprintf(stderr, "Failed to acquire WGPU device: %s\n",
                ds.error_msg[0] ? ds.error_msg : "(no message)");
        wgpuAdapterRelease(adapter);
        wgpuInstanceRelease(instance);
        return 1;
    }

    struct yetty_ymsdf_wgsl_config cfg = {0};
    cfg.ttf_path = ttf_path;
    cfg.cdb_path = cdb_path;
    cfg.font_size = font_size;
    cfg.pixel_range = pixel_range;
    cfg.device = device;
    cfg.instance = instance;
    cfg.shader_path = shader_path;

    struct yetty_ycore_void_result res = yetty_ymsdf_wgsl_config_generate(&cfg);
    int rc = 0;
    if (YETTY_IS_ERR(res)) {
        fprintf(stderr, "Error: %s\n", res.error.msg);
        rc = 1;
    } else {
        fprintf(stderr, "Done.\n");
    }

    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);
    return rc;
}
