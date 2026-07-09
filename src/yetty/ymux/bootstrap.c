/*
 * ymux first-launch bootstrap — connect to an existing server or spawn a
 * detached one, tmux-style. See bootstrap.h.
 */
#include <yetty/ymux/bootstrap.h>

#include <yetty/ytrace/ytrace.h>
#include <yetty/yplatform/process.h>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

enum {
    YMUX_BOOTSTRAP_WAIT_TRIES = 100, /* × 50ms = ~5s readiness budget */
    YMUX_BOOTSTRAP_WAIT_STEP_US = 50000,
};

/* Non-blocking-ish TCP connect probe to loopback. Returns 1 if a server
 * accepts, 0 otherwise. Blocking connect is fine for loopback (immediate
 * ECONNREFUSED when nothing listens). */
static int ymux_try_connect(const char *host, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        return 0;
    }
    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);
    return rc == 0;
}

/* Absolute path to this executable, so the spawned server is the same binary. */
static int ymux_self_exe(char *buf, size_t size)
{
#if defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", buf, size - 1);
    if (len <= 0) {
        return 0;
    }
    buf[len] = '\0';
    return 1;
#elif defined(__APPLE__)
    uint32_t bufsize = (uint32_t)size;
    if (_NSGetExecutablePath(buf, &bufsize) != 0) {
        return 0;
    }
    return 1;
#else
    (void)buf;
    (void)size;
    return 0;
#endif
}

/* Open + exclusively lock a per-port lock file. Returns the fd (held for the
 * duration of the spawn) or -1. The caller closes it to release. */
static int ymux_lock(int port)
{
    const char *dir = getenv("XDG_RUNTIME_DIR");
    if (!dir || dir[0] == '\0') {
        dir = "/tmp";
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/yetty-ymux-%d.lock", dir, port);
    int fd = open(path, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        return -1;
    }
    if (flock(fd, LOCK_EX) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int yetty_ymux_ensure_server(const char *host, int port)
{
    if (!host) {
        host = "127.0.0.1";
    }

    /* Fast path: a server is already up. */
    if (ymux_try_connect(host, port)) {
        ydebug("ymux bootstrap: server already running at %s:%d", host, port);
        return 1;
    }

    /* Serialize first-launch racers on a per-port lock. */
    int lock_fd = ymux_lock(port);
    if (lock_fd < 0) {
        ywarn("ymux bootstrap: could not take spawn lock for port %d", port);
        /* Still try, unsynchronized — a bind collision is handled cleanly by
         * the server exiting, and we detect readiness by connect. */
    }

    /* Someone may have won the race while we waited for the lock. */
    if (ymux_try_connect(host, port)) {
        if (lock_fd >= 0) {
            close(lock_fd);
        }
        ydebug("ymux bootstrap: server appeared while acquiring lock");
        return 1;
    }

    char exe[1024];
    if (!ymux_self_exe(exe, sizeof(exe))) {
        if (lock_fd >= 0) {
            close(lock_fd);
        }
        yerror("ymux bootstrap: cannot resolve own executable path to spawn server");
        return 0;
    }

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    const char *argv[] = {exe, "--ymux-server", "--ymux-port", port_str, NULL};
    yinfo("ymux bootstrap: spawning detached server: %s --ymux-server --ymux-port %s", exe,
          port_str);

    struct yetty_yplatform_yprocess *proc =
        yetty_yplatform_yprocess_spawn(argv, /*detached=*/1, /*stdio_to_null=*/1);
    if (proc == YPROCESS_INVALID) {
        if (lock_fd >= 0) {
            close(lock_fd);
        }
        yerror("ymux bootstrap: server spawn failed");
        return 0;
    }

    /* Readiness by successful connect, bounded. */
    int ready = 0;
    for (int attempt = 0; attempt < YMUX_BOOTSTRAP_WAIT_TRIES; attempt++) {
        usleep(YMUX_BOOTSTRAP_WAIT_STEP_US);
        if (ymux_try_connect(host, port)) {
            ready = 1;
            break;
        }
        if (!yetty_yplatform_yprocess_is_running(proc)) {
            yerror("ymux bootstrap: spawned server exited before becoming ready "
                   "(port %d in use?)",
                   port);
            break;
        }
    }

    /* The server is meant to outlive us — do NOT terminate it. We intentionally
     * drop the handle (a tiny leak for the client's lifetime); the detached
     * server is reparented to init when this client exits. */
    if (lock_fd >= 0) {
        close(lock_fd);
    }

    if (ready) {
        yinfo("ymux bootstrap: server ready at %s:%d", host, port);
        return 1;
    }
    return 0;
}

#else /* _WIN32 */

int yetty_ymux_ensure_server(const char *host, int port)
{
    (void)host;
    (void)port;
    /* Windows auto-spawn (named pipes) is a later phase; fall back to legacy. */
    return 0;
}

#endif
