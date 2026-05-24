/*
 * Windows tty abstraction — Windows yetty doesn't have a `yetty -e`
 * style PTY-share scenario, so the stderr-rerouting probe is a
 * no-op. The function still exists with the same signature so client
 * tools (ygreeter, yplot, …) can call it unconditionally.
 *
 * See include/yetty/yplatform/tty.h for the cross-platform contract.
 */

#include <yetty/yplatform/tty.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>

struct yetty_yplatform_tty_redirected_result
yetty_yplatform_tty_redirect_stderr_if_shared_with_stdout(const char *basename)
{
    (void)basename;
    return YETTY_OK(yetty_yplatform_tty_redirected, 0);
}

/*=============================================================================
 * Raw-mode stdin + stdin wait/read used by interactive ydraw producers
 * (yjungle, yzoo, ymesh, ymaze) and capture tools (decode-ydraw).
 *===========================================================================*/

static DWORD saved_console_mode = 0;
static int raw_active = 0;

void yetty_yplatform_tty_binary_io(void)
{
    /* Switch CRT streams to binary so OSC byte streams pass through
     * unchanged (CR/LF translation off). Safe to call early in main
     * before any stdio. */
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
}

int yetty_yplatform_tty_stdin_is_tty(void)
{
    return _isatty(_fileno(stdin));
}

int yetty_yplatform_tty_stdout_is_tty(void)
{
    return _isatty(_fileno(stdout));
}

int yetty_yplatform_tty_set_raw(void)
{
    if (!_isatty(_fileno(stdin))) {
        return 0;
    }
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    if (!GetConsoleMode(h, &saved_console_mode)) {
        return -1;
    }
    DWORD raw = saved_console_mode;
    /* Disable line buffering, echo, and Ctrl-C/Ctrl-Break processing
     * so keystrokes arrive immediately as raw bytes. Mirrors what
     * cfmakeraw() does on POSIX. */
    raw &= ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    if (!SetConsoleMode(h, raw)) {
        return -1;
    }
    raw_active = 1;
    return 0;
}

void yetty_yplatform_tty_restore(void)
{
    if (!raw_active) {
        return;
    }
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE) {
        SetConsoleMode(h, saved_console_mode);
    }
    raw_active = 0;
}

int yetty_yplatform_tty_stdin_wait(unsigned timeout_ms)
{
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    DWORD wr = WaitForSingleObject(h, timeout_ms);
    if (wr == WAIT_TIMEOUT) {
        return 0;
    }
    return (wr == WAIT_OBJECT_0) ? 1 : -1;
}

int yetty_yplatform_tty_stdin_read(void *buf, size_t buf_size)
{
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    DWORD got = 0;
    if (!ReadFile(h, buf, (DWORD)buf_size, &got, NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF) {
            return 0;
        }
        return -1;
    }
    return (int)got;
}
