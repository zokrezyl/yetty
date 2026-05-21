/*
 * 02_gradient — present a single 256×256 RGBA8 gradient frame.
 *
 * Exercises the BULK channel + the yetty-defined yetty_ywasm_present_frame
 * meta-method. Layer rebuilds its display texture to match, uploads via
 * wgpuQueueWriteTexture, render path samples it as a fullscreen quad.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <yetty/ywasm/client.h>

#include "common.h"

#define W 256u
#define H 256u

static void make_gradient(uint8_t *p)
{
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            uint8_t *px = p + (y * W + x) * 4u;
            px[0] = (uint8_t)x;            /* R  sweeps left → right    */
            px[1] = (uint8_t)y;            /* G  sweeps top → bottom    */
            px[2] = (uint8_t)(255u - x);   /* B  is the inverse of R     */
            px[3] = 255u;
        }
    }
}

int main(void)
{
    demo_raw_stdin();
    FILE *trace = demo_trace_open("02-gradient");
#define LOG(...) do { if (trace) fprintf(trace, __VA_ARGS__); } while (0)

    struct yetty_ywasm_client_ptr_result cr =
        yetty_ywasm_client_create(STDIN_FILENO, STDOUT_FILENO);
    if (cr.ok != 1) {
        LOG("02_gradient: client_create failed: %s\n", cr.error.msg);
        return 1;
    }
    struct yetty_ywasm_client *c = cr.value;

    (void)yetty_ywasm_client_send_hello(c);
    for (int i = 0; i < 200 && !yetty_ywasm_client_connected(c); ++i) {
        (void)yetty_ywasm_client_pump(c);
        demo_sleep_ms(10);
    }
    LOG("02_gradient: connected=%d\n", yetty_ywasm_client_connected(c));

    uint8_t *pixels = (uint8_t *)malloc((size_t)W * H * 4u);
    if (!pixels) {
        LOG("02_gradient: oom\n");
        return 1;
    }
    make_gradient(pixels);

    struct yetty_ycore_void_result pr =
        yetty_ywasm_client_present_frame(c, W, H, pixels, (size_t)W * H * 4u);
    free(pixels);
    if (pr.ok != 1) {
        LOG("02_gradient: present_frame failed: %s\n", pr.error.msg);
        return 1;
    }
    LOG("02_gradient: presented %ux%u\n", W, H);

    /* Linger so the rendered gradient stays on screen. */
    for (int i = 0; i < 500; ++i) {
        (void)yetty_ywasm_client_pump(c);
        demo_sleep_ms(10);
    }

    (void)yetty_ywasm_client_send_bye(c);
    (void)yetty_ywasm_client_destroy(c);
    LOG("02_gradient: done\n");
    if (trace) fclose(trace);
    return 0;
}
