/*
 * 05_adapter_info — query Dawn for the local adapter's metadata.
 *
 * Exercises:
 *   - async requestAdapter (curated trampoline)
 *   - output-struct-fill: wgpuAdapterGetInfo fills WGPUAdapterInfo
 *     server-side, server encodes it into the REPLY payload, client
 *     decoder allocates inner WGPUStringView buffers via malloc
 *   - wgpuAdapterInfoFreeMembers client-side walker that releases
 *     those malloc'd inner pointers
 *
 * Run via:  ./yetty -e demo-yrdawn-05-adapter-info
 * Trace at: /tmp/yrdawn-demo-05-adapter-info.trace
 */
#include <stdio.h>
#include <unistd.h>

#include <webgpu/webgpu.h>
#include <yetty/yrdawn/client.h>
#include <yetty/yrdawn/methods.gen.h>

#include "common.h"

static int s_adapter_done;
static uint32_t s_adapter_status;

static void on_adapter(void *u, uint32_t status, uint32_t mid,
                       const uint8_t *body, size_t body_len)
{
    (void)u; (void)mid; (void)body; (void)body_len;
    s_adapter_status = status;
    s_adapter_done = 1;
}

int main(void)
{
    demo_raw_stdin();
    FILE *trace = demo_trace_open("05-adapter-info");
#define LOG(...) do { if (trace) fprintf(trace, __VA_ARGS__); } while (0)

    struct yetty_yrdawn_client *c = NULL;
    struct yetty_yrdawn_canvas *canvas = demo_bringup_single_canvas(/*figure_id=*/1, trace, &c);
    if (!canvas) {
        LOG("05_adapter_info: bringup failed\n");
        return 1;
    }
    LOG("05_adapter_info: connected=%d\n", yetty_yrdawn_canvas_connected(canvas));

    uint64_t instance = yrdawn_client_wgpuCreateInstance(c);
    uint64_t adapter  = yrdawn_client_wgpuInstanceRequestAdapter(c, instance, on_adapter, NULL);
    for (int i = 0; i < 300 && !s_adapter_done; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }
    LOG("05_adapter_info: adapter status=%u\n", s_adapter_status);
    if (s_adapter_status != 0)
        goto cleanup;

    WGPUAdapterInfo info = {0};
    WGPUStatus st = yrdawn_client_wgpuAdapterGetInfo(c, adapter, &info);
    LOG("05_adapter_info: getInfo status=%u\n", (unsigned)st);
    LOG("  vendor:       %.*s\n", (int)info.vendor.length,       info.vendor.data       ? info.vendor.data       : "");
    LOG("  architecture: %.*s\n", (int)info.architecture.length, info.architecture.data ? info.architecture.data : "");
    LOG("  device:       %.*s\n", (int)info.device.length,       info.device.data       ? info.device.data       : "");
    LOG("  description:  %.*s\n", (int)info.description.length,  info.description.data  ? info.description.data  : "");
    LOG("  vendorID:     0x%x\n", info.vendorID);
    LOG("  deviceID:     0x%x\n", info.deviceID);
    LOG("  backendType:  %u\n",   (unsigned)info.backendType);
    LOG("  adapterType:  %u\n",   (unsigned)info.adapterType);

    /* Releases the strings the client decoder malloc'd. */
    yrdawn_client_wgpuAdapterInfoFreeMembers(c, info);
    LOG("05_adapter_info: freed adapter info members\n");

cleanup:
    (void)yrdawn_client_wgpuAdapterRelease(c, adapter);
    (void)yrdawn_client_wgpuInstanceRelease(c, instance);
    (void)yetty_yrdawn_canvas_destroy(canvas);
    (void)yetty_yrdawn_client_destroy(c);
    LOG("05_adapter_info: done\n");
    if (trace) fclose(trace);
    return 0;
}
