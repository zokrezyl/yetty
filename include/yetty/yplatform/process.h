/*
 * yplatform/process.h - Cross-platform child process spawn / wait / kill
 *
 * Minimal API: spawn a binary with an argv list, optionally detach it from
 * the parent's session/console, optionally redirect stdio to the null device,
 * later check if it's still running and terminate it.
 *
 * Backends:
 *   POSIX:   fork() + execvp() + setsid() + dup2(/dev/null) + waitpid + kill
 *   Windows: CreateProcess + DETACHED_PROCESS + STD_HANDLES=NUL + TerminateProcess
 */

#ifndef YETTY_YPLATFORM_PROCESS_H
#define YETTY_YPLATFORM_PROCESS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque process handle. NULL == invalid. */
struct yetty_yplatform_yprocess;

#define YPROCESS_INVALID NULL

/*
 * Spawn a child.
 *   argv:           NULL-terminated. argv[0] is the executable path
 *                   (PATH-resolved on POSIX via execvp; absolute path
 *                   recommended on Windows for deterministic behavior).
 *   detached:       non-zero: child runs in its own session/console.
 *   stdio_to_null:  non-zero: child's stdin/stdout/stderr point at the
 *                   null device.
 * Returns YPROCESS_INVALID on failure.
 */
struct yetty_yplatform_yprocess *yetty_yplatform_yprocess_spawn(const char *const argv[],
                                                                int detached, int stdio_to_null);

/*
 * Ask the child to terminate. POSIX sends SIGTERM, waits up to grace_ms,
 * then SIGKILL if still alive. Windows has no graceful kill, so it waits
 * up to grace_ms in case the child exits on its own, then TerminateProcess.
 * Always frees the handle (do not call yprocess_is_running afterwards).
 */
void yetty_yplatform_yprocess_terminate(struct yetty_yplatform_yprocess *proc, unsigned grace_ms);

/* 1 if still running, 0 if exited. Safe with NULL (returns 0). */
int yetty_yplatform_yprocess_is_running(struct yetty_yplatform_yprocess *proc);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_PROCESS_H */
