#!/usr/bin/env python3
"""
Headful ybrowser image-position regression guard.

Launches the real yetty running ybrowser in INTERACTIVE mode on the committed
demo/assets/ybrowser/images.html, captures a screenshot, and asserts the images
land where the layout put them — the red rose (top "PNG" strip) must render in
the UPPER part of the page, not shifted down into the lower "JPEG" strip.

This exists because a figure-anchor double-count regressed the interactive
(ygrid) render path: the complex render op adds the wire origin, and the grid
ALSO anchored at the figure's own bounds, so every image painted at ~2x its
laid-out position. Negligible for a figure near the top-left, catastrophic for
one in a lower row — Google News story photos flew off the pane. The whole
existing image test surface (render/, anchors/, ut/) checks LAYOUT geometry or
the browser's WIRE emission, both of which stayed correct; only actual pixels of
the interactive path expose the doubling. This test closes that gap by looking
at the framebuffer.

Detection is content-based, not coordinate-based: the rose is the only strongly
RED thing on the page and the gradient the only strongly TEAL thing, so their
vertical centroids are unambiguous. The window is resized to a fixed size first
so the "upper strip" band is deterministic regardless of the host's default
window geometry.

Correct render:  rose centroid ~0.42 of frame height, gradient ~0.44.
Doubled (buggy): rose ~0.76, gradient ~0.65 — both well past the 0.58 gate.

Requires a real display/GPU (yetty opens a WebGPU surface) and a runnable yctl
client, so it SKIPS (exit 77) when there is no display, no yetty/ybrowser binary,
or yctl cannot be launched — safe in headless CI. Screenshots use the server's
built-in PPM (P6) writer, so there is no image-codec dependency.

Env:
  YETTY     path to the yetty binary
  YBROWSER  path to the ybrowser binary
  YCTL      path to yctl.py
  YBROWSER_PAGE  override the page to load (default: the committed images.html)
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
YBROWSER = os.environ.get(
    "YBROWSER", os.path.join(ROOT, "build-desktop-ytrace-release", "tools", "ybrowser", "ybrowser"))
YCTL = os.environ.get("YCTL", os.path.join(ROOT, "tools", "yctl-client", "yctl.py"))
PAGE = os.environ.get("YBROWSER_PAGE", os.path.join(ROOT, "demo", "assets", "ybrowser", "images.html"))
SKIP = 77

# Fixed window so the "upper strip" band is deterministic. The rose sits a
# content-fixed distance below the top (chrome + h1 + intro + h2 ≈ 400 logical),
# so a tall enough window keeps its ratio well under the gate.
WIN_W, WIN_H = 1400, 1000
UPPER_GATE = 0.58     # rose/gradient centroid must be above this fraction of H
MIN_RED = 1500        # rose must actually be painted (px count)
MIN_TEAL = 3000       # gradient must actually be painted (px count)


def skip(reason):
    print(f"SKIP: {reason}")
    sys.exit(SKIP)


def ensure_display():
    """True if a display is usable, discovering an owned X/Wayland socket when
    the environment didn't pass DISPLAY/WAYLAND_DISPLAY through."""
    if os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"):
        return True
    xdg = os.environ.get("XDG_RUNTIME_DIR")
    if xdg:
        try:
            for name in sorted(os.listdir(xdg)):
                if name.startswith("wayland-") and not name.endswith(".lock"):
                    os.environ["WAYLAND_DISPLAY"] = name
                    return True
        except OSError:
            pass
    xdir = "/tmp/.X11-unix"
    uid = os.getuid()
    try:
        names = sorted(os.listdir(xdir))
    except OSError:
        names = []
    for name in names:
        if not (name.startswith("X") and name[1:].isdigit()):
            continue
        try:
            if os.stat(os.path.join(xdir, name)).st_uid != uid:
                continue
        except OSError:
            continue
        os.environ["DISPLAY"] = ":" + name[1:]
        return True
    return False


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


def classify(frame):
    """Locate the rose (strong red) and the gradient (strong teal). Returns
    {red:(count, centroid_y_ratio), teal:(count, centroid_y_ratio)}. Centroid is
    a fraction of frame height (0 = top). None ratio when nothing matched."""
    w, h, px = frame
    stride = w * 3
    red_n = red_sy = 0
    teal_n = teal_sy = 0
    for y in range(h):
        row = y * stride
        for x in range(w):
            p = row + x * 3
            r, g, b = px[p], px[p + 1], px[p + 2]
            if r > 120 and r - g > 35 and r - b > 35:
                red_n += 1
                red_sy += y
            elif g > 120 and b > 120 and g - r > 15 and b - r > 15 and r < 170:
                teal_n += 1
                teal_sy += y
    red = (red_n, (red_sy / red_n / h) if red_n else None)
    teal = (teal_n, (teal_sy / teal_n / h) if teal_n else None)
    return {"red": red, "teal": teal}


