#include "common.h"

#include <signal.h>
#include <stdio.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

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
    snprintf(path, sizeof(path), "/tmp/ywasm-demo-%s.trace", demo_name);
    FILE *f = fopen(path, "w");
    if (f)
        setvbuf(f, NULL, _IOLBF, 0);
    return f;
}
