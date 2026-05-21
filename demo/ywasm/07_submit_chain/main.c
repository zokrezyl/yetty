/*
 * 07_submit_chain — encode an empty command buffer and submit it.
 *
 * Exercises:
 *   - wgpuDeviceCreateCommandEncoder + wgpuCommandEncoderFinish
 *     (handle-returning entrypoints with no descriptors)
 *   - **handle_array codec**: wgpuQueueSubmit takes
 *     (size_t count, WGPUCommandBuffer const *commands). Our codegen
 *     wires this as a u64 count + count×u64 handles in the trailing
 *     blob, with server-side handle table lookup + arena allocation.
 *
 * Run via:  ./yetty -e demo-ywasm-07-submit-chain
 * Trace at: /tmp/ywasm-demo-07-submit-chain.trace
 */
#include <stdio.h>
#include <stdint.h>
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
    FILE *trace = demo_trace_open("07-submit-chain");
#define LOG(...) do { if (trace) fprintf(trace, __VA_ARGS__); } while (0)

    struct yetty_ywasm_client_ptr_result cr =
        yetty_ywasm_client_create(STDIN_FILENO, STDOUT_FILENO);
    if (cr.ok != 1) return 1;
    struct yetty_ywasm_client *c = cr.value;
    demo_install_quit_on_q(c);
    (void)yetty_ywasm_client_send_hello(c);
    for (int i = 0; i < 200 && !yetty_ywasm_client_connected(c); ++i) {
        (void)yetty_ywasm_client_pump(c);
        demo_sleep_ms(10);
    }
    LOG("07_submit_chain: connected=%d\n", yetty_ywasm_client_connected(c));

    uint64_t instance = ywasm_client_wgpuCreateInstance(c);
    uint64_t adapter  = ywasm_client_wgpuInstanceRequestAdapter(c, instance, on_adapter, NULL);
    for (int i = 0; i < 300 && !s_adapter_done; ++i) {
        (void)yetty_ywasm_client_pump(c);
        demo_sleep_ms(10);
    }
    if (s_adapter_status != 0) { LOG("adapter status=%u\n", s_adapter_status); goto cleanup; }

    uint64_t device = ywasm_client_wgpuAdapterRequestDevice(c, adapter, on_device, NULL);
    for (int i = 0; i < 300 && !s_device_done; ++i) {
        (void)yetty_ywasm_client_pump(c);
        demo_sleep_ms(10);
    }
    if (s_device_status != 0) { LOG("device status=%u\n", s_device_status); goto cleanup; }

    uint64_t queue   = ywasm_client_wgpuDeviceGetQueue(c, device);
    uint64_t encoder = ywasm_client_wgpuDeviceCreateCommandEncoder(c, device);
    uint64_t cb      = ywasm_client_wgpuCommandEncoderFinish(c, encoder);
    LOG("07_submit_chain: encoder=%lu cb=%lu\n",
        (unsigned long)encoder, (unsigned long)cb);

    /* Submit an array of one command buffer. Our codegen serialises
     * (count + each handle) automatically. The cast is needed because
     * on the client side handles are bare uint64_t but the user-facing
     * wgpu* signature expects WGPUCommandBuffer (an opaque pointer
     * typedef). The wire layout doesn't care. */
    uint64_t cbs[1] = { cb };
    (void)ywasm_client_wgpuQueueSubmit(c, queue, 1,
                                       (WGPUCommandBuffer const *)cbs);
    LOG("07_submit_chain: queueSubmit dispatched count=1\n");

    (void)ywasm_client_wgpuCommandBufferRelease(c, cb);
    (void)ywasm_client_wgpuCommandEncoderRelease(c, encoder);
    (void)ywasm_client_wgpuQueueRelease(c, queue);
    (void)ywasm_client_wgpuDeviceRelease(c, device);
cleanup:
    (void)ywasm_client_wgpuAdapterRelease(c, adapter);
    (void)ywasm_client_wgpuInstanceRelease(c, instance);
    (void)yetty_ywasm_client_send_bye(c);
    (void)yetty_ywasm_client_destroy(c);
    LOG("07_submit_chain: done\n");
    if (trace) fclose(trace);
    return 0;
}