def main():
    if not ensure_display():
        skip("no display available (no DISPLAY/WAYLAND, no owned X/Wayland socket)")
    if not os.path.exists(YETTY):
        skip(f"yetty binary not found at {YETTY}")
    if not os.path.exists(YBROWSER):
        skip(f"ybrowser binary not found at {YBROWSER}")
    if not os.path.exists(YCTL):
        skip(f"yctl client not found at {YCTL}")
    if not os.path.exists(PAGE):
        skip(f"page fixture not found at {PAGE}")
    if not yctl_runnable():
        skip(f"yctl client at {YCTL} is not runnable (interpreter/deps missing)")

    port = free_port()
    tmp = tempfile.mkdtemp(prefix="yetty-imgpos-")
    shot = os.path.join(tmp, "shot.ppm")
    log_path = os.path.join(tmp, "yetty.log")  # temp dir, never the worktree
    log = open(log_path, "w")
    # Keep the child ybrowser quiet on the PTY and hold the shell open so the
    # window stays up for the screenshot; yetty still binds its RPC server.
    child = f"bash -c '{YBROWSER} --interactive {PAGE} 2>/dev/null; sleep 3600'"
    proc = subprocess.Popen([YETTY, "-r", str(port), "-e", child],
                            stdout=log, stderr=log, cwd=ROOT)

    def dump(msg):
        log.flush()
        print(f"FAIL: {msg}")
        try:
            with open(log_path) as f:
                tail = f.read().splitlines()[-60:]
            sys.stderr.write("\n".join(tail) + "\n")
        except OSError:
            pass

    def yctl(*args, timeout=20):
        return subprocess.run([YCTL, "-p", str(port), *args], timeout=timeout,
                              stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, cwd=ROOT)

    def rpc_up():
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1):
                return True
        except OSError:
            return False

    try:
        deadline = time.time() + 30
        listening = False
        while time.time() < deadline:
            if proc.poll() is not None:
                dump(f"yetty exited early (rc={proc.returncode}) before the RPC server bound")
                return 1
            if rpc_up():
                listening = True
                break
            time.sleep(0.25)
        if not listening:
            dump("RPC port did not accept a connection within 30s")
            return 1

        # Fixed window → deterministic strip band, then let the page relayout
        # and its (local, fast) images decode + paint.
        yctl("resize", str(WIN_W), str(WIN_H))
        time.sleep(5.0)
        yctl("screenshot", shot)
        for _ in range(20):
            if os.path.exists(shot):
                break
            time.sleep(0.25)

        frame = read_ppm(shot)
        if frame is None:
            dump("screenshot PPM missing/invalid")
            return 1
        w, h, _ = frame
        if w <= 0 or h <= 0:
            dump(f"screenshot has degenerate size {w}x{h}")
            return 1
        if h < 800:
            # The rose sits a content-fixed ~400px below the top; below ~800px
            # tall its ratio creeps toward the gate and the margin gets thin.
            # A real CI display (Xvfb 1280x1024+) never trips this.
            skip(f"window too short ({w}x{h}); the upper-strip gate needs >= 800px height")

        marks = classify(frame)
        (red_n, red_cy) = marks["red"]
        (teal_n, teal_cy) = marks["teal"]
        print(f"frame {w}x{h}  rose(red): count={red_n} centroid_y="
              f"{'-' if red_cy is None else round(red_cy, 3)}  "
              f"gradient(teal): count={teal_n} centroid_y="
              f"{'-' if teal_cy is None else round(teal_cy, 3)}")

        failures = []
        if red_n < MIN_RED:
            failures.append(f"rose not rendered (red px {red_n} < {MIN_RED})")
        elif red_cy >= UPPER_GATE:
            failures.append(
                f"rose shifted down: centroid_y {red_cy:.3f} >= {UPPER_GATE} "
                f"(it belongs in the top PNG strip; a value near ~0.76 is the "
                f"figure-anchor double-count)")
        if teal_n < MIN_TEAL:
            failures.append(f"gradient not rendered (teal px {teal_n} < {MIN_TEAL})")
        elif teal_cy >= UPPER_GATE:
            failures.append(
                f"gradient shifted down: centroid_y {teal_cy:.3f} >= {UPPER_GATE}")

        if failures:
            for f in failures:
                dump(f)
            return 1

        print("OK: rose + gradient render in the upper PNG strip (no anchor double-count)")
        try:
            yctl("shutdown")
            proc.wait(timeout=20)
        except (subprocess.TimeoutExpired, OSError, subprocess.SubprocessError):
            pass
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
