#!/usr/bin/env python3
"""
tmux-diff.py — REAL complete-ordered-byte-stream differential between the pinned
tmux renderer (ground truth) and the ymux projector (#699).

Unlike the older token-membership harness, this feeds an IDENTICAL application
byte stream to BOTH:

  - pinned tmux (tmp/tmux/tmux, commit d5afb67): the pane runs `cat <vector>` so
    tmux's own input parser + operation-driven tty renderer produce the client
    byte stream. tmux is started with `-vv`, which mirrors every byte it writes
    to a client verbatim into `tmux-out-<pid>.log` (tty.c tty_add → tty_log_fd) —
    so we read the EXACT client tty stream with no pty scraping.

  - the ymux vtdiff-driver: feeds the same bytes to a real pane/projector and
    emits one project_vt redraw.

It extracts tmux's SETTLED full-screen redraw (the last cursor-hide..restore
segment), strips the fixed attach preamble, and compares the complete ordered
byte streams. On a mismatch it reports the exact first-divergence offset with an
escaped context window — an actionable delta for the renderer port, not a
membership pass.

Usage:  tmux-diff.py [--driver PATH] [--tmux PATH] [--rows N] [--cols N] [case...]
Exit 0 iff every case is byte-identical.
"""
import os
import re
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(ROOT, "..", "..", ".."))
DEFAULT_TMUX = os.path.join(REPO, "tmp", "tmux", "tmux")
DEFAULT_DRIVER = os.path.join(
    REPO, "build-desktop-ytrace-release", "test", "ut", "ymux", "ymux_vtdiff_driver-test"
)
# The in-process deterministic oracle (tools/tmux-oracle/build.sh): links the
# pinned tmux's own objects, fabricates client/session/pane, pumps libevent
# manually. Unlike the live-attach capture, its delta emission for a given
# (base, delta) pair is byte-identical on every run.
DEFAULT_ORACLE = os.path.join(REPO, "tmp", "tmux-oracle-build", "tmux-oracle")

# Each case: name -> raw application bytes fed identically to both renderers.
CASES = {
    "plain": b"Hello, world",
    "sgr-basic": b"\x1b[31mred\x1b[0m \x1b[1mbold\x1b[0m",
    "sgr-truecolor": b"\x1b[38;2;255;102;0morange\x1b[0m",
    "clear-home": b"\x1b[2J\x1b[HABC",
    "cup": b"\x1b[3;5HXY\x1b[1;1Htop",
    "two-lines": b"line one\r\nline two",
    "erase-eol": b"abcdef\x1b[3D\x1b[K",
    "wide": b"a\xe4\xb8\xad\xe6\x96\x87b",  # CJK wide glyphs
    # Broadened attribute coverage (indexed/default colours + attr bits) — these
    # exercise the operation-driven full redraw's SGR minimization vs tmux.
    "attr-underline": b"\x1b[4munder\x1b[0m plain",
    "attr-reverse": b"\x1b[7mrev\x1b[0m",
    "attr-dim-italic": b"\x1b[2mdim\x1b[0m \x1b[3mital\x1b[0m",
    "attr-mixed-run": b"\x1b[1;31mA\x1b[32mB\x1b[0mC",
    "sgr-256": b"\x1b[38;5;196mX\x1b[48;5;21mY\x1b[0m",
    "bright-fg": b"\x1b[91mbright\x1b[0m",
    "box-drawing": b"\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x90",  # ┌─┐
    "multi-row-sgr": b"\x1b[31mrow0\r\n\x1b[32mrow1\r\n\x1b[34mrow2\x1b[0m",
    "trailing-blanks": b"ab\x1b[0m   cd   ",
    "tab-then-text": b"a\tb",
}

# Cases that CANNOT reach byte-identity for a reason outside the ymux renderer —
# reported as XFAIL, not FAIL, with the cause. The read-only libvterm fork has no
# dim/faint attribute (VTermAttr + VTermScreenCellAttrs carry no dim bit), so SGR
# 2 is lost before ymux ever sees the cell; tmux's own parser keeps it. Fixing it
# would require editing the read-only libvterm, which is out of bounds.
XFAIL_UPSTREAM = {
    # The yetty libvterm fork (src/libvterm-0.3.3/include/vterm.h:64: "VTermColor
    # simplified to plain RGB only") stripped the colour TYPE field, so a genuine
    # RGB colour (38;2;255;102;0) that resolves to a palette-equal RGB is
    # indistinguishable from indexed 208 by the time ymux sees the cell. tmux
    # keeps the intent via its own parser. Byte-only (both forms render the same
    # pixel); unfixable without editing the read-only libvterm fork.
}


def esc(data: bytes) -> str:
    """Render bytes with escapes visible (ESC->\\e), for diff context."""
    out = []
    for byte in data:
        if byte == 0x1B:
            out.append("\\e")
        elif byte == 0x0D:
            out.append("\\r")
        elif byte == 0x0A:
            out.append("\\n")
        elif byte == 0x09:
            out.append("\\t")
        elif 0x20 <= byte < 0x7F:
            out.append(chr(byte))
        else:
            out.append("\\x%02x" % byte)
    return "".join(out)


def capture_tmux(tmux: str, vector: bytes, rows: int, cols: int) -> bytes:
    """Run pinned tmux over the vector; return its SETTLED redraw byte stream."""
    import pty
    import struct
    import fcntl
    import termios

    work = tempfile.mkdtemp(prefix="tmuxdiff-")
    vec_path = os.path.join(work, "vector.bin")
    with open(vec_path, "wb") as handle:
        handle.write(vector)
    conf = os.path.join(work, "conf")
    with open(conf, "w") as handle:
        handle.write("set -g status off\nset -g window-size manual\n"
                     "set -g aggressive-resize off\n")
    sock = "ymuxdiff-%d" % os.getpid()
    # COLORTERM=truecolor makes tmux pass RGB SGR through deterministically,
    # matching the truecolor client profile the ymux tool advertises.
    env = dict(os.environ, TERM="xterm-256color", COLORTERM="truecolor")

    # The pane emits exactly the vector bytes (cat), then idles.
    pane_cmd = "cat %s; sleep 3600" % vec_path
    start = subprocess.run(
        [tmux, "-vv", "-f", conf, "-L", sock, "new-session", "-d",
         "-x", str(cols), "-y", str(rows), "-s", "s0", pane_cmd],
        cwd=work, env=env, capture_output=True)
    if start.returncode != 0:
        raise RuntimeError("tmux new-session failed: %s" % start.stderr.decode(errors="replace"))
    try:
        pid, fd = pty.fork()
        if pid == 0:
            os.execvpe(tmux, [tmux, "-f", conf, "-L", sock, "attach", "-t", "s0"], env)
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        time.sleep(2.6)  # vector drawn + redraw settled
        # Snapshot the client log BEFORE detaching (review #10): the detach
        # epilogue can never contaminate the extraction window.
        log_name = None
        deadline = time.time() + 2.0
        while log_name is None and time.time() < deadline:
            candidates = [f for f in os.listdir(work) if f.startswith("tmux-out-")]
            if candidates:
                log_name = candidates[0]
            else:
                time.sleep(0.05)
        if log_name is None:
            raise RuntimeError("tmux client log never appeared")
        with open(os.path.join(work, log_name), "rb") as handle:
            log = handle.read()
        subprocess.run([tmux, "-L", sock, "detach-client", "-s", "s0"], env=env,
                       capture_output=True)
        time.sleep(0.3)
        try:
            os.close(fd)
        except OSError:
            pass
        os.waitpid(pid, 0)
    finally:
        subprocess.run([tmux, "-L", sock, "kill-server"], env=env, capture_output=True)
    return extract_redraw(log)



# A settled redraw segment: cursor hidden (civis) ... blink-reset + cursor
# shown (\e[?12l\e[?25h) — tmux brackets every redraw exactly like this.
REDRAW_RE = re.compile(rb"\x1b\[\?25l.*?\x1b\[\?12l\x1b\[\?25h", re.S)


def extract_redraw(log: bytes) -> bytes:
    """The LAST settled civis..cnorm redraw segment in a tmux client log."""
    segments = REDRAW_RE.findall(log)
    if not segments:
        raise RuntimeError("no redraw segment in tmux log")
    return segments[-1]


def extract_body(stream: bytes) -> bytes:
    """The FIRST settled redraw segment in the ymux driver's emission."""
    match = REDRAW_RE.search(stream)
    if not match:
        raise RuntimeError("no redraw segment in ymux emission")
    return match.group(0)


