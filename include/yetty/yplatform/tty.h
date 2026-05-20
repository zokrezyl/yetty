#ifndef YETTY_YPLATFORM_TTY_H
#define YETTY_YPLATFORM_TTY_H

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reroute stderr to a log file when it shares the same kernel object
 * as stdout — i.e. both fds point at the same PTY slave.
 *
 * Motivation: tools like ygreeter, yplot, ymesh, ycat emit OSC complex-prim
 * envelopes through stdout to a parent yetty. When the user runs
 * `yetty -e ./ytool`, the child inherits a shared PTY slave for stdout
 * and stderr; any diagnostic emitted on stderr lands in the same byte
 * stream and corrupts the OSC envelope parser in the parent. Detect
 * that case at startup and redirect stderr to a per-pid log file under
 * the runtime dir, leaving stdout clean.
 *
 * Behaviour:
 *   - POSIX: probes `isatty(stderr) && isatty(stdout)` and compares
 *     `(st_dev, st_ino)` via fstat. On match, opens a fresh log file
 *     and dup2's it onto stderr. Result.ok iff the rerouting actually
 *     happened (or wasn't needed); the result.value is 1 when stderr
 *     got redirected and 0 when no redirection was warranted.
 *   - Windows: no PTY-sharing concept under `yetty -e`; this function
 *     is a no-op (returns ok with value=0).
 *
 * The log file path is "<runtime_dir>/<basename>-<pid>.log" where
 * runtime_dir comes from yetty_yplatform_get_runtime_dir().
 *
 * Idempotent: calling this more than once is safe — the second call
 * sees stderr already pointing at a regular file and returns value=0.
 */
YETTY_YRESULT_DECLARE(yetty_yplatform_tty_redirected, int);

struct yetty_yplatform_tty_redirected_result
yetty_yplatform_tty_redirect_stderr_if_shared_with_stdout(const char *basename);

#include <stddef.h>

/* Switch stdin/stdout to binary mode. On Windows this prevents CRT
 * CRLF translation that would corrupt binary OSC streams. No-op on
 * POSIX. Call once at startup before any stdio. */
void yetty_yplatform_tty_binary_io(void);

/* Non-zero if stdin / stdout refers to a terminal. */
int yetty_yplatform_tty_stdin_is_tty(void);
int yetty_yplatform_tty_stdout_is_tty(void);

/* Switch stdin (if a tty) to raw mode — no line buffering, no echo,
 * no Ctrl-C handling, byte-at-a-time delivery. Caller should also
 * arrange to restore on exit (atexit / signal handler). Returns 0 on
 * success or when stdin is not a tty; -1 on failure. */
int yetty_yplatform_tty_set_raw(void);

/* Restore stdin's pre-set_raw mode. Safe to call when set_raw was
 * never invoked. */
void yetty_yplatform_tty_restore(void);

/* Wait up to `timeout_ms` for stdin data. Returns:
 *    1 — data ready (call stdin_read next)
 *    0 — timeout
 *   -1 — error */
int yetty_yplatform_tty_stdin_wait(unsigned timeout_ms);

/* Read up to `buf_size` bytes from stdin. Caller should have called
 * stdin_wait first. Returns bytes read, 0 on EOF, -1 on error.
 * Transient interruptions (EINTR) are retried internally. */
int yetty_yplatform_tty_stdin_read(void *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_TTY_H */
