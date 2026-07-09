#ifndef YETTY_YMUX_BOOTSTRAP_H
#define YETTY_YMUX_BOOTSTRAP_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ensure a ymux server is reachable at host:port, spawning one if needed.
 *
 * The tmux-style first-launch flow:
 *   1. Try to connect. If a server answers, use it.
 *   2. Otherwise take a per-port file lock (races between simultaneous first
 *      launches), re-check (someone may have won), then spawn a detached
 *      `yetty --ymux-server --ymux-port <port>` and wait — bounded — until a
 *      connect succeeds (readiness by connect, not by socket existence).
 *
 * Returns 1 if a server is reachable (pre-existing or freshly spawned), 0 on
 * failure (caller should fall back to a local shell). POSIX only; returns 0 on
 * platforms without fork/IPC.
 */
int yetty_ymux_ensure_server(const char *host, int port);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMUX_BOOTSTRAP_H */
