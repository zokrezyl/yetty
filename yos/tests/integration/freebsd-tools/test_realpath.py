"""realpath(1): resolve a path to its canonical form.

Status: PASS. realpath(3) is one of the custom_vfs hand-written
bridges in impl/posix.c; this test guards against regression on
that path.
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    r = run(yos, libexec, "realpath", "/", expect_rc=0, timeout=5)
    out = r.stdout.decode().strip()
    if out == "/":
        print("PASS: realpath /")
        sys.exit(0)
    print(f"FAIL: realpath / -> {out!r}, want '/'; stderr={r.stderr!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
