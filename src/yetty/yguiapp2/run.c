/* yguiapp2 terminal host — see include/yetty/yguiapp2/run.h. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <yetty/api/ygui2/framework.h>
#include <yetty/ygui2/defs.h>
#include <yetty/yguiapp2/run.h>

struct yguiapp2_state {
    int running;
};

static int yguiapp2_key_cb(uint32_t key, uint32_t mods, void *userdata)
{
    struct yguiapp2_state *state = userdata;
    (void)mods;
    if (key == 'q' || key == 0x03) {
        state->running = 0;
        return 1;
    }
    return 0;
}

/* Wait (bounded) for the terminal's HOLD-ACK while keeping the parser
 * alive — a key the host forwarded before arming its barrier comes back
 * here and is consumed by the still-live framework, not by whatever reads
 * the PTY after us. */
static void yguiapp2_hold_barrier(struct yetty_yclass_object *framework)
{
    struct yetty_ycore_void_result hold_res = yetty_ygui2_framework_send_hold(framework);
    if (YETTY_IS_ERR(hold_res)) {
        yetty_ycore_error_destroy(hold_res.error);
        return;
    }
    for (int round = 0; round < 50; ++round) { /* <= 500 ms */
        struct yetty_ycore_int_result ack_res = yetty_ygui2_framework_hold_ack_seen(framework);
        if (YETTY_IS_OK(ack_res) && ack_res.value) {
            return;
        }
        if (YETTY_IS_ERR(ack_res)) {
            yetty_ycore_error_destroy(ack_res.error);
            return;
        }
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 10000};
        int ready = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);
        if (ready > 0 && FD_ISSET(STDIN_FILENO, &read_fds)) {
            uint8_t bytes[256];
            ssize_t byte_count = read(STDIN_FILENO, bytes, sizeof(bytes));
            if (byte_count <= 0) {
                return;
            }
            struct yetty_ycore_void_result feed_res =
                yetty_ygui2_framework_feed_input(framework, bytes, (size_t)byte_count);
            if (YETTY_IS_ERR(feed_res)) {
                yetty_ycore_error_destroy(feed_res.error);
            }
        }
    }
}

