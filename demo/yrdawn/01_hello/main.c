/*
 * 01_hello — minimal smoke test: open one canvas, hold it open until 'q'.
 *
 * The figure-tree CREATE_CHILD that demo_bringup_single_canvas emits
 * already carries SUB_HELLO; the server's HELLO_ACK flips the
 * canvas's `connected` bit. This demo doesn't present any pixels;
 * the layer holds whatever it has (typically nothing) until exit.
 */
#include <stdio.h>
#include <unistd.h>

#include <yetty/yrdawn/client.h>

#include "common.h"

int main(void)
{
    demo_raw_stdin();
    FILE *trace = demo_trace_open("01-hello");
#define LOG(...) \
    do { \
        if (trace) \
            fprintf(trace, __VA_ARGS__); \
    } while (0)

    struct yetty_yrdawn_client *c = NULL;
    struct yetty_yrdawn_canvas *canvas =
        demo_bringup_single_canvas(/*figure_id=*/1, 256.0f, 256.0f, trace, &c);
    if (!canvas) {
        LOG("01_hello: bringup failed\n");
        return 1;
    }
    LOG("01_hello: connected=%d\n", yetty_yrdawn_canvas_connected(canvas));

    for (int i = 0; i < 100 && !demo_quit_flag; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }

    (void)yetty_yrdawn_canvas_destroy(canvas);
    (void)yetty_yrdawn_client_destroy(c);
    LOG("01_hello: done\n");
    if (trace)
        fclose(trace);
    return 0;
}
