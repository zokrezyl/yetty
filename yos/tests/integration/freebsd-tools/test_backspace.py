"""zsh interactive backspace: ZLE must emit \\b \\b, not just space.

This test exists because the user hit this regression: typing a
character then backspace under `./tools/yos.sh` moved the cursor
forward (writing a literal space) instead of erasing.

Two host-side gaps had to land for this to work end to end:

  1. `env.poll` had to actually exist as a bridge. zsh's ZLE
     calls poll() from getbyte() the moment it peeks for an
     escape-sequence continuation; without it, every interactive
     session traps the first time you press anything ZLE wants to
     stage-look at (arrow keys, ESC, …) and falls back to a
     degraded line driver.
  2. libyos_stubs' termcap shim had to return a real cursor-left
     string ("\\b") for tgetstr("le") / "bc", plus the auto-margin
     `am` flag. Without it ZLE thinks the terminal can't move the
     cursor backward and only writes a space to overwrite the
     erased column — the cursor never steps back, so visually it
     looks like backspace IS a space.

We drive zsh through a real pty, type "abc<BS>", wait, and assert
the bytes coming back contain a literal `\\x08` (backspace) right
before or after the space. The destructive-backspace sequence is
typically `\\b \\b` (3 bytes).
"""
import os
import pty
import select
import sys
import time
from run_tool import get_paths


def main():
    yos, libexec = get_paths()
    zsh_abs = os.path.join(libexec, "zsh")
    if not os.path.exists(zsh_abs):
        print(f"SKIP: zsh not in libexec")
        sys.exit(0)

    pid, fd = pty.fork()
    if pid == 0:
        # Clean zsh — skip the user's ~/.zshenv/~/.zshrc.
        os.environ.clear()
        os.environ["ZDOTDIR"] = "/tmp"
        os.environ["PATH"] = libexec
        os.environ["TERM"] = "xterm"
        os.execvp(yos, [yos, zsh_abs, "-f"])

    def drain(s):
        out = bytearray(); end = time.time() + s
        while time.time() < end:
            rs, _, _ = select.select([fd], [], [], 0.2)
            if not rs:
                continue
            try:
                d = os.read(fd, 65536)
            except OSError:
                break
            if not d:
                break
            out.extend(d)
        return bytes(out)

    time.sleep(1.5)
    drain(0.5)
    os.write(fd, b"abc")
    time.sleep(0.5)
    drain(0.5)
    os.write(fd, b"\x08")
    time.sleep(0.5)
    bs_out = drain(0.5)
    os.write(fd, b"\n")
    drain(0.3)
    os.write(fd, b"exit\n")
    drain(0.5)
    os.close(fd)
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass

    if b"\x08" not in bs_out:
        print(f"FAIL: backspace did not emit \\b — got {bs_out!r}")
        print("  ZLE is still treating the terminal as dumb. Check the")
        print("  termcap shim in nixpkgs/sysroot/default.nix and the")
        print("  env.poll bridge in src/yos/impl/dir.c.")
        sys.exit(1)
    print(f"PASS: backspace emitted destructive sequence ({bs_out!r})")
    sys.exit(0)


if __name__ == "__main__":
    main()
