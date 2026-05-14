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

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_TTY_H */
