/*
 * 06_buffer_write — create a buffer through the bridge, fill it via
 * wgpuQueueWriteBuffer.
 *
 * Exercises:
 *   - encodable descriptor: WGPUBufferDescriptor carries a
 *     WGPUStringView label, scalars, flags — recursively serialised
 *     over the wire by the codegen-emitted encoder.
 *   - byte_array arg: wgpuQueueWriteBuffer(queue, buf, off, data, size)
 *     ships `size` bytes inline as a variable-length wire chunk.
 *   - wgpuDeviceCreateBuffer's `WGPU_NULLABLE WGPUBuffer` return.
 *
 * Run via:  ./yetty -e demo-ywasm-06-buffer-write
 * Trace at: /tmp/ywasm-demo-06-buffer-write.trace
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <webgpu/webgpu.h>
#include <yetty/ywasm/client.h>
#include <yetty/ywasm/methods.gen.h>

#include "common.h"

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

int main(void)
{
    demo_raw_stdin();
    FILE *trace = demo_trace_open("06-buffer-write");
#define LOG(...) do { if (trace) fprintf(trace, __VA_ARGS__); } while (0)

    struct yetty_ywasm_client_ptr_result cr =
        yetty_ywasm_client_create(STDIN_FILENO, STDOUT_FILENO);
    if (cr.ok != 1) return 1;
    struct yetty_ywasm_client *c = cr.value;
    (void)yetty_ywasm_client_send_hello(c);
    for (int i = 0; i < 200 && !yetty_ywasm_client_connected(c); ++i) {
        (void)yetty_ywasm_client_pump(c);
        demo_sleep_ms(10);
    }
    LOG("06_buffer_write: connected=%d\n", yetty_ywasm_client_connected(c));

    uint64_t instance = ywasm_client_wgpuCreateInstance(c);
    uint64_t adapter  = ywasm_client_wgpuInstanceRequestAdapter(c, instance, on_adapter, NULL);
    for (int i = 0; i < 300 && !s_adapter_done; ++i) {
        (void)yetty_ywasm_client_pump(c);
        demo_sleep_ms(10);
    }
    if (s_adapter_status != 0) {
        LOG("06_buffer_write: adapter failed status=%u\n", s_adapter_status);
        goto cleanup;
    }
    uint64_t device = ywasm_client_wgpuAdapterRequestDevice(c, adapter, on_device, NULL);
    for (int i = 0; i < 300 && !s_device_done; ++i) {
        (void)yetty_ywasm_client_pump(c);
        demo_sleep_ms(10);
    }
    if (s_device_status != 0) {
        LOG("06_buffer_write: device failed status=%u\n", s_device_status);
        goto cleanup;
    }
    LOG("06_buffer_write: have device=%lu\n", (unsigned long)device);

    uint64_t queue = ywasm_client_wgpuDeviceGetQueue(c, device);

    WGPUBufferDescriptor bd = {0};
    const char *label = "demo-06-buffer";
    bd.label = (WGPUStringView){label, strlen(label)};
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    bd.size = 64;
    bd.mappedAtCreation = 0u;
    uint64_t buffer = ywasm_client_wgpuDeviceCreateBuffer(c, device, &bd);
    LOG("06_buffer_write: createBuffer -> handle=%lu\n", (unsigned long)buffer);

    uint8_t data[64];
    for (int i = 0; i < 64; ++i) data[i] = (uint8_t)i;
    (void)ywasm_client_wgpuQueueWriteBuffer(c, queue, buffer, 0, data, sizeof(data));
    LOG("06_buffer_write: wrote %zu bytes\n", sizeof(data));

    (void)ywasm_client_wgpuBufferRelease(c, buffer);
    (void)ywasm_client_wgpuQueueRelease(c, queue);
    (void)ywasm_client_wgpuDeviceRelease(c, device);
cleanup:
    (void)ywasm_client_wgpuAdapterRelease(c, adapter);
    (void)ywasm_client_wgpuInstanceRelease(c, instance);
    (void)yetty_ywasm_client_send_bye(c);
    (void)yetty_ywasm_client_destroy(c);
    LOG("06_buffer_write: done\n");
    if (trace) fclose(trace);
    return 0;
}