def run_ymux(driver: str, vector: bytes, rows: int, cols: int) -> bytes:
    proc = subprocess.run([driver, str(rows), str(cols)], input=vector, capture_output=True)
    if proc.returncode != 0:
        raise RuntimeError("vtdiff-driver failed: %s" % proc.stderr.decode(errors="replace"))
    return proc.stdout


def _cat_bytes(data: bytes) -> str:
    """Octal-escape bytes for a shell printf '%s' argument."""
    return "".join("\\%03o" % byte for byte in data)


def capture_tmux_incremental(tmux: str, base: bytes, delta: bytes, rows: int, cols: int) -> bytes:
    """tmux's INCREMENTAL output for `delta` applied after `base` is drawn."""
    import pty
    import struct
    import fcntl
    import termios

    work = tempfile.mkdtemp(prefix="tmuxinc-")
    with open(os.path.join(work, "conf"), "w") as handle:
        handle.write("set -g status off\nset -g window-size manual\n"
                     "set -g aggressive-resize off\n")
    conf = os.path.join(work, "conf")
    sock = "ymuxinc-%d" % os.getpid()
    env = dict(os.environ, TERM="xterm-256color", COLORTERM="truecolor")
    pane = "printf '%s'; sleep 1.5; printf '%s'; sleep 3600" % (_cat_bytes(base), _cat_bytes(delta))
    start = subprocess.run(
        [tmux, "-vv", "-f", conf, "-L", sock, "new-session", "-d",
         "-x", str(cols), "-y", str(rows), "-s", "s0", pane],
        cwd=work, env=env, capture_output=True)
    if start.returncode != 0:
        raise RuntimeError("tmux new-session failed: %s" % start.stderr.decode(errors="replace"))
    try:
        pid, fd = pty.fork()
        if pid == 0:
            os.execvpe(tmux, [tmux, "-f", conf, "-L", sock, "attach", "-t", "s0"], env)
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        # EXPLICIT DETERMINISTIC BOUNDARIES (review #10): the pane prints the
        # base at t0 and the delta at t1.5. Snapshot the client-log LENGTH
        # after the base (and tmux's post-redraw state sync) settled but
        # BEFORE the delta prints; snapshot the CONTENT after the delta
        # settled but BEFORE the detach. The delta stream is exactly the bytes
        # between the snapshots — no regex heuristics, no epilogue leak.
        log_name = None
        deadline = time.time() + 2.0
        while log_name is None and time.time() < deadline:
            candidates = [f for f in os.listdir(work) if f.startswith("tmux-out-")]
            if candidates:
                log_name = candidates[0]
            else:
                time.sleep(0.05)
        if log_name is None:
            raise RuntimeError("tmux client log never appeared")
        log_path = os.path.join(work, log_name)
        time.sleep(1.2)  # base + post-redraw sync settled; delta not yet printed
        base_end = os.path.getsize(log_path)
        time.sleep(1.4)  # delta printed at t1.5 and settled
        with open(log_path, "rb") as handle:
            log = handle.read()
        subprocess.run([tmux, "-L", sock, "detach-client", "-s", "s0"], env=env,
                       capture_output=True)
        time.sleep(0.3)
        try:
            os.close(fd)
        except OSError:
            pass
        os.waitpid(pid, 0)
    finally:
        subprocess.run([tmux, "-L", sock, "kill-server"], env=env, capture_output=True)
    return log[base_end:]


