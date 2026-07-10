"""whois(1): networking tool on the yos libc socket surface.

Status: PASS (offline smoke). whois has no extra library dependency
(LIBADD is empty) — it is plain libc + sockets (socket/connect/
getaddrinfo), all of which yos already bridges (the same surface
openssh/telnetd use). This confirms the tool builds, loads, and runs
under yos and its argument handling works.

A real lookup needs outbound network to a whois server on port 43, so
this test does NOT perform a query — it runs whois with no arguments
and asserts the EX_USAGE (64) path: getopt runs, the usage string is
printed, and the process exits cleanly under yos rather than trapping
on an unresolved import.
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    # No query argument -> whois prints usage and exits EX_USAGE (64).
    r = run(yos, libexec, "whois", expect_rc=64, timeout=10)
    err = r.stderr.decode(errors="replace")
    if "usage:" not in err or "whois" not in err:
        print(f"FAIL: whois usage banner missing — stderr={r.stderr!r}")
        sys.exit(1)
    print(f"PASS: whois runs under yos (usage path): {err.splitlines()[0]!r}")
    sys.exit(0)


if __name__ == "__main__":
    main()
