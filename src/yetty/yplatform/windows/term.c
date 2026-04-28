/* term.c - Windows terminal helpers */

#include <yetty/yplatform/term.h>
#include <windows.h>
#include <stdio.h>

int yplatform_stderr_supports_color(void)
{
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    if (hErr != INVALID_HANDLE_VALUE && GetConsoleMode(hErr, &mode)) {
        SetConsoleMode(hErr, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        return 1;
    }
    return 0;
}

void yplatform_format_timestamp(char *buf, size_t bufsize)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(buf, bufsize, "%02d:%02d:%02d.%03d", st.wHour, st.wMinute, st.wSecond,
             st.wMilliseconds);
}
