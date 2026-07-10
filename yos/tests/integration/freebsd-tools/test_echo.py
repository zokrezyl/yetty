"""echo(1): write args to stdout.

Status: PASS. echo has no libc extras; trivially exercises argv
delivery + write bridge.
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    r = run(yos, libexec, "echo", "hello", "world", expect_rc=0, timeout=5)
    if r.stdout == b"hello world\n":
        print("PASS: echo hello world")
        sys.exit(0)
    print(f"FAIL: echo unexpected output {r.stdout!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
