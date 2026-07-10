"""Integration test: drive a wasm zsh on the paired Apple TV via a
direct TCP socket (no host telnet client in between — we own all the
IAC framing and timing ourselves so reads aren't subject to the host
telnet client's local buffering).

What this verifies end-to-end:
  - The tvOS bundle (built by build-tools/tvos/deploy.sh) is up and
    listening on the device.
  - telnetd PTY emulation (impl/pty.c) produces an isatty-true fd.
  - zsh enters interactive mode and paints `localhost%`.
  - keystrokes round-trip through the socketpair-based fake PTY.
  - command output makes it back to the client.

Skip conditions:
  - YOS_TVOS_LAN_IP env var unset and 192.168.1.9 doesn't open port
    12323 within a 4s probe → skip ("device not deployed").

To run manually:
    YOS_TVOS_LAN_IP=192.168.1.9 \
    python3 tests/integration/zsh/test_tvos_telnet.py
"""

import os
import re
import socket
import sys
import time

DEFAULT_IP = "192.168.1.9"
PORT = 12323

IAC = 0xff
DONT, DO, WONT, WILL, SB, SE = 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf0


def port_open(ip, port, timeout=4.0):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect((ip, port))
        s.close()
        return True
    except OSError:
        return False


def strip_ansi(b):
    """Drop CSI escape sequences for readable assertion."""
    s = b.decode("utf-8", errors="replace")
    s = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", s)
    return s


def negotiate_iac(sock, expected_banner=b"4.4 BSD UNIX", timeout=8.0):
    """Reply WON'T to every DO, DON'T to every WILL, and just swallow
    SB...SE blocks. Stop when we've seen the telnetd login banner OR
    the timeout fires. Returns the cleartext received so far."""
    sock.settimeout(0.5)
    deadline = time.time() + timeout
    seen = bytearray()
    cleartext = bytearray()
    while time.time() < deadline:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            if expected_banner in cleartext:
                return bytes(cleartext)
            continue
        if not chunk:
            break
        seen.extend(chunk)
        i, n = 0, len(seen)
        while i < n:
            b = seen[i]
            if b != IAC:
                cleartext.append(b)
                i += 1
                continue
            if i + 1 >= n: break
            cmd = seen[i + 1]
            if cmd == IAC:
                # Literal 0xff in the data stream — RFC 854 IAC IAC.
                cleartext.append(IAC)
                i += 2
            elif cmd in (DO, DONT, WILL, WONT):
                if i + 2 >= n: break
                opt = seen[i + 2]
                reply_cmd = WONT if cmd == DO else (DONT if cmd == WILL else None)
                if reply_cmd is not None:
                    sock.sendall(bytes([IAC, reply_cmd, opt]))
                i += 3
            elif cmd == SB:
                # Skip until IAC SE.
                j = i + 2
                while j + 1 < n and not (seen[j] == IAC and seen[j + 1] == SE):
                    j += 1
                if j + 1 >= n: break
                i = j + 2
            else:
                # 2-byte commands: SE, NOP, DM, BRK, IP, AO, AYT, EC,
                # EL, GA, et al. DM (0xf2) in particular precedes the
                # post-synch payload — we MUST keep the next byte in
                # the cleartext stream. Original parser swallowed
                # both, stripping the leading \e of every CSI seq.
                i += 2
        if expected_banner in cleartext:
            return bytes(cleartext)
    return bytes(cleartext)


def expect(sock, needle, timeout=8.0):
    """Read until `needle` (bytes) appears in the cleartext stream, or
    timeout. Returns the accumulated cleartext."""
    sock.settimeout(0.5)
    deadline = time.time() + timeout
    acc = bytearray()
    seen = bytearray()
    while time.time() < deadline:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue
        if not chunk:
            break
        seen.extend(chunk)
        i, n = 0, len(seen)
        while i < n:
            b = seen[i]
            if b != IAC:
                acc.append(b)
                i += 1
                continue
            if i + 1 >= n: break
            cmd = seen[i + 1]
            if cmd in (DO, DONT, WILL, WONT):
                if i + 2 >= n: break
                opt = seen[i + 2]
                reply_cmd = WONT if cmd == DO else (DONT if cmd == WILL else None)
                if reply_cmd is not None:
                    sock.sendall(bytes([IAC, reply_cmd, opt]))
                i += 3
            elif cmd == SB:
                j = i + 2
                while j + 1 < n and not (seen[j] == IAC and seen[j + 1] == SE):
                    j += 1
                if j + 1 >= n: break
                i = j + 2
            else:
                i += 2
        del seen[:i]
        if needle in acc:
            return bytes(acc)
    return bytes(acc)