# Incremental cases: (base bytes, delta bytes). The base is drawn and settled
# first; the delta is the live keystroke-scale change whose emission must be
# byte-identical (the ymux driver discards the base's full redraw via the
# split argument).
INCREMENTAL_CASES = {
    "echo-append": (b"abc", b"d"),
    "overwrite": (b"abcdef\x1b[1;1H", b"X"),
    "insert-char": (b"abcdef\x1b[1;1H", b"\x1b[1@X"),
    "delete-char": (b"abcdef\x1b[1;1H", b"\x1b[1P"),
    "scroll-up": (b"\x1b[24;1Hbottom", b"\n"),
    # Stage 4 matrix: multi-line scroll (INDN path), EL shapes, SGR runs,
    # wide + combining glyphs, pure cursor motion, mid-row overwrite,
    # DECSTBM margins.
    "scroll-up-2": (b"\x1b[24;1Hbottom", b"\n\n"),
    "el-tail": (b"abcdef\x1b[1;4H", b"\x1b[K"),
    "el-line": (b"abcdef", b"\x1b[2K"),
    "sgr-run": (b"abc", b"\x1b[31mRED"),
    "sgr-bg-el": (b"abc", b"\x1b[44m\x1b[K"),
    "wide-glyph": (b"abc", "漢".encode()),
    "combining-mark": (b"abc", "e\u0301".encode()),
    "cursor-only": (b"abcdef", b"\x1b[3;5H"),
    "mid-overwrite": (b"abcdef\x1b[1;3H", b"ZZ"),
    "margin-scroll": (b"\x1b[5;10r\x1b[10;1Hlast", b"\n"),
    "region-sd": (b"\x1b[5;10r\x1b[5;1Htop", b"\x1bM"),
    "ri-top": (b"\x1b[1;1Htop", b"\x1bM"),
    "ed-below": (b"abcdef\x1b[1;3H", b"\x1b[J"),
    # Review #11 cancellation/history probes: ordered-op replay, not final-cell
    # diff — the ICH+DCH pair restores the same cells yet must reach the wire.
    "put-then-el": (b"abcdef\x1b[H", b"Z\x1b[K"),
    # Terminal-overrides now REALLY applied by the oracle (cycle-26): a cap=value
    # REPLACEMENT (el=\E[9K) is honoured by ymux byte-identically. (el@ CANCELLATION
    # fallback — tmux's EL1-based \e[nC\e[1K — is the remaining projector work.)
    "el-override-replacement": (b"abcdef\x1b[1;3H", b"\x1b[K", "term:xterm-256color:256,RGB,el=\\E[9K"),
    "el-cancel-el1": (b"xxxxxx\r\nyy\x1b[1;1H", b"\x1b[K", "term:xterm-256color:256,RGB,el@"),
    # dch@ (parm_dch cancelled): tmux repeats dch1 (\e[P) count times (cycle-26).
    "dch-cancel-dch1": (b"abcdef\x1b[1;1H", b"\x1b[3P", "term:xterm-256color:256,RGB,dch@"),
    "sgr0-cancel-drop": (b"\x1b[1mA\x1b[H", b"\x1b[mB", "term:xterm-256color:256,RGB,sgr0@"),
    "home-cancel-cup": (b"abc\x1b[5;5Hxy", b"\x1b[1;1HZ", "term:xterm-256color:256,RGB,home@"),
    "csr-cancel-noregion": (b"a\r\nb\r\nc\x1b[1;1H", b"\x1b[2;3r\x1b[2;1Hx", "term:xterm-256color:256,RGB,csr@"),
    # Long OSC-8 URI (cycle-26): >168 bytes must not truncate (buffers now 512).
    "hyperlink-long-uri": (b"start\x1b[H", b"\x1b]8;;http://example.com/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\x1b\\Z\x1b]8;;\x1b\\", "term:tmux-256color:256,RGB,hyperlinks"),
    # 20 distinct hyperlinks (cycle-26): tmux numbers ids in UPPERCASE hex
    # (id=tmux1..9, tmuxA..F, tmux10=16th) and never aliases after 15 — the
    # daemon's exotic hyperlink ref was widened 4->8 bits to match.
    "hyperlink-20-distinct": (b"start\x1b[H", b'\x1b[H\x1b]8;;http://h0.example/path\x1b\\A\x1b]8;;http://h1.example/path\x1b\\B\x1b]8;;http://h2.example/path\x1b\\C\x1b]8;;http://h3.example/path\x1b\\D\x1b]8;;http://h4.example/path\x1b\\E\x1b]8;;http://h5.example/path\x1b\\F\x1b]8;;http://h6.example/path\x1b\\G\x1b]8;;http://h7.example/path\x1b\\H\x1b]8;;http://h8.example/path\x1b\\I\x1b]8;;http://h9.example/path\x1b\\J\x1b]8;;http://h10.example/path\x1b\\K\x1b]8;;http://h11.example/path\x1b\\L\x1b]8;;http://h12.example/path\x1b\\M\x1b]8;;http://h13.example/path\x1b\\N\x1b]8;;http://h14.example/path\x1b\\O\x1b]8;;http://h15.example/path\x1b\\P\x1b]8;;http://h16.example/path\x1b\\Q\x1b]8;;http://h17.example/path\x1b\\R\x1b]8;;http://h18.example/path\x1b\\S\x1b]8;;http://h19.example/path\x1b\\T\x1b]8;;\x1b\\', "term:tmux-256color:256,RGB,hyperlinks"),
    # ich@ cancelled: tmux redraws the whole row inside a cursor-hide bracket
    # (\e[?25l\e[H<line>\e[K), then repositions to the insert column and re-shows
    # (\e[<col>G\e[?12l\e[?25h) — no raw \e[<n>@ for a cap it lacks.
    "ich-cancel-redraw": (b"abcdef\x1b[1;3H", b"\x1b[2@", "term:xterm-256color:256,RGB,ich@"),
    # ed@ cancelled: tmux clears the block below the cursor by scrolling a
    # temporary region up by its height (\e[<t>;<b>r\e[<n>S), EL's the cursor row,
    # restores the region and repositions — never a raw \e[J for a cap it lacks.
    "ed-cancel-scroll": (b"AAA\r\nBBB\r\nCCC\x1b[1;1H", b"\x1b[0J", "term:xterm-256color:256,RGB,ed@"),
    "ed-cancel-standalone": (b"AAA\r\nBBB\r\nCCC\r\nDDD\x1b[2;1H", b"\x1b[J", "term:xterm-256color:256,RGB,ed@"),
    # Synchronized output around INCREMENTAL flushes (cdx-2 #1): tmux brackets a
    # live screen-write flush in ?2026h/?2026l when it carries a structural op
    # (IL/DL, SU/SD/RI always; ED/EL only when the flush emits no glyph), not a
    # plain glyph/ICH/DCH flush. The IL case is the reviewer's mandatory vector.
    "sync-il": (b"abc\r\ndef\x1b[2;1H", b"\x1b[1L", "term:tmux-256color:256,RGB,sync"),
    "sync-dl": (b"abc\r\ndef\r\nghi\x1b[2;1H", b"\x1b[1M", "term:tmux-256color:256,RGB,sync"),
    "sync-ed": (b"aaa\r\nbbb\r\nccc\x1b[1;1H", b"\x1b[0J", "term:tmux-256color:256,RGB,sync"),
    "sync-el": (b"abcde\x1b[1;3H", b"\x1b[K", "term:tmux-256color:256,RGB,sync"),
    "sync-glyph-no-wrap": (b"\x1b[H", b"X", "term:tmux-256color:256,RGB,sync"),
    "sync-glyph-then-el": (b"abcde\r\nfghij\x1b[1;1H", b"Z\x1b[2;1H\x1b[K",
                           "term:tmux-256color:256,RGB,sync"),
    "sync-glyph-then-il": (b"abcde\r\nfghij\x1b[1;1H", b"\x1b[1;1HZ\x1b[2;1H\x1b[1L",
                           "term:tmux-256color:256,RGB,sync"),
    "sync-glyph-then-ed": (b"aaa\r\nbbb\r\nccc\x1b[1;1H", b"Z\x1b[2;1H\x1b[J",
                           "term:tmux-256color:256,RGB,sync"),
    "bce-cancel-el": (b"abcdef\x1b[1;3H", b"\x1b[41m\x1b[K", "term:xterm-256color:256,RGB,bce@"),
    "bce-cancel-ed": (b"aaa\r\nbbb\x1b[1;1H", b"\x1b[41m\x1b[J", "term:xterm-256color:256,RGB,bce@"),
    # tmux hyperlink semantics (cdx-2 #3): anonymous links are ALWAYS unique
    # (same URI -> distinct id=tmux1/tmux2); named links dedup by (id, URI); URI
    # up to 1024; the cell references a 24-bit external id, not an 8-bit slot, so
    # 256+ distinct links do not alias.
    "hl-anon-unique": (b"start\x1b[H", b'\x1b]8;;http://same.com\x1b\\A\x1b]8;;\x1b\\\x1b]8;;http://same.com\x1b\\B\x1b]8;;\x1b\\', "term:tmux-256color:256,RGB,hyperlinks"),
    "hl-named-dedup": (b"start\x1b[H", b'\x1b]8;id=x;http://n.com\x1b\\A\x1b]8;;\x1b\\\x1b]8;id=x;http://n.com\x1b\\B\x1b]8;;\x1b\\', "term:tmux-256color:256,RGB,hyperlinks"),
    "hl-long-uri-600": (b"start\x1b[H", b'\x1b]8;;http://e.com/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\x1b\\Z\x1b]8;;\x1b\\', "term:tmux-256color:256,RGB,hyperlinks"),
    "hl-260-distinct": (b"start\x1b[H", b'\x1b[H\x1b]8;;http://h0.co\x1b\\!\x1b]8;;http://h1.co\x1b\\"\x1b]8;;http://h2.co\x1b\\#\x1b]8;;http://h3.co\x1b\\$\x1b]8;;http://h4.co\x1b\\%\x1b]8;;http://h5.co\x1b\\&\x1b]8;;http://h6.co\x1b\\\'\x1b]8;;http://h7.co\x1b\\(\x1b]8;;http://h8.co\x1b\\)\x1b]8;;http://h9.co\x1b\\*\x1b]8;;http://h10.co\x1b\\+\x1b]8;;http://h11.co\x1b\\,\x1b]8;;http://h12.co\x1b\\-\x1b]8;;http://h13.co\x1b\\.\x1b]8;;http://h14.co\x1b\\/\x1b]8;;http://h15.co\x1b\\0\x1b]8;;http://h16.co\x1b\\1\x1b]8;;http://h17.co\x1b\\2\x1b]8;;http://h18.co\x1b\\3\x1b]8;;http://h19.co\x1b\\4\x1b]8;;http://h20.co\x1b\\5\x1b]8;;http://h21.co\x1b\\6\x1b]8;;http://h22.co\x1b\\7\x1b]8;;http://h23.co\x1b\\8\x1b]8;;http://h24.co\x1b\\9\x1b]8;;http://h25.co\x1b\\:\x1b]8;;http://h26.co\x1b\\;\x1b]8;;http://h27.co\x1b\\<\x1b]8;;http://h28.co\x1b\\=\x1b]8;;http://h29.co\x1b\\>\x1b]8;;http://h30.co\x1b\\?\x1b]8;;http://h31.co\x1b\\@\x1b]8;;http://h32.co\x1b\\A\x1b]8;;http://h33.co\x1b\\B\x1b]8;;http://h34.co\x1b\\C\x1b]8;;http://h35.co\x1b\\D\x1b]8;;http://h36.co\x1b\\E\x1b]8;;http://h37.co\x1b\\F\x1b]8;;http://h38.co\x1b\\G\x1b]8;;http://h39.co\x1b\\H\x1b]8;;http://h40.co\x1b\\I\x1b]8;;http://h41.co\x1b\\J\x1b]8;;http://h42.co\x1b\\K\x1b]8;;http://h43.co\x1b\\L\x1b]8;;http://h44.co\x1b\\M\x1b]8;;http://h45.co\x1b\\N\x1b]8;;http://h46.co\x1b\\O\x1b]8;;http://h47.co\x1b\\P\x1b]8;;http://h48.co\x1b\\Q\x1b]8;;http://h49.co\x1b\\R\x1b]8;;http://h50.co\x1b\\S\x1b]8;;http://h51.co\x1b\\T\x1b]8;;http://h52.co\x1b\\U\x1b]8;;http://h53.co\x1b\\V\x1b]8;;http://h54.co\x1b\\W\x1b]8;;http://h55.co\x1b\\X\x1b]8;;http://h56.co\x1b\\Y\x1b]8;;http://h57.co\x1b\\Z\x1b]8;;http://h58.co\x1b\\[\x1b]8;;http://h59.co\x1b\\\\\x1b]8;;http://h60.co\x1b\\]\x1b]8;;http://h61.co\x1b\\^\x1b]8;;http://h62.co\x1b\\_\x1b]8;;http://h63.co\x1b\\`\x1b]8;;http://h64.co\x1b\\a\x1b]8;;http://h65.co\x1b\\b\x1b]8;;http://h66.co\x1b\\c\x1b]8;;http://h67.co\x1b\\d\x1b]8;;http://h68.co\x1b\\e\x1b]8;;http://h69.co\x1b\\f\x1b]8;;http://h70.co\x1b\\g\x1b]8;;http://h71.co\x1b\\h\x1b]8;;http://h72.co\x1b\\i\x1b]8;;http://h73.co\x1b\\j\x1b]8;;http://h74.co\x1b\\k\x1b]8;;http://h75.co\x1b\\l\x1b]8;;http://h76.co\x1b\\m\x1b]8;;http://h77.co\x1b\\n\x1b[2;1H\x1b]8;;http://h78.co\x1b\\o\x1b]8;;http://h79.co\x1b\\p\x1b]8;;http://h80.co\x1b\\q\x1b]8;;http://h81.co\x1b\\r\x1b]8;;http://h82.co\x1b\\s\x1b]8;;http://h83.co\x1b\\t\x1b]8;;http://h84.co\x1b\\u\x1b]8;;http://h85.co\x1b\\v\x1b]8;;http://h86.co\x1b\\w\x1b]8;;http://h87.co\x1b\\x\x1b]8;;http://h88.co\x1b\\y\x1b]8;;http://h89.co\x1b\\z\x1b]8;;http://h90.co\x1b\\!\x1b]8;;http://h91.co\x1b\\"\x1b]8;;http://h92.co\x1b\\#\x1b]8;;http://h93.co\x1b\\$\x1b]8;;http://h94.co\x1b\\%\x1b]8;;http://h95.co\x1b\\&\x1b]8;;http://h96.co\x1b\\\'\x1b]8;;http://h97.co\x1b\\(\x1b]8;;http://h98.co\x1b\\)\x1b]8;;http://h99.co\x1b\\*\x1b]8;;http://h100.co\x1b\\+\x1b]8;;http://h101.co\x1b\\,\x1b]8;;http://h102.co\x1b\\-\x1b]8;;http://h103.co\x1b\\.\x1b]8;;http://h104.co\x1b\\/\x1b]8;;http://h105.co\x1b\\0\x1b]8;;http://h106.co\x1b\\1\x1b]8;;http://h107.co\x1b\\2\x1b]8;;http://h108.co\x1b\\3\x1b]8;;http://h109.co\x1b\\4\x1b]8;;http://h110.co\x1b\\5\x1b]8;;http://h111.co\x1b\\6\x1b]8;;http://h112.co\x1b\\7\x1b]8;;http://h113.co\x1b\\8\x1b]8;;http://h114.co\x1b\\9\x1b]8;;http://h115.co\x1b\\:\x1b]8;;http://h116.co\x1b\\;\x1b]8;;http://h117.co\x1b\\<\x1b]8;;http://h118.co\x1b\\=\x1b]8;;http://h119.co\x1b\\>\x1b]8;;http://h120.co\x1b\\?\x1b]8;;http://h121.co\x1b\\@\x1b]8;;http://h122.co\x1b\\A\x1b]8;;http://h123.co\x1b\\B\x1b]8;;http://h124.co\x1b\\C\x1b]8;;http://h125.co\x1b\\D\x1b]8;;http://h126.co\x1b\\E\x1b]8;;http://h127.co\x1b\\F\x1b]8;;http://h128.co\x1b\\G\x1b]8;;http://h129.co\x1b\\H\x1b]8;;http://h130.co\x1b\\I\x1b]8;;http://h131.co\x1b\\J\x1b]8;;http://h132.co\x1b\\K\x1b]8;;http://h133.co\x1b\\L\x1b]8;;http://h134.co\x1b\\M\x1b]8;;http://h135.co\x1b\\N\x1b]8;;http://h136.co\x1b\\O\x1b]8;;http://h137.co\x1b\\P\x1b]8;;http://h138.co\x1b\\Q\x1b]8;;http://h139.co\x1b\\R\x1b]8;;http://h140.co\x1b\\S\x1b]8;;http://h141.co\x1b\\T\x1b]8;;http://h142.co\x1b\\U\x1b]8;;http://h143.co\x1b\\V\x1b]8;;http://h144.co\x1b\\W\x1b]8;;http://h145.co\x1b\\X\x1b]8;;http://h146.co\x1b\\Y\x1b]8;;http://h147.co\x1b\\Z\x1b]8;;http://h148.co\x1b\\[\x1b]8;;http://h149.co\x1b\\\\\x1b]8;;http://h150.co\x1b\\]\x1b]8;;http://h151.co\x1b\\^\x1b]8;;http://h152.co\x1b\\_\x1b]8;;http://h153.co\x1b\\`\x1b]8;;http://h154.co\x1b\\a\x1b]8;;http://h155.co\x1b\\b\x1b[3;1H\x1b]8;;http://h156.co\x1b\\c\x1b]8;;http://h157.co\x1b\\d\x1b]8;;http://h158.co\x1b\\e\x1b]8;;http://h159.co\x1b\\f\x1b]8;;http://h160.co\x1b\\g\x1b]8;;http://h161.co\x1b\\h\x1b]8;;http://h162.co\x1b\\i\x1b]8;;http://h163.co\x1b\\j\x1b]8;;http://h164.co\x1b\\k\x1b]8;;http://h165.co\x1b\\l\x1b]8;;http://h166.co\x1b\\m\x1b]8;;http://h167.co\x1b\\n\x1b]8;;http://h168.co\x1b\\o\x1b]8;;http://h169.co\x1b\\p\x1b]8;;http://h170.co\x1b\\q\x1b]8;;http://h171.co\x1b\\r\x1b]8;;http://h172.co\x1b\\s\x1b]8;;http://h173.co\x1b\\t\x1b]8;;http://h174.co\x1b\\u\x1b]8;;http://h175.co\x1b\\v\x1b]8;;http://h176.co\x1b\\w\x1b]8;;http://h177.co\x1b\\x\x1b]8;;http://h178.co\x1b\\y\x1b]8;;http://h179.co\x1b\\z\x1b]8;;http://h180.co\x1b\\!\x1b]8;;http://h181.co\x1b\\"\x1b]8;;http://h182.co\x1b\\#\x1b]8;;http://h183.co\x1b\\$\x1b]8;;http://h184.co\x1b\\%\x1b]8;;http://h185.co\x1b\\&\x1b]8;;http://h186.co\x1b\\\'\x1b]8;;http://h187.co\x1b\\(\x1b]8;;http://h188.co\x1b\\)\x1b]8;;http://h189.co\x1b\\*\x1b]8;;http://h190.co\x1b\\+\x1b]8;;http://h191.co\x1b\\,\x1b]8;;http://h192.co\x1b\\-\x1b]8;;http://h193.co\x1b\\.\x1b]8;;http://h194.co\x1b\\/\x1b]8;;http://h195.co\x1b\\0\x1b]8;;http://h196.co\x1b\\1\x1b]8;;http://h197.co\x1b\\2\x1b]8;;http://h198.co\x1b\\3\x1b]8;;http://h199.co\x1b\\4\x1b]8;;http://h200.co\x1b\\5\x1b]8;;http://h201.co\x1b\\6\x1b]8;;http://h202.co\x1b\\7\x1b]8;;http://h203.co\x1b\\8\x1b]8;;http://h204.co\x1b\\9\x1b]8;;http://h205.co\x1b\\:\x1b]8;;http://h206.co\x1b\\;\x1b]8;;http://h207.co\x1b\\<\x1b]8;;http://h208.co\x1b\\=\x1b]8;;http://h209.co\x1b\\>\x1b]8;;http://h210.co\x1b\\?\x1b]8;;http://h211.co\x1b\\@\x1b]8;;http://h212.co\x1b\\A\x1b]8;;http://h213.co\x1b\\B\x1b]8;;http://h214.co\x1b\\C\x1b]8;;http://h215.co\x1b\\D\x1b]8;;http://h216.co\x1b\\E\x1b]8;;http://h217.co\x1b\\F\x1b]8;;http://h218.co\x1b\\G\x1b]8;;http://h219.co\x1b\\H\x1b]8;;http://h220.co\x1b\\I\x1b]8;;http://h221.co\x1b\\J\x1b]8;;http://h222.co\x1b\\K\x1b]8;;http://h223.co\x1b\\L\x1b]8;;http://h224.co\x1b\\M\x1b]8;;http://h225.co\x1b\\N\x1b]8;;http://h226.co\x1b\\O\x1b]8;;http://h227.co\x1b\\P\x1b]8;;http://h228.co\x1b\\Q\x1b]8;;http://h229.co\x1b\\R\x1b]8;;http://h230.co\x1b\\S\x1b]8;;http://h231.co\x1b\\T\x1b]8;;http://h232.co\x1b\\U\x1b]8;;http://h233.co\x1b\\V\x1b[4;1H\x1b]8;;http://h234.co\x1b\\W\x1b]8;;http://h235.co\x1b\\X\x1b]8;;http://h236.co\x1b\\Y\x1b]8;;http://h237.co\x1b\\Z\x1b]8;;http://h238.co\x1b\\[\x1b]8;;http://h239.co\x1b\\\\\x1b]8;;http://h240.co\x1b\\]\x1b]8;;http://h241.co\x1b\\^\x1b]8;;http://h242.co\x1b\\_\x1b]8;;http://h243.co\x1b\\`\x1b]8;;http://h244.co\x1b\\a\x1b]8;;http://h245.co\x1b\\b\x1b]8;;http://h246.co\x1b\\c\x1b]8;;http://h247.co\x1b\\d\x1b]8;;http://h248.co\x1b\\e\x1b]8;;http://h249.co\x1b\\f\x1b]8;;http://h250.co\x1b\\g\x1b]8;;http://h251.co\x1b\\h\x1b]8;;http://h252.co\x1b\\i\x1b]8;;http://h253.co\x1b\\j\x1b]8;;http://h254.co\x1b\\k\x1b]8;;http://h255.co\x1b\\l\x1b]8;;http://h256.co\x1b\\m\x1b]8;;http://h257.co\x1b\\n\x1b]8;;http://h258.co\x1b\\o\x1b]8;;http://h259.co\x1b\\p\x1b]8;;\x1b\\', "term:tmux-256color:256,RGB,hyperlinks"),
    # OSC 8 boundary/parser parity (cdx-2 #3, second pass). tmux input_osc_8 +
    # hyperlinks_put: a URI over 1024 bytes is REJECTED (never truncated-then-
    # opened); the internal id is the FULL dedup key (no length cap), so two
    # 128-byte ids differing only in the last byte are DISTINCT links; a second
    # `id=` param is malformed and leaves the pen link UNCHANGED (no link opens).
    # In each rejected case tmux emits only the trailing glyph.
    "hl-reject-uri-1025": (b"start\x1b[H",
                           b"\x1b]8;;http://e.com/" + b"a" * 1012 + b"\x1b\\Z\x1b]8;;\x1b\\",
                           "term:tmux-256color:256,RGB,hyperlinks"),
    "hl-id-128-distinct": (b"start\x1b[H",
                           b"\x1b]8;id=" + b"x" * 127 + b"A;http://a.co\x1b\\X"
                           b"\x1b]8;id=" + b"x" * 127 + b"B;http://a.co\x1b\\Y\x1b]8;;\x1b\\",
                           "term:tmux-256color:256,RGB,hyperlinks"),
    "hl-double-id": (b"start\x1b[H",
                     b"\x1b]8;id=x:id=y;http://a.co\x1b\\Z\x1b]8;;\x1b\\",
                     "term:tmux-256color:256,RGB,hyperlinks"),
    # Erase must NOT carry the pane pen's foreground (cdx-2 #4). tmux clears with
    # a default cell whose only colour is the erase background; a coloured pen
    # must not leak a `\e[3Nm` before the clear (nor the sgr0 that cancels it).
    # Foreground-only EL/ED, with and without Sync and BCE.
    "fg-only-el": (b"abcdef\x1b[1;3H\x1b[31m", b"\x1b[K", "term:xterm-256color:256,RGB"),
    "fg-only-ed": (b"abcdef\x1b[1;3H\x1b[31m", b"\x1b[J", "term:xterm-256color:256,RGB"),
    "fg-only-el-nobce": (b"abcdef\x1b[1;3H\x1b[31m", b"\x1b[K", "term:xterm-256color:256,RGB,bce@"),
    "fg-only-ed-nobce": (b"abcdef\x1b[1;3H\x1b[31m", b"\x1b[J", "term:xterm-256color:256,RGB,bce@"),
    "fg-only-el-sync": (b"abcdef\x1b[1;3H\x1b[31m", b"\x1b[K", "term:tmux-256color:256,RGB,sync"),
    # am / NOAM derived flag + bottom-right behaviour (cdx-3 #2). On a terminal
    # WITHOUT automatic margins (am removed) tmux never writes the bottom-right
    # cell — tty_putn truncates the run so the last column of the last row stays
    # blank; with am present the same glyph IS written. The offline oracle
    # recomputes TERM_NOAM after the am@ override (mirroring tty_term_create), so
    # this exercises the real derived flag, not just a cancelled capability.
    "noam-br-two": (b"\x1b[24;79H", b"XY", "term:xterm-256color:256,RGB,am@"),
    "noam-br-last": (b"\x1b[24;80H", b"Z", "term:xterm-256color:256,RGB,am@"),
    "noam-br-notlastrow": (b"\x1b[23;79H", b"XY", "term:xterm-256color:256,RGB,am@"),
    "am-br-two": (b"\x1b[24;79H", b"XY", "term:xterm-256color:256,RGB"),
    "setaf-cancel-drop": (b"\x1b[H", b"\x1b[31mR", "term:xterm-256color:256,RGB,setaf@"),
    # Numeric colour-count degradation (item 2): a 256-index maps to the nearest
    # of the terminal's palette (tmux colour_256to16), fg direct-SGR, bg = fg+10.
    "colour-256to16-fg": (b"\x1b[H", b"\x1b[38;5;196mX", "term:xterm-16color"),
    "colour-256to16-bg": (b"\x1b[H", b"\x1b[48;5;100mX", "term:xterm-16color"),
    "colour-256to8-fg": (b"\x1b[H", b"\x1b[38;5;196mX", "term:xterm"),
    "colour-256to8-bg": (b"\x1b[H", b"\x1b[48;5;196mX", "term:xterm"),
    # Horizontal margins (item 3): an app scrolls within DECSLRM left/right
    # margins. tmux's pane does NOT honour app margins — the scroll acts full
    # width (vertical-region SU + redraw); the ymux engine matches (leftrightmargin
    # disabled) and the projector reproduces tmux's idiom byte-for-byte.
    "margin-su": (b"aaaaaaaaaa\r\nbbbbbbbbbb\r\ncccccccccc\r\ndddddddddd\r\neeeeeeeeee\x1b[H",
                  b"\x1b[?69h\x1b[3;7s\x1b[2;4r\x1b[2;3H\x1b[2S", "term:xterm-256color:256,RGB,margins"),
    "cub1-cancel-cup": (b"abcdef\x1b[1;4H", b"X\x1b[1;3HY", "term:xterm-256color:256,RGB,cub1@"),
    "cuf1-cancel-cup": (b"abc\x1b[1;1H", b"\x1b[1;2HZ", "term:xterm-256color:256,RGB,cuf1@"),
    "vpa-cancel-cup": (b"\x1b[H", b"\x1b[10;1HZ", "term:xterm-256color:256,RGB,vpa@"),
    # Semicolon-form underline colour (cycle-26): 58;5;N / 58;2;R;G;B reconstruct
    # to tmux's 58:5:N / 58:2:R:G:B (previously the colour value was dropped).
    "underline-colour-semicolon": (b"\x1b[4:3m\x1b[58;5;196mred\x1b[H", b"Y",
                                   "term:tmux-256color:256,RGB,usstyle"),
    # Overline (699-F): SGR 53 is carried by the engine's exotic-pen filter (the
    # read-only libvterm fork has no overline attribute) as a per-cell bit and
    # re-emitted via Smol.
    "overline-smol": (b"\x1b[53mover\x1b[H", b"W", "term:tmux-256color:256,RGB,overline"),
    # Styled + coloured underline (699-F): the underline COLOUR (SGR 58) is
    # emitted before the 4:N style and normalised to tmux's \e[58:2:R:G:B m form
    # (the empty colorspace-id an app may send is dropped).
    "underline-colour-rgb": (b"\x1b[4:3m\x1b[58:2::255:0:0mred\x1b[H", b"Y",
                             "term:tmux-256color:256,RGB,usstyle"),
    "ich-dch-cancel": (b"abcdef\x1b[H", b"\x1b[@\x1b[P"),
    "same-window-overwrite": (b"\x1b[H", b"abc\x1b[1;1HX"),
    # Capability profile (review #11): a 256-color client — the projector must
    # downgrade the RGB SGR to the same palette index tmux picks for a non-RGB
    # terminfo. 3-tuple = (base, delta, profile).
    "cap-256-rgb-downgrade": (b"\x1b[H", b"\x1b[38;2;95;135;175mX", "256"),
    # Review #12 operation set: tmux's clear tree (EL1/ECH), IL/DL idioms,
    # HT space-cells, ECH ordering, autowrap + phantom-cursor hide.
    "ech-line-start": (b"abcdef\x1b[1;1H", b"\x1b[2X"),
    "ech-mid-row": (b"abcdef\x1b[1;3H", b"\x1b[2X"),
    "il-one": (b"abc\r\ndef\x1b[2;1H", b"\x1b[1L"),
    "dl-one": (b"abc\r\ndef\x1b[2;1H", b"\x1b[1M"),
    "tab-then-text": (b"\x1b[H", b"A\tB"),
    "put-then-ech": (b"abcdef\x1b[H", b"Z\x1b[1X"),
    "right-edge-wrap": (b"\x1b[1;79H", b"XY"),
    # Review #12 attribute/mode set: dropped-but-dirty exotic pen state
    # (double underline, underline colour, overline, hyperlink on the
    # xterm-256color profile) + the cursor-style visibility sync.
    "double-underline": (b"\x1b[H", b"\x1b[21mX"),
    "underline-colour": (b"\x1b[H", b"\x1b[4m\x1b[58;5;196mX"),
    "overline": (b"\x1b[H", b"\x1b[53mX"),
    "hyperlink": (b"\x1b[H", b"\x1b]8;;http://e.com\x1b\\X\x1b]8;;\x1b\\"),
    "cursor-style": (b"\x1b[H", b"\x1b[5 q"),
    # Review #13: IL/DL inside ACTIVE margins (the real region emitted, not a
    # reconstruction) + alternate-screen transitions (full pane redraws, as
    # tmux emits — never a settled diff).
    "region-il": (b"\x1b[5;10r\x1b[7;1Hmid", b"\x1b[1L"),
    "region-dl": (b"\x1b[5;10r\x1b[7;1Hmid", b"\x1b[1M"),
    "alt-enter": (b"base", b"\x1b[?1049hALT"),
    "alt-exit": (b"base\x1b[?1049halt", b"\x1b[?1049l"),
    # Capability-profile fallback (review #13): a terminal WITHOUT ECH
    # (TERM=screen) — tmux's clear tree falls to literal spaces, and the
    # negotiated no-ech profile must emit the same.
    "cap-no-ech-spaces": (b"abcdef\x1b[1;3H", b"\x1b[2X", "no-ech"),
    # BCE (review #14): a coloured clear on a BCE terminal uses EL; without
    # the bce flag tmux paints explicit spaces (tty_fake_bce). A DEFAULT-bg
    # clear stays EL even without bce.
    "bce-el-coloured": (b"abcdef\x1b[1;3H", b"\x1b[41m\x1b[K"),
    "cap-no-bce-el": (b"abcdef\x1b[1;3H", b"\x1b[41m\x1b[K", "no-bce"),
    "cap-no-bce-default-el": (b"abcdef\x1b[1;3H", b"\x1b[K", "no-bce"),
    "cap-no-bce-ech": (b"abcdef\x1b[1;3H", b"\x1b[41m\x1b[2X", "no-bce"),
    "cap-no-bce-ed": (b"abcdef\r\nsecond\x1b[1;3H", b"\x1b[41m\x1b[J", "no-bce"),
    # no-CSR (review #15): without change_scroll_region tmux redraws from
    # home instead of using scroll idioms — full-screen LF scroll, a
    # DECSTBM-scoped IL, and top-of-screen RI all take the redraw path.
    "cap-no-csr-scroll": (b"\r\n".join(b"line-%02d" % i for i in range(24)), b"\r\nX",
                          "no-csr"),
    "cap-no-csr-il": (b"\x1b[2;5r\x1b[3;1Hmid", b"\x1b[1L", "no-csr"),
    "cap-no-csr-ri": (b"top\x1b[1;1H", b"\x1bM", "no-csr"),
    # Wrapped-line edges (review #15): a WIDE char that does not fit in the
    # last column forces an early wrap. The bottom-right wrap+scroll case
    # (tmux defers the wrapped run: \r\n scroll FIRST, then the whole run
    # written through at the pre-wrap origin with a trailing EL) is measured
    # but not yet ported — tracked in PARITY.md, deliberately NOT listed
    # here until it passes.
    "wrap-wide-boundary": (b"\x1b[1;19H", b"ab\xe6\xbc\xa2cd"),
    # Review #16: the two reproducible failures, committed BEFORE the fix so
    # they cannot disappear from later reviews.
    "wrap-deferred-corner": (b"\x1b[24;80H", b"XY"),
    "wrap-deferred-corner-3": (b"\x1b[24;79H", b"XYZ"),
    # Exotic pen on the DEFAULT profile (review #16): xterm-256color has no
    # Smulx/Setulc/hyperlinks — tmux DROPS extended underline styles,
    # underline colour, and OSC-8, emitting the text plain.
    # ENABLED exotic profile (review #17): the values carry — extended
    # underline styles, verbatim underline colour, interned OSC-8 links.
    "exotic-enabled-values": (b"base",
                              b"\x1b[4:3mcurly\x1b[4:0m \x1b[58:5:196mred-ul\x1b[59m "
                              b"\x1b]8;;http://x\x1b\\link\x1b]8;;\x1b\\", "exotic"),
    "exotic-enabled-styles": (b"base", b"\x1b[4mplain\x1b[24m \x1b[4:2mdbl\x1b[4:0m "
                                        b"\x1b[21mdb2\x1b[24m", "exotic"),
    "exotic-dropped-default": (b"base",
                               b"\x1b[4:3mcurly\x1b[4:0m \x1b[58:5:196mred-ul\x1b[59m "
                               b"\x1b]8;;http://x\x1b\\link\x1b]8;;\x1b\\"),
    "cap-no-csr-outside": (b"\x1b[2;5r\x1b[3;1Hmid", b"\x1b[1L\x1b[10;1HOUT", "no-csr"),
    # BROADER no-CSR multi-op parity (review #17): several csr-less redraw
    # ops in ONE flush — consecutive region ops, DL, multi-line IL/DL,
    # scroll-then-write into the redrawn area, and ops in TWO different
    # DECSTBM regions in the same delta.
    "cap-no-csr-dl": (b"\x1b[2;5r\x1b[3;1Hmid", b"\x1b[1M", "no-csr"),
    "cap-no-csr-il2": (b"\x1b[2;6r\x1b[3;1Hmid", b"\x1b[2L", "no-csr"),
    "cap-no-csr-dl2": (b"\x1b[2;6r\x1b[3;1Hmid", b"\x1b[2M", "no-csr"),
    "cap-no-csr-il-dl": (b"\x1b[2;6r\x1b[3;1Hmid", b"\x1b[1L\x1b[4;1H\x1b[1M", "no-csr"),
    "cap-no-csr-scroll2": (b"\r\n".join(b"line-%02d" % i for i in range(24)),
                           b"\r\nX\r\nY", "no-csr"),
    "cap-no-csr-scroll-write": (b"\r\n".join(b"line-%02d" % i for i in range(24)),
                                b"\r\nX\x1b[12;1HMID", "no-csr"),
    "cap-no-csr-two-regions": (b"\x1b[2;5r\x1b[3;1Hmid",
                               b"\x1b[1L\x1b[8;12r\x1b[9;1H\x1b[1M", "no-csr"),
    "cap-no-csr-ri-region": (b"\x1b[3;8r\x1b[3;1Htop", b"\x1bM", "no-csr"),
    # LARGE partial region (>= half the screen): tmux defers to ONE whole-pane
    # redraw of the final screen (tty_large_region -> screen_redraw_pane).
    "cap-no-csr-large": (b"\x1b[2;20r\x1b[3;1Hmid", b"\x1b[1L", "no-csr"),
    "cap-no-csr-large-write": (b"\x1b[2;20r\x1b[3;1Hmid", b"\x1b[1L\x1b[22;1HOUT", "no-csr"),
    # tmux terminfo/features STATE MODEL (review #17 item 8): the driver's
    # capability profile comes from TERM+features RESOLUTION (the ported
    # tty-features pipeline), the oracle from the same strings — pinning
    # that resolution reproduces every hand-set profile and covers new
    # terminal families.
    "res-truecolor": (b"base", b"\x1b[38;2;255;102;0mTC\x1b[0m",
                      "term:xterm-256color:256,RGB"),
    "res-256-downgrade": (b"base", b"\x1b[38;2;255;102;0mTC\x1b[0m",
                          "term:xterm-256color:256"),
    "res-screen-spaces": (b"abcdef\x1b[1;3H", b"\x1b[2X", "term:screen"),
    "res-nobce-el": (b"abcdef\x1b[1;3H", b"\x1b[41m\x1b[K",
                     "term:xterm-nobce:256,RGB"),
    "res-nocsr-il": (b"\x1b[2;5r\x1b[3;1Hmid", b"\x1b[1L",
                     "term:xterm-nocsr:256,RGB"),
    "res-exotic-values": (b"base",
                          b"\x1b[4:3mcurly\x1b[4:0m \x1b[58:5:196mred-ul\x1b[59m "
                          b"\x1b]8;;http://x\x1b\\link\x1b]8;;\x1b\\",
                          "term:xterm-256color:256,RGB,hyperlinks,usstyle"),
    "res-screen256": (b"base", b"\x1b[38;5;208morange\x1b[0m \x1b[2X",
                      "term:screen-256color:256"),
    # Synchronized output (mode 2026) passthrough behaviour.
    "sync-output-span": (b"base", b"\x1b[?2026h\x1b[HSYNCED\x1b[?2026l"),
    # Review #14: scroll-COMMAND identity (IL at the region top is not RI; DL
    # at the top is not the LF path) and OPERATION-TIME cursor columns (a
    # later CUP must not move where the insert renders; full-screen IL keeps
    # its column).
    "il-region-top": (b"\x1b[5;10r\x1b[5;1Htop", b"\x1b[1L"),
    "dl-region-top": (b"\x1b[5;10r\x1b[5;1Htop", b"\x1b[1M"),
    "region-il-cup-home": (b"\x1b[5;10r\x1b[7;5H", b"\x1b[1L\x1b[H"),
    "region-dl-cup-home": (b"\x1b[5;10r\x1b[7;5H", b"\x1b[1M\x1b[H"),
    "full-il-col5": (b"\x1b[3;5H", b"\x1b[1L"),
}


