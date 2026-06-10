#!/usr/bin/env python3
"""
Run real sites through ybrowser (interactive, so console output is emitted) and
assert there are NO JavaScript errors/exceptions. The harness that catches
regressions like jQuery failing on Wikipedia.

  check-sites-js.py            # all sites
  check-sites-js.py wikipedia  # one

Env: YBROWSER=<path>, SECONDS=<boot wait>. Returns non-zero if any site has
unexpected JS errors.

CAVEAT — this is a best-effort live-site smoke test, NOT a deterministic guard.
Some errors only surface from DEFERRED modules (e.g. Wikipedia's
ext.visualEditor.desktopArticleTarget.init, loaded via idle/timer callbacks)
that may not fire within the boot window in a headless run. A short window
(the old 7s default) terminated before those modules ran and FALSELY reported
clean. The window is now 15s; raise SECONDS to widen it. For a root cause you
can pin reliably, add a focused fixture under test/ybrowser/wpt/ (e.g.
22-url/url-conformance.html pins the URL bug behind the VE crash) — that runs
on every suite invocation and cannot flake.
"""
import os, subprocess, sys, time, re

YB = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")
SECS = int(os.environ.get("SECONDS", "15"))
SITES = {
    "wikipedia": "https://en.wikipedia.org/wiki/Cat",
    "cnn": "https://www.cnn.com/",
    "gnews": "https://news.google.com/",
    "github": "https://github.com/",
}
# benign messages that are not real JS errors (info logs, expected env gaps)
BENIGN = [
    "using default (us)", "unable to determine country",
    "libvulkan", "maxdynamic", "wl_display", "loader_icd",
]
ERR_RE = re.compile(r"js:error|js:warn|Exception in|Uncaught|TypeError|ReferenceError|"
                    r"is not a function|cannot read|cannot write|not defined|invalid brand",
                    re.I)


def run(name, url):
    env = {**os.environ, "DISPLAY": os.environ.get("DISPLAY", ":1.0")}
    for v in ("TERM_PROGRAM", "TMUX"):
        env.pop(v, None)
    p = subprocess.Popen([YB, "--interactive", "--no-ui", url],
                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env)
    time.sleep(SECS)
    p.terminate()
    try:
        _, err = p.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        p.kill(); _, err = p.communicate()
    lines = err.decode("utf-8", "replace").splitlines()
    errs = []
    for ln in lines:
        if not ERR_RE.search(ln):
            continue
        if any(b in ln.lower() for b in BENIGN):
            continue
        errs.append(ln.strip())
    return errs


def main():
    which = sys.argv[1:] or list(SITES)
    total = 0
    for name in which:
        url = SITES.get(name, name)
        errs = run(name, url)
        uniq = sorted(set(errs))
        status = "OK" if not uniq else f"{len(uniq)} ERROR(S)"
        print(f"=== {name:10s} {status}")
        for e in uniq[:25]:
            print(f"    {e}")
        total += len(uniq)
    print(f"\n{'ALL CLEAN' if total == 0 else str(total) + ' total JS errors'}")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
