/* Windows impl: PeekNamedPipe / GetNumberOfConsoleInputEvents to gate
 * the ReadFile so the call never blocks. CRT file descriptors 0/1/2
 * map to OS HANDLEs via _get_osfhandle.
 *
 * The yrdawn client opens its read fd from an inherited stdin which
 * Windows backs as either a console handle (interactive) or a pipe
 * (yetty's ConPTY child). We branch on GetFileType and only call
 * ReadFile when we know bytes are buffered. */

#include <yetty/yplatform/io.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>

int yetty_yplatform_io_wait_readable(int fd, int timeout_ms)
{
    if (fd < 0) {
        return -1;
    }
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }

    DWORD type = GetFileType(h);
    int elapsed = 0;
    /* No "bytes ready" wait primitive for CRT pipe/console handles, so poll
     * the same readiness checks io_read_nonblocking uses with a short sleep. */
    for (;;) {
        if (type == FILE_TYPE_PIPE) {
            DWORD avail = 0;
            if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) {
                /* Broken pipe — report readable so the caller's next read
                 * observes EOF and terminates its loop. */
                return (GetLastError() == ERROR_BROKEN_PIPE) ? 1 : -1;
            }
            if (avail > 0) {
                return 1;
            }
        } else if (type == FILE_TYPE_CHAR) {
            DWORD events = 0;
            if (!GetNumberOfConsoleInputEvents(h, &events) || events > 0) {
                return 1;
            }
        } else {
            /* Disk / unknown: always immediately readable until EOF. */
            return 1;
        }

        if (timeout_ms == 0) {
            return 0;
        }
        if (timeout_ms > 0 && elapsed >= timeout_ms) {
            return 0;
        }
        Sleep(2);
        if (timeout_ms > 0) {
            elapsed += 2;
        }
    }
}

struct yetty_yplatform_io_size_result yetty_yplatform_io_read_nonblocking(int fd, void *buf,
                                                                          size_t cap)
{
    if (fd < 0 || !buf || cap == 0) {
        return YETTY_OK(yetty_yplatform_io_size, 0);
    }

    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) {
        return YETTY_ERR(yetty_yplatform_io_size, "io_read_nonblocking: bad fd");
    }

    DWORD type = GetFileType(h);
    DWORD want = (DWORD)(cap > 0x7FFFFFFFu ? 0x7FFFFFFFu : cap);

    if (type == FILE_TYPE_PIPE) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) {
            DWORD err = GetLastError();
            /* Broken pipe surfaces as ERROR_BROKEN_PIPE — treat as EOF
             * (value == 0) so the caller's pump loop terminates cleanly
             * instead of erroring out. */
            if (err == ERROR_BROKEN_PIPE) {
                return YETTY_OK(yetty_yplatform_io_size, 0);
            }
            return YETTY_ERR(yetty_yplatform_io_size, "io_read_nonblocking: PeekNamedPipe failed");
        }
        if (avail == 0) {
            return YETTY_OK(yetty_yplatform_io_size, 0);
        }
        if (avail < want) {
            want = avail;
        }
    } else if (type == FILE_TYPE_CHAR) {
        DWORD events = 0;
        if (GetNumberOfConsoleInputEvents(h, &events) && events == 0) {
            return YETTY_OK(yetty_yplatform_io_size, 0);
        }
        /* If GetNumberOfConsoleInputEvents fails (non-console char dev)
         * we fall through and let ReadFile decide. */
    }
    /* FILE_TYPE_DISK and FILE_TYPE_UNKNOWN go straight to ReadFile —
     * regular files are always immediately readable until EOF. */

    DWORD got = 0;
    if (!ReadFile(h, buf, want, &got, NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF) {
            return YETTY_OK(yetty_yplatform_io_size, 0);
        }
        return YETTY_ERR(yetty_yplatform_io_size, "io_read_nonblocking: ReadFile failed");
    }
    return YETTY_OK(yetty_yplatform_io_size, (size_t)got);
}
