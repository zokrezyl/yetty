#!/usr/bin/env python3
"""
Nightly ymux grid GPU-readback test (#699.5 follow-on).

End-to-end through the REAL stack: ymux daemon (private socket) -> `ymux
attach` bridge inside a launched yetty pane -> vtsink yRPC lane -> yscene
terminal grid -> GPU compositor -> screenshot readback.

The assertion is DETERMINISTIC and font-independent: keys sent to the daemon
paint a run of red-background cells (SGR 41), and the screenshot after must
contain a block of reddish pixels that the screenshot before does not. The
brand palette has no red anywhere, so the count is a clean signal of the grid's
background-fill path actually reaching the framebuffer.

Requires a real display/GPU; SKIPS (exit 77) when there is no display, or the
yetty / ymux binaries or yctl client are missing — safe in headless CI.

Env:
  YETTY  path to the yetty binary (default: release build path)
  YMUX   path to the ymux CLI     (default: release build path)
  YCTL   path to yctl.py          (default: tools/yctl-client/yctl.py)
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
YMUX = os.environ.get("YMUX", os.path.join(ROOT, "build-desktop-ytrace-release", "tools",
                                           "ymux", "ymux"))
YCTL = os.environ.get("YCTL", os.path.join(ROOT, "tools", "yctl-client", "yctl.py"))
SKIP = 77
RED_RUN_CELLS = 40


def skip(reason):
    print(f"SKIP: {reason}")
    sys.exit(SKIP)


def ensure_display():
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
        sock_path = os.path.join(xdir, name)
        try:
            if os.stat(sock_path).st_uid != uid:
                continue
        except OSError:
            continue
        # LIVENESS probe: a stale socket from a dead server must not be
        # trusted (it turns the skip into a false failure).
        try:
            probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            probe.settimeout(1.0)
            probe.connect(sock_path)
            probe.close()
        except OSError:
            continue
        os.environ["DISPLAY"] = ":" + name[1:]
        return True
    return False


def yctl_runnable():
    try:
        proc = subprocess.run([YCTL, "--help"], timeout=60, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, cwd=ROOT)
        return proc.returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def capture_screen(yctl_call, path):
    """Capture the VISIBLE screen. Under X, `import -window root` reads the
    real window content; the yctl screenshot verb reads the WGPU render
    target's texture, which the X11-tile presentation path can leave STALE
    (observed: minutes-old frames) — it stays as the non-X fallback only."""
    if os.environ.get("DISPLAY"):
        try:
            proc = subprocess.run(["import", "-window", "root", path], timeout=20,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                  env=os.environ)
            if proc.returncode == 0 and os.path.exists(path):
                return
        except (OSError, subprocess.SubprocessError):
            pass
    yctl_call("screenshot", path)


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def read_ppm(path):
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
    if len(tokens) < 3:
        return None
    idx += 1
    try:
        width, height = int(tokens[0]), int(tokens[1])
    except ValueError:
        return None
    return width, height, data[idx:]


def hueish_pixels(frame, channel):
    """Count pixels dominated by one channel (0=red, 1=green): strong there,
    weak elsewhere. Font-independent for bg runs; for fg it counts the lit
    glyph coverage of U+2588 FULL BLOCK, which fills the cell."""
    width, height, pixels = frame
    count = 0
    limit = width * height * 3
    for offset in range(0, min(len(pixels), limit) - 2, 3):
        rgb = (pixels[offset], pixels[offset + 1], pixels[offset + 2])
        others = [rgb[i] for i in range(3) if i != channel]
        if rgb[channel] > 100 and max(others) < 60:
            count += 1
    return count


def reddish_pixels(frame):
    """Count pixels that read as a red cell background: strong red channel,
    weak green/blue. The SGR-41 palette red lands here on any reasonable
    palette; nothing in the brand chrome does."""
    width, height, pixels = frame
    count = 0
    limit = width * height * 3
    for offset in range(0, min(len(pixels), limit) - 2, 3):
        red = pixels[offset]
        green = pixels[offset + 1]
        blue = pixels[offset + 2]
        if red > 100 and green < 60 and blue < 60:
            count += 1
    return count


def main():
    if not ensure_display():
        skip("no display available (no DISPLAY/WAYLAND, no owned X/Wayland socket)")
    if not os.path.exists(YETTY):
        skip(f"yetty binary not found at {YETTY}")
    if not os.path.exists(YMUX):
        skip(f"ymux binary not found at {YMUX}")
    if not os.path.exists(YCTL):
        skip(f"yctl client not found at {YCTL}")
    if not yctl_runnable():
        skip(f"yctl client at {YCTL} is not runnable (interpreter/deps missing)")

    port = free_port()
    socket_name = f"readback-{os.getpid()}"
    tmp = tempfile.mkdtemp(prefix="ymux-readback-")
    shot_before = os.path.join(tmp, "before.ppm")
    shot_after = os.path.join(tmp, "after.ppm")
    log_path = os.path.join(tmp, "yetty.log")
    log = open(log_path, "w")

    def ymux(*args, timeout=30):
        return subprocess.run([YMUX, "-L", socket_name, *args], timeout=timeout,
                              stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, cwd=ROOT)

    def yctl(*args, timeout=20):
        return subprocess.run([YCTL, "-p", str(port), *args], timeout=timeout,
                              stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, cwd=ROOT)

    def rpc_up():
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1):
                return True
        except OSError:
            return False

    def dump(msg):
        log.flush()
        print(f"FAIL: {msg}")
        try:
            with open(log_path) as handle:
                tail = handle.read().splitlines()[-60:]
            sys.stderr.write("\n".join(tail) + "\n")
        except OSError:
            pass

    proc = None
    try:
        # Detached session on a PRIVATE socket (auto-starts the daemon).
        created = ymux("new", "-d", "-s", "smoke", "-x", "80", "-y", "24")
        if created.returncode != 0:
            dump("ymux new-session failed (daemon did not start)")
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
            dump("RPC port did not accept a connection within 30s")
            return 1

        time.sleep(1.0)
        capture_screen(yctl, shot_before)

        # Attach the bridge in the pane; the grid scenes take over rendering.
        yctl("run", f"{YMUX} -L {socket_name} attach -t smoke")
        time.sleep(2.0)

        # Paint a red-background run THROUGH the daemon (send-keys -> pty ->
        # engine -> vtsink lane -> grid), then let the frame settle.
        spaces = " " * RED_RUN_CELLS
        ymux("send", "-t", "smoke", "-l", f"printf '\\033[41m{spaces}\\033[0m\\n'")
        ymux("send", "-t", "smoke", "Enter")
        time.sleep(2.0)
        capture_screen(yctl, shot_after)

        for _ in range(20):
            if os.path.exists(shot_before) and os.path.exists(shot_after):
                break
            time.sleep(0.25)

        before = read_ppm(shot_before)
        after = read_ppm(shot_after)
        if before is None or after is None:
            dump(f"screenshot PPMs missing/invalid (before={before is not None}, "
                 f"after={after is not None})")
            return 1

        red_before = reddish_pixels(before)
        red_after = reddish_pixels(after)
        # A 40-cell run at any plausible cell size is thousands of pixels;
        # demand a conservative floor and a near-clean baseline.
        if red_before > 50:
            dump(f"baseline frame already has {red_before} reddish pixels — "
                 "assertion basis invalid")
            return 1
        if red_after < 400:
            dump(f"red-background run did not reach the framebuffer "
                 f"(before={red_before}, after={red_after} reddish pixels)")
            return 1

        # FG GLYPH rasterization readback (review #11): green U+2588 FULL
        # BLOCK glyphs — the atlas-sampled foreground path, font-independent
        # (the block fills its cell).
        # BLUE (34), not green: yetty's X11-tile presentation currently drops
        # green-dominant pixels (bug characterized: the WGPU target shows
        # green, the X window shows black — same frame). Blue and red
        # survive; the assertion must measure ymux, not that yetty bug.
        blocks = "\\342\\226\\210" * 20  # U+2588 FULL BLOCK as octal UTF-8
        ymux("send", "-t", "smoke", "-l",
             f"printf '\\033[34m{blocks}\\033[0m\\n'")
        ymux("send", "-t", "smoke", "Enter")
        time.sleep(2.0)
        shot_glyphs = os.path.join(tmp, "glyphs.ppm")
        capture_screen(yctl, shot_glyphs)
        for _ in range(20):
            if os.path.exists(shot_glyphs):
                break
            time.sleep(0.25)
        glyphs = read_ppm(shot_glyphs)
        if glyphs is None:
            dump("glyph screenshot missing/invalid")
            return 1
        green_after = hueish_pixels(glyphs, 2)
        green_before = hueish_pixels(before, 2)
        if green_before > 50:
            dump(f"baseline has {green_before} greenish pixels — basis invalid")
            return 1
        # Threshold is PRESENCE-based (>250): absolute counts through the
        # X11-tile path are depressed by a characterized yetty presentation
        # issue; zero-vs-present is the ymux assertion.
        if green_after < 250:
            dump(f"fg glyph run did not reach the framebuffer "
                 f"(before={green_before}, after={green_after} greenish pixels)")
            return 1

        # ATTRIBUTE rendering (review #15): REVERSE video (SGR 7) swaps
        # fg/bg — green-fg SPACES paint nothing normally, but with reverse
        # they fill the cells green. The delta between the two runs pins the
        # attribute path on the GPU, font-independently.
        ymux("send", "-t", "smoke", "-l", "clear")
        ymux("send", "-t", "smoke", "Enter")
        time.sleep(1.0)
        spaces20 = " " * 20
        ymux("send", "-t", "smoke", "-l",
             f"printf '\\033[34m{spaces20}\\033[0m|\\033[34;7m{spaces20}\\033[0m\\n'")
        ymux("send", "-t", "smoke", "Enter")
        time.sleep(2.0)
        shot_reverse = os.path.join(tmp, "reverse.ppm")
        capture_screen(yctl, shot_reverse)
        for _ in range(20):
            if os.path.exists(shot_reverse):
                break
            time.sleep(0.25)
        reverse_frame = read_ppm(shot_reverse)
        if reverse_frame is None:
            dump("reverse screenshot missing/invalid")
            return 1
        green_reverse = hueish_pixels(reverse_frame, 2)
        # Non-reverse green spaces paint ~0; the reverse run fills ~20 cells.
        if green_reverse < 120:
            dump(f"reverse-video run did not fill cells ({green_reverse} greenish pixels)")
            return 1

        # WIDE placement (review #15): "AB" (2 narrow columns) and a CJK
        # ideograph (1 wide char = 2 columns) must indent a following red
        # run IDENTICALLY. A wide char rendered 1 column wide shifts the
        # second run left by a cell — the leftmost-red x per row band
        # detects it, font-independently.
        def red_row_bands(frame, channel=0):
            width, height, pixels = frame
            row_min = {}
            scan_rows = min(height, len(pixels) // (width * 3))
            for y in range(scan_rows):
                for x in range(width):
                    offset = (y * width + x) * 3
                    others = [offset + other for other in (0, 1, 2) if other != channel]
                    if (pixels[offset + channel] > 100 and
                            pixels[others[0]] < 60 and pixels[others[1]] < 60):
                        row_min.setdefault(y, x)
                        break
            red_rows = sorted(row_min)
            if not red_rows:
                return row_min, []
            bands = [[red_rows[0]]]
            for y in red_rows[1:]:
                if y - bands[-1][-1] > 4:
                    bands.append([])
                bands[-1].append(y)
            return row_min, bands

        def two_band_indents(tag, print_command, shot_name):
            """Print two prefixed red runs, return (left_a, left_b).

            The X11-tile presentation intermittently drops a freshly-printed
            row (characterized yetty defect, PARITY.md); a repaint nudge +
            recapture recovers it. GATES after the retries are exhausted —
            a row still missing after three repaints is a real regression.
            """
            ymux("send", "-t", "smoke", "-l", "clear")
            ymux("send", "-t", "smoke", "Enter")
            time.sleep(1.0)
            ymux("send", "-t", "smoke", "-l", print_command)
            ymux("send", "-t", "smoke", "Enter")
            time.sleep(2.0)
            for attempt in range(3):
                shot = os.path.join(tmp, f"{shot_name}-{attempt}.ppm")
                capture_screen(yctl, shot)
                for _ in range(20):
                    if os.path.exists(shot):
                        break
                    time.sleep(0.25)
                frame = read_ppm(shot)
                if frame is None:
                    continue
                row_min, bands = red_row_bands(frame)
                if len(bands) >= 2:
                    if len(bands) > 2:
                        # Row remnants from earlier phases can linger; keep
                        # the two TALLEST bands (the freshly printed rows).
                        bands.sort(key=len, reverse=True)
                        bands = sorted(bands[:2], key=lambda band: band[0])
                    left_a = min(row_min[y] for y in bands[0])
                    left_b = min(row_min[y] for y in bands[1])
                    return left_a, left_b
                # Repaint nudge: a fresh scroll invalidates the stale tiles.
                ymux("send", "-t", "smoke", "-l", "printf '\\n'")
                ymux("send", "-t", "smoke", "Enter")
                time.sleep(1.5)
            dump(f"{tag}: second red row absent after 3 repaint retries "
                 f"({len(bands)} band) — presentation regression")
            return None

        red10 = " " * 10
        wide_indents = two_band_indents(
            "wide-placement",
            f"printf 'AB\\033[41m{red10}\\033[0m\\n\\n\\n\\n\\346\\274\\242\\033[41m{red10}\\033[0m\\n'",
            "wide")
        if wide_indents is None:
            return 1
        left_a, left_b = wide_indents
        if abs(left_a - left_b) > 2:
            dump(f"wide-char placement drift: narrow-prefix x={left_a}, "
                 f"wide-prefix x={left_b} — a wide glyph is not 2 columns")
            return 1

        # COMBINING placement (review #17): "AB" (2 columns) and "Ae" +
        # U+0301 combining acute (the cluster composites into ONE column,
        # so also 2 columns) must indent the red run IDENTICALLY. A mark
        # given its own column shifts the second run right by a cell.
        combining_indents = two_band_indents(
            "combining-placement",
            f"printf 'AB\\033[41m{red10}\\033[0m\\n\\n\\n\\n"
            f"Ae\\314\\201\\033[41m{red10}\\033[0m\\n'",
            "combining")
        if combining_indents is None:
            return 1
        left_a2, left_b2 = combining_indents
        if abs(left_a2 - left_b2) > 2:
            dump(f"combining placement drift: narrow-prefix x={left_a2}, "
                 f"cluster-prefix x={left_b2} — a combining cluster is not 1 column")
            return 1

        # SELECTION readback (review #16): a left-button drag over printed
        # text drives bridge -> set_terminal_selection -> the grid's
        # inverted span. Inverted default cells render LIGHT backgrounds —
        # count light pixels before/after the drag.
        ymux("send", "-t", "smoke", "-l", "clear")
        ymux("send", "-t", "smoke", "Enter")
        time.sleep(1.0)
        ymux("send", "-t", "smoke", "-l", "printf 'select-me-select-me-select-me\\n'")
        ymux("send", "-t", "smoke", "Enter")
        time.sleep(2.0)
        shot_presel = os.path.join(tmp, "presel.ppm")
        capture_screen(yctl, shot_presel)
        # Drag INSIDE the pane content (below any tab chrome): the printed
        # row sits in the first text rows of the pane.
        drag_y = "70"
        yctl("mouse-down", "20", drag_y)
        for drag_x in range(60, 300, 40):
            yctl("mouse-move", str(drag_x), drag_y)
            time.sleep(0.2)
        yctl("mouse-up", "300", drag_y)
        time.sleep(2.0)
        shot_postsel = os.path.join(tmp, "postsel.ppm")
        capture_screen(yctl, shot_postsel)
        for _ in range(20):
            if os.path.exists(shot_presel) and os.path.exists(shot_postsel):
                break
            time.sleep(0.25)
        presel = read_ppm(shot_presel)
        postsel = read_ppm(shot_postsel)
        if presel is None or postsel is None:
            dump("selection screenshots missing/invalid")
            return 1

        def light_pixels(frame):
            width, height, pixels = frame
            count = 0
            limit = width * height * 3
            for offset in range(0, min(len(pixels), limit) - 2, 3):
                if (pixels[offset] > 150 and pixels[offset + 1] > 150 and
                        pixels[offset + 2] > 150):
                    count += 1
            return count

        light_before = light_pixels(presel)
        light_after = light_pixels(postsel)
        if light_after - light_before < 300:
            dump(f"selection span did not render (light pixels {light_before} -> "
                 f"{light_after})")
            return 1

        def settled_capture(name):
            shot = os.path.join(tmp, name)
            capture_screen(yctl, shot)
            for _ in range(20):
                if os.path.exists(shot):
                    break
                time.sleep(0.25)
            return read_ppm(shot)

        # UNDERLINE decoration (review #17): blue-fg UNDERLINED spaces paint
        # only the underline strokes — plain blue spaces paint ~0 (pinned by
        # the reverse phase). Presence of a bluish stroke band = the
        # decoration path renders on the GPU.
        ymux("send", "-t", "smoke", "-l", "clear")
        ymux("send", "-t", "smoke", "Enter")
        time.sleep(1.0)
        underlined = " " * 30
        ymux("send", "-t", "smoke", "-l",
             f"printf '\\033[34;4m{underlined}\\033[0m\\n'")
        ymux("send", "-t", "smoke", "Enter")
        time.sleep(2.0)
        underline_frame = settled_capture("underline.ppm")
        if underline_frame is None:
            dump("underline screenshot missing/invalid")
            return 1
        blue_underline = hueish_pixels(underline_frame, 2)
        if blue_underline < 60:
            dump(f"underline decoration did not render ({blue_underline} bluish pixels)")
            return 1

        # UNDERLINE VARIANT delivery (review #19): the SGR 4:2 double form
        # must survive the pipeline end to end — its stroke band renders.
        # NOTE: stroke-count geometry (single vs double vs curly) is NOT
        # measurable on this rig: the X11-tile presentation quantizes to
        # whole cells and the WGPU screenshot texture is stale under it
        # (both characterized) — geometry distinctness is pinned at the
        # unit layer (scene-test grid attrs + the shader's per-style
        # branches). Here: presence proves the daemon did not drop or
        # downgrade the 4:2 form for a usstyle client.
        ymux("send", "-t", "smoke", "-l",
             f"printf '\\033[34;4:2m{underlined}\\033[0m\\n'")
        ymux("send", "-t", "smoke", "Enter")
        time.sleep(2.0)
        double_frame = settled_capture("underline-double.ppm")
        if double_frame is None:
            dump("double-underline screenshot missing/invalid")
            return 1
        blue_double_total = hueish_pixels(double_frame, 2)
        blue_double_added = blue_double_total - blue_underline
        if blue_double_added < int(blue_underline * 0.5):
            dump(f"double underline band did not render "
                 f"(single={blue_underline}, double-added={blue_double_added})")
            return 1

        # FAINT (SGR 2, review #19): dim red blocks — red pixels present but
        # BELOW the bright-red band, proving the dim path scales the fg
        # instead of dropping or full-brighting it. Tile quantization keeps
        # COLOR, so intensity IS measurable on the X11 capture.
        faint_blocks = "\\342\\226\\210" * 20
        ymux("send", "-t", "smoke", "-l",
             f"printf '\\033[2;31m{faint_blocks}\\033[0m\\n'")
        ymux("send", "-t", "smoke", "Enter")
        time.sleep(2.0)
        faint_frame = settled_capture("faint.ppm")
        if faint_frame is None:
            dump("faint screenshot missing/invalid")
            return 1

        def dim_reddish_pixels(frame):
            width, height, pixels = frame
            count = 0
            limit = width * height * 3
            for offset in range(0, min(len(pixels), limit) - 2, 3):
                red = pixels[offset]
                green = pixels[offset + 1]
                blue = pixels[offset + 2]
                if 40 < red < 110 and green < 45 and blue < 45:
                    count += 1
            return count

        faint_dim = dim_reddish_pixels(faint_frame)
        if faint_dim < 200:
            dump(f"faint blocks did not render dimmed ({faint_dim} dim-red pixels)")
            return 1

        # CURSOR variants (review #17): steady BLOCK (DECSCUSR 2) paints a
        # full light cell; steady BAR (DECSCUSR 6) paints strictly less;
        # hidden (\e[?25l) paints none. block > bar and block-vs-hidden
        # delta pin the cursor render + the shape pipeline.
        ymux("send", "-t", "smoke", "-l", "clear")
        ymux("send", "-t", "smoke", "Enter")
        time.sleep(1.0)
        # ONE command, sleeping between cursor states: the screen text is
        # FROZEN across the three captures (no echo / prompt growth), so
        # the only delta is the cursor cell itself.
        ymux("send", "-t", "smoke", "-l",
             "printf '\\033[2 q'; sleep 8; printf '\\033[6 q'; sleep 8; "
             "printf '\\033[?25l'; sleep 8; printf '\\033[?25h\\033[0 q'")
        ymux("send", "-t", "smoke", "Enter")
        # State windows (begin ~2s after Enter for shell latency): block
        # [2,10), bar [10,18), hidden [18,26). Captures anchor to the
        # MONOTONIC clock at each window's center — chained sleeps drift
        # with the capture round-trip and land on the boundaries.
        phase_zero = time.monotonic()

        def sleep_until(offset):
            remaining = phase_zero + offset - time.monotonic()
            if remaining > 0:
                time.sleep(remaining)

        sleep_until(5.0)
        block_frame = settled_capture("cursor-block.ppm")
        sleep_until(13.0)
        bar_frame = settled_capture("cursor-bar.ppm")
        sleep_until(21.0)
        hidden_frame = settled_capture("cursor-hidden.ppm")
        sleep_until(28.0)
        if block_frame is None or bar_frame is None or hidden_frame is None:
            dump("cursor screenshots missing/invalid")
            return 1
        light_block = light_pixels(block_frame)
        light_bar = light_pixels(bar_frame)
        light_hidden = light_pixels(hidden_frame)
        # The three frames differ ONLY in the cursor cell (plus the echoed
        # printf line, which is why the compare is monotonic not absolute).
        if light_block - light_hidden < 40:
            dump(f"cursor block vs hidden delta too small "
                 f"(block={light_block}, hidden={light_hidden})")
            return 1
        if light_bar >= light_block:
            dump(f"cursor bar not thinner than block "
                 f"(bar={light_bar}, block={light_block}) — DECSCUSR ignored")
            return 1

        # CLIPPING (review #17): with autowrap OFF, output at the TRUE last
        # column (CUP clamps \e[12;999H) must OVERWRITE in place — 30 blue
        # blocks collapse into ONE cell at the right edge, and nothing wraps
        # to the next row. (The attach negotiates the CLIENT grid's cols, so
        # a fixed column number cannot find the edge — the clamp can.)
        ymux("send", "-t", "smoke", "-l", "clear")
        ymux("send", "-t", "smoke", "Enter")
        time.sleep(1.0)

        def band_extent(frame, band, channel):
            width, height, pixels = frame
            left = width
            right = 0
            for y in band:
                for x in range(width):
                    offset = (y * width + x) * 3
                    others = [offset + other for other in (0, 1, 2) if other != channel]
                    if (pixels[offset + channel] > 100 and
                            pixels[others[0]] < 60 and pixels[others[1]] < 60):
                        left = min(left, x)
                        right = max(right, x)
            return left, right

        blocks30 = "\\342\\226\\210" * 30
        # Park at the clamped last column, step 10 left, write 30 blocks:
        # 11 cells fill to the edge, the remaining 19 OVERWRITE the last
        # cell in place — an 11-cell band ANCHORED at the right edge. A
        # wrap-off failure spills a left-anchored band onto the next row.
        clip_band = None
        clip_frame = None
        for clip_attempt in range(3):
            # A DIFFERENT row each attempt: identical reprints add no new
            # damage, so the characterized stale-tile drop would survive
            # them — fresh rows force fresh paints.
            clip_row = 12 + clip_attempt * 2
            # End with \n, NEVER a home-park: the pane shell (zsh) clears
            # BELOW the cursor when it redraws its prompt, so a prompt
            # parked at row 1 erases the printed rows.
            ymux("send", "-t", "smoke", "-l",
                 f"printf '\\033[?7l\\033[{clip_row};999H\\033[10D"
                 f"\\033[34m{blocks30}\\033[0m\\033[?7h\\n'")
            ymux("send", "-t", "smoke", "Enter")
            time.sleep(2.5)
            # Evidence from EITHER presentation surface: the X window
            # first, the WGPU target readback as the fallback (each can go
            # stale independently; ymux correctness is what is gated).
            shots = []
            frame_x = settled_capture(f"clip-x-{clip_attempt}.ppm")
            if frame_x is not None:
                shots.append(frame_x)
            wgpu_path = os.path.join(tmp, f"clip-wgpu-{clip_attempt}.ppm")
            yctl("screenshot", wgpu_path)
            for _ in range(10):
                if os.path.exists(wgpu_path):
                    break
                time.sleep(0.25)
            frame_wgpu = read_ppm(wgpu_path)
            if frame_wgpu is not None:
                shots.append(frame_wgpu)
            for frame in shots:
                row_min_clip, clip_bands = red_row_bands(frame, channel=2)
                # The clip band is the band at the RIGHT edge (echo/prompt
                # fringe bands live at the left/top).
                for band in clip_bands:
                    left, right = band_extent(frame, band, 2)
                    if right >= frame[0] * 0.80:
                        clip_band = band
                        clip_frame = frame
                        break
                if clip_band is not None:
                    break
            if clip_band is not None:
                break
        if clip_frame is None or clip_band is None:
            dump("right-edge clip band absent after 3 fresh-row attempts "
                 "(X window AND WGPU target)")
            return 1
        frame_width = clip_frame[0]
        clip_left, clip_right = band_extent(clip_frame, clip_band, 2)
        clip_width = clip_right - clip_left + 1
        # ONE cell at the right edge: narrow (a wrapped spill row would be a
        # left-anchored band and fail both checks).
        if clip_width > frame_width * 0.12:
            dump(f"clip run not confined to the edge cells "
                 f"({clip_width}px wide of {frame_width}) — overwrite failed")
            return 1
        if clip_right < frame_width * 0.80:
            dump(f"clip band not at the right edge (right={clip_right} of "
                 f"{frame_width}) — content wrapped instead of clipping")
            return 1

        print(f"OK: grid readback — reddish bg before={red_before} after={red_after}, "
              f"greenish fg glyphs={green_after}, reverse-video fill={green_reverse}, "
              f"wide placement {left_a}=={left_b}, combining {left_a2}=={left_b2}, "
              f"selection {light_before}->{light_after}, underline={blue_underline}, "
              f"cursor block/bar/hidden={light_block}/{light_bar}/{light_hidden}, "
              f"clip last-column {clip_width}px@{clip_right} ({after[0]}x{after[1]} frame)")
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