int yetty_yguiapp2_terminal_main(yetty_yguiapp2_build_fn build, yetty_yguiapp2_tick_fn tick,
                                 int tick_ms, void *userdata)
{
    if (!build) {
        fprintf(stderr, "yguiapp2: NULL build callback\n");
        return 2;
    }
    struct yguiapp2_state state = {.running = 1};
    struct winsize window_size = {0};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_size);
    float viewport_w = (float)(window_size.ws_col ? window_size.ws_col : 80) * 8.0f;
    float viewport_h = (float)(window_size.ws_row ? window_size.ws_row : 40) * 16.0f;

    struct termios saved_termios;
    if (tcgetattr(STDIN_FILENO, &saved_termios) != 0) {
        fprintf(stderr, "yguiapp2: stdin is not a terminal\n");
        return 2;
    }
    /* Real raw input: no canonical buffering, no echo, no signals (Ctrl-C
     * must arrive as byte 0x03 and take the clean quit path), no CR->LF
     * translation (Enter is CR 13, the widget key contract), no flow
     * control. Output processing stays on — nothing here needs it off. */
    struct termios raw_termios = saved_termios;
    raw_termios.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG | IEXTEN);
    raw_termios.c_iflag &= ~(tcflag_t)(ICRNL | INLCR | IXON | IXOFF | BRKINT | INPCK | ISTRIP);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios) != 0) {
        fprintf(stderr, "yguiapp2: tcsetattr failed\n");
        return 2;
    }
    fputs("\x1b[?1049h\x1b[?25l\x1b[H", stdout);
    fflush(stdout);

    int exit_code = 0;
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_framework_make();
    if (YETTY_IS_ERR(framework_res)) {
        fprintf(stderr, "yguiapp2: framework: %s\n", framework_res.error.msg);
        yetty_ycore_error_destroy(framework_res.error);
        exit_code = 1;
        goto restore;
    }
    struct yetty_yclass_object *framework = framework_res.value;
    {
        struct yetty_ycore_void_result attach_res =
            yetty_ygui2_framework_attach(framework, STDIN_FILENO, STDOUT_FILENO);
        struct yetty_ycore_void_result viewport_res =
            yetty_ygui2_framework_set_viewport(framework, viewport_w, viewport_h);
        struct yetty_ycore_void_result key_cb_res =
            yetty_ygui2_framework_set_key_cb(framework, yguiapp2_key_cb, &state);
        if (YETTY_IS_ERR(attach_res) || YETTY_IS_ERR(viewport_res) || YETTY_IS_ERR(key_cb_res)) {
            if (YETTY_IS_ERR(attach_res)) {
                yetty_ycore_error_destroy(attach_res.error);
            }
            if (YETTY_IS_ERR(viewport_res)) {
                yetty_ycore_error_destroy(viewport_res.error);
            }
            if (YETTY_IS_ERR(key_cb_res)) {
                yetty_ycore_error_destroy(key_cb_res.error);
            }
            exit_code = 1;
            goto teardown;
        }
    }

    struct yetty_ycore_void_result build_res = build(framework, userdata);
    if (YETTY_IS_ERR(build_res)) {
        fprintf(stderr, "yguiapp2: build: %s\n", build_res.error.msg);
        yetty_ycore_error_destroy(build_res.error);
        exit_code = 1;
        goto teardown;
    }
    {
        struct yetty_ycore_void_result emit_res = yetty_ygui2_framework_emit(framework);
        if (YETTY_IS_ERR(emit_res)) {
            fprintf(stderr, "yguiapp2: first emit: %s\n", emit_res.error.msg);
            yetty_ycore_error_destroy(emit_res.error);
            exit_code = 1;
            goto teardown;
        }
    }
    if (tick_ms <= 0) {
        tick_ms = 250;
    }
    /* MONOTONIC tick schedule. The select below wakes on EVERY input
     * envelope — pane mouse moves arrive at pointer frequency — and the
     * animation tick must not follow that: it fires on its own deadline
     * only, no matter how busy stdin is. */
    struct timespec now_spec;
    clock_gettime(CLOCK_MONOTONIC, &now_spec);
    double now_seconds = (double)now_spec.tv_sec + (double)now_spec.tv_nsec / 1e9;
    double tick_interval = (double)tick_ms / 1000.0;
    double next_tick_seconds = now_seconds + tick_interval;
    while (state.running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        clock_gettime(CLOCK_MONOTONIC, &now_spec);
        now_seconds = (double)now_spec.tv_sec + (double)now_spec.tv_nsec / 1e9;
        double wait_seconds = next_tick_seconds - now_seconds;
        if (wait_seconds < 0.0) {
            wait_seconds = 0.0;
        }
        struct timeval timeout = {
            .tv_sec = (time_t)wait_seconds,
            .tv_usec = (suseconds_t)((wait_seconds - (double)(time_t)wait_seconds) * 1e6)};
        int ready = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);
        if (ready > 0 && FD_ISSET(STDIN_FILENO, &read_fds)) {
            uint8_t bytes[256];
            ssize_t byte_count = read(STDIN_FILENO, bytes, sizeof(bytes));
            if (byte_count <= 0) {
                break; /* EOF / error: never spin */
            }
            struct yetty_ycore_void_result feed_res =
                yetty_ygui2_framework_feed_input(framework, bytes, (size_t)byte_count);
            if (YETTY_IS_ERR(feed_res)) {
                yetty_ycore_error_destroy(feed_res.error);
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &now_spec);
        now_seconds = (double)now_spec.tv_sec + (double)now_spec.tv_nsec / 1e9;
        if (now_seconds >= next_tick_seconds) {
            /* Deadline work: resolve a retained lone Esc as a real
             * Escape keypress, then the app's animation tick. */
            struct yetty_ycore_void_result flush_res =
                yetty_ygui2_framework_feed_input_flush(framework);
            if (YETTY_IS_ERR(flush_res)) {
                yetty_ycore_error_destroy(flush_res.error);
            }
            if (tick) {
                struct yetty_ycore_void_result tick_res = tick(framework, userdata);
                if (YETTY_IS_ERR(tick_res)) {
                    yetty_ycore_error_destroy(tick_res.error);
                }
            }
            next_tick_seconds = now_seconds + tick_interval;
        }
        struct yetty_ycore_int_result dirty_res = yetty_ygui2_framework_is_dirty(framework);
        if (YETTY_IS_OK(dirty_res) && dirty_res.value) {
            struct yetty_ycore_void_result emit_res = yetty_ygui2_framework_emit(framework);
            if (YETTY_IS_ERR(emit_res)) {
                yetty_ycore_error_destroy(emit_res.error);
            }
        } else if (YETTY_IS_ERR(dirty_res)) {
            yetty_ycore_error_destroy(dirty_res.error);
        }
        fflush(stdout);
    }
teardown:
    /* Exit-window input barrier FIRST: the host holds keystrokes host-side
     * until our PTY closes, so nothing typed during teardown is consumed
     * by the dying client or leaks as envelope bytes to the shell. */
    yguiapp2_hold_barrier(framework);
    {
        struct yetty_ycore_void_result clear_res = yetty_ygui2_framework_clear(framework);
        if (YETTY_IS_ERR(clear_res)) {
            yetty_ycore_error_destroy(clear_res.error);
        }
        /* Unsubscribe pane input BEFORE the terminal restore — the pane
         * must stop forwarding mouse envelopes once the app is gone. */
        struct yetty_ycore_void_result detach_res = yetty_ygui2_framework_detach(framework);
        if (YETTY_IS_ERR(detach_res)) {
            yetty_ycore_error_destroy(detach_res.error);
        }
        struct yetty_ycore_void_result dispose_res = yetty_ygui2_framework_dispose(framework);
        if (YETTY_IS_ERR(dispose_res)) {
            yetty_ycore_error_destroy(dispose_res.error);
        }
    }
restore:
    fputs("\x1b[?25h\x1b[?1049l", stdout);
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
    return exit_code;
}
