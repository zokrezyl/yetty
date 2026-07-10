/* impl/main-windows.c — Windows-host startup wiring.
 *
 * No sigaltstack on Windows. yos delivers synchronous CPU faults via
 * Windows SEH (Structured Exception Handling), wired up elsewhere —
 * this slice handles the *startup-time* concerns:
 *
 *   1. Calm the debug CRT. msvcrt's _close / _read / _write on a
 *      non-CRT fd (e.g. a Winsock SOCKET that wasn't routed through
 *      closesocket) otherwise raises a debug dialog and exits status
 *      3 even when callers handled the EBADF return.
 *
 *   2. WSAStartup — Winsock APIs return WSANOTINITIALISED until it
 *      runs. We start it once at process boot so any guest bridge
 *      (socketpair, gethostname, getaddrinfo, …) just works.
 *
 * NO #ifdef in this file — meson selects it only on windows hosts.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <crtdbg.h>
#include <signal.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

/* From src/yos/platform/windows/platform-windows.c */
#define YOS_WIN_HOST_EPIPE_SENTINEL 1024
/* From the generated codegen output. Declared extern here so we don't
 * pull yos_remap.h's full surface into this startup file. */
extern int yos_remap_errno_h2g(int);

static void noop_invalid_parameter(const wchar_t *expression,
                                   const wchar_t *function,
                                   const wchar_t *file,
                                   unsigned int   line,
                                   uintptr_t      pReserved)
{
    (void)expression; (void)function; (void)file;
    (void)line; (void)pReserved;
}

void yos_main_install_altstack(void *sp, size_t sz)
{
    (void)sp; (void)sz;
}

void yos_main_install_signal_infra(void)
{
    /* Calm the debug CRT — these are process-wide one-shot calls. */
    _set_invalid_parameter_handler(noop_invalid_parameter);
    _set_thread_local_invalid_parameter_handler(noop_invalid_parameter);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR,  _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR,  _CRTDBG_FILE_STDERR);
    /* SIGABRT default exits 3; install SIG_IGN so a stray abort
     * doesn't bypass our own exit-code propagation either. */
    signal(SIGABRT, SIG_IGN);

    /* Initialise Winsock once at process startup so any wasm guest's
     * gethostname / socketpair / getaddrinfo bridge succeeds — Winsock
     * APIs return WSANOTINITIALISED before WSAStartup runs. */
    {
        WSADATA d;
        (void)WSAStartup(MAKEWORD(2, 2), &d);
    }

    /* Self-check: yos_plat_write writes YOS_WIN_HOST_EPIPE_SENTINEL into
     * errno when a host pipe write fails with EPIPE, then relies on the
     * codegen remap to translate it to guest-EPIPE (32). If the codegen
     * regenerates and the sentinel no longer maps the way we expect,
     * broken-pipe handling silently breaks. Fail loudly here at startup
     * instead. Guest EPIPE on FreeBSD is 32. */
    if (yos_remap_errno_h2g(YOS_WIN_HOST_EPIPE_SENTINEL) != 32) {
        fprintf(stderr,
                "yos: EPIPE sentinel %d remaps to %d, expected 32 — "
                "codegen errno table changed, update sentinel in "
                "platform-windows.c\n",
                YOS_WIN_HOST_EPIPE_SENTINEL,
                yos_remap_errno_h2g(YOS_WIN_HOST_EPIPE_SENTINEL));
        exit(2);
    }
}
