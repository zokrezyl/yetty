#include "common.h"

#include <signal.h>
#include <stdio.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <yetty/yrdawn/client.h>

int demo_quit_flag = 0;

static void demo_on_key(void *user, uint32_t kind, int32_t key, int32_t mods,
                        uint32_t codepoint)
{
    (void)user; (void)key; (void)mods;
    if (kind == 2 /* YETTY_YRDAWN_INPUT_KEY_CHAR */ && codepoint == 'q')
        demo_quit_flag = 1;
}

void demo_install_quit_on_q(struct yetty_yrdawn_client *c)
{
    if (!c) return;
    yetty_yrdawn_client_set_input_key_cb(c, demo_on_key, NULL);
}

void demo_raw_stdin(void)
{
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) != 0)
        return;
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
    if (f)
        setvbuf(f, NULL, _IOLBF, 0);
    return f;
}
