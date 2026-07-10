"""rm(1): remove a regular file.

Status: PASS. Exercises unlink bridge end-to-end (filesystem state
verified host-side).
"""
import os
import sys
import tempfile
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    with tempfile.TemporaryDirectory(prefix="yos-rmtest-") as base:
        victim = os.path.join(base, "doomed.txt")
        with open(victim, "w") as f:
            f.write("delete me")

        r = run(yos, libexec, "rm", victim, expect_rc=0, timeout=5)
        if os.path.exists(victim):
            print(f"FAIL: rm rc={r.returncode} but file still present "
                  f"stderr={r.stderr!r}")
            sys.exit(1)
    print("PASS: rm removed file")
    sys.exit(0)


if __name__ == "__main__":
    main()
