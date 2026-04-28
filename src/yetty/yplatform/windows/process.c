/* process.c - Windows impl of yplatform/process.h */

#include <yetty/yplatform/process.h>
#include <yetty/yplatform/time.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdlib.h>
#include <string.h>

struct yprocess {
    HANDLE process;
    HANDLE thread;
};

/*
 * Build a single CreateProcess command line out of an argv vector.
 *
 * Quoting follows the rules expected by the standard MSVC CRT parser
 * (see "Parsing C Command-Line Arguments" in Microsoft docs):
 *   - wrap the argument in double quotes if it contains space, tab, or "
 *   - inside quotes, double up backslashes that immediately precede a "
 *     and escape literal " as \"
 * Good enough for spawning qemu-system-riscv64 with normal-looking paths.
 *
 * out must be at least out_size bytes. Returns 0 on success.
 */
static int build_command_line(const char *const argv[], char *out, size_t out_size)
{
    size_t pos = 0;

    for (size_t i = 0; argv[i]; i++) {
        const char *a = argv[i];
        int needs_quotes = (*a == '\0') || strchr(a, ' ') != NULL || strchr(a, '\t') != NULL ||
                           strchr(a, '"') != NULL;

        if (i > 0) {
            if (pos + 1 >= out_size) {
                return -1;
            }
            out[pos++] = ' ';
        }

        if (!needs_quotes) {
            size_t alen = strlen(a);
            if (pos + alen >= out_size) {
                return -1;
            }
            memcpy(out + pos, a, alen);
            pos += alen;
            continue;
        }

        if (pos + 1 >= out_size) {
            return -1;
        }
        out[pos++] = '"';

        size_t backslashes = 0;
        for (const char *p = a; *p; p++) {
            if (*p == '\\') {
                backslashes++;
                continue;
            }
            if (*p == '"') {
                /* Double the run of backslashes (so they survive past the
                 * embedded quote) and escape the quote itself. */
                if (pos + 2 * backslashes + 2 >= out_size) {
                    return -1;
                }
                for (size_t k = 0; k < 2 * backslashes; k++) {
                    out[pos++] = '\\';
                }
                out[pos++] = '\\';
                out[pos++] = '"';
            } else {
                if (pos + backslashes + 1 >= out_size) {
                    return -1;
                }
                for (size_t k = 0; k < backslashes; k++) {
                    out[pos++] = '\\';
                }
                out[pos++] = *p;
            }
            backslashes = 0;
        }
        /* Trailing backslashes need doubling before the closing quote. */
        if (pos + 2 * backslashes + 1 >= out_size) {
            return -1;
        }
        for (size_t k = 0; k < 2 * backslashes; k++) {
            out[pos++] = '\\';
        }
        out[pos++] = '"';
    }

    if (pos >= out_size) {
        return -1;
    }
    out[pos] = '\0';
    return 0;
}

yprocess_t *yprocess_spawn(const char *const argv[], int detached, int stdio_to_null)
{
    if (!argv || !argv[0]) {
        return YPROCESS_INVALID;
    }

    char cmdline[8192];
    if (build_command_line(argv, cmdline, sizeof(cmdline)) != 0) {
        return YPROCESS_INVALID;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);

    HANDLE nul = INVALID_HANDLE_VALUE;
    BOOL inherit = FALSE;
    if (stdio_to_null) {
        SECURITY_ATTRIBUTES sa;
        memset(&sa, 0, sizeof(sa));
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        nul = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          &sa, OPEN_EXISTING, 0, NULL);
        if (nul != INVALID_HANDLE_VALUE) {
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = nul;
            si.hStdOutput = nul;
            si.hStdError = nul;
            inherit = TRUE;
        }
    }

    DWORD flags = CREATE_NO_WINDOW;
    if (detached) {
        flags |= DETACHED_PROCESS;
    }

    BOOL ok = CreateProcessA(argv[0], cmdline, NULL, NULL, inherit, flags, NULL, NULL, &si, &pi);

    if (nul != INVALID_HANDLE_VALUE) {
        CloseHandle(nul);
    }

    if (!ok) {
        return YPROCESS_INVALID;
    }

    yprocess_t *p = malloc(sizeof(*p));
    if (!p) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return YPROCESS_INVALID;
    }
    p->process = pi.hProcess;
    p->thread = pi.hThread;
    return p;
}

void yprocess_terminate(yprocess_t *proc, unsigned grace_ms)
{
    if (!proc) {
        return;
    }

    /* No graceful "please exit" signal on Windows. Wait briefly in case the
     * child is exiting on its own, then force-terminate if still alive. */
    if (grace_ms > 0) {
        WaitForSingleObject(proc->process, grace_ms);
    }

    DWORD code;
    if (!GetExitCodeProcess(proc->process, &code) || code == STILL_ACTIVE) {
        TerminateProcess(proc->process, 1);
        WaitForSingleObject(proc->process, INFINITE);
    }

    CloseHandle(proc->process);
    CloseHandle(proc->thread);
    free(proc);
}

int yprocess_is_running(yprocess_t *proc)
{
    if (!proc) {
        return 0;
    }
    DWORD r = WaitForSingleObject(proc->process, 0);
    return r == WAIT_TIMEOUT;
}
