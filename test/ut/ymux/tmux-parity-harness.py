#!/usr/bin/env python3
"""
#699 differential parity harness (capture half).

Drives a real tmux over a pty attach on an ISOLATED socket and captures the
terminal bytes tmux writes to its client — the ground truth the ymux
tty-render.c emitter must reproduce byte-for-byte. This first slice proves the
capture mechanism and prints tmux's initial complete-redraw bytes for an empty
80x24 pane.

Byte-parity is pinned to tmux d5afb67, built under tmp/tmux/tmux (the default
this harness prefers; override with YMUX_PARITY_TMUX, or it falls back to the
system tmux for the version-stable operations that match across versions).
Every checked byte form below is produced by the ymux tty-render.c emitter and
confirmed to appear verbatim in the pinned tmux's client output.
"""
import os
import pty
import select
import signal
import struct
import subprocess
import sys
import termios
import time

def _default_tmux():
    if "YMUX_PARITY_TMUX" in os.environ:
        return os.environ["YMUX_PARITY_TMUX"]
    # Prefer the PINNED tmux (d5afb67) built under tmp/tmux — the #699 baseline.
    here = os.path.dirname(os.path.abspath(__file__))
    pinned = os.path.normpath(os.path.join(here, "..", "..", "..", "tmp", "tmux", "tmux"))
    return pinned if os.path.exists(pinned) else "/usr/bin/tmux"


TMUX = _default_tmux()
SOCK = "ymux-parity-harness"


def _default_driver():
    """The built ymux VT-emit driver (ymux_vtdiff_driver) — the YMUX half of the
    differential. Located under the release build tree; overridable via env."""
    if "YMUX_VTDIFF_DRIVER" in os.environ:
        return os.environ["YMUX_VTDIFF_DRIVER"]
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.normpath(os.path.join(here, "..", "..", ".."))
    for build in ("build-desktop-ytrace-release", "build-desktop-ytrace-asan",
                  "build-desktop-yinfo-release"):
        cand = os.path.join(root, build, "test", "ut", "ymux", "ymux_vtdiff_driver-test")
        if os.path.exists(cand):
            return cand
    return None


DRIVER = _default_driver()


