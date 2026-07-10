"""ZLE destructive-backspace behavior under yos.

Pins what zsh ACTUALLY does when the user presses backspace inside an
interactive ZLE session, drives it through a real PTY, and asserts:

  1. ZLE removes the typed char from its buffer (semantic correctness).
  2. ZLE emits the destructive-backspace sequence `\\b \\b` to the
     terminal (visual correctness — the literal byte sequence that
     instructs a vt100-ish terminal to move-back, overwrite-with-space,
     move-back).

Why this exists: a user reported "backspace = space inside zsh".
Probing showed ZLE is emitting the right sequence; the visual
appearance depends on whether the host terminal honors `\\b`. Plus,
between `\\b \\b` and the next event there's an intermittent run of 8
`\\xff` bytes — a separate yos write-stream bug whose root cause is
still under investigation. This test xfails on those `\\xff` bytes so
it surfaces if they ever go away (or get worse).
"""
import os
import pty
import select
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from _zsh_path import find_zsh_wasm


def main():
    import glob
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    # Prefer the umbrella result tree (yos + zsh in one place) over
    # find_zsh_wasm's `bin/zsh.wasm` lookup, which hits the standalone
    # zsh derivation but won't find the libexec/zsh that the umbrella
    # symlinks under `nix build .#all`.
    yos = None; zsh = None
    for sym in sorted(glob.glob(os.path.join(repo, "result*"))):
        if not yos:
            p = os.path.join(sym, "bin", "yos")
            if os.path.exists(p): yos = p
        if not zsh:
            for cand in [os.path.join(sym, "libexec", "zsh"),
                         os.path.join(sym, "bin", "zsh.wasm")]:
                if os.path.exists(cand): zsh = cand; break
    if not (yos and zsh):
        print(f"SKIP: yos and/or zsh.wasm not found "
              f"(yos={yos}, zsh={zsh})")
        sys.exit(0)

    # Use a sandbox HOME so the user's ~/.zshrc (antidote, plugins,
    # custom keybindings) doesn't leak into the test. The test is
    # asserting default ZLE behavior for backspace; user keybindings
    # would mask the very byte sequence we're trying to pin.
    import tempfile
    sandbox_home = tempfile.mkdtemp(prefix="yos-zsh-bs-")
    pid, fd = pty.fork()
    if pid == 0:
        env = {"TERM": "xterm-256color",
               "PATH": os.environ.get("PATH", ""),
               "HOME": sandbox_home,
               "ZDOTDIR": sandbox_home,
               "USER": os.environ.get("USER", "test")}
        os.execvpe(yos, [yos, zsh, "-i"], env)

    # Drive: type 'a', backspace, 'b', Enter, then watch for the prompt
    # to reappear.
    inputs = [(0.4, b"a"), (0.6, b"\x7f"), (0.8, b"b"),
              (1.0, b"\r"), (3.0, None)]
    buf = bytearray()
    start = time.monotonic(); i = 0
    while True:
        now = time.monotonic() - start
        if i < len(inputs) and now >= inputs[i][0]:
            if inputs[i][1] is not None:
                os.write(fd, inputs[i][1])
            i += 1
        if i >= len(inputs) and now >= inputs[-1][0]:
            break
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try: d = os.read(fd, 4096)
            except OSError: break
            if not d: break
            buf.extend(d)
    os.close(fd)
    try: os.kill(pid, 9); os.waitpid(pid, 0)
    except OSError: pass

    s = bytes(buf)
    # Required: the destructive-backspace sequence MUST appear in
    # the output stream after we send 0x7f. ZLE's `backward-delete-
    # char` widget emits `\b \b` (BS + space + BS) to redraw.
    if b"\x08 \x08" not in s:
        print("FAIL: destructive backspace `\\b \\b` not in output")
        print(f"  full stream: {s!r}")
        sys.exit(1)

    # Required: 'a' must echo (ZLE in raw mode echoes the typed char)
    # and ZLE's buffer-after-backspace must contain only 'b'. Easiest
    # heuristic: 'a' AND 'b' both appear, and 'a' is followed by
    # `\b \b` somewhere before 'b'.
    a_pos = s.find(b"a")
    bs_pos = s.find(b"\x08 \x08")
    b_pos = s.find(b"b", bs_pos)
    if not (0 <= a_pos < bs_pos < b_pos):
        print(f"FAIL: bad ZLE event order — a@{a_pos} bs@{bs_pos} b@{b_pos}")
        print(f"  full stream: {s!r}")
        sys.exit(1)

    # User-reported "backspace = space" bug. Without the workaround
    # in vfs.c/file.c that drops short pure-0xff writes to a tty,
    # the post-fork garbage from impl/proc.c's asyncify leak landed
    # on the user's terminal between 'b' and the destructive
    # backspace, breaking column accounting on modern UTF-8
    # terminals. With the workaround, those writes are silently
    # dropped (real text is never pure 0xff). Assert no 0xff bytes
    # made it through to the captured PTY stream.
    ff_run = b"\xff\xff\xff\xff\xff\xff\xff\xff"
    if ff_run in s:
        print(f"FAIL: 8x 0xff garbage still in tty stream — workaround "
              f"in vfs.c/file.c didn't catch this write path. Run "
              f"with YOS_TRACE_0XFF=1 to find the bypassing call.")
        print(f"  full stream: {s!r}")
        sys.exit(1)
    print(f"PASS: ZLE backspace deletes char + emits "
          f"\\b \\b destructive echo (no 0xff garbage on tty)")


if __name__ == "__main__":
    main()