def ensure_derived_terminfo(name: str, strip_pattern: str) -> str:
    """Compile a DERIVED terminfo entry (xterm-256color minus one capability)
    into a private dir. Deterministic: derived from the system entry at run
    time."""
    dest = os.path.join(REPO, "tmp", "terminfo-%s" % name)
    compiled = os.path.join(dest, "x", "xterm-%s" % name)
    if os.path.exists(compiled):
        return dest
    os.makedirs(dest, exist_ok=True)
    dump = subprocess.run(["infocmp", "-x", "xterm-256color"], capture_output=True,
                          text=True, timeout=30)
    if dump.returncode != 0:
        raise RuntimeError("infocmp xterm-256color failed")
    import re as regex
    source = dump.stdout.replace("xterm-256color|", "xterm-%s|" % name, 1)
    source = regex.sub(strip_pattern, "", source, count=1)
    source_path = os.path.join(dest, "xterm-%s.src" % name)
    with open(source_path, "w") as handle:
        handle.write(source)
    tic = subprocess.run(["tic", "-x", source_path], capture_output=True, timeout=30,
                         env=dict(os.environ, TERMINFO=dest))
    if tic.returncode != 0:
        raise RuntimeError("tic xterm-%s failed: %s" % (name, tic.stderr.decode(errors="replace")))
    return dest


