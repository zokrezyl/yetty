"""nc(1): OpenBSD netcat on the yos libc socket surface.

Status: PASS. nc is built from contrib/netcat verbatim with no extra
library (IPSEC/WITH_STATS left undefined), so it's plain libc + sockets
— all bridged by yos. This is a real end-to-end socket test: a host
loopback echo server is started, nc connects to it THROUGH yos
(guest socket()/connect()/poll() -> host), sends stdin, and the echoed
bytes come back on nc's stdout.

(This is also the regression guard for the wasm stack-size fix: nc's
main frame overflowed the default wasm stack, corrupting argv and
trapping in getopt_internal, until the freebsd-tools link was given a
larger -z stack-size.)
"""
import os
import socket
import subprocess
import sys
import threading
import time

from run_tool import get_paths


def main():
    yos, libexec = get_paths()
    nc = os.path.join(libexec, "nc")
    if not os.path.exists(nc):
        print("SKIP: nc not in libexec")
        sys.exit(0)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    port = srv.getsockname()[1]
    received = {}

    def serve():
        try:
            c, _ = srv.accept()
            buf = b""
            while True:
                b = c.recv(4096)
                if not b:
                    break
                buf += b
                c.sendall(b)        # echo
            received["data"] = buf
            c.close()
        except Exception as e:  # noqa: BLE001
            received["err"] = str(e)

    threading.Thread(target=serve, daemon=True).start()
    time.sleep(0.2)

    payload = b"hello over the socket\n"
    r = subprocess.run([yos, nc, "-N", "127.0.0.1", str(port)],
                       input=payload, capture_output=True, timeout=20)
    time.sleep(0.3)

    if r.returncode != 0:
        print(f"FAIL: nc rc={r.returncode} stderr={r.stderr!r}")
        sys.exit(1)
    if r.stdout != payload:
        print(f"FAIL: nc stdout {r.stdout!r} != echoed {payload!r}")
        sys.exit(1)
    if received.get("data") != payload:
        print(f"FAIL: server received {received!r}, expected {payload!r}")
        sys.exit(1)

    print("PASS: nc round-tripped data through a loopback socket")
    sys.exit(0)


if __name__ == "__main__":
    main()
