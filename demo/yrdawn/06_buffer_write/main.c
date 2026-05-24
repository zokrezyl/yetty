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
 * Run via:  ./yetty -e demo-yrdawn-06-buffer-write
 * Trace at: /tmp/yrdawn-demo-06-buffer-write.trace
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <webgpu/webgpu.h>
#include <yetty/yrdawn/client.h>
#include <yetty/yrdawn/methods.gen.h>

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

    struct yetty_yrdawn_client *c = NULL;
    struct yetty_yrdawn_canvas *canvas =
        demo_bringup_single_canvas(/*figure_id=*/1, 256.0f, 256.0f, trace, &c);
    if (!canvas) {
        LOG("06_buffer_write: bringup failed\n");
        return 1;
    }
    LOG("06_buffer_write: connected=%d\n", yetty_yrdawn_canvas_connected(canvas));

    uint64_t instance = yrdawn_client_wgpuCreateInstance(c);
    uint64_t adapter  = yrdawn_client_wgpuInstanceRequestAdapter(c, instance, on_adapter, NULL);
    for (int i = 0; i < 300 && !s_adapter_done; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }
    if (s_adapter_status != 0) {
        LOG("06_buffer_write: adapter failed status=%u\n", s_adapter_status);
        goto cleanup;
    }
    uint64_t device = yrdawn_client_wgpuAdapterRequestDevice(c, adapter, on_device, NULL);
    for (int i = 0; i < 300 && !s_device_done; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }
    if (s_device_status != 0) {
        LOG("06_buffer_write: device failed status=%u\n", s_device_status);
        goto cleanup;
    }
    LOG("06_buffer_write: have device=%lu\n", (unsigned long)device);

    uint64_t queue = yrdawn_client_wgpuDeviceGetQueue(c, device);

    WGPUBufferDescriptor bd = {0};
    const char *label = "demo-06-buffer";
    bd.label = (WGPUStringView){label, strlen(label)};
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    bd.size = 64;
    bd.mappedAtCreation = 0u;
    uint64_t buffer = yrdawn_client_wgpuDeviceCreateBuffer(c, device, &bd);
    LOG("06_buffer_write: createBuffer -> handle=%lu\n", (unsigned long)buffer);

    uint8_t data[64];
    for (int i = 0; i < 64; ++i) data[i] = (uint8_t)i;
    (void)yrdawn_client_wgpuQueueWriteBuffer(c, queue, buffer, 0, data, sizeof(data));
    LOG("06_buffer_write: wrote %zu bytes\n", sizeof(data));

    (void)yrdawn_client_wgpuBufferRelease(c, buffer);
    (void)yrdawn_client_wgpuQueueRelease(c, queue);
    (void)yrdawn_client_wgpuDeviceRelease(c, device);
cleanup:
    (void)yrdawn_client_wgpuAdapterRelease(c, adapter);
    (void)yrdawn_client_wgpuInstanceRelease(c, instance);
    (void)yetty_yrdawn_canvas_destroy(canvas);
    (void)yetty_yrdawn_client_destroy(c);
    LOG("06_buffer_write: done\n");
    if (trace) fclose(trace);
    return 0;
}
