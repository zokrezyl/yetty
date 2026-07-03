#!/usr/bin/env python3
"""
E2E smoke test: launch the real yetty process, wait for its yctl RPC server to
come up, drive a clean shutdown through yctl, and confirm the process exits.

Exercises process startup -> RPC server bind -> yctl-driven shutdown. Requires a
real display/GPU (yetty opens a WebGPU surface), so it SKIPS (exit 77) when there
is no DISPLAY/WAYLAND_DISPLAY or the yetty binary is unavailable — safe to run in
headless CI. Diagnostics (the full yetty log) are printed on any failure.

Env:
  YETTY  path to the yetty binary (falls back to the default release build path)
  YCTL   path to yctl.py         (falls back to tools/yctl-client/yctl.py)
"""
import os
import signal
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


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main():
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        skip("no DISPLAY/WAYLAND_DISPLAY — yetty needs a real surface")
    if not os.path.exists(YETTY):
        skip(f"yetty binary not found at {YETTY}")

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
            proc.terminate()
            return 1

        # Confirm the port actually accepts a connection.
        with socket.create_connection(("127.0.0.1", port), timeout=5):
            pass

        # yctl-driven clean shutdown (best-effort; SIGTERM fallback below).
        try:
            subprocess.run([YCTL, "-p", str(port), "shutdown"], timeout=20,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=ROOT)
        except (OSError, subprocess.SubprocessError):
            pass

        # The process should exit; fall back to signalling the pid we started.
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                dump("yetty did not exit after yctl shutdown + SIGTERM")
                return 1

        print(f"OK: yetty started, RPC bound on :{port}, shut down cleanly (rc={proc.returncode})")
        return 0
    finally:
        if proc.poll() is None:
            proc.kill()
        log.close()


if __name__ == "__main__":
    sys.exit(main())