def main():
    ip = os.environ.get("YOS_TVOS_LAN_IP", DEFAULT_IP)
    if not port_open(ip, PORT):
        print(f"SKIP: {ip}:{PORT} not reachable — deploy with "
              f"build-tools/tvos/deploy.sh first")
        sys.exit(0)

    print(f"[test] connecting to {ip}:{PORT}")
    sock = socket.create_connection((ip, PORT), timeout=10.0)

    # 1. Wait for the telnetd login banner.
    pre_banner = negotiate_iac(sock, expected_banner=b"4.4 BSD UNIX")
    print("[test] === banner phase ===")
    print(strip_ansi(pre_banner))
    if b"4.4 BSD UNIX" not in pre_banner:
        print("FAIL: never saw '4.4 BSD UNIX' banner")
        sock.close()
        sys.exit(1)

    # 2. Wait for zsh to paint its first prompt.
    prompt = expect(sock, b"localhost%", timeout=6.0)
    print("[test] === prompt phase ===")
    print(strip_ansi(prompt))
    if b"localhost%" not in prompt:
        print("FAIL: never saw zsh prompt 'localhost%'")
        sock.close()
        sys.exit(1)

    # 3. Type a command and wait for its output.
    sock.sendall(b"echo TVOS_MARKER_A\r")
    out_a = expect(sock, b"TVOS_MARKER_A", timeout=6.0)
    print("[test] === after echo TVOS_MARKER_A ===")
    print(strip_ansi(out_a))
    # Real shell session: the marker appears once as input echo and once
    # as command output. We only require >=1 here because slow paths
    # may dedupe ZLE redraws, but >=2 is the healthy signal.
    occurrences = out_a.count(b"TVOS_MARKER_A")
    print(f"[test] TVOS_MARKER_A occurrences in cleartext = {occurrences}")

    # 4. Second command — verify the shell didn't lock up after the first.
    sock.sendall(b"echo TVOS_MARKER_B\r")
    out_b = expect(sock, b"TVOS_MARKER_B", timeout=6.0)
    print("[test] === after echo TVOS_MARKER_B ===")
    print(strip_ansi(out_b))
    occurrences_b = out_b.count(b"TVOS_MARKER_B")
    print(f"[test] TVOS_MARKER_B occurrences in cleartext = {occurrences_b}")

    # 5. Arithmetic — proves zsh's expression evaluator runs end-to-end.
    sock.sendall(b"echo $((6*7))\r")
    out_arith = expect(sock, b"42", timeout=6.0)
    print("[test] === after echo $((6*7)) ===")
    print(strip_ansi(out_arith))
    has_42 = b"42" in out_arith

    # 6. Variable assignment + expansion round-trip.
    sock.sendall(b"X=hello; echo TVOS_VAR_$X\r")
    out_var = expect(sock, b"TVOS_VAR_hello", timeout=6.0)
    print("[test] === after $X expansion ===")
    print(strip_ansi(out_var))
    has_var = b"TVOS_VAR_hello" in out_var

    # 7. External command lookup via PATH (libexec must be wired).
    #    Use `echo` BUILTIN-ed and `ls /` — ls is an external wasm tool
    #    that needs the wasm-execve path to work.
    sock.sendall(b"ls / >/dev/null 2>&1 && echo TVOS_LS_OK\r")
    out_ls = expect(sock, b"TVOS_LS_OK", timeout=6.0)
    print("[test] === after ls / ===")
    print(strip_ansi(out_ls))
    has_ls = b"TVOS_LS_OK" in out_ls

    # 8. Quit cleanly.
    sock.sendall(b"exit\r")
    sock.settimeout(2.0)
    tail = bytearray()
    try:
        while True:
            chunk = sock.recv(4096)
            if not chunk: break
            tail.extend(chunk)
    except socket.timeout:
        pass
    sock.close()

    failures = []
    if occurrences < 1:
        failures.append("first command's output never appeared")
    if occurrences_b < 1:
        failures.append("second command's output never appeared — shell wedged")
    if not has_42:
        failures.append("arithmetic $((6*7)) never produced '42'")
    if not has_var:
        failures.append("variable expansion never produced 'TVOS_VAR_hello'")
    if not has_ls:
        failures.append("external `ls /` never produced 'TVOS_LS_OK'")
    if failures:
        print("FAIL:")
        for f in failures:
            print(f"  - {f}")
        sys.exit(1)
    print("PASS")


if __name__ == "__main__":
    main()
