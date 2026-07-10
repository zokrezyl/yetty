"""Shared pty driver (copy of tests/integration/nvim/run_in_pty.py).

Maintained as a duplicate rather than imported because Python's import
machinery refuses to import a module of the same name from a sibling
directory ('cannot import name X from X'). If the implementation
diverges, sync both files.
"""
import os
import pty
import fcntl
import termios
import struct
import select
import time
import signal


def run_in_pty(argv, rows=24, cols=80, driver=None, kill_after=2.0,
               env=None, setup=None):
    if driver is None:
        driver = [(3.0, b"")]
    pid, fd = pty.fork()
    if pid == 0:
        if env is not None:
            os.environ.clear()
            os.environ.update(env)
        try:
            os.execvp(argv[0], argv)
        except Exception as e:
            os.write(2, f"exec failed: {e}\n".encode())
            os._exit(127)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    if setup is not None:
        setup(fd)
    buf = bytearray()
    for wait, payload in driver:
        end = time.time() + wait
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
        if payload:
            try:
                os.write(fd, payload)
            except OSError:
                pass
    end = time.time() + kill_after
    status = None
    while time.time() < end:
        try:
            p, st = os.waitpid(pid, os.WNOHANG)
        except ChildProcessError:
            status = -1
            break
        if p:
            status = st
            break
        rs, _, _ = select.select([fd], [], [], 0.2)
        if rs:
            try:
                d = os.read(fd, 65536)
                if d:
                    buf.extend(d)
            except OSError:
                pass
    if status is None:
        try:
            os.kill(pid, signal.SIGKILL)
            _, status = os.waitpid(pid, 0)
        except ProcessLookupError:
            status = -1
    try:
        os.close(fd)
    except OSError:
        pass
    return bytes(buf), status
