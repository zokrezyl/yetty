"""telnet(1): on the yos libc socket surface.

Status: PASS (loopback connect). telnet is built from contrib/telnet
WITHOUT OpenSSL/Kerberos/IPSEC (all #ifdef-gated), so it needs no
mp/crypto/pam/krb5/roken/ipsec — just libc + sockets (bridged) +
libtelnet's genget.c/misc.c + the sysroot termcap stub.

A full interactive session isn't scriptable offline, so this test
proves the socket path: telnet connects to a host loopback server
THROUGH yos and prints its connection banner ("Connected to ...").
That banner only appears after socket()/connect() succeed.
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
    tel = os.path.join(libexec, "telnet")
    if not os.path.exists(tel):
        print("SKIP: telnet not in libexec")
        sys.exit(0)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    port = srv.getsockname()[1]

    def serve():
        try:
            c, _ = srv.accept()
            time.sleep(0.3)
            c.close()
        except Exception:  # noqa: BLE001
            pass

    threading.Thread(target=serve, daemon=True).start()
    time.sleep(0.2)

    r = subprocess.run([yos, tel, "127.0.0.1", str(port)],
                       input=b"", capture_output=True, timeout=20)
    out = r.stdout.decode(errors="replace")

    # The connect banner is proof the socket connect() succeeded.
    if "Connected to" not in out:
        print(f"FAIL: telnet did not report a connection — stdout={out!r} "
              f"stderr={r.stderr!r}")
        sys.exit(1)
    print(f"PASS: telnet connected over a loopback socket: "
          f"{out.strip().splitlines()[-1]!r}")
    sys.exit(0)


if __name__ == "__main__":
    main()