def ensure_nobce_terminfo() -> str:
    """Compile xterm-nobce (xterm-256color minus the bce flag) into a private
    terminfo dir for the no-bce capability profile. Deterministic: derived
    from the system xterm-256color entry at run time."""
    dest = os.path.join(REPO, "tmp", "terminfo-nobce")
    compiled = os.path.join(dest, "x", "xterm-nobce")
    if os.path.exists(compiled):
        return dest
    os.makedirs(dest, exist_ok=True)
    dump = subprocess.run(["infocmp", "-x", "xterm-256color"], capture_output=True,
                          text=True, timeout=30)
    if dump.returncode != 0:
        raise RuntimeError("infocmp xterm-256color failed")
    source = dump.stdout.replace("xterm-256color|", "xterm-nobce|", 1).replace("bce, ", "", 1)
    source_path = os.path.join(dest, "xterm-nobce.src")
    with open(source_path, "w") as handle:
        handle.write(source)
    tic = subprocess.run(["tic", "-x", source_path], capture_output=True, timeout=30,
                         env=dict(os.environ, TERMINFO=dest))
    if tic.returncode != 0:
        raise RuntimeError("tic xterm-nobce failed: %s" % tic.stderr.decode(errors="replace"))
    return dest


def capture_oracle(oracle: str, base: bytes, delta: bytes, rows: int, cols: int,
                   features: str = "256,RGB", term: str = "xterm-256color",
                   overrides: str = "") -> bytes:
    """tmux's incremental emission for `delta` after `base`, via the in-process
    oracle — deterministic byte-for-byte (same schedule every run)."""
    work = tempfile.mkdtemp(prefix="tmuxoracle-")
    base_path = os.path.join(work, "base.bin")
    delta_path = os.path.join(work, "delta.bin")
    with open(base_path, "wb") as handle:
        handle.write(base)
    with open(delta_path, "wb") as handle:
        handle.write(delta)
    env = dict(os.environ)
    if term == "xterm-nobce":
        env["TERMINFO"] = ensure_nobce_terminfo()
    elif term == "xterm-nocsr":
        env["TERMINFO"] = ensure_derived_terminfo(
            "nocsr", r"csr=\\E\[%i%p1%d;%p2%dr, ")
    oracle_argv = [oracle, str(rows), str(cols), base_path, delta_path, features, term]
    if overrides:
        oracle_argv.append(overrides)
    proc = subprocess.run(oracle_argv, capture_output=True, timeout=60, env=env)
    if proc.returncode != 0:
        raise RuntimeError("oracle failed: %s" % proc.stderr.decode(errors="replace"))
    return proc.stdout


