/*
 * ytrace — strace-style libc-surface tracer for wasm-under-yos.
 *
 * Invoked from inside the yos sandbox (zsh, etc.) as:
 *     ytrace ls /
 *     ytrace -o /tmp/trace nvim file.txt
 *
 * Mechanics: set YTRACE_DEFAULT_ON=yes (and optionally YTRACE_FILE_PREFIX)
 * in the environment, then execvp the rest of argv. yos_execve scans the
 * new process's env on exec; if YTRACE_DEFAULT_ON is set it flips
 * ytrace_set_all_enabled(true) so every per-callsite trace point in the
 * host's m3w_<name> wrappers starts firing.
 *
 * Caveat: ytrace's enable state is process-wide on the host side, so once
 * `ytrace ls` enables tracing the parent shell sees its own bridge calls
 * traced too until it exits. For now that's acceptable — the typical
 * interactive use is "ytrace this one command, look at the output".
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void)
{
    fprintf(stderr,
        "ytrace - strace-style libc trace for wasm under yos.\n"
        "usage: ytrace [-o file] <prog> [args...]\n"
        "  -o file   write per-thread trace to <file>-<comm>-<tid>\n");
}

int main(int argc, char **argv)
{
    const char *out_prefix = NULL;
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_prefix = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (argv[i][0] == '-' && argv[i][1] != 0) {
            fprintf(stderr, "ytrace: unknown flag %s\n", argv[i]);
            return 2;
        } else {
            break;
        }
    }
    if (i >= argc) { usage(); return 2; }

    setenv("YTRACE_DEFAULT_ON", "yes", 1);
    if (out_prefix) setenv("YTRACE_FILE_PREFIX", out_prefix, 1);

    execvp(argv[i], argv + i);
    fprintf(stderr, "ytrace: %s: %s\n", argv[i], strerror(errno));
    return 127;
}
