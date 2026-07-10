/*
 * test_fork_fprintf_stderr.c — child after fork must successfully
 *                              write to stderr via stdio (fprintf).
 *
 * Background: zsh-wasm's "command not found" message is generated in
 * the forked child (via zerr → zwarning → nicezputs → fputc on
 * stderr). Under yos that output is lost — child exits silently.
 * This isolates whether stdio writes from a fork child actually
 * land on the host fd.
 *
 * Parent forks. Child does fprintf(stderr, "hello\n") + _exit(0).
 * Parent waitpid's child and reports.
 *
 * Expected: stderr contains "hello", stdout contains "ok".
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

int main(void) {
    pid_t pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        return 1;
    }
    if (pid == 0) {
        /* CHILD: write to stderr via fprintf */
        fprintf(stderr, "hello\n");
        fflush(stderr);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    printf("ok\n");
    return 0;
}

void _start(void) {
    _exit(main());
}
