/* term.c - POSIX terminal helpers */

#include <yetty/yplatform/term.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>

int yetty_yplatform_stderr_supports_color(void)
{
    const char *term = getenv("TERM");
    if (!term || !isatty(fileno(stderr))) {
        return 0;
    }

    return (strstr(term, "color") != NULL || strstr(term, "xterm") != NULL ||
            strstr(term, "screen") != NULL || strstr(term, "tmux") != NULL ||
            strcmp(term, "linux") == 0);
}

void yetty_yplatform_format_timestamp(char *buf, size_t bufsize)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm = localtime(&tv.tv_sec);
    snprintf(buf, bufsize, "%02d:%02d:%02d.%03ld", tm->tm_hour, tm->tm_min, tm->tm_sec,
             tv.tv_usec / 1000);
}

int yetty_yplatform_term_get_size_pixels(int *cols, int *rows, int *pixel_width, int *pixel_height)
{
    /* Probe stdout first, then stdin — interactive shells usually have
     * both, but a pipe on stdout (e.g. `yetty | tee`) shouldn't make us
     * report (0, 0). ws_xpixel / ws_ypixel carry the pane pixel area when
     * the terminal reports it (yetty populates them); they are 0 otherwise. */
    struct winsize ws = {0};
    int fds[] = {STDOUT_FILENO, STDIN_FILENO, STDERR_FILENO};
    for (size_t i = 0; i < sizeof(fds) / sizeof(fds[0]); i++) {
        if (ioctl(fds[i], TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
            if (cols) {
                *cols = ws.ws_col;
            }
            if (rows) {
                *rows = ws.ws_row;
            }
            if (pixel_width) {
                *pixel_width = ws.ws_xpixel;
            }
            if (pixel_height) {
                *pixel_height = ws.ws_ypixel;
            }
            return 0;
        }
    }
    return -1;
}

int yetty_yplatform_term_get_size(int *cols, int *rows)
{
    return yetty_yplatform_term_get_size_pixels(cols, rows, NULL, NULL);
}
