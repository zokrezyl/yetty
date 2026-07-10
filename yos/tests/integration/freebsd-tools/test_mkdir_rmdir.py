"""mkdir(1) + rmdir(1): create and remove a directory.

Status: PASS. Exercises the mkdir/rmdir/access bridges and proves
the operation actually took effect on the host filesystem (yos
mounts the host fs directly, so we can verify with os.path.isdir).
"""
import os
import sys
import tempfile
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    with tempfile.TemporaryDirectory(prefix="yos-mkdirtest-") as base:
        target = os.path.join(base, "subdir")

        r = run(yos, libexec, "mkdir", target, expect_rc=0, timeout=5)
        if not os.path.isdir(target):
            print(f"FAIL: mkdir {target} rc={r.returncode} but "
                  f"host doesn't see the dir; stderr={r.stderr!r}")
            sys.exit(1)

        r = run(yos, libexec, "rmdir", target, expect_rc=0, timeout=5)
        if os.path.exists(target):
            print(f"FAIL: rmdir {target} rc={r.returncode} but "
                  f"host still sees the dir; stderr={r.stderr!r}")
            sys.exit(1)

    print("PASS: mkdir + rmdir round-trip")
    sys.exit(0)


if __name__ == "__main__":
    main()
