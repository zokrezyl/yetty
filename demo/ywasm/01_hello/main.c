/*
 * 01_hello — minimal smoke test: HELLO → HELLO_ACK → BYE.
 *
 * Demonstrates the bridge bootstrap. After connecting, the layer holds
 * its default mint-coloured placeholder texture; this demo doesn't
 * paint anything itself.
 */
#include <stdio.h>
#include <unistd.h>

#include <yetty/ywasm/client.h>

#include "common.h"

int main(void)
{
    demo_raw_stdin();
    FILE *trace = demo_trace_open("01-hello");
#define LOG(...) do { if (trace) fprintf(trace, __VA_ARGS__); } while (0)

    struct yetty_ywasm_client_ptr_result cr =
        yetty_ywasm_client_create(STDIN_FILENO, STDOUT_FILENO);
    if (cr.ok != 1) {
        LOG("01_hello: client_create failed: %s\n", cr.error.msg);
        return 1;
    }
    struct yetty_ywasm_client *c = cr.value;
    LOG("01_hello: client ready\n");

    struct yetty_ycore_void_result hr = yetty_ywasm_client_send_hello(c);
    if (hr.ok != 1) {
        LOG("01_hello: send_hello failed: %s\n", hr.error.msg);
        return 1;
    }

    for (int i = 0; i < 200 && !yetty_ywasm_client_connected(c); ++i) {
        (void)yetty_ywasm_client_pump(c);
        demo_sleep_ms(10);
    }
    LOG("01_hello: connected=%d\n", yetty_ywasm_client_connected(c));

    /* Hold the session briefly so the user can see the layer is up. */
    for (int i = 0; i < 100; ++i) {
        (void)yetty_ywasm_client_pump(c);
        demo_sleep_ms(10);
    }

    (void)yetty_ywasm_client_send_bye(c);
    (void)yetty_ywasm_client_destroy(c);
    LOG("01_hello: done\n");
    if (trace) fclose(trace);
    return 0;
}
