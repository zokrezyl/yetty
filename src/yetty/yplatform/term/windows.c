/* term.c - Windows terminal helpers */

#include <yetty/yplatform/term.h>
#include <windows.h>
#include <stdio.h>

int yetty_yplatform_stderr_supports_color(void)
{
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    if (hErr != INVALID_HANDLE_VALUE && GetConsoleMode(hErr, &mode)) {
        SetConsoleMode(hErr, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        return 1;
    }
    return 0;
}

void yetty_yplatform_format_timestamp(char *buf, size_t bufsize)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(buf, bufsize, "%02d:%02d:%02d.%03d", st.wHour, st.wMinute, st.wSecond,
             st.wMilliseconds);
}

int yetty_yplatform_term_get_size(int *cols, int *rows)
{
    /* Try stdout first, then stderr, then stdin. The console-buffer
     * "window" rectangle (srWindow) is the visible viewport — what users
     * mean by "terminal size" — not the back-buffer dimensions. */
    DWORD handles[] = {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE, STD_INPUT_HANDLE};
    for (size_t i = 0; i < sizeof(handles) / sizeof(handles[0]); i++) {
        HANDLE h = GetStdHandle(handles[i]);
        if (h == INVALID_HANDLE_VALUE || h == NULL) {
            continue;
        }
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(h, &csbi)) {
            int c = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            int r = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
            if (c > 0 && r > 0) {
                if (cols) {
                    *cols = c;
                }
                if (rows) {
                    *rows = r;
                }
                return 0;
            }
        }
    }
    return -1;
}

int yetty_yplatform_term_get_size_pixels(int *cols, int *rows, int *pixel_width, int *pixel_height)
{
    /* The Win32 console reports size in character cells only, never pixels. */
    if (pixel_width) {
        *pixel_width = 0;
    }
    if (pixel_height) {
        *pixel_height = 0;
    }
    return yetty_yplatform_term_get_size(cols, rows);
}
