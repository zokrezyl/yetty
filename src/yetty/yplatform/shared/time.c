/* time.c - POSIX time implementation */

#include <yetty/yplatform/time.h>
#include <time.h>
#include <unistd.h>

double yetty_yplatform_ytime_monotonic_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void yetty_yplatform_ytime_sleep_ms(unsigned ms)
{
    usleep((useconds_t)ms * 1000U);
}
