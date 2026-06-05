#include "common.h"

#include <signal.h>
#include <stdio.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <yetty/yrdawn/client.h>

int demo_quit_flag = 0;

/* Input key kinds (mirrors enum yetty_client_input_key_kind). */
enum {
    DEMO_KEY_DOWN = 0,
    DEMO_KEY_UP = 1,
    DEMO_KEY_CHAR = 2,
};

/* Quit on 'q' or Ctrl-C.
 *
 * 'q' arrives as a CHAR event (codepoint 'q'). Ctrl-C does NOT produce a
 * CHAR under GLFW (control combos generate no text), so it comes as a
 * KEY_DOWN for the C key (GLFW keycode 67) with the control modifier bit
 * (GLFW_MOD_CONTROL = 0x0002) set. We also accept a CHAR of ETX (3) in
 * case a host ever cooks Ctrl-C into a literal control byte. */
static void demo_on_key(void *user, uint32_t kind, int32_t key, int32_t mods, uint32_t codepoint)
{
    (void)user;
    const int control_held = (mods & 0x0002) != 0; /* GLFW_MOD_CONTROL */

    if (kind == DEMO_KEY_CHAR && (codepoint == 'q' || codepoint == 3 /* ETX */)) {
        demo_quit_flag = 1;
    } else if (kind == DEMO_KEY_DOWN && control_held && key == 67 /* GLFW_KEY_C */) {
        demo_quit_flag = 1;
    }
}

void demo_install_quit_on_q(struct yetty_yrdawn_canvas *canvas)
{
    if (!canvas) {
        return;
    }
    yetty_yrdawn_canvas_set_input_key_cb(canvas, demo_on_key, NULL);
}

/* Raw-byte input: plain keystrokes the host forwards to the PTY when no
 * figure has keyboard focus. A focused yrdawn canvas would get key events
 * via demo_on_key, but the demos don't grab focus, so this is the path that
 * actually fires. 'q' arrives as the byte 'q'; Ctrl-C as ETX (0x03). */
static void demo_on_raw(void *user, const char *bytes, size_t len)
{
    (void)user;
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)bytes[i];
        if (ch == 'q' || ch == 0x03 /* Ctrl-C (ETX) */) {
            demo_quit_flag = 1;
        }
    }
}

void demo_install_quit_input(struct yetty_yrdawn_client *client)
{
    if (!client) {
        return;
    }
    yetty_yrdawn_client_set_raw_input_cb(client, demo_on_raw, NULL);
}

void demo_raw_stdin(void)
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

    /* SIGPIPE on the layer dropping our pipe would otherwise tear us
     * down; let the write fail with EPIPE and propagate normally. */
    signal(SIGPIPE, SIG_IGN);
}

void demo_sleep_ms(int ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};
    (void)nanosleep(&ts, NULL);
}

FILE *demo_trace_open(const char *demo_name)
{
    char path[256];
    snprintf(path, sizeof(path), "/tmp/yrdawn-demo-%s.trace", demo_name);
    FILE *f = fopen(path, "w");
    if (f) {
        setvbuf(f, NULL, _IOLBF, 0);
    }
    return f;
}

struct yetty_yrdawn_canvas *demo_bringup_single_canvas(uint32_t figure_id, float w, float h,
                                                       FILE *trace,
                                                       struct yetty_yrdawn_client **out_client)
{
    if (out_client) {
        *out_client = NULL;
    }
    /* session_id must be non-zero. pid is the obvious unique pick per
     * process and survives `&`-backgrounded demos sharing one PTY. */
    uint32_t session_id = (uint32_t)getpid();
    if (session_id == 0) {
        session_id = 1; /* defensive — pid 0 doesn't exist on POSIX */
    }
    struct yetty_yrdawn_client_ptr_result cr =
        yetty_yrdawn_client_create(STDIN_FILENO, STDOUT_FILENO, session_id);
    if (cr.ok != 1) {
        if (trace) {
            fprintf(trace, "demo_bringup: client_create failed: %s\n", cr.error.msg);
        }
        return NULL;
    }
    struct yetty_yrdawn_client *c = cr.value;
    /* Quit on 'q' / Ctrl-C via the raw-tty input path. */
    demo_install_quit_input(c);

    /* Small inset from the pane's top-left so the canvas doesn't sit
     * underneath the host's chrome (tab bar etc). */
    struct yetty_yrdawn_canvas_ptr_result kr =
        yetty_yrdawn_canvas_create(c, figure_id, 16.0f, 16.0f, w, h);
    if (kr.ok != 1) {
        if (trace) {
            fprintf(trace, "demo_bringup: canvas_create failed: %s\n", kr.error.msg);
        }
        (void)yetty_yrdawn_client_destroy(c);
        return NULL;
    }
    struct yetty_yrdawn_canvas *cv = kr.value;
    demo_install_quit_on_q(cv);

    /* Pump until HELLO_ACK lands. ~2 s cap. */
    for (int i = 0; i < 200 && !yetty_yrdawn_canvas_connected(cv); ++i) {
        (void)yetty_yrdawn_client_pump(c);
        demo_sleep_ms(10);
    }
    if (trace) {
        fprintf(trace, "demo_bringup: connected=%d figure_id=%u\n",
                yetty_yrdawn_canvas_connected(cv), figure_id);
    }

    if (out_client) {
        *out_client = c;
    }
    return cv;
}
