#!/usr/bin/env python3
"""
Production overlay scroll-mode test (#699 review #17).

The attach bridge stages a REAL overlay chrome surface — the scroll-mode
indicator strip in the pane's top-right corner. This test proves the FULL
input path, from actual Yetty ingress:

  click on the indicator (yctl mouse) -> compositor routes to the overlay
  child (opaque chrome hit) -> ordered pointer dispatch claims scene key
  focus + bridge chrome focus -> REAL arrow keypresses (yctl press) travel
  the pane stdin into the bridge classifier -> consumed by the overlay
  scene -> drained -> OVERLAY_INPUT over the socket -> the daemon-role
  chrome consumer scrolls the attachment's view.

Assertion: a blue marker printed early (scrolled far off screen) REAPPEARS
after clicking the indicator and pressing Up repeatedly. A content click
then releases focus and Down/live content returns.

Requires a display; SKIPS (77) headlessly.
"""
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
readback = __import__("ymux-grid-readback")

YETTY = readback.YETTY
YMUX = readback.YMUX
YCTL = readback.YCTL


def blueish_pixels(frame):
    width, height, pixels = frame
    count = 0
    limit = width * height * 3
    for offset in range(0, min(len(pixels), limit) - 2, 3):
        if (pixels[offset + 2] > 100 and pixels[offset] < 60 and
                pixels[offset + 1] < 60):
            count += 1
    return count


