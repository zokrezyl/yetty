/* time.c - Windows time implementation */

#include <yetty/yplatform/time.h>
#include <windows.h>

double ytime_monotonic_sec(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;

    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }

    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}

void ytime_sleep_ms(unsigned ms)
{
    Sleep((DWORD)ms);
}