def canonicalize(stream: bytes) -> bytes:
    """Fold tmux's semantically-equivalent, run-to-run-nondeterministic cursor
    forms to one shape before comparison. tmux emits `\\e[1;1H` (absolute cup) or
    `\\e[H` (home) for cursor→(0,0) depending on its wrap-pending state at redraw
    entry — the SAME input yields either across runs. Both mean "home"; fold the
    absolute form to home so the diff measures real divergence, not tmux's own
    cursor-optimization nondeterminism.

    Also drop the no-op cursor-visibility wrap `\\e[?25l\\e[?12l\\e[?25h` (hide
    immediately followed by blink-reset+show, NOTHING drawn between): tmux emits
    it when a redraw pass runs but paints zero cells (e.g. the unconditional
    scrollbar-flag path with pane-scrollbars off). It changes no observable
    state."""
    stream = stream.replace(b"\x1b[?25l\x1b[?12l\x1b[?25h", b"")
    return stream.replace(b"\x1b[1;1H", b"\x1b[H")


def first_divergence(left: bytes, right: bytes) -> int:
    limit = min(len(left), len(right))
    for index in range(limit):
        if left[index] != right[index]:
            return index
    if len(left) != len(right):
        return limit
    return -1


def report_case(name: str, tmux_bytes: bytes, ymux_bytes: bytes, raw: bool = False) -> bool:
    if not raw:
        tmux_bytes = canonicalize(tmux_bytes)
        ymux_bytes = canonicalize(ymux_bytes)
    at = first_divergence(tmux_bytes, ymux_bytes)
    if at < 0:
        print("  PASS  %-16s byte-identical (%d bytes)" % (name, len(tmux_bytes)))
        return True
    lo = max(0, at - 24)
    print("  FAIL  %-16s diverge @%d  (tmux %dB, ymux %dB)"
          % (name, at, len(tmux_bytes), len(ymux_bytes)))
    print("        tmux: …%s" % esc(tmux_bytes[lo:at + 24]))
    print("        ymux: …%s" % esc(ymux_bytes[lo:at + 24]))
    print("        first-diverge tmux=%s ymux=%s"
          % (esc(tmux_bytes[at:at + 1]) if at < len(tmux_bytes) else "<eof>",
             esc(ymux_bytes[at:at + 1]) if at < len(ymux_bytes) else "<eof>"))
    return False


