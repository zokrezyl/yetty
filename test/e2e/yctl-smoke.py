#!/usr/bin/env python3
"""
E2E smoke test: launch the real yetty process, wait for its yctl RPC server to
come up, drive a clean shutdown *through yctl*, and confirm the process exits on
its own in response.

Exercises process startup -> RPC server bind -> yctl-driven shutdown. Requires a
real display/GPU (yetty opens a WebGPU surface) and a runnable yctl client, so it
SKIPS (exit 77) when there is no DISPLAY/WAYLAND_DISPLAY, no yetty binary, or yctl
cannot be launched at all — safe in headless CI.

The success path is strict: the `yctl shutdown` call must launch and exit 0, and
yetty must then exit ON ITS OWN. yctl failing/timing out, or yetty only dying
because we had to SIGTERM/kill it, is a FAILURE (with the yetty log dumped).
SIGTERM/kill is used purely for cleanup on failure/exit paths, never as success.

Env:
  YETTY  path to the yetty binary (falls back to the default release build path)
  YCTL   path to yctl.py         (falls back to tools/yctl-client/yctl.py)
"""
import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
YETTY = os.environ.get("YETTY", os.path.join(ROOT, "build-desktop-ytrace-release", "yetty"))
YCTL = os.environ.get("YCTL", os.path.join(ROOT, "tools", "yctl-client", "yctl.py"))
SKIP = 77


def skip(reason):
    print(f"SKIP: {reason}")
    sys.exit(SKIP)


def yctl_runnable():
    """True if the yctl client can be launched at all (interpreter + deps)."""
    try:
        r = subprocess.run([YCTL, "--help"], timeout=60, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, cwd=ROOT)
        return r.returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main():
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        skip("no DISPLAY/WAYLAND_DISPLAY — yetty needs a real surface")
    if not os.path.exists(YETTY):
        skip(f"yetty binary not found at {YETTY}")
    if not os.path.exists(YCTL):
        skip(f"yctl client not found at {YCTL}")
    if not yctl_runnable():
        skip(f"yctl client at {YCTL} is not runnable (interpreter/deps missing)")

    port = free_port()
    log_path = os.path.join(HERE, "yctl-smoke.yetty.log")
    log = open(log_path, "w")
    env = dict(os.environ, YTRACE_DEFAULT_ON="yes")
    proc = subprocess.Popen([YETTY, "-r", str(port)], stdout=log, stderr=log, env=env, cwd=ROOT)

    def dump(msg):
        log.flush()
        print(f"FAIL: {msg}")
        try:
            with open(log_path) as f:
                sys.stderr.write(f.read())
        except OSError:
            pass

    try:
        # Wait for the RPC server to bind (yetty logs "yctl: server listening").
        deadline = time.time() + 25
        listening = False
        while time.time() < deadline:
            if proc.poll() is not None:
                dump(f"yetty exited early (rc={proc.returncode}) before the RPC server bound")
                return 1
            try:
                with open(log_path) as f:
                    if "yctl: server listening" in f.read():
                        listening = True
                        break
            except OSError:
                pass
            time.sleep(0.25)
        if not listening:
            dump("RPC server did not report 'server listening' within 25s")
            return 1

        # Confirm the port actually accepts a connection.
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=5):
                pass
        except OSError as exc:
            dump(f"RPC port :{port} did not accept a connection: {exc}")
            return 1

        # yctl-driven shutdown — REQUIRED to launch and exit 0.
        try:
            rc = subprocess.run([YCTL, "-p", str(port), "shutdown"], timeout=20,
                                stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, cwd=ROOT)
        except subprocess.TimeoutExpired:
            dump("yctl shutdown timed out")
            return 1
        except OSError as exc:
            dump(f"yctl shutdown could not be launched: {exc}")
            return 1
        if rc.returncode != 0:
            dump(f"yctl shutdown exited non-zero (rc={rc.returncode})")
            return 1

        # yetty must exit ON ITS OWN in response to the yctl shutdown — not
        # because we signalled it.
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            dump("yctl shutdown succeeded but yetty did not exit within 15s")
            return 1

        print(f"OK: yetty started, RPC bound on :{port}, shut down via yctl "
              f"(rc={proc.returncode})")
        return 0
    finally:
        # Cleanup only — never a success path. If the process is still alive on
        # any exit (failure or skip), stop the one we started.
        if proc.poll() is None:
            proc.kill()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        log.close()


if __name__ == "__main__":
    sys.exit(main())
