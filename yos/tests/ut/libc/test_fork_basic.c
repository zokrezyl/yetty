/*
 * test_fork_basic.c — fork() + waitpid() round-trip via custom_proc.
 *
 * WHAT this verifies:
 *   The wasm guest calls fork() (env.fork). bridge.py's m3w_fork
 *   wrapper unpacks zero args and dispatches to yos_fork(ctx) in
 *   src/yos/impl/proc.c (the asyncify-based fork from yos-private).
 *   Parent gets the child pid; child gets 0. Both write a marker
 *   line to stdout. Parent then waitpid()s for the child and exits
 *   with the child's status.
 *
 * WHY this matters:
 *   This is the smoke test for the entire custom_proc routing path
 *   AND the asyncify fork dance simultaneously. If the bridge
 *   doesn't route fork to yos_fork, we'd see the host process get
 *   forked (pid > 1, two yos binaries running). If asyncify is
 *   broken, the child wouldn't reach _start. If waitpid wiring is
 *   wrong, the parent hangs.
 *
 * Expected: exit 7, stdout contains "child says hi", stdout contains "parent saw child".
 */

#include "yos_imports.h"

/* yos_imports.h gives us:
 *   int  fork(void);                         (custom_proc)
 *   int  waitpid(int, int *, int);           (custom_proc)
 *   ssize_t write(int, const void *, size_t); (passthrough)
 *   void _exit(int);                          (custom_proc — alias for exit)
 */

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

void _start(void) {
    int pid = fork();
    if (pid == 0) {
        say("child says hi\n");
        _exit(7);
    }
    if (pid < 0) {
        say("fork failed\n");
        _exit(1);
    }
    int status = 0;
    int rpid = waitpid(pid, &status, 0);
    if (rpid != pid) {
        say("waitpid wrong pid\n");
        _exit(2);
    }
    say("parent saw child\n");
    /* Propagate the child's exit code. POSIX status: low byte = signal,
     * next byte = exit code; ((status >> 8) & 0xff) extracts the latter. */
    _exit((status >> 8) & 0xff);
}
