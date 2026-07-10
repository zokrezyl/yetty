"""top(1): the real interactive FreeBSD top, retargeted to yos.

Status: PASS. top is built from FreeBSD's usr.bin/top source verbatim
(top.c/display.c/screen.c/commands.c/utils.c/username.c); only the
OS-specific machine.c is replaced by yos_machine.c, which sources the
process list from sysctl(CTL_KERN, KERN_PROC, KERN_PROC_PROC) +
getloadavg instead of libkvm. termcap comes from the sysroot's
libyos_stubs (it now advertises an 80x24 ANSI screen + cursor
addressing so screen.c keeps smart_terminal on).

This guards two things:

  1. Batch mode (`top -b`): non-interactive one-shot. The real top
     header (load averages / processes / CPU / Mem) and the process
     table render, and the calling process shows up as pid 1 / top.

  2. Interactive mode ("top mode") over a pty: top must enter the
     full-screen path — clear + cursor addressing (ESC[r;cH) — refresh
     on its timer, and quit on 'q'. This is the regression that a
     plain snapshot tool could never satisfy.
"""
import os
import re
import select
import sys
import time

from run_tool import get_paths, run


def check_batch(yos, libexec):
    r = run(yos, libexec, "top", "-b", "1", expect_rc=0, timeout=12)
    out = r.stdout.decode()
    lines = out.splitlines()
    if not lines or "load averages" not in lines[0]:
        print(f"FAIL: top summary line missing — stdout={r.stdout!r}")
        sys.exit(1)
    if "processes" not in out:
        print(f"FAIL: top process tally missing — stdout={r.stdout!r}")
        sys.exit(1)
    hdr = next((i for i, ln in enumerate(lines)
                if ln.startswith("  PID") and "COMMAND" in ln), None)
    if hdr is None:
        print(f"FAIL: top column header missing — stdout={r.stdout!r}")
        sys.exit(1)
    body = [ln for ln in lines[hdr + 1:] if ln.strip()]
    if not body:
        print(f"FAIL: top printed no process rows — stdout={r.stdout!r}")
        sys.exit(1)
    cols = body[0].split()
    try:
        pid = int(cols[0])
    except (ValueError, IndexError):
        print(f"FAIL: top row pid not numeric: {body[0]!r}")
        sys.exit(1)
    if pid != 1:
        print(f"FAIL: standalone top should be pid 1, got {pid}")
        sys.exit(1)
    if "top" not in body[0]:
        print(f"FAIL: top row missing comm 'top': {body[0]!r}")
        sys.exit(1)
    print(f"PASS(batch): {body[0].strip()!r}")


def check_interactive(yos, libexec):
    import pty
    top = os.path.join(libexec, "top")
    env = dict(os.environ)
    env["TERM"] = "xterm"
    env.pop("YTRACE_DEFAULT_ON", None)

    pid, fd = pty.fork()
    if pid == 0:
        os.environ.clear()
        os.environ.update(env)
        os.execvp(yos, [yos, top])   # interactive, default delay

    def drain(seconds):
        end = time.time() + seconds
        buf = bytearray()
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
            buf.extend(d)
        return bytes(buf)

    time.sleep(1.0)
    data = bytearray(drain(3.0))     # first full-screen paint
    os.write(fd, b" ")               # space -> force an immediate update
    data.extend(drain(2.0))
    os.write(fd, b"q")               # quit
    time.sleep(0.4)
    try:
        os.close(fd)
    except OSError:
        pass
    rc = None
    try:
        _, status = os.waitpid(pid, 0)
        rc = status
    except ChildProcessError:
        pass

    data = bytes(data)
    if b"load averages" not in data or b"COMMAND" not in data:
        print(f"FAIL: interactive top header missing — got {data[:200]!r}")
        sys.exit(1)
    # The defining property of "top mode": full-screen cursor addressing
    # (ESC[<row>;<col>H). A dumb/batch fallback never emits these.
    if not re.search(rb"\x1b\[\d+;\d+H", data):
        print("FAIL: top did not enter full-screen mode "
              "(no cursor addressing) — smart_terminal off?")
        print(f"  raw: {data[:300]!r}")
        sys.exit(1)
    if b"\x1b[2J" not in data and b"\x1b[J" not in data and b"\x1b[H" not in data:
        print(f"FAIL: top never cleared/repositioned the screen — {data[:300]!r}")
        sys.exit(1)
    print("PASS(interactive): entered full-screen top mode, refreshed, quit on 'q'")


def main():
    yos, libexec = get_paths()
    check_batch(yos, libexec)
    check_interactive(yos, libexec)
    sys.exit(0)


if __name__ == "__main__":
    main()