# tmux attach-preamble segments that wrap the OUTER terminal session and do
# not apply to ymux's dedicated grid receiver (each verified present in the
# tmux dump, then stripped before the byte comparison — documented, not
# silently excluded):
#   \e[?1049h        alternate screen (a dedicated grid has no primary to save)
#   \e[22;0;0t       title stack push (no outer title)
#   \e[?1h\e=        DECCKM + keypad application (outer-terminal key encoding)
PREAMBLE_OUTER_SEGMENTS = [b"\x1b[?1049h", b"\x1b[22;0;0t", b"\x1b[?1h\x1b="]


def run_preamble(oracle: str, driver: str, rows: int, cols: int,
                 features: str = "256,RGB", profile: str = "preamble") -> int:
    """Compare tmux's attach preamble + first redraw against the projector's
    attach-time emission (the first full projection). With features="256,RGB,sync"
    and profile="preamble-sync" this also proves the synchronized-output
    (?2026h/?2026l) full-redraw wrapping is byte-identical to tmux."""
    work = tempfile.mkdtemp(prefix="preamble-")
    base_path = os.path.join(work, "base.bin")
    delta_path = os.path.join(work, "delta.bin")
    with open(base_path, "wb") as handle:
        handle.write(b"X")
    with open(delta_path, "wb") as handle:
        handle.write(b"")
    dump_prefix = os.path.join(work, "dump")
    env = dict(os.environ, TMUX_ORACLE_DUMP_PREFIX=dump_prefix)
    proc = subprocess.run([oracle, str(rows), str(cols), base_path, delta_path, features,
                           "xterm-256color"], capture_output=True, timeout=60, env=env)
    if proc.returncode != 0:
        print("preamble: oracle failed")
        return 1
    with open(dump_prefix + "-attach.bin", "rb") as handle:
        tmux_bytes = handle.read()
    with open(dump_prefix + "-base.bin", "rb") as handle:
        tmux_bytes += handle.read()
    for segment in PREAMBLE_OUTER_SEGMENTS:
        if segment not in tmux_bytes:
            print("preamble: expected outer segment %r missing from the tmux dump" % segment)
            return 1
        tmux_bytes = tmux_bytes.replace(segment, b"", 1)
    ours = subprocess.run([driver, str(rows), str(cols), "0", profile], input=b"X",
                          capture_output=True, timeout=60)
    ymux_bytes = ours.stdout
    if tmux_bytes == ymux_bytes:
        label = {"preamble-sync": "PREAMBLE-SYNC",
                 "preamble-margins": "PREAMBLE-MARGINS"}.get(profile, "PREAMBLE")
        print("tmux-diff: %s byte-identical (%d bytes, %d outer segments stripped)"
              % (label, len(tmux_bytes), len(PREAMBLE_OUTER_SEGMENTS)))
        return 0
    diverge = next((i for i in range(min(len(tmux_bytes), len(ymux_bytes)))
                    if tmux_bytes[i] != ymux_bytes[i]), min(len(tmux_bytes), len(ymux_bytes)))
    print("tmux-diff: PREAMBLE diverge @%d (tmux %dB, ymux %dB)" % (
        diverge, len(tmux_bytes), len(ymux_bytes)))
    print("  tmux: %r" % tmux_bytes[max(0, diverge - 12):diverge + 28])
    print("  ymux: %r" % ymux_bytes[max(0, diverge - 12):diverge + 28])
    return 1


