/*
 * 03_async_init — full WebGPU init chain through the bridge:
 *
 *   wgpuCreateInstance → wgpuInstanceRequestAdapter (async)
 *     → wgpuAdapterRequestDevice (async) → wgpuDeviceGetQueue
 *     → wgpuDeviceCreateCommandEncoder → wgpuCommandEncoderFinish
 *     → wgpuCommandBufferRelease → wgpuCommandEncoderRelease
 *     → wgpuQueueRelease → wgpuDeviceRelease → wgpuAdapterRelease
 *     → wgpuInstanceRelease
 *
 * Every method is dispatched to local Dawn via the codegen-emitted
 * server dispatcher; async ones reply through the trampoline once
 * Dawn's callback fires.
 */
#include <stdio.h>
#include <unistd.h>

#include <yetty/yrdawn/client.h>
#include <yetty/yrdawn/methods.gen.h>

#include "common.h"

struct init_state {
    FILE *trace;
    int adapter_done;
    int device_done;
    uint32_t adapter_status;
    uint32_t device_status;
};

static void on_adapter(void *u, uint32_t status, uint32_t mid,
                       const uint8_t *body, size_t body_len)
{
    (void)mid; (void)body; (void)body_len;
    struct init_state *s = (struct init_state *)u;
    s->adapter_status = status;
    s->adapter_done = 1;
    if (s->trace) fprintf(s->trace, "03_async_init: adapter status=%u\n", status);
}

static void on_device(void *u, uint32_t status, uint32_t mid,
                      const uint8_t *body, size_t body_len)
{
    (void)mid; (void)body; (void)body_len;
    struct init_state *s = (struct init_state *)u;
    s->device_status = status;
    s->device_done = 1;
    if (s->trace) fprintf(s->trace, "03_async_init: device status=%u\n", status);
}

int main(void)
{
    demo_raw_stdin();
    FILE *trace = demo_trace_open("03-async-init");
#define LOG(...) do { if (trace) fprintf(trace, __VA_ARGS__); } while (0)

    struct yetty_yrdawn_client_ptr_result cr =
        yetty_yrdawn_client_create(STDIN_FILENO, STDOUT_FILENO);
    if (cr.ok != 1) {
        LOG("03_async_init: client_create failed: %s\n", cr.error.msg);
        return 1;
    }
    struct yetty_yrdawn_client *c = cr.value;
    demo_install_quit_on_q(c);

    (void)yetty_yrdawn_client_send_hello(c);
    for (int i = 0; i < 200 && !yetty_yrdawn_client_connected(c); ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }
    LOG("03_async_init: connected=%d\n", yetty_yrdawn_client_connected(c));

    uint64_t instance = yrdawn_client_wgpuCreateInstance(c);
    LOG("03_async_init: instance=%lu\n", (unsigned long)instance);

    struct init_state st = {trace, 0, 0, 0, 0};

    uint64_t adapter = yrdawn_client_wgpuInstanceRequestAdapter(c, instance, on_adapter, &st);
    LOG("03_async_init: adapter handle=%lu (pending)\n", (unsigned long)adapter);
    for (int i = 0; i < 300 && !st.adapter_done; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }
    if (!st.adapter_done || st.adapter_status != 0) {
        LOG("03_async_init: adapter failed (done=%d status=%u)\n",
            st.adapter_done, st.adapter_status);
        goto cleanup_instance;
    }

    uint64_t device = yrdawn_client_wgpuAdapterRequestDevice(c, adapter, on_device, &st);
    LOG("03_async_init: device handle=%lu (pending)\n", (unsigned long)device);
    for (int i = 0; i < 300 && !st.device_done; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }
    if (!st.device_done || st.device_status != 0) {
        LOG("03_async_init: device failed (done=%d status=%u)\n",
            st.device_done, st.device_status);
        goto cleanup_adapter;
    }

    uint64_t queue = yrdawn_client_wgpuDeviceGetQueue(c, device);
    LOG("03_async_init: queue=%lu\n", (unsigned long)queue);

    uint64_t encoder = yrdawn_client_wgpuDeviceCreateCommandEncoder(c, device);
    LOG("03_async_init: encoder=%lu\n", (unsigned long)encoder);

    uint64_t cb = yrdawn_client_wgpuCommandEncoderFinish(c, encoder);
    LOG("03_async_init: command_buffer=%lu\n", (unsigned long)cb);

    /* Tear-down in reverse. Each release prompts a sync REPLY (req_id=0
     * so no callback is fired locally). */
    (void)yrdawn_client_wgpuCommandBufferRelease(c, cb);
    (void)yrdawn_client_wgpuCommandEncoderRelease(c, encoder);
    (void)yrdawn_client_wgpuQueueRelease(c, queue);
    (void)yrdawn_client_wgpuDeviceRelease(c, device);
cleanup_adapter:
    (void)yrdawn_client_wgpuAdapterRelease(c, adapter);
cleanup_instance:
    (void)yrdawn_client_wgpuInstanceRelease(c, instance);
    LOG("03_async_init: chain torn down\n");

    /* Hold open long enough that the layer's debug trace lands in the
     * yetty log before we tear the PTY down. 'q' cuts the wait short. */
    for (int i = 0; i < 100 && !demo_quit_flag; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }

    (void)yetty_yrdawn_client_send_bye(c);
    (void)yetty_yrdawn_client_destroy(c);
    LOG("03_async_init: done\n");
    if (trace) fclose(trace);
    return 0;
}
