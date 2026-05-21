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
 * Run via:  ./yetty -e demo-ywasm-05-adapter-info
 * Trace at: /tmp/ywasm-demo-05-adapter-info.trace
 */
#include <stdio.h>
#include <unistd.h>

#include <webgpu/webgpu.h>
#include <yetty/ywasm/client.h>
#include <yetty/ywasm/methods.gen.h>

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

    struct yetty_ywasm_client_ptr_result cr =
        yetty_ywasm_client_create(STDIN_FILENO, STDOUT_FILENO);
    if (cr.ok != 1) {
        LOG("05_adapter_info: client_create failed\n");
        return 1;
    }
    struct yetty_ywasm_client *c = cr.value;
    (void)yetty_ywasm_client_send_hello(c);
    for (int i = 0; i < 200 && !yetty_ywasm_client_connected(c); ++i) {
        (void)yetty_ywasm_client_pump(c);
        demo_sleep_ms(10);
    }
    LOG("05_adapter_info: connected=%d\n", yetty_ywasm_client_connected(c));

    uint64_t instance = ywasm_client_wgpuCreateInstance(c);
    uint64_t adapter  = ywasm_client_wgpuInstanceRequestAdapter(c, instance, on_adapter, NULL);
    for (int i = 0; i < 300 && !s_adapter_done; ++i) {
        (void)yetty_ywasm_client_pump(c);
        demo_sleep_ms(10);
    }
    LOG("05_adapter_info: adapter status=%u\n", s_adapter_status);
    if (s_adapter_status != 0)
        goto cleanup;

    WGPUAdapterInfo info = {0};
    WGPUStatus st = ywasm_client_wgpuAdapterGetInfo(c, adapter, &info);
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
    ywasm_client_wgpuAdapterInfoFreeMembers(c, info);
    LOG("05_adapter_info: freed adapter info members\n");

cleanup:
    (void)ywasm_client_wgpuAdapterRelease(c, adapter);
    (void)ywasm_client_wgpuInstanceRelease(c, instance);
    (void)yetty_ywasm_client_send_bye(c);
    (void)yetty_ywasm_client_destroy(c);
    LOG("05_adapter_info: done\n");
    if (trace) fclose(trace);
    return 0;
}