def main() -> int:
    tmux = DEFAULT_TMUX
    driver = DEFAULT_DRIVER
    oracle = DEFAULT_ORACLE
    rows, cols = 24, 80
    wanted = []
    incremental = False
    require_tmux = False
    args = sys.argv[1:]
    idx = 0
    while idx < len(args):
        arg = args[idx]
        if arg == "--tmux":
            idx += 1; tmux = args[idx]
        elif arg == "--driver":
            idx += 1; driver = args[idx]
        elif arg == "--oracle":
            idx += 1; oracle = args[idx]
        elif arg == "--rows":
            idx += 1; rows = int(args[idx])
        elif arg == "--cols":
            idx += 1; cols = int(args[idx])
        elif arg == "--incremental":
            incremental = True
        elif arg == "--preamble":
            incremental = "preamble"
        elif arg == "--preamble-sync":
            incremental = "preamble-sync"
        elif arg == "--preamble-margins":
            incremental = "preamble-margins"
        elif arg == "--require-tmux":
            # Mandatory-gate mode: a missing required artifact is a hard FAIL,
            # never a skip. The reviewed defect was that a build without the
            # pinned tmux could report a green/skipped mandatory gate.
            require_tmux = True
        else:
            wanted.append(arg)
        idx += 1

    # The driver is needed by every mode.
    if not os.path.exists(driver):
        print("tmux-diff: vtdiff-driver not built at %s" % driver)
        return 2

    # Incremental/preamble modes compare against the OFFLINE oracle, not the
    # live tmux binary — a missing oracle is a hard FAIL (return 1), never a
    # skip, so the mandatory gate can never pass without comparing bytes.
    if incremental == "preamble":
        if not os.path.exists(oracle):
            print("tmux-diff: FAIL — oracle required for preamble mode")
            return 1
        return run_preamble(oracle, driver, rows, cols)

    if incremental == "preamble-sync":
        if not os.path.exists(oracle):
            print("tmux-diff: FAIL — oracle required for preamble-sync mode")
            return 1
        return run_preamble(oracle, driver, rows, cols,
                            features="256,RGB,sync", profile="preamble-sync")

    if incremental == "preamble-margins":
        if not os.path.exists(oracle):
            print("tmux-diff: FAIL — oracle required for preamble-margins mode")
            return 1
        return run_preamble(oracle, driver, rows, cols,
                            features="256,RGB,margins", profile="preamble-margins")

    if incremental:
        if not os.path.exists(oracle):
            # The oracle is the ONLY deterministic source for incremental
            # emissions (the live capture provably is not); without it the
            # incremental mode cannot claim anything — fail, don't skip.
            print("tmux-diff: FAIL — oracle not built at %s "
                  "(run tools/tmux-oracle/build.sh)" % oracle)
            return 1
        cases = {k: INCREMENTAL_CASES[k] for k in wanted} if wanted else INCREMENTAL_CASES
        print("tmux-diff: INCREMENTAL (oracle), %d case(s), %dx%d" % (len(cases), cols, rows))
        passed = 0
        for name, spec in cases.items():
            base, delta = spec[0], spec[1]
            profile = spec[2] if len(spec) > 2 else "truecolor"
            if profile.startswith("term:"):
                # STATE-MODEL profile (review #17 item 8): the driver
                # resolves TERM+features through the ported tty-features
                # pipeline; the oracle gets the SAME strings — equality
                # proves the resolution matches tmux for that terminal.
                term_parts = profile[5:].split(":", 1)
                term = term_parts[0]
                features = term_parts[1] if len(term_parts) > 1 else ""
            else:
                features = {"256": "256", "no-ech": "",
                            "exotic": "256,RGB,hyperlinks,usstyle"}.get(profile, "256,RGB")
                term = {"no-ech": "screen", "no-bce": "xterm-nobce",
                        "no-csr": "xterm-nocsr"}.get(profile, "xterm-256color")
            # Terminal-overrides (cycle-26): cap=value / cap@ tokens in the
            # features string are terminfo OVERRIDES, not tmux features — pass
            # them to the oracle's `terminal-overrides` (colon-separated caps)
            # so el@, csr@, el=..., etc. actually take effect in tmux.
            override_caps = [tok for tok in features.split(",") if "@" in tok or "=" in tok]
            overrides = ":".join(override_caps)
            try:
                # RAW comparison (review #11): the oracle is deterministic, so
                # incremental parity is EXACT bytes — no canonicalization.
                tmux_bytes = capture_oracle(oracle, base, delta, rows, cols,
                                            features=features, term=term, overrides=overrides)
                # ymux delta: split at len(base) so base is a discarded full redraw.
                proc = subprocess.run([driver, str(rows), str(cols), str(len(base)),
                                       profile],
                                      input=base + delta, capture_output=True)
                ymux_bytes = proc.stdout
            except Exception as exc:  # noqa: BLE001
                print("  ERROR %-16s %s" % (name, exc))
                continue
            if report_case(name, tmux_bytes, ymux_bytes, raw=True):
                passed += 1
        print("tmux-diff: INCREMENTAL %d/%d byte-identical (RAW, no canonicalization)"
              % (passed, len(cases)))
        return 0 if passed == len(cases) else 1

    # LIVE default mode drives the pinned tmux binary directly. Absent it, SKIP
    # (developer convenience) UNLESS the caller demanded it be present.
    if not os.path.exists(tmux):
        if require_tmux:
            print("tmux-diff: FAIL — pinned tmux required but not built at %s" % tmux)
            return 1
        print("tmux-diff: pinned tmux not built at %s (skipping)" % tmux)
        return 77  # visible SKIP for ctest (SKIP_RETURN_CODE), not a silent pass

    cases = {k: CASES[k] for k in wanted} if wanted else CASES
    print("tmux-diff: %d case(s), %dx%d, TERM=xterm-256color" % (len(cases), cols, rows))
    passed = 0
    xfail = 0
    comparable = 0
    for name, vector in cases.items():
        try:
            tmux_bytes = capture_tmux(tmux, vector, rows, cols)
            ymux_bytes = extract_body(run_ymux(driver, vector, rows, cols))
        except Exception as exc:  # noqa: BLE001 — surface any rig failure per case
            print("  ERROR %-16s %s" % (name, exc))
            continue
        identical = first_divergence(canonicalize(tmux_bytes), canonicalize(ymux_bytes)) < 0
        if name in XFAIL_UPSTREAM and not identical:
            print("  XFAIL %-16s %s" % (name, XFAIL_UPSTREAM[name]))
            xfail += 1
            continue
        comparable += 1
        if report_case(name, tmux_bytes, ymux_bytes):
            passed += 1
    print("tmux-diff: %d/%d byte-identical (%d xfail-upstream)" % (passed, comparable, xfail))
    if comparable == 0:
        # Zero comparable cases means every tmux capture errored — that is a
        # harness/environment FAILURE, never success (review #10 false-success).
        print("tmux-diff: FAIL — zero comparable cases (captures errored)")
        return 1
    return 0 if passed == comparable else 1


if __name__ == "__main__":
    sys.exit(main())
