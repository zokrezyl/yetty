#!/usr/bin/env python3
"""
Attach-level epoch-reset ordering test (#699 review #14).

End-to-end through the REAL stack: ymux daemon (private socket) -> `ymux
attach` bridge inside a launched yetty pane -> vtsink yRPC lane -> yscene
terminal grid -> GPU compositor -> screenshot readback.

Mid-session, `ymux recover` forces the daemon's slow-client recovery (queued
terminal frames dropped, vtsink epoch reset). The bridge must run its
receiver reset BEFORE the fresh epoch republishes: if the ordering broke, the
stale grid parser would consume the new epoch's redraw mid-state and the
post-recovery content would not render. The assertion is the same
deterministic color probe as the grid-readback test: a green-background run
painted AFTER the forced recovery must reach the framebuffer.

Requires a real display/GPU; SKIPS (exit 77) headlessly.
"""
import os
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
readback = __import__("ymux-grid-readback")

YETTY = readback.YETTY
YMUX = readback.YMUX
YCTL = readback.YCTL
SKIP = 77


def main():
    if not readback.ensure_display():
        readback.skip("no display available")
    for path, name in ((YETTY, "yetty"), (YMUX, "ymux"), (YCTL, "yctl")):
        if not os.path.exists(path):
            readback.skip(f"{name} not found at {path}")
    if not readback.yctl_runnable():
        readback.skip("yctl client not runnable")

    port = readback.free_port()
    socket_name = f"reset-{os.getpid()}"
    tmp = tempfile.mkdtemp(prefix="ymux-reset-")
    shot_before = os.path.join(tmp, "before.ppm")
    shot_after = os.path.join(tmp, "after.ppm")
    log_path = os.path.join(tmp, "yetty.log")
    log = open(log_path, "w")

    def ymux(*args, timeout=30):
        return subprocess.run([YMUX, "-L", socket_name, *args], timeout=timeout,
                              stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, cwd=ROOT)

    def yctl(*args, timeout=20):
        return subprocess.run([YCTL, "-p", str(port), *args], timeout=timeout,
                              stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, cwd=ROOT)

    def rpc_up():
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1):
                return True
        except OSError:
            return False

    def dump(msg):
        log.flush()
        print(f"FAIL: {msg}")
        try:
            with open(log_path) as handle:
                sys.stderr.write("\n".join(handle.read().splitlines()[-60:]) + "\n")
        except OSError:
            pass

    proc = None
    try:
        created = ymux("new", "-d", "-s", "reset", "-x", "80", "-y", "24")
        if created.returncode != 0:
            dump("ymux new-session failed")
            return 1
        proc = subprocess.Popen([YETTY, "-r", str(port)], stdout=log, stderr=log, cwd=ROOT)
        deadline = time.time() + 30
        while time.time() < deadline:
            if proc.poll() is not None:
                dump(f"yetty exited early (rc={proc.returncode})")
                return 1
            if rpc_up():
                break
            time.sleep(0.25)
        else:
            dump("RPC port never came up")
            return 1

        time.sleep(1.0)
        readback.capture_screen(yctl, shot_before)
        bridge_log = os.path.join(tmp, "bridge.log")
        env_prefix = ("YMUX_YTEST_SKIP_RECEIVER_RESET=1 "
                      if os.environ.get("YMUX_RESET_NEGATIVE_CONTROL") else "")
        yctl("run", f"{env_prefix}{YMUX} -L {socket_name} attach -t reset 2>{bridge_log}")
        time.sleep(2.0)

        # POISONED receiver state (review #15): put the pane INSIDE the
        # alternate screen with distinctive content before the reset — the
        # discarded epoch ends mid-mode, so a receiver that skipped its
        # reset would keep a stale-mode parser for the fresh redraw.
        ymux("send", "-t", "reset", "-l",
             "printf '\\033[?1049h\\033[2J\\033[HALT-SCREEN-CONTENT'")
        ymux("send", "-t", "reset", "Enter")
        time.sleep(1.0)
        # FORCE the epoch reset. The CLI now waits for the daemon ACK and
        # FAILS unless at least one attached client actually recovered —
        # an ignored/dropped request can no longer pass this test.
        recovered = subprocess.run([YMUX, "-L", socket_name, "recover"], timeout=30,
                                   capture_output=True, text=True, cwd=ROOT)
        if recovered.returncode != 0:
            dump(f"ymux recover reported no recovery: {recovered.stdout} {recovered.stderr}")
            return 1
        if "recovered 1" not in recovered.stdout:
            dump(f"unexpected recover ack: {recovered.stdout!r}")
            return 1
        time.sleep(1.0)
        # Leave the alternate screen AFTER the reset — the fresh epoch's
        # parser must track the mode change correctly.
        ymux("send", "-t", "reset", "-l", "printf '\\033[?1049l'")
        ymux("send", "-t", "reset", "Enter")
        time.sleep(1.0)

        # Content painted AFTER the reset must render: green-background run.
        # BLUE (44), not green — yetty's X11-tile path drops green (bug
        # characterized separately); blue measures ymux correctly.
        spaces = " " * 40
        ymux("send", "-t", "reset", "-l", f"printf '\\033[44m{spaces}\\033[0m\\n'")
        ymux("send", "-t", "reset", "Enter")
        time.sleep(2.0)
        readback.capture_screen(yctl, shot_after)
        for _ in range(20):
            if os.path.exists(shot_before) and os.path.exists(shot_after):
                break
            time.sleep(0.25)

        before = readback.read_ppm(shot_before)
        after = readback.read_ppm(shot_after)
        if before is None or after is None:
            dump("screenshots missing/invalid")
            return 1
        green_before = readback.hueish_pixels(before, 2)
        green_after = readback.hueish_pixels(after, 2)
        if green_before > 50:
            dump(f"baseline already greenish ({green_before}) — basis invalid")
            return 1
        # Presence-based threshold (see the note in ymux-grid-readback.py).
        if green_after < 250:
            dump(f"post-reset content did not render (before={green_before}, "
                 f"after={green_after} blueish pixels) — epoch ordering broken?")
            return 1
        # RECEIVER-EPOCH observability (review #16): the bridge stamps a
        # monotonic marker on every APPLIED receiver reset and on the first
        # fresh feed after it. The reset must have happened AND preceded the
        # first fresh feed. The framebuffer alone cannot distinguish a
        # skipped reset — these markers can (proven by the negative
        # control, which skips the reset via the test seam and FAILS here).
        try:
            with open(bridge_log) as handle:
                bridge_text = handle.read()
        except OSError:
            bridge_text = ""
        reset_at = bridge_text.find("receiver-reset applied epoch=1")
        feed_at = bridge_text.find("first feed after reset epoch=1")
        if reset_at < 0:
            dump("receiver reset was NEVER APPLIED (no epoch marker) — "
                 "the fresh epoch opened onto the stale parser")
            return 1
        if feed_at < 0:
            dump("no fresh feed followed the receiver reset")
            return 1
        if feed_at < reset_at:
            dump("fresh feed PRECEDED the receiver reset — ordering broken")
            return 1
        print(f"OK: reset ordering — reset applied before the fresh feed "
              f"(markers at {reset_at} < {feed_at}), post-recovery paint "
              f"greenish {green_before} -> {green_after}")
        try:
            yctl("shutdown")
            proc.wait(timeout=20)
        except (subprocess.TimeoutExpired, OSError, subprocess.SubprocessError):
            pass
        return 0
    finally:
        try:
            ymux("kill-server", timeout=10)
        except (OSError, subprocess.SubprocessError):
            pass
        if proc is not None and proc.poll() is None:
            proc.kill()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        log.close()


if __name__ == "__main__":
    sys.exit(main())