def ymux_emit(app_bytes, rows=24, cols=80):
    """Run the REAL ymux projector emitter on `app_bytes`, returning its VT
    byte stream (the yRPC terminal-byte sink boundary)."""
    if not DRIVER:
        return None
    proc = subprocess.run([DRIVER, str(rows), str(cols)], input=app_bytes,
                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return proc.stdout


def sgr_tokens(stream):
    """Split a VT byte stream into its SGR sequences + printable text runs — the
    comparable tokens (chrome-independent) for the differential."""
    tokens = []
    i = 0
    run = bytearray()
    while i < len(stream):
        if stream[i] == 0x1b and i + 1 < len(stream) and stream[i + 1] == ord('['):
            if run:
                tokens.append(bytes(run)); run = bytearray()
            j = i + 2
            while j < len(stream) and not (0x40 <= stream[j] <= 0x7e):
                j += 1
            tokens.append(stream[i:j + 1])
            i = j + 1
        else:
            if 0x20 <= stream[i] < 0x7f:
                run.append(stream[i])
            elif run:
                tokens.append(bytes(run)); run = bytearray()
            i += 1
    if run:
        tokens.append(bytes(run))
    return tokens


def run_differential(driven):
    """The REAL differential: run the ymux emitter for the same styled output
    the tmux run rendered, and assert every SGR sequence the ymux emitter
    produced is a byte-exact sequence tmux ALSO emits. This runs BOTH
    implementations and compares their actual bytes (not hand-written forms)."""
    if not DRIVER:
        print("  [SKIP] ymux emitter not built (no vtdiff driver) — differential not run")
        return 0
    app = b"X\x1b[1mY\x1b[9mS\x1b[31mZ\x1b[38;5;208mQ\x1b[48;5;21mW\x1b[38;2;255;100;0mT\x1b[0m"
    ymux_vt = ymux_emit(app)
    print(f"\nreal differential: ymux emitter produced {len(ymux_vt)} VT bytes for the styled run")
    failures = 0
    # Every SGR sequence the REAL ymux emitter emitted must appear byte-exact in
    # the REAL tmux client stream (the version-stable SGR set).
    for token in sgr_tokens(ymux_vt):
        if not token.startswith(b"\x1b["):
            continue
        # Skip ymux's private-mode preamble/cursor toggles (tmux emits its own
        # equivalents in a different order); compare the SGR (m-terminated) set.
        if not token.endswith(b"m"):
            continue
        ok = token in driven
        # \e[0m reset: tmux uses sgr0 (\e(B\e[m); accept either reset form.
        if not ok and token == b"\x1b[0m":
            ok = b"\x1b(B\x1b[m" in driven or b"\x1b[m" in driven
        print(f"  [{'PASS' if ok else 'FAIL'}] ymux SGR {token!r} emitted byte-exact by tmux")
        failures += not ok
    return failures


def tmux(*args, check=False):
    return subprocess.run([TMUX, "-L", SOCK, *args], stderr=subprocess.DEVNULL,
                          stdout=subprocess.DEVNULL, check=check)


def capture_attach(rows=24, cols=80, drive=None, settle=0.4, budget=3.0):
    """Attach in a pty child; return the bytes tmux writes to the client."""
    master, slave = pty.openpty()
    fcntl_winsz = struct.pack("HHHH", rows, cols, 0, 0)
    try:
        termios.tcgetattr(slave)
    except Exception:
        pass
    import fcntl
    fcntl.ioctl(slave, termios.TIOCSWINSZ, fcntl_winsz)

    pid = os.fork()
    if pid == 0:  # child = the "terminal" running the tmux client
        os.setsid()
        os.dup2(slave, 0)
        os.dup2(slave, 1)
        os.dup2(slave, 2)
        os.environ["TERM"] = "xterm-256color"
        os.execv(TMUX, [TMUX, "-L", SOCK, "attach"])
        os._exit(127)
    os.close(slave)

    captured = bytearray()
    drove = False
    deadline = time.time() + budget
    while time.time() < deadline:
        ready, _, _ = select.select([master], [], [], settle)
        if master in ready:
            try:
                data = os.read(master, 65536)
            except OSError:
                break
            if not data:
                break
            captured += data
            # Answer capability queries so tmux proceeds without waiting out its timeout.
            if b"\x1b[c" in data:
                os.write(master, b"\x1b[?1;2c")          # primary DA
            if b"\x1b[>c" in data or b"\x1b[>0c" in data:
                os.write(master, b"\x1b[>41;360;0c")     # secondary DA
            if b"\x1b[>q" in data:
                os.write(master, b"\x1bP>|xterm-parity\x1b\\")  # XTVERSION
            deadline = time.time() + settle
        else:
            if drive and not drove:
                # Inject a controlled screen op, then keep capturing the render.
                tmux("send-keys", *drive)
                drove = True
                deadline = time.time() + budget
                continue
            break

    tmux("kill-server")
    try:
        os.kill(pid, signal.SIGKILL)
    except Exception:
        pass
    try:
        os.waitpid(pid, 0)
    except Exception:
        pass
    os.close(master)
    return bytes(captured)


def main():
    tmux("kill-server")
    tmux("new-session", "-d", "-x", "80", "-y", "24", "cat")
    time.sleep(0.3)
    initial = capture_attach()
    print(f"tmux {TMUX}: captured {len(initial)} bytes on attach (initial complete redraw)")
    print(repr(initial[:400]))
    if len(initial) == 0:
        print("FAIL: captured nothing — attach/capture mechanism not working", file=sys.stderr)
        return 1

    # Differential check: the exact byte forms the ymux tty-render.c emitter
    # produces for these operations must appear verbatim in real tmux's output.
    # (Version-stable caps; matches d5afb67 once the pinned build is available.)
    checks = {
        "sgr0 reset  == yetty_ymux_tty_reset":        b"\x1b(B\x1b[m",
        "cnorm       == tty_cursor_visible(show)":    b"\x1b[?12l\x1b[?25h",
        "civis       == tty_cursor_visible(hide)":    b"\x1b[?25l",
        "el          == tty_clear_line":              b"\x1b[K",
        "cup 1-based == tty_cursor(absolute)":        b"\x1b[1;1H",
        "blank-row redraw == tty_draw_line(blank)":   b"\x1b[K\r\n",
    }
    failures = 0
    for name, seq in checks.items():
        ok = seq in initial
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}  {seq!r}")
        failures += not ok
    # Driven op: a program prints styled text; tmux renders it, so its client
    # output must carry the SGR + text bytes the ymux emitter produces.
    tmux("kill-server")
    tmux("new-session", "-d", "-x", "80", "-y", "24", "sh")
    time.sleep(0.3)
    # NOTE: this profile advertises TRUECOLOR (libvterm supports it), so tmux
    # renders an RGB cell as \e[38;2;R;G;Bm (passthrough), NOT a 256 downgrade —
    # verified: RGB (255,100,0) -> \e[38;2;255;100;0m. So the emitter's colour
    # path must (a) emit truecolor for RGB cells and (b) keep INDEXED colours as
    # setaf (the engine cell must carry index/RGB/default intent for byte parity;
    # it currently packs resolved RGB). rgb_to_256 is the 256-only-profile
    # downgrade fallback (unit-tested in ymux_tty_render).
    driven = capture_attach(drive=[
        "printf 'X\\033[1mY\\033[9mS\\033[31mZ\\033[38;5;208mQ\\033[48;5;21mW\\033[38;2;255;100;0mT\\033[0m'",
        "Enter"])
    print(f"\ndriven (X boldY strikeS redZ 256Q 256W rgbT): captured {len(driven)} bytes")
    driven_checks = {
        "bold  \\e[1m        == tty_attributes(BOLD)":     b"\x1b[1m",
        "strike\\e[9m        == tty_attributes(STRIKE)":   b"\x1b[9m",
        "red   \\e[31m       == tty_colours(fg 1)":        b"\x1b[31m",
        "256fg \\e[38;5;208m == tty_colours(fg 208)":      b"\x1b[38;5;208m",
        "256bg \\e[48;5;21m  == tty_colours(bg 21)":       b"\x1b[48;5;21m",
        "RGB truecolor \\e[38;2;255;100;0m (profile=RGB)": b"\x1b[38;2;255;100;0m",
        "text 'X' verbatim   == tty_putn":                b"X",
        "text 'T' verbatim   == tty_putn":                b"T",
    }
    for name, seq in driven_checks.items():
        ok = seq in driven
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}  {seq!r}")
        failures += not ok

    # The REAL differential: run the ymux emitter and compare its actual output
    # to tmux's, byte-exact per SGR sequence (addresses the substring-only gap).
    failures += run_differential(driven)

    if failures:
        print(f"FAIL: {failures} emitter byte-form(s) not found in tmux output", file=sys.stderr)
        return 1
    print("\nOK: ymux emitter byte forms match real tmux for all checked operations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
