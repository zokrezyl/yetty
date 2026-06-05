/*
 * 11_two_canvases — one client process, two yrdawn canvases.
 *
 * The whole point of the figure-tree migration: a single yrdawn
 * client can now mint as many independent canvases as it wants. Both
 * canvases share one session (pid-derived session_id) on the server,
 * so the WGPU handle table is shared — but each canvas owns its own
 * presentation texture and renders into its own rect on the host
 * pane.
 *
 * Visual: side-by-side at the top of the host pane. Left canvas
 * (figure_id=1) presents a red→yellow horizontal gradient; right
 * canvas (figure_id=2) presents a blue→cyan vertical gradient.
 * 'q' on either canvas exits.
 *
 * Run:    ./yetty -e demo-yrdawn-11-two-canvases
 * Trace:  /tmp/yrdawn-demo-11-two-canvases.trace
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <yetty/yrdawn/client.h>

#include "common.h"

#define W 256u
#define H 256u

static void make_left(uint8_t *p)
{
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            uint8_t *px = p + (y * W + x) * 4u;
            float u = (float)x / (float)(W - 1u);
            px[0] = 255u;                    /* red */
            px[1] = (uint8_t)(u * 255.0f);   /* green ramps L→R */
            px[2] = 0u;
            px[3] = 255u;
        }
    }
}

static void make_right(uint8_t *p)
{
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            uint8_t *px = p + (y * W + x) * 4u;
            float v = (float)y / (float)(H - 1u);
            px[0] = 0u;
            px[1] = (uint8_t)(v * 255.0f);   /* green ramps T→B */
            px[2] = 255u;                    /* blue */
            px[3] = 255u;
        }
    }
}

int main(void)
{
    demo_raw_stdin();
    FILE *trace = demo_trace_open("11-two-canvases");
#define LOG(...) \
    do { \
        if (trace) \
            fprintf(trace, __VA_ARGS__); \
    } while (0)

    /* One client, one session — both canvases share the same WGPU
     * handle table on the server. */
    uint32_t session_id = (uint32_t)getpid();
    if (session_id == 0) {
        session_id = 1;
    }
    struct yetty_yrdawn_client_ptr_result cr =
        yetty_yrdawn_client_create(STDIN_FILENO, STDOUT_FILENO, session_id);
    if (cr.ok != 1) {
        LOG("11: client_create failed: %s\n", cr.error.msg);
        return 1;
    }
    struct yetty_yrdawn_client *c = cr.value;
    /* Quit on 'q' / Ctrl-C (raw-tty input path). */
    demo_install_quit_input(c);

    /* Side-by-side at the top of the pane. The compositor will accept
     * arbitrary rects; the host has the final word via SET_CHILD_RECT
     * if it wants to reposition. */
    struct yetty_yrdawn_canvas_ptr_result lr =
        yetty_yrdawn_canvas_create(c, /*figure_id=*/1, 16.0f, 16.0f, (float)W, (float)H);
    if (lr.ok != 1) {
        LOG("11: left canvas create failed: %s\n", lr.error.msg);
        (void)yetty_yrdawn_client_destroy(c);
        return 1;
    }
    struct yetty_yrdawn_canvas *left = lr.value;

    struct yetty_yrdawn_canvas_ptr_result rr =
        yetty_yrdawn_canvas_create(c, /*figure_id=*/2, 16.0f + (float)W + 16.0f, 16.0f, (float)W,
                                   (float)H);
    if (rr.ok != 1) {
        LOG("11: right canvas create failed: %s\n", rr.error.msg);
        (void)yetty_yrdawn_canvas_destroy(left);
        (void)yetty_yrdawn_client_destroy(c);
        return 1;
    }
    struct yetty_yrdawn_canvas *right = rr.value;

    demo_install_quit_on_q(left);
    demo_install_quit_on_q(right);

    /* Pump until both HELLO_ACKs land. */
    for (int i = 0; i < 200; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        if (yetty_yrdawn_canvas_connected(left) && yetty_yrdawn_canvas_connected(right)) {
            break;
        }
        demo_sleep_ms(10);
    }
    LOG("11: connected left=%d right=%d (session_id=%u)\n", yetty_yrdawn_canvas_connected(left),
        yetty_yrdawn_canvas_connected(right), session_id);

    uint8_t *pixels_left = malloc((size_t)W * H * 4u);
    uint8_t *pixels_right = malloc((size_t)W * H * 4u);
    if (!pixels_left || !pixels_right) {
        LOG("11: oom\n");
        free(pixels_left);
        free(pixels_right);
        (void)yetty_yrdawn_canvas_destroy(right);
        (void)yetty_yrdawn_canvas_destroy(left);
        (void)yetty_yrdawn_client_destroy(c);
        return 1;
    }
    make_left(pixels_left);
    make_right(pixels_right);

    struct yetty_ycore_void_result lp =
        yetty_yrdawn_canvas_present_frame(left, W, H, pixels_left, (size_t)W * H * 4u);
    if (lp.ok != 1) {
        LOG("11: present left failed: %s\n", lp.error.msg);
    }
    struct yetty_ycore_void_result rp =
        yetty_yrdawn_canvas_present_frame(right, W, H, pixels_right, (size_t)W * H * 4u);
    if (rp.ok != 1) {
        LOG("11: present right failed: %s\n", rp.error.msg);
    }
    LOG("11: presented both canvases\n");

    free(pixels_left);
    free(pixels_right);

    /* Hold open so the user can see both rectangles. 'q' on either
     * canvas exits. */
    for (int i = 0; i < 1000 && !demo_quit_flag; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }

    (void)yetty_yrdawn_canvas_destroy(right);
    (void)yetty_yrdawn_canvas_destroy(left);
    (void)yetty_yrdawn_client_destroy(c);
    LOG("11: done\n");
    if (trace) {
        fclose(trace);
    }
    return 0;
}
