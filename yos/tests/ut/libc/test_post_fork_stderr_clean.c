/*
 * test_post_fork_stderr_clean.c — fork's first stderr write must be
 * byte-clean (no asyncify-state leak prefix).
 *
 * Background: openssh failed under yos with output of the form
 *   "\xff\xff\xff\xff\xff\xff\xff\xffad file descriptor\n"
 * — eight bytes of 0xff prefix, then the real strerror(EBADF) tail
 * minus its first character. The 0xff bytes leak out of yos's fork
 * asyncify save buffer because they sit in the wasm guest's stack
 * frame at the moment the child rewinds. yos's vfs.c::yos_write +
 * file.c::yos_fwrite already filter PURE-0xff payloads ≤ 16 B
 * destined for a tty (the "backspace looks like space" workaround);
 * MIXED payloads slip through.
 *
 * Reproducer: fork(), in the child do write(2, "X") with a one-byte
 * known payload. Read the byte back from a pipe set up by the parent
 * over fd 2 BEFORE the fork. If it isn't 'X', the asyncify state
 * leak corrupted the write.
 *
 * Note this test runs WITHOUT a tty (the parent uses a pipe), so the
 * 0xff workaround doesn't trigger — letting us see the raw leak.
 *
 * Expected: exit 0, stdout contains "post-fork-stderr-clean ok".
 */

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>

static void emit(const char *s) {
    size_t n = 0; while (s[n]) n++; write(1, s, n);
}

int main(void) {
    int pp[2];
    if (pipe(pp) < 0) { emit("pipe failed\n"); return 1; }
    pid_t pid = fork();
    if (pid < 0) { emit("fork failed\n"); return 1; }
    if (pid == 0) {
        /* CHILD: redirect stderr to the pipe write end, then emit a
         * single known byte. Anything else in the pipe is leak.
         * We deliberately do NOT close the original pipe fds — yos
         * has a bug where close()ing the source fd of a recent
         * fcntl(F_DUPFD) silently invalidates the dup. Skipping the
         * close lets the write actually land, exposing the leak we
         * came here to test for. (The close-invalidates-dup bug is
         * pinned in tests/ut/libc/test_dup_then_close_source.c.) */
        dup2(pp[1], 2);
        write(2, "X", 1);
        _exit(0);
    }
    /* PARENT: drain the pipe, expect EXACTLY 1 byte = 'X'. */
    close(pp[1]);
    char buf[64];
    ssize_t total = 0;
    int status = 0;
    waitpid(pid, &status, 0);
    /* Drain whatever's there. */
    for (;;) {
        ssize_t n = read(pp[0], buf + total, sizeof buf - (size_t)total);
        if (n <= 0) break;
        total += n;
        if (total >= (ssize_t)sizeof buf) break;
    }
    close(pp[0]);
    if (total != 1 || buf[0] != 'X') {
        emit("LEAK: child stderr was not clean — got ");
        char hex[3]; const char *h = "0123456789abcdef";
        for (ssize_t i = 0; i < total && i < 32; i++) {
            hex[0] = h[(buf[i] >> 4) & 0xf];
            hex[1] = h[ buf[i]       & 0xf];
            hex[2] = ' ';
            write(1, hex, 3);
        }
        emit("\n");
        return 1;
    }
    emit("post-fork-stderr-clean ok\n");
    return 0;
}

void _start(void) {
    _exit(main());
}
