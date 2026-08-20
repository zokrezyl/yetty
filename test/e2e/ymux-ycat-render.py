#!/usr/bin/env python3
"""Real-ycat figure render through ymux, asserted on the framebuffer
(#699 review cycle 19 — the blocking-regression display gate).

The seam every synthetic test misses: the BUILT ycat executable runs inside
an attached ymux pane on committed fixtures; the assertions read the actual
framebuffer:

  1. the figure's pixels are present (tall red SVG -> a red band),
  2. the scene scrolled by the document height: text typed after ycat
     (a blue-background marker) renders BELOW the figure's bottom edge,
  3. the figure survives view scrolling and detach/reattach while ordinary
     terminal painting continues.

SKIPs (exit 77) when no display/GPU is available — same contract as the
other ymux display tests.
"""
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
readback = __import__("ymux-grid-readback")

YETTY = readback.YETTY
YMUX = readback.YMUX
YCTL = readback.YCTL
YCAT = os.environ.get("YCAT", os.path.join(readback.ROOT, "build-desktop-ytrace-release",
                                           "tools", "ycat", "ycat"))
FIXTURES = os.environ.get("FIXTURES", os.path.join(readback.ROOT, "test", "fixtures", "ymux"))


def parse_ppm(path):
    """8/16-bit-aware P6 parser -> (width, height, sample_fn(x, y))."""
    try:
        with open(path, "rb") as handle:
            data = handle.read()
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
    idx += 1
    try:
        width, height, maxval = int(tokens[0]), int(tokens[1]), int(tokens[2])
    except (ValueError, IndexError):
        return None
    pixels = data[idx:]
    wide = maxval > 255

    def sample(x, y):
        if wide:
            offset = (y * width + x) * 6
            if offset + 6 > len(pixels):
                return (0, 0, 0)
            return (pixels[offset], pixels[offset + 2], pixels[offset + 4])
        offset = (y * width + x) * 3
        if offset + 3 > len(pixels):
            return (0, 0, 0)
        return (pixels[offset], pixels[offset + 1], pixels[offset + 2])

    return width, height, sample


def band_rows(frame, predicate, min_run=20, x_step=4):
    width, height, sample = frame
    rows = []
    for y in range(0, height, 2):
        run = 0
        for x in range(0, width, x_step):
            if predicate(*sample(x, y)):
                run += 1
                if run >= min_run:
                    rows.append(y)
                    break
            else:
                run = 0
    return rows


def red_pixel(r, g, b):
    return r > 150 and g < 60 and b < 60


def blue_pixel(r, g, b):
    return b > 90 and r < 80 and g < 80


