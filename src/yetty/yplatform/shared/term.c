/* term.c - POSIX terminal helpers */

#include <yetty/yplatform/term.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

int yplatform_stderr_supports_color(void)
{
    const char *term = getenv("TERM");
    if (!term || !isatty(fileno(stderr))) {
        return 0;
    }

    return (strstr(term, "color") != NULL || strstr(term, "xterm") != NULL ||
            strstr(term, "screen") != NULL || strstr(term, "tmux") != NULL ||
            strcmp(term, "linux") == 0);
}

void yplatform_format_timestamp(char *buf, size_t bufsize)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm = localtime(&tv.tv_sec);
    snprintf(buf, bufsize, "%02d:%02d:%02d.%03ld", tm->tm_hour, tm->tm_min, tm->tm_sec,
             tv.tv_usec / 1000);
}
