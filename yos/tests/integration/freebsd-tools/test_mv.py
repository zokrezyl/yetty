"""mv(1): rename a file in the same directory.

Status: PASS. Same-dir rename hits the rename(2) bridge; we
verify by checking host filesystem state.
"""
import os
import sys
import tempfile
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    payload = b"yos mv test\n"
    with tempfile.TemporaryDirectory(prefix="yos-mvtest-") as base:
        src = os.path.join(base, "old.txt")
        dst = os.path.join(base, "new.txt")
        with open(src, "wb") as f:
            f.write(payload)

        r = run(yos, libexec, "mv", src, dst, expect_rc=0, timeout=5)
        if os.path.exists(src):
            print(f"FAIL: mv: source {src} still present after rc={r.returncode}")
            sys.exit(1)
        if not os.path.exists(dst):
            print(f"FAIL: mv: destination {dst} missing; stderr={r.stderr!r}")
            sys.exit(1)
        with open(dst, "rb") as f:
            if f.read() != payload:
                print(f"FAIL: mv: dst content changed")
                sys.exit(1)
    print("PASS: mv same-dir rename")
    sys.exit(0)


if __name__ == "__main__":
    main()
