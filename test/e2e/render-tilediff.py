#!/usr/bin/env python3
"""
Nightly headful render + capture + tile-diff smoke (#424, also exercises the
#425 screenshot capture path).

Launches the real yetty, captures a screenshot of the initial frame, renders
visible content by running a command through yctl, captures a second
screenshot, and asserts the two frames differ at tile granularity (i.e. the
render actually changed the framebuffer and the capture path round-trips it).
Finally shuts down cleanly through yctl.

Requires a real display/GPU (yetty opens a WebGPU surface) and a runnable yctl
client, so it SKIPS (exit 77) when there is no DISPLAY/WAYLAND_DISPLAY, no yetty
binary, or yctl cannot be launched — safe in headless CI. Screenshots use the
server's built-in PPM (P6) writer, so there is no image-codec dependency.

Env:
  YETTY  path to the yetty binary (falls back to the default release build path)
  YCTL   path to yctl.py         (falls back to tools/yctl-client/yctl.py)
"""
import os
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
YETTY = os.environ.get("YETTY", os.path.join(ROOT, "build-desktop-ytrace-release", "yetty"))
YCTL = os.environ.get("YCTL", os.path.join(ROOT, "tools", "yctl-client", "yctl.py"))
SKIP = 77
TILE = 64


def skip(reason):
    print(f"SKIP: {reason}")
    sys.exit(SKIP)


def yctl_runnable():
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


def read_ppm(path):
    """Parse a binary PPM (P6). Returns (width, height, bytes) or None."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return None
    if not data.startswith(b"P6"):
        return None
    # Tokenize the header: P6 <w> <h> <maxval>\n<binary>
    idx = 2
    tokens = []
    while len(tokens) < 3 and idx < len(data):
        while idx < len(data) and data[idx:idx + 1].isspace():
            idx += 1
        start = idx
        while idx < len(data) and not data[idx:idx + 1].isspace():
            idx += 1
        tokens.append(data[start:idx])
    if len(tokens) < 3:
        return None
    idx += 1  # single whitespace after maxval
    try:
        w, h = int(tokens[0]), int(tokens[1])
    except ValueError:
        return None
    return w, h, data[idx:]


def dirty_tiles(a, b):
    """Count 64x64 tiles that differ between two same-size PPM frames."""
    (aw, ah, apx), (bw, bh, bpx) = a, b
    if (aw, ah) != (bw, bh):
        return -1  # size changed — treat as a hard signal
    stride = aw * 3
    dirty = 0
    for ty in range(0, ah, TILE):
        for tx in range(0, aw, TILE):
            differ = False
            for row in range(ty, min(ty + TILE, ah)):
                off = row * stride + tx * 3
                span = min(TILE, aw - tx) * 3
                if apx[off:off + span] != bpx[off:off + span]:
                    differ = True
                    break
            if differ:
                dirty += 1
    return dirty


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
    tmp = tempfile.mkdtemp(prefix="yetty-tilediff-")
    shot_a = os.path.join(tmp, "a.ppm")
    shot_b = os.path.join(tmp, "b.ppm")
    log_path = os.path.join(HERE, "render-tilediff.yetty.log")
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

    def yctl(*args, timeout=20):
        return subprocess.run([YCTL, "-p", str(port), *args], timeout=timeout,
                              stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, cwd=ROOT)

    try:
        # Wait for the RPC server to bind.
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

        # Initial frame → screenshot A.
        time.sleep(1.0)
        yctl("screenshot", shot_a)
        # Render visible content, then screenshot B.
        yctl("run", "printf 'RENDER-TILEDIFF-MARKER\\n'")
        time.sleep(1.0)
        yctl("screenshot", shot_b)

        # Give the async screenshot writes a moment to land.
        for _ in range(20):
            if os.path.exists(shot_a) and os.path.exists(shot_b):
                break
            time.sleep(0.25)

        a = read_ppm(shot_a)
        b = read_ppm(shot_b)
        if a is None or b is None:
            dump(f"screenshot PPMs missing/invalid (a={a is not None}, b={b is not None})")
            return 1
        if a[0] <= 0 or a[1] <= 0:
            dump(f"screenshot A has degenerate size {a[0]}x{a[1]}")
            return 1

        dirty = dirty_tiles(a, b)
        if dirty == 0:
            dump("render produced no framebuffer change between the two captures")
            return 1

        print(f"OK: captured {a[0]}x{a[1]} frames; {dirty} tile(s) changed after render")
        yctl("shutdown")
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            dump("shutdown succeeded path but yetty did not exit within 15s")
            return 1
        return 0
    finally:
        if proc.poll() is None:
            proc.kill()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        log.close()


if __name__ == "__main__":
    sys.exit(main())