def main():
    if not readback.ensure_display():
        readback.skip("no display available")
    for name, path in (("yetty", YETTY), ("ymux", YMUX), ("ycat", YCAT), ("yctl", YCTL)):
        if not os.path.exists(path):
            readback.skip(f"{name} not found at {path}")
    if not readback.yctl_runnable():
        readback.skip("yctl client not runnable")
    svg = os.path.join(FIXTURES, "red-box.svg")
    if not os.path.exists(svg):
        readback.skip(f"fixture missing: {svg}")

    port = readback.free_port()
    socket_name = f"ycatrender-{os.getpid()}"
    tmp = tempfile.mkdtemp(prefix="ymux-ycat-render-")
    log_path = os.path.join(tmp, "yetty.log")
    log = open(log_path, "w")

    def ymux(*args, timeout=30):
        env = dict(os.environ)
        env.pop("TMUX", None)
        env["TERM_PROGRAM"] = "yetty"
        env["YTRACE_DEFAULT_ON"] = "no"
        return subprocess.run([YMUX, "-L", socket_name, *args], timeout=timeout, env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
                              cwd=readback.ROOT)

    def yctl(*args, timeout=20):
        return subprocess.run([YCTL, "-p", str(port), *args], timeout=timeout,
                              stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
                              cwd=readback.ROOT)

    def dump(msg):
        log.flush()
        print(f"FAIL: {msg}")
        try:
            with open(log_path) as handle:
                sys.stderr.write("\n".join(handle.read().splitlines()[-40:]) + "\n")
        except OSError:
            pass

    def shot(name):
        path = os.path.join(tmp, name)
        readback.capture_screen(yctl, path)
        return parse_ppm(path)

    def figure_and_marker(frame, want_marker=True):
        """Red band rows + blue marker rows; None-safe."""
        if frame is None:
            return None, None
        reds = band_rows(frame, red_pixel)
        blues = band_rows(frame, blue_pixel, min_run=8) if want_marker else []
        return reds, blues

    proc = None
    try:
        if ymux("new", "-d", "-s", "render", "-x", "100", "-y", "30").returncode != 0:
            dump("ymux new-session failed")
            return 1
        proc = subprocess.Popen([YETTY, "-r", str(port)], stdout=log, stderr=log,
                                cwd=readback.ROOT)
        deadline = time.time() + 30
        import socket as socket_module
        while time.time() < deadline:
            if proc.poll() is not None:
                dump(f"yetty exited early rc={proc.returncode}")
                return 1
            try:
                with socket_module.create_connection(("127.0.0.1", port), timeout=1):
                    break
            except OSError:
                time.sleep(0.25)
        else:
            dump("RPC port never came up")
            return 1
        time.sleep(1.5)

        bridge_log = os.path.join(tmp, "bridge.log")
        yctl("run", f"{YMUX} -L {socket_name} attach -t render 2>{bridge_log}")
        time.sleep(3.0)

        # The REAL producer inside the pane, then a blue marker line after.
        ymux("send", "-t", "render", "-l", f"clear; {YCAT} {svg} 2>/dev/null")
        ymux("send", "-t", "render", "Enter")
        time.sleep(5.0)
        ymux("send", "-t", "render", "-l",
             "printf '\\033[44m          MARKER          \\033[0m\\n'")
        ymux("send", "-t", "render", "Enter")
        time.sleep(3.0)

        reds, blues = figure_and_marker(shot("after.ppm"))
        if reds is None:
            dump("screenshot unreadable")
            return 1
        if not reds:
            dump("figure (red band) never reached the framebuffer")
            return 1
        if not blues:
            dump("post-figure marker (blue) never reached the framebuffer")
            return 1
        red_bottom = reds[-1]
        blue_top = blues[0]
        if blue_top <= red_bottom:
            dump(f"marker OVERLAPS the figure (red bottom={red_bottom}, "
                 f"blue top={blue_top}) — the scene did not scroll by the "
                 f"document height")
            return 1

        # View scroll (wheel up then back): the figure must repaint, not
        # vanish or go stale, while the terminal keeps painting.
        for _ in range(10):
            yctl("mouse-scroll", "600", "300", "0", "1")
            time.sleep(0.1)
        time.sleep(1.5)
        reds_scrolled, _ = figure_and_marker(shot("scrolled.ppm"), want_marker=False)
        if not reds_scrolled:
            dump("figure vanished while scrolled into history")
            return 1
        for _ in range(12):
            yctl("mouse-scroll", "600", "300", "0", "-1")
            time.sleep(0.1)
        time.sleep(1.5)

        # Detach, reattach: the republished session must still show the
        # figure with the marker below it.
        ymux("detach", "-s", "render")
        time.sleep(2.0)
        yctl("run", f"{YMUX} -L {socket_name} attach -t render 2>{bridge_log}.2")
        time.sleep(4.0)
        reds_re, blues_re = figure_and_marker(shot("reattached.ppm"))
        if not reds_re:
            dump("figure missing after detach/reattach")
            return 1
        if not blues_re or blues_re[0] <= reds_re[-1]:
            dump(f"marker not below figure after reattach "
                 f"(red bottom={reds_re[-1] if reds_re else -1}, "
                 f"blue top={blues_re[0] if blues_re else -1})")
            return 1

        # RESIZE (review #19): the figure and its below-marker survive a
        # window resize (the layout barrier applies it as one frame).
        yctl("resize", "1100", "720")
        time.sleep(3.0)
        reds_resized, blues_resized = figure_and_marker(shot("resized.ppm"))
        if not reds_resized:
            dump("figure missing after window resize")
            return 1
        if not blues_resized or blues_resized[0] <= reds_resized[-1]:
            dump(f"marker not below figure after resize "
                 f"(red bottom={reds_resized[-1] if reds_resized else -1}, "
                 f"blue top={blues_resized[0] if blues_resized else -1})")
            return 1

        # FORCED RECOVERY (review #19): `ymux recover` discards the queued
        # stream and forces a fresh complete republication — the figure must
        # come back, not vanish or go stale.
        ymux("recover")
        time.sleep(4.0)
        reds_recovered, blues_recovered = figure_and_marker(shot("recovered.ppm"))
        if not reds_recovered:
            dump("figure missing after forced recovery")
            return 1
        if not blues_recovered or blues_recovered[0] <= reds_recovered[-1]:
            dump(f"marker not below figure after recovery "
                 f"(red bottom={reds_recovered[-1] if reds_recovered else -1}, "
                 f"blue top={blues_recovered[0] if blues_recovered else -1})")
            return 1

        # The markdown FIGURE framebuffer regression lives in its own
        # standalone gate (test/e2e/ymux-markdown-reserve.py) with a clean
        # session — reusing this SVG session contaminates it (the SVG figure
        # is scroll-anchored and survives `clear`). The headless ycat_real
        # test additionally proves markdown materializes real scene geometry.
        print(f"OK: ycat render — figure rows {reds[0]}..{red_bottom}, marker below at "
              f"{blue_top}; survives scroll ({len(reds_scrolled)} rows), reattach "
              f"(red bottom={reds_re[-1]}, marker top={blues_re[0]}), resize "
              f"(red bottom={reds_resized[-1]}, marker top={blues_resized[0]}), and "
              f"recovery (red bottom={reds_recovered[-1]}, marker top={blues_recovered[0]})")
        try:
            yctl("shutdown")
            proc.wait(timeout=20)
        except (subprocess.TimeoutExpired, OSError, subprocess.SubprocessError):
            pass
        return 0
    finally:
        try:
            ymux("kill-server", timeout=10)
        except (OSError, subprocess.SubprocessError):
            pass
        if proc is not None and proc.poll() is None:
            proc.kill()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        log.close()


if __name__ == "__main__":
    sys.exit(main())
