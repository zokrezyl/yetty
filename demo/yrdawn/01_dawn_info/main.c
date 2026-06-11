/*
 * 01_dawn_info — query the REMOTE (server-side) WebGPU adapter and print
 * its metadata into the host pane.
 *
 * Unlike the pixel-presenting demos, this one's "display" is text: it opens
 * a yrdawn session, asks the remote Dawn for adapter info over the bridge,
 * and prints vendor / device / backend straight to the terminal. A quick
 * "is the remote GPU bridge alive, and what is it?" probe.
 *
 * Exercises the same async path as the bridge init demos:
 *   - wgpuCreateInstance → wgpuInstanceRequestAdapter (async trampoline)
 *   - wgpuAdapterGetInfo output-struct-fill (server encodes WGPUAdapterInfo
 *     into the REPLY; the client decoder malloc's the inner string buffers)
 *   - wgpuAdapterInfoFreeMembers releases those client-side allocations
 *
 * Exit: 'q' or Ctrl-C.
 *
 * Run via:  ./yetty -e demo-yrdawn-01-dawn-info
 * Trace at: /tmp/yrdawn-demo-01-dawn-info.trace
 */
#include <stdio.h>
#include <unistd.h>

#include <webgpu/webgpu.h>
#include <yetty/yrdawn/client.h>

#include "common.h"

static int s_adapter_done;
static uint32_t s_adapter_status;

static void on_adapter(void *user, uint32_t status, uint32_t mid, const uint8_t *body,
                       size_t body_len)
{
    (void)user;
    (void)mid;
    (void)body;
    (void)body_len;
    s_adapter_status = status;
    s_adapter_done = 1;
}

/* Raw mode turns OPOST off, so '\n' is NOT translated to '\r\n'; emit both
 * explicitly to keep printed lines left-aligned in the pane. */
static void print_strview(const char *label, WGPUStringView value)
{
    printf("  %-13s %.*s\r\n", label, (int)value.length, value.data ? value.data : "");
}

int main(void)
{
    demo_raw_stdin();
    /* Unbuffered so our text interleaves cleanly with the wire envelopes
     * the yrdawn client writes directly to the same stdout fd. */
    setvbuf(stdout, NULL, _IONBF, 0);
    FILE *trace = demo_trace_open("01-dawn-info");
#define LOG(...)                                                                                   \
    do {                                                                                           \
        if (trace)                                                                                 \
            fprintf(trace, __VA_ARGS__);                                                           \
    } while (0)

    struct yetty_yrdawn_client *c = NULL;
    struct yetty_yrdawn_canvas *canvas =
        demo_bringup_single_canvas(/*figure_id=*/1, 256.0f, 256.0f, trace, &c);
    if (!canvas) {
        LOG("01_dawn_info: bringup failed\n");
        return 1;
    }
    printf("yrdawn remote — connected=%d\r\n", yetty_yrdawn_canvas_connected(canvas));

    uint64_t instance = yrdawn_client_wgpuCreateInstance(c);
    uint64_t adapter = yrdawn_client_wgpuInstanceRequestAdapter(c, instance, on_adapter, NULL);
    for (int i = 0; i < 300 && !s_adapter_done && !demo_quit_flag; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }

    if (s_adapter_status != 0) {
        printf("requestAdapter failed (status=%u)\r\n", s_adapter_status);
        LOG("01_dawn_info: adapter status=%u\n", s_adapter_status);
    } else {
        WGPUAdapterInfo info = {0};
        WGPUStatus st = yrdawn_client_wgpuAdapterGetInfo(c, adapter, &info);
        if (st == WGPUStatus_Success) {
            printf("remote WebGPU adapter:\r\n");
            print_strview("vendor:", info.vendor);
            print_strview("architecture:", info.architecture);
            print_strview("device:", info.device);
            print_strview("description:", info.description);
            printf("  vendorID:     0x%x\r\n", info.vendorID);
            printf("  deviceID:     0x%x\r\n", info.deviceID);
            printf("  backendType:  %u\r\n", (unsigned)info.backendType);
            printf("  adapterType:  %u\r\n", (unsigned)info.adapterType);
            LOG("01_dawn_info: printed adapter info (backend=%u)\n", (unsigned)info.backendType);
            yrdawn_client_wgpuAdapterInfoFreeMembers(c, info);
        } else {
            printf("adapterGetInfo failed (status=%u)\r\n", (unsigned)st);
            LOG("01_dawn_info: getInfo status=%u\n", (unsigned)st);
        }
    }

    printf("\r\npress 'q' or Ctrl-C to exit\r\n");

    /* Hold the session open so the printed info stays on screen until the
     * user quits (demo_on_key flips demo_quit_flag on 'q' / Ctrl-C). */
    while (!demo_quit_flag) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }

    (void)yrdawn_client_wgpuAdapterRelease(c, adapter);
    (void)yrdawn_client_wgpuInstanceRelease(c, instance);
    (void)yetty_yrdawn_canvas_destroy(canvas);
    (void)yetty_yrdawn_client_destroy(c);
    LOG("01_dawn_info: done\n");
    if (trace) {
        fclose(trace);
    }
    return 0;
}
