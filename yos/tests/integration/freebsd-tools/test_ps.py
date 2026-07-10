"""ps(1): list yos processes.

Status: PASS. The yos build ships a small native ps that reads
yos's synthetic /proc (mounted at /proc by src/yos/main.c via
src/yos/vfs/procfs.c). FreeBSD's bin/ps uses libkvm and a kernel-
shaped proc table that yos doesn't have — porting it would mean
shipping libkvm + 3 kloc of bin/ps. Instead we ship a ~80-line
tool that opens /proc, walks numeric subdirectories, parses
/proc/<pid>/stat and prints PID/PPID/STATE/COMMAND.

This test guards both halves of the chain:

  - src/yos/impl/dir.c routes opendir("/proc") through the mount
    table so readdir returns yos PIDs (not the host's PIDs).
  - the ps binary itself parses /proc/<pid>/stat correctly.
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    r = run(yos, libexec, "ps", expect_rc=0, timeout=10)
    lines = r.stdout.decode().splitlines()
    if not lines or "PID" not in lines[0]:
        print(f"FAIL: ps header missing — stdout={r.stdout!r}")
        sys.exit(1)
    body = lines[1:]
    if not body:
        print(f"FAIL: ps printed no process rows — stdout={r.stdout!r}")
        sys.exit(1)
    # The standalone ps runs as PID 1 (the only yos process this
    # session). Verify the row is well-formed.
    first = body[0].split(None, 3)
    if len(first) < 3:
        print(f"FAIL: ps row malformed: {body[0]!r}")
        sys.exit(1)
    try:
        pid = int(first[0]); int(first[1])
    except ValueError:
        print(f"FAIL: ps row not numeric: {body[0]!r}")
        sys.exit(1)
    if pid != 1:
        print(f"FAIL: standalone ps should be pid 1, got {pid}")
        sys.exit(1)
    # Verify the COMMAND column has "ps" somewhere (the proc table
    # records comm[] from the last exec).
    if "ps" not in body[0]:
        print(f"FAIL: ps row missing comm 'ps': {body[0]!r}")
        sys.exit(1)
    print(f"PASS: ps printed {len(body)} row(s) — head: {body[0]!r}")
    sys.exit(0)


if __name__ == "__main__":
    main()
