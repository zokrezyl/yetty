/*
 * 02_gradient — present a single 256×256 RGBA8 gradient frame to one canvas.
 *
 * Exercises the BULK channel + the yetty-defined yetty_yrdawn_present_frame
 * meta-method. The figure rebuilds its display texture to match,
 * uploads via wgpuQueueWriteTexture, and the render path samples it
 * as a fullscreen quad inside the canvas's rect.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <yetty/yrdawn/client.h>

#include "common.h"

#define W 256u
#define H 256u

static void make_gradient(uint8_t *p)
{
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            uint8_t *px = p + (y * W + x) * 4u;
            px[0] = (uint8_t)x;          /* R sweeps left → right     */
            px[1] = (uint8_t)y;          /* G sweeps top  → bottom    */
            px[2] = (uint8_t)(255u - x); /* B is the inverse of R     */
            px[3] = 255u;
        }
    }
}

int main(void)
{
    demo_raw_stdin();
    FILE *trace = demo_trace_open("02-gradient");
#define LOG(...) \
    do { \
        if (trace) \
            fprintf(trace, __VA_ARGS__); \
    } while (0)

    struct yetty_yrdawn_client *c = NULL;
    struct yetty_yrdawn_canvas *canvas = demo_bringup_single_canvas(/*figure_id=*/1, trace, &c);
    if (!canvas) {
        LOG("02_gradient: bringup failed\n");
        return 1;
    }
    LOG("02_gradient: connected=%d\n", yetty_yrdawn_canvas_connected(canvas));

    uint8_t *pixels = malloc((size_t)W * H * 4u);
    if (!pixels) {
        LOG("02_gradient: oom\n");
        return 1;
    }
    make_gradient(pixels);

    struct yetty_ycore_void_result pr =
        yetty_yrdawn_canvas_present_frame(canvas, W, H, pixels, (size_t)W * H * 4u);
    free(pixels);
    if (pr.ok != 1) {
        LOG("02_gradient: present_frame failed: %s\n", pr.error.msg);
        return 1;
    }
    LOG("02_gradient: presented %ux%u\n", W, H);

    for (int i = 0; i < 500 && !demo_quit_flag; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }

    (void)yetty_yrdawn_canvas_destroy(canvas);
    (void)yetty_yrdawn_client_destroy(c);
    LOG("02_gradient: done\n");
    if (trace)
        fclose(trace);
    return 0;
}
