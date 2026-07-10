"""uuidgen(1): generate a v4 UUID.

Status: XFAIL today. Traps with `env.uuidgen unresolved import` —
the host doesn't expose a uuidgen syscall analogue. Fix path:
either bridge to libuuid on the host, or generate v4 entirely
inside the guest using arc4random_buf (already bridged) and skip
the syscall. The FreeBSD utility ships both paths; we need to
prefer the userspace one.
"""
import re
import sys
from run_tool import get_paths, run


_UUID_RE = re.compile(
    r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"
)


def main():
    yos, libexec = get_paths()
    r = run(yos, libexec, "uuidgen", expect_rc=None, timeout=5)
    out = r.stdout.decode().strip()
    if r.returncode == 0 and _UUID_RE.match(out):
        print(f"PASS: uuidgen → {out}")
        sys.exit(0)
    if b"unresolved import env.uuidgen" in r.stderr:
        print("XFAIL: uuidgen needs host bridge or userspace v4 path")
        sys.exit(1)
    print(f"FAIL: uuidgen unexpected: rc={r.returncode}, "
          f"stdout={r.stdout!r}, stderr={r.stderr!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
