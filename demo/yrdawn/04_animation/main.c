/*
 * 04_animation — continuous frame streaming through the bridge.
 *
 * Pushes a 256×256 RGBA8 frame ~30 times per second for ~5 seconds.
 * Each frame is a hue-shifted gradient driven by a phase counter, so
 * the visible layer animates smoothly. Exercises sustained BULK
 * throughput, per-frame texture upload, and the layer's
 * request_render_fn pulse on every set_frame.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <yetty/yrdawn/client.h>

#include "common.h"

#define W 256u
#define H 256u
#define FRAMES 150           /* ~5 s at 30 fps */
#define FRAME_INTERVAL_MS 33

static void hsv_to_rgb(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    float c = v * s;
    float hh = h * 6.0f;
    float x = c * (1.0f - fabsf(fmodf(hh, 2.0f) - 1.0f));
    float rr, gg, bb;
    if (hh < 1.0f)      { rr = c; gg = x; bb = 0.0f; }
    else if (hh < 2.0f) { rr = x; gg = c; bb = 0.0f; }
    else if (hh < 3.0f) { rr = 0.0f; gg = c; bb = x; }
    else if (hh < 4.0f) { rr = 0.0f; gg = x; bb = c; }
    else if (hh < 5.0f) { rr = x; gg = 0.0f; bb = c; }
    else                { rr = c; gg = 0.0f; bb = x; }
    float m = v - c;
    *r = (uint8_t)((rr + m) * 255.0f);
    *g = (uint8_t)((gg + m) * 255.0f);
    *b = (uint8_t)((bb + m) * 255.0f);
}

static void make_frame(uint8_t *p, float phase)
{
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            float u = (float)x / (float)W;
            float v = (float)y / (float)H;
            float h = fmodf(u + v * 0.5f + phase, 1.0f);
            float s = 1.0f;
            float val = 0.6f + 0.4f * v;
            uint8_t *px = p + (y * W + x) * 4u;
            hsv_to_rgb(h, s, val, &px[0], &px[1], &px[2]);
            px[3] = 255u;
        }
    }
}

int main(void)
{
    demo_raw_stdin();
    FILE *trace = demo_trace_open("04-animation");
#define LOG(...) do { if (trace) fprintf(trace, __VA_ARGS__); } while (0)

    struct yetty_yrdawn_client *c = NULL;
    struct yetty_yrdawn_canvas *canvas =
        demo_bringup_single_canvas(/*figure_id=*/1, (float)W, (float)H, trace, &c);
    if (!canvas) {
        LOG("04_animation: bringup failed\n");
        return 1;
    }
    LOG("04_animation: connected=%d\n", yetty_yrdawn_canvas_connected(canvas));

    uint8_t *pixels = (uint8_t *)malloc((size_t)W * H * 4u);
    if (!pixels) {
        LOG("04_animation: oom\n");
        return 1;
    }

    int failures = 0;
    int i;
    for (i = 0; i < FRAMES && !demo_quit_flag; ++i) {
        float phase = (float)i / (float)FRAMES;
        make_frame(pixels, phase);
        struct yetty_ycore_void_result pr =
            yetty_yrdawn_canvas_present_frame(canvas, W, H, pixels, (size_t)W * H * 4u);
        if (pr.ok != 1) {
            LOG("04_animation: frame %d present_frame failed: %s\n", i, pr.error.msg);
            ++failures;
            if (failures > 3)
                break;
        }
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(FRAME_INTERVAL_MS);
    }
    if (demo_quit_flag) LOG("04_animation: 'q' pressed at frame %d\n", i);
    free(pixels);
    LOG("04_animation: %d frames done (failures=%d)\n", FRAMES, failures);

    (void)yetty_yrdawn_canvas_destroy(canvas);
    (void)yetty_yrdawn_client_destroy(c);
    if (trace) fclose(trace);
    return 0;
}
