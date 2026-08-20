#!/usr/bin/env python3
"""Real-ycat MARKDOWN figure reservation through ymux, asserted on the
framebuffer (#699 review cycle 21 — the markdown zero-height regression gate).

The bug this pins: a markdown document serialized scene_max_y=0 (viewport-vs-
content bounds), so the daemon reserved 0 rows and a line typed AFTER the
document landed on top of it. Here the BUILT ycat runs a compact markdown
fixture inside a fresh, uncontaminated attached ymux pane, between a RED
marker line above and a BLUE marker line below; a correct reservation pushes
BELOW well past ABOVE (the document's rendered height), so the vertical gap
between the two markers must span many cell rows, not a single line.

A dedicated session (not the ycat-render SVG session) because ycat figures are
scroll-anchored and survive `clear` — reusing a session would paint the SVG
over this document. SKIPs (exit 77) when no display/GPU is available.
"""
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
readback = __import__("ymux-grid-readback")
ycat_render = __import__("ymux-ycat-render")

YETTY = readback.YETTY
YMUX = readback.YMUX
YCTL = readback.YCTL
YCAT = ycat_render.YCAT
FIXTURES = ycat_render.FIXTURES
parse_ppm = ycat_render.parse_ppm
band_rows = ycat_render.band_rows
red_pixel = ycat_render.red_pixel
blue_pixel = ycat_render.blue_pixel


def main():
    if not readback.ensure_display():
        readback.skip("no display available")
    for name, path in (("yetty", YETTY), ("ymux", YMUX), ("ycat", YCAT), ("yctl", YCTL)):
        if not os.path.exists(path):
            readback.skip(f"{name} not found at {path}")
    if not readback.yctl_runnable():
        readback.skip("yctl client not runnable")
    md = os.path.join(FIXTURES, "doc-small.md")
    if not os.path.exists(md):
        readback.skip(f"fixture missing: {md}")

    port = readback.free_port()
    socket_name = f"mdreserve-{os.getpid()}"
    tmp = tempfile.mkdtemp(prefix="ymux-md-reserve-")
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

    proc = None
    try:
        if ymux("new", "-d", "-s", "md", "-x", "100", "-y", "30").returncode != 0:
            dump("ymux new-session failed")
            return 1
        proc = subprocess.Popen([YETTY, "-r", str(port)], stdout=log, stderr=log,
                                cwd=readback.ROOT)
        import socket as socket_module
        deadline = time.time() + 30
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
        yctl("run", f"{YMUX} -L {socket_name} attach -t md 2>{bridge_log}")
        time.sleep(3.0)

        # ABOVE marker, the real markdown figure, then the BELOW marker.
        ymux("send", "-t", "md", "-l", "clear")
        ymux("send", "-t", "md", "Enter")
        time.sleep(1.0)
        ymux("send", "-t", "md", "-l",
             "printf '\\033[41m       ABOVE       \\033[0m\\n'")
        ymux("send", "-t", "md", "Enter")
        time.sleep(0.5)
        ymux("send", "-t", "md", "-l", f"{YCAT} {md} 2>/dev/null")
        ymux("send", "-t", "md", "Enter")
        time.sleep(5.0)
        ymux("send", "-t", "md", "-l",
             "printf '\\033[44m       BELOW       \\033[0m\\n'")
        ymux("send", "-t", "md", "Enter")
        time.sleep(3.0)

        frame = shot("markdown.ppm")
        if frame is None:
            dump("markdown screenshot unreadable")
            return 1
        reds = band_rows(frame, red_pixel, min_run=8)
        blues = band_rows(frame, blue_pixel, min_run=8)
        if not reds:
            dump("ABOVE marker (red) never reached the framebuffer")
            return 1
        if not blues:
            dump("BELOW marker (blue) never reached the framebuffer")
            return 1
        red_bottom = reds[-1]
        blue_top = blues[0]
        gap = blue_top - red_bottom
        # doc-small.md renders a styled H1 + three paragraphs — several cell
        # rows tall. A zero/near-zero reservation (the bug) would put BELOW one
        # line under ABOVE; require the gap to exceed several rows (~18-24px
        # each) so the regression fails loudly.
        if gap < 80:
            dump(f"BELOW marker too close to ABOVE (gap={gap}px, red_bottom={red_bottom}, "
                 f"blue_top={blue_top}) — the markdown document reserved no height "
                 f"(the zero scene_max_y bug)")
            return 1

        # VISIBLE CONTENT (review cycle 22): the reservation gap alone could be
        # satisfied by a blank figure with the right height. Assert the document
        # BAND between the markers actually carries rendered glyph pixels — light
        # text on the dark canvas — so a blank figure fails.
        width, height, sample = frame
        content_pixels = 0
        for y in range(red_bottom + 4, blue_top - 4, 2):
            for x in range(0, width, 4):
                r, g, b = sample(x, y)
                # Off-canvas light pixels (glyph strokes) — the brand canvas is
                # near-black (#0B1014); text is off-white.
                if r > 120 and g > 120 and b > 120:
                    content_pixels += 1
        if content_pixels < 40:
            dump(f"markdown band has no visible glyph pixels (found {content_pixels}) — "
                 f"the figure reserved height but rendered blank")
            return 1

        print(f"OK: markdown reserve — document reserves {gap}px between ABOVE "
              f"({red_bottom}) and BELOW ({blue_top}), {content_pixels} glyph pixels rendered")
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
