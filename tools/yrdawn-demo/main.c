/*
 * yrdawn-demo — smoke test for the WebGPU-over-OSC bridge.
 *
 * Run from inside a yetty terminal as `./yetty -e yrdawn-demo`. The tool
 * links libyetty_yrdawn, talks to the local yetty's yrdawn-layer over
 * stdin/stdout (OSC envelopes via yface), and presents a 256x256 RGBA8
 * gradient — that lands in the layer's display texture and the layer
 * renders it as a fullscreen quad.
 *
 * No webgpu.h calls yet — this exercises only the protocol round trip
 * and the present_frame path. Real wgpu* calls come once the codegen
 * method table expands.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <yetty/yrdawn/client.h>
#include <yetty/yrdawn/methods.gen.h>

#define W 256u
#define H 256u

/* Async exercise state — populated by file-scope reply callbacks via the
 * `user` cookie. Held in main()'s stack; passed by address. */
struct async_state {
    FILE *trace;
    int adapter_done;
    int device_done;
    uint32_t adapter_status;
    uint32_t device_status;
    uint64_t adapter_h;
    uint64_t device_h;
};

static void on_adapter(void *u, uint32_t status, uint32_t mid, const uint8_t *body, size_t body_len)
{
    (void)mid;
    (void)body;
    (void)body_len;
    struct async_state *s = (struct async_state *)u;
    s->adapter_status = status;
    s->adapter_done = 1;
    if (s->trace) {
        fprintf(s->trace, "demo: adapter cb status=%u\n", status);
    }
}

static void on_device(void *u, uint32_t status, uint32_t mid, const uint8_t *body, size_t body_len)
{
    (void)mid;
    (void)body;
    (void)body_len;
    struct async_state *s = (struct async_state *)u;
    s->device_status = status;
    s->device_done = 1;
    if (s->trace) {
        fprintf(s->trace, "demo: device cb status=%u\n", status);
    }
}

/* Raw mode on stdin: no canonical processing, no echo, no CR↔LF
 * fiddling, no SIGINT/SIGTSTP, no XON/XOFF. The OSC envelopes from
 * yetty must arrive byte-for-byte, otherwise the kernel echoes ESC as
 * `^[` and yface can never parse the reply. */
static void setup_raw_stdin(void)
{
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) != 0) {
        return;
    }
    t.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ISIG);
    t.c_iflag &= ~(IXON | IXOFF | ICRNL | INLCR | IGNCR | BRKINT | INPCK | ISTRIP);
    t.c_oflag &= ~OPOST;
    t.c_cflag |= CS8;
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

static void make_gradient(uint8_t *p)
{
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            uint8_t *px = p + (y * W + x) * 4u;
            px[0] = (uint8_t)(x);        /* R sweeps left→right */
            px[1] = (uint8_t)(y);        /* G sweeps top→bottom */
            px[2] = (uint8_t)(255u - x); /* B inverse of R */
            px[3] = 255u;
        }
    }
}

static void sleep_ms(int ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};
    (void)nanosleep(&ts, NULL);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    setup_raw_stdin();
    /* Avoid SIGPIPE bringing us down if the layer drops the pipe
     * before the linger loop ends. */
    signal(SIGPIPE, SIG_IGN);

    /* Trace lines go to a side log file. fprintf to stderr would mix
     * into the PTY stream and corrupt the wire protocol. */
    FILE *trace = fopen("/tmp/yrdawn-demo.trace", "w");
    if (trace) {
        setvbuf(trace, NULL, _IOLBF, 0);
    }
#define DEMO_TRACE(...)                                                                            \
    do {                                                                                           \
        if (trace) {                                                                               \
            fprintf(trace, __VA_ARGS__);                                                           \
        }                                                                                          \
    } while (0)

    DEMO_TRACE("demo: start\n");

    uint32_t session_id = (uint32_t)getpid();
    if (session_id == 0) {
        session_id = 1;
    }
    struct yetty_yrdawn_client_ptr_result cr =
        yetty_yrdawn_client_create(STDIN_FILENO, STDOUT_FILENO, session_id);
    if (cr.ok != 1) {
        DEMO_TRACE("demo: client_create failed: %s\n", cr.error.msg);
        return 1;
    }
    struct yetty_yrdawn_client *c = cr.value;
    DEMO_TRACE("demo: client created\n");

    struct yetty_yrdawn_canvas_ptr_result kr =
        yetty_yrdawn_canvas_create(c, /*figure_id=*/1, 16.0f, 16.0f, (float)W, (float)H);
    if (kr.ok != 1) {
        DEMO_TRACE("demo: canvas_create failed: %s\n", kr.error.msg);
        return 1;
    }
    struct yetty_yrdawn_canvas *canvas = kr.value;
    DEMO_TRACE("demo: canvas created\n");

    /* Pump up to ~2s waiting for HELLO_ACK. */
    for (int i = 0; i < 200; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        if (yetty_yrdawn_canvas_connected(canvas)) {
            break;
        }
        sleep_ms(10);
    }
    DEMO_TRACE("demo: connected=%d after pump\n", yetty_yrdawn_canvas_connected(canvas));

    uint8_t *pixels = (uint8_t *)malloc((size_t)W * H * 4u);
    if (!pixels) {
        DEMO_TRACE("demo: oom\n");
        return 1;
    }
    make_gradient(pixels);
    DEMO_TRACE("demo: gradient built; presenting %ux%u\n", W, H);

    struct yetty_ycore_void_result pr =
        yetty_yrdawn_canvas_present_frame(canvas, W, H, pixels, (size_t)W * H * 4u);
    free(pixels);
    if (pr.ok != 1) {
        DEMO_TRACE("demo: present_frame failed: %s\n", pr.error.msg);
        return 1;
    }
    DEMO_TRACE("demo: present_frame returned OK\n");

    struct async_state st = {trace, 0, 0, 0, 0, 0, 0};

    uint64_t instance_h = yrdawn_client_wgpuCreateInstance(c);
    DEMO_TRACE("demo: createInstance -> handle=%lu\n", (unsigned long)instance_h);

    st.adapter_h = yrdawn_client_wgpuInstanceRequestAdapter(c, instance_h, on_adapter, &st);
    DEMO_TRACE("demo: requestAdapter pending (handle=%lu)\n", (unsigned long)st.adapter_h);

    for (int i = 0; i < 300 && !st.adapter_done; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        sleep_ms(10);
    }
    DEMO_TRACE("demo: adapter_done=%d status=%u\n", st.adapter_done, st.adapter_status);

    if (st.adapter_done && st.adapter_status == 0) {
        st.device_h = yrdawn_client_wgpuAdapterRequestDevice(c, st.adapter_h, on_device, &st);
        DEMO_TRACE("demo: requestDevice pending (handle=%lu)\n", (unsigned long)st.device_h);
        for (int i = 0; i < 300 && !st.device_done; ++i) {
            (void)yetty_yrdawn_client_pump(c);
            sleep_ms(10);
        }
        DEMO_TRACE("demo: device_done=%d status=%u\n", st.device_done, st.device_status);
    }

    /* Linger so the user sees the rendered frame before stdin closes. */
    for (int i = 0; i < 100; ++i) {
        (void)yetty_yrdawn_client_pump(c);
        sleep_ms(10);
    }
    DEMO_TRACE("demo: linger done\n");

    (void)yetty_yrdawn_canvas_destroy(canvas);
    (void)yetty_yrdawn_client_destroy(c);
    DEMO_TRACE("demo: clean exit\n");
    if (trace) {
        fclose(trace);
    }
    return 0;
}
