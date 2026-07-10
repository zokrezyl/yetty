"""ls(1) under a tty: filenames must render as text, not `?????`.

This test exists because the user hit exactly this regression:
running `./tools/yos.sh ls` from an interactive terminal printed
every filename as `?????` even though `ls /` over a pipe worked.

Root cause: when stdout is a tty, FreeBSD ls turns on its default
`-q` mode and runs every output byte through isprint(3). Without
yos_locale_stub.c compiled into the binary, _CurrentRuneLocale is
NULL so isprint() returns 0 for every byte and ls substitutes
'?' for every character.

This test forces a tty via pty.fork and asserts the output
contains an actual filename we put there — not just `?`s.
"""
import os
import pty
import select
import sys
import tempfile
import time
from run_tool import get_paths


def main():
    yos, libexec = get_paths()
    with tempfile.TemporaryDirectory(prefix="yos-lsttytest-") as base:
        # Filename that survives only if isprint() returns true for ASCII.
        sentinel = "hello-world"
        open(os.path.join(base, sentinel), "w").close()

        pid, fd = pty.fork()
        if pid == 0:
            os.chdir(base)
            os.execvp(yos, [yos, os.path.join(libexec, "ls")])
        # Read up to 2 seconds of output.
        buf = bytearray()
        deadline = time.time() + 2.0
        while time.time() < deadline:
            rs, _, _ = select.select([fd], [], [], 0.2)
            if not rs:
                continue
            try:
                d = os.read(fd, 65536)
            except OSError:
                break
            if not d:
                break
            buf.extend(d)
        os.close(fd)
        try:
            os.waitpid(pid, 0)
        except ChildProcessError:
            pass
        out = bytes(buf)

    if sentinel.encode() in out:
        print(f"PASS: ls under tty printed {sentinel!r}")
        sys.exit(0)
    # If we see only ?'s, that's the locale-stub regression.
    if b"?" in out and sentinel.encode() not in out:
        print(f"FAIL: ls under tty printed only '?' "
              f"(locale-stub gap — output={out[:120]!r})")
        sys.exit(1)
    print(f"FAIL: ls under tty unexpected output {out[:200]!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
