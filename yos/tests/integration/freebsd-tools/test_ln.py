"""ln(1) -s: create a symbolic link.

Status: PASS. Exercises symlink(2) bridge; we verify via
os.readlink that the link target is what we asked for.
"""
import os
import sys
import tempfile
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    with tempfile.TemporaryDirectory(prefix="yos-lntest-") as base:
        target = "real-target"  # relative target — symlink resolution test
        link = os.path.join(base, "the-link")

        r = run(yos, libexec, "ln", "-s", target, link,
                expect_rc=0, timeout=5)
        if not os.path.islink(link):
            print(f"FAIL: ln -s rc={r.returncode}: link not created "
                  f"stderr={r.stderr!r}")
            sys.exit(1)
        got = os.readlink(link)
        if got != target:
            print(f"FAIL: ln -s: readlink={got!r} want={target!r}")
            sys.exit(1)
    print("PASS: ln -s")
    sys.exit(0)


if __name__ == "__main__":
    main()
