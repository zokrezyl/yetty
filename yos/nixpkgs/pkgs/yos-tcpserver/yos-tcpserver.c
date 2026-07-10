/* yos-tcpserver.c — minimal listen+accept+fork+exec super-server.
 *
 * Usage:
 *     yos-tcpserver PORT PROGRAM [ARGS...]
 *
 * Binds 0.0.0.0:PORT (visible to the host kernel, so `ss -tnlp` /
 * `netstat -an` show it). Each accept forks a child; the child dup2's
 * the connected socket onto stdin/stdout/stderr and execve's PROGRAM
 * with ARGS. Parent loops forever. Children are reaped via
 * SIGCHLD=SIG_IGN (POSIX auto-reap).
 *
 * NOT a telnet server — no IAC negotiation, no PTY allocation,
 * no line/echo handshake. The connection is a raw byte pipe.
 * Workable from:
 *
 *     telnet -E HOST PORT    # -E disables telnet escape so raw bytes pass
 *     nc HOST PORT           # plain raw bytes
 *
 * Pair with a wasm program that wants a socket on its stdio:
 *
 *     ./tools/yos.sh yos-tcpserver 12323 /libexec/zsh
 *
 * Each connecting client gets its own forked wasm zsh — multi-
 * session by construction, no shared state between clients except
 * the parent listener socket (which the child closes before exec).
 *
 * Doesn't depend on runit or any cwd state — bypasses the
 * pthread-shared-cwd race that blocks runsv/runsvdir on yos today.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <signal.h>
#include <arpa/inet.h>

extern char **environ;

static void diag(const char *s) { write(2, s, strlen(s)); }

int main(int argc, char **argv)
{
    if (argc < 3) {
        diag("usage: yos-tcpserver PORT PROGRAM [ARGS...]\n");
        return 2;
    }
    int port = atoi(argv[1]);
    if (port < 1 || port > 65535) {
        diag("yos-tcpserver: bad port\n");
        return 2;
    }

    /* Auto-reap children. Without this every accepted connection
     * leaves a zombie in the proc table — runs out after ~YOS_MAX_PROCS
     * connections, which would be a poor multi-session story. POSIX
     * lets us opt out of being a zombie's reaper with SIG_IGN on
     * SIGCHLD (the kernel discards exit status). */
    signal(SIGCHLD, SIG_IGN);

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }

    int one = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);          /* 0.0.0.0 */
    sa.sin_port        = htons((unsigned short)port);
    if (bind(sfd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        perror("bind"); return 1;
    }
    if (listen(sfd, 16) < 0) { perror("listen"); return 1; }

    fprintf(stderr,
            "yos-tcpserver: listening on 0.0.0.0:%d → %s\n",
            port, argv[2]);
    fflush(stderr);

    for (;;) {
        struct sockaddr_in pa;
        socklen_t plen = sizeof pa;
        int cfd = accept(sfd, (struct sockaddr *)&pa, &plen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept"); break;
        }

        char peer[64];
        peer[0] = '?'; peer[1] = '\0';
        inet_ntop(AF_INET, &pa.sin_addr, peer, sizeof peer);
        fprintf(stderr,
                "yos-tcpserver: accept %s:%u → cfd=%d, forking\n",
                peer, (unsigned)ntohs(pa.sin_port), cfd);
        fflush(stderr);

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); close(cfd); continue; }

        if (pid == 0) {
            /* Child. Replace stdio with the socket and exec the
             * target program. argv layout: argv+2 is "PROGRAM
             * ARGS...", which is exactly what execve wants. */
            close(sfd);
            if (dup2(cfd, 0) < 0) _exit(70);
            if (dup2(cfd, 1) < 0) _exit(71);
            if (dup2(cfd, 2) < 0) _exit(72);
            if (cfd > 2) close(cfd);

            /* execvp: PATH-based lookup so the user can pass a bare
             * name (`zsh`) and yos.sh's PATH=$ALL/libexec resolves
             * it. Absolute paths still work — execvp short-circuits
             * the lookup if the program contains a '/'. */
            execvp(argv[2], argv + 2);
            /* exec failed — write to the socket since stdio IS the
             * socket now; the connecting client sees the error. */
            const char *m = "yos-tcpserver: execvp failed\n";
            write(2, m, strlen(m));
            _exit(127);
        }

        /* Parent: drop our copy of the connection fd; the child
         * has it. Loop and wait for the next accept. */
        close(cfd);
    }
    return 1;
}