def main():
    if not readback.ensure_display():
        readback.skip("no display available")
    for path, name in ((YETTY, "yetty"), (YMUX, "ymux"), (YCTL, "yctl")):
        if not os.path.exists(path):
            readback.skip(f"{name} not found at {path}")
    if not readback.yctl_runnable():
        readback.skip("yctl client not runnable")

    port = readback.free_port()
    socket_name = f"ovscroll-{os.getpid()}"
    tmp = tempfile.mkdtemp(prefix="ymux-ovscroll-")
    log_path = os.path.join(tmp, "yetty.log")
    log = open(log_path, "w")

    def ymux(*args, timeout=30):
        return subprocess.run([YMUX, "-L", socket_name, *args], timeout=timeout,
                              stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, cwd=ROOT)

    def yctl(*args, timeout=20):
        return subprocess.run([YCTL, "-p", str(port), *args], timeout=timeout,
                              stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, cwd=ROOT)

    def rpc_up():
        import socket as socket_module
        try:
            with socket_module.create_connection(("127.0.0.1", port), timeout=1):
                return True
        except OSError:
            return False

    def dump(msg):
        log.flush()
        print(f"FAIL: {msg}")
        try:
            with open(log_path) as handle:
                sys.stderr.write("\n".join(handle.read().splitlines()[-40:]) + "\n")
        except OSError:
            pass

    proc = None
    try:
        created = ymux("new", "-d", "-s", "ovs", "-x", "80", "-y", "24")
        if created.returncode != 0:
            dump("ymux new-session failed")
            return 1
        proc = subprocess.Popen([YETTY, "-r", str(port)], stdout=log, stderr=log, cwd=ROOT)
        deadline = time.time() + 30
        while time.time() < deadline:
            if proc.poll() is not None:
                dump(f"yetty exited early (rc={proc.returncode})")
                return 1
            if rpc_up():
                break
            time.sleep(0.25)
        else:
            dump("RPC port never came up")
            return 1
        time.sleep(1.0)
        bridge_log = os.path.join(tmp, "bridge.log")
        yctl("run", f"{YMUX} -L {socket_name} attach -t ovs 2>{bridge_log}")
        time.sleep(2.0)

        # A BLUE marker, then 30 lines that push it far into scrollback.
        ymux("send", "-t", "ovs", "-l",
             "printf '\\033[44m                              \\033[0m\\n'")
        ymux("send", "-t", "ovs", "Enter")
        time.sleep(0.5)
        ymux("send", "-t", "ovs", "-l", "for i in $(seq 1 30); do echo line-$i; done")
        ymux("send", "-t", "ovs", "Enter")
        time.sleep(2.0)
        shot_live = os.path.join(tmp, "live.ppm")
        readback.capture_screen(yctl, shot_live)
        time.sleep(1.0)
        live = readback.read_ppm(shot_live)
        if live is None:
            dump("live screenshot missing")
            return 1
        blue_live = blueish_pixels(live)
        # A full marker row is ~2500+ px; a few hundred is unrelated chrome
        # noise in the root capture.
        if blue_live > 1200:
            dump(f"marker still visible before scrolling ({blue_live}) — "
                 "not enough scrollback")
            return 1

        # RESIZE first (review #19): the chrome strip must be re-staged from
        # the NEW geometry — the click below targets the post-resize width,
        # so a strip left at its attach-time x would miss and the whole
        # scroll leg would fail.
        yctl("resize", "1000", "700")
        time.sleep(2.5)
        width = 1000

        # CLICK the indicator strip (top-right of the pane) — claims chrome
        # focus through the real compositor routing.
        yctl("mouse-down", str(width - 30), "50")
        yctl("mouse-up", str(width - 30), "50")
        time.sleep(1.0)
        # REAL arrow keypresses through the pane stdin (GLFW code 265 = Up
        # — the press verb takes the numeric key code).
        for _ in range(40):
            yctl("press", "265")
        time.sleep(4.0)
        # DUAL-SURFACE evidence (as the readback matrix rows): the X window
        # and the WGPU target each can go stale independently; the marker
        # reappearing on EITHER is the ymux assertion. The sparse X11-tile
        # presentation yields ~20px/cell, so the delta floor is 300 (a
        # 30-cell marker row), not a solid-fill count.
        shot_scrolled = os.path.join(tmp, "scrolled.ppm")
        readback.capture_screen(yctl, shot_scrolled)
        shot_scrolled_wgpu = os.path.join(tmp, "scrolled-wgpu.ppm")
        yctl("screenshot", shot_scrolled_wgpu)
        time.sleep(1.0)
        scrolled = readback.read_ppm(shot_scrolled)
        scrolled_wgpu = readback.read_ppm(shot_scrolled_wgpu)
        if scrolled is None and scrolled_wgpu is None:
            dump("scrolled screenshots missing")
            return 1
        blue_scrolled = blueish_pixels(scrolled) if scrolled else 0
        blue_scrolled_wgpu = blueish_pixels(scrolled_wgpu) if scrolled_wgpu else 0
        if blue_scrolled - blue_live < 300 and blue_scrolled_wgpu < 300:
            dump(f"scroll-mode did not reach the daemon consumer "
                 f"(blue marker {blue_live} -> x11 {blue_scrolled} / wgpu {blue_scrolled_wgpu})")
            return 1
        blue_scrolled = max(blue_scrolled, blue_scrolled_wgpu)

        # Return the view to the live bottom WHILE chrome still holds focus
        # (GLFW 264 = Down), so the post-release marker lands in the
        # displayed viewport.
        for _ in range(48):
            yctl("press", "264")
        time.sleep(2.0)

        # Content click RELEASES focus; keys return to the terminal.
        yctl("mouse-down", "200", "300")
        yctl("mouse-up", "200", "300")
        time.sleep(1.0)

        # PROVE the release (review cycle 19): subsequent keys must reach
        # the PANE APPLICATION again. Type a red-background marker through
        # the ordinary terminal path and demand its pixels appear — red is
        # absent from the brand chrome and from this test's blue marker.
        yctl("type", "printf '\\033[41m                    \\033[0m\\n'")
        yctl("enter")
        time.sleep(3.0)
        shot_released = os.path.join(tmp, "released.ppm")
        readback.capture_screen(yctl, shot_released)
        shot_released_wgpu = os.path.join(tmp, "released-wgpu.ppm")
        yctl("screenshot", shot_released_wgpu)
        time.sleep(1.0)
        released = readback.read_ppm(shot_released)
        released_wgpu = readback.read_ppm(shot_released_wgpu)
        red_released = readback.reddish_pixels(released) if released else 0
        red_released_wgpu = readback.reddish_pixels(released_wgpu) if released_wgpu else 0
        if red_released < 300 and red_released_wgpu < 300:
            dump(f"post-release keys did NOT reach the pane application "
                 f"(reddish x11={red_released} wgpu={red_released_wgpu})")
            return 1

        print(f"OK: overlay scroll-mode — indicator click + real arrow keys "
              f"scrolled the view (blue {blue_live} -> {blue_scrolled}); "
              f"release verified (reddish {max(red_released, red_released_wgpu)})")
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
