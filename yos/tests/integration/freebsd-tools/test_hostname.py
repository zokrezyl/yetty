"""hostname(1): print host's name.

Status: PASS. Exercises uname/gethostname bridge.
"""
import sys
import socket
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    r = run(yos, libexec, "hostname", expect_rc=0, timeout=5)
    out = r.stdout.decode().strip()
    host = socket.gethostname()
    if out and out == host:
        print(f"PASS: hostname → {out!r}")
        sys.exit(0)
    if out:
        # FreeBSD hostname(1) trims to first dot — accept either form.
        if out == host.split(".")[0]:
            print(f"PASS: hostname → {out!r} (short form of {host!r})")
            sys.exit(0)
    print(f"FAIL: hostname={out!r} host={host!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
