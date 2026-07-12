#!/usr/bin/env bash
# Per-connection ephemeral demo container for the yetty SSH-over-WebSocket
# showcase.
#
# This is the forced login command for the throwaway `yetty` user (set via
# sshd's ForceCommand). Every SSH session — one per browser WebSocket — gets a
# fresh, locked-down, disposable container that is destroyed the moment the
# session ends. There is no shell escape: sshd ignores whatever command the
# client requests and always runs this script.
#
# Isolation applied to each container:
#   - fresh instance, removed on exit (--rm)
#   - non-root user, all capabilities dropped, no privilege escalation
#   - read-only root filesystem; only tmpfs /tmp and tmpfs home are writable
#   - no network by default
#   - memory / cpu / pids caps so one visitor can't starve the host
#   - a hard wall-clock session timeout
# A global concurrency cap keeps a burst of visitors from exhausting the box.
#
# Teardown is guaranteed on ANY exit path — clean `exit`, a browser tab close
# (sshd delivers SIGHUP), or the wall-clock timeout — via a trap that kills the
# uniquely-named container. We must NOT `exec` here: exec would replace this
# shell and lose the trap, which is exactly how an orphaned `docker run` used to
# leave containers running after a disconnect.

set -euo pipefail

IMAGE="${YETTY_DEMO_IMAGE:-yetty-demo:latest}"
MAX_SESSIONS="${YETTY_DEMO_MAX_SESSIONS:-25}"
SESSION_TIMEOUT="${YETTY_DEMO_TIMEOUT:-1800}" # seconds; hard cap per session
NETWORK="${YETTY_DEMO_NETWORK:-none}"         # 'none' (default) or 'bridge'
MEMORY="${YETTY_DEMO_MEMORY:-512m}"           # the yetty tools load font atlases
CPUS="${YETTY_DEMO_CPUS:-1.0}"
PIDS="${YETTY_DEMO_PIDS:-256}"

LABEL="yetty-demo"

# Refuse new sessions past the concurrency cap.
running="$(docker ps --filter "label=${LABEL}" --format '{{.ID}}' | wc -l)"
if [ "${running}" -ge "${MAX_SESSIONS}" ]; then
    printf '\r\n  The yetty demo is at capacity (%s active sessions).\r\n' "${MAX_SESSIONS}"
    printf '  Please try again in a few minutes.\r\n\r\n'
    exit 0
fi

# Allocate a docker tty only when sshd gave us a real pty (it does for the
# browser client, which requests one). Fall back gracefully otherwise.
tty_flag=()
if [ -t 0 ]; then
    tty_flag=(-t)
fi

# Unique per-session container name so teardown can target exactly this one.
name="yetty-demo-$$-${RANDOM}"

# Reliable teardown. On a browser disconnect sshd SIGKILLs this shell before an
# in-shell trap (blocked in the foreground `docker run`) can fire, which used to
# orphan `docker run` and leave the container alive. So the primary reaper is an
# INDEPENDENT, detached process: it watches the sshd session that launched us
# and kills the container the moment that session goes away — it survives even a
# hard kill of our process group. The trap is the secondary path for clean exit
# and the wall-clock timeout.
session_pid="${PPID}"
setsid bash -c \
    'while kill -0 '"${session_pid}"' 2>/dev/null; do sleep 1; done; docker kill '"${name}"' >/dev/null 2>&1' \
    >/dev/null 2>&1 &
reaper=$!

cleanup() {
    docker kill "${name}" >/dev/null 2>&1 || true
    kill "${reaper}" >/dev/null 2>&1 || true
}
trap cleanup EXIT HUP TERM INT

timeout --signal=TERM --foreground "${SESSION_TIMEOUT}" \
    docker run --rm -i "${tty_flag[@]}" \
        --name "${name}" \
        --label "${LABEL}" \
        --network "${NETWORK}" \
        --memory "${MEMORY}" --memory-swap "${MEMORY}" \
        --cpus "${CPUS}" \
        --pids-limit "${PIDS}" \
        --read-only \
        --tmpfs /tmp:rw,nosuid,nodev,size=128m \
        --tmpfs /home/yetty:rw,nosuid,nodev,size=128m,uid=1000,gid=1000,mode=0700 \
        --cap-drop ALL \
        --security-opt no-new-privileges \
        --user 1000:1000 \
        --hostname yetty-demo \
        "${IMAGE}"
