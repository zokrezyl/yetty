#!/usr/bin/env python3
"""Standalone dev server for the yetty + yos browser showcase.

Serves, from one origin:
  /                     -> index.html (this directory)
  /yos-iframe.html      -> the yos engine host page (this directory)
  /yetty.js|.wasm|.data -> the webasm yetty build artifacts
  /engine/<path>        -> the yos browser engine (yos_proc.mjs & friends),
                           with a fallback to a sibling yos checkout so
                           untracked build artifacts (lua/liblua.wasm) are
                           found too
  /tools/list.json      -> JSON array of every wasm tool in the yos umbrella
  /tools/<name>.wasm    -> that tool's wasm binary (result/libexec/<name>)
  /fs/pack.bin          -> the guest /usr/share tree as one YFS1 blob
                           (nvim runtime, zsh functions)

Directory resolution (override via environment):
  YETTY_WEBASM_BUILD  webasm build dir holding yetty.js/.wasm/.data
  YOS_RESULT          the yos nix umbrella result (libexec/ + share/)
  YOS_ENGINE_DIR      the yos browser engine sources

Usage:  ./serve.py [port] [bind-address]
        (default port 8100; binds 0.0.0.0 so the page is reachable
         over the LAN, e.g. http://192.168.1.10:<port>/)
"""

import http.server
import json
import os
import ssl
import struct
import subprocess
import sys
import threading
from pathlib import Path

SHOWCASE_DIR = Path(__file__).resolve().parent
REPO_ROOT = SHOWCASE_DIR.parents[2]

MIME_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript",
    ".mjs": "text/javascript",
    ".wasm": "application/wasm",
    ".json": "application/json",
    ".css": "text/css",
    ".data": "application/octet-stream",
    ".bin": "application/octet-stream",
    ".svg": "image/svg+xml",
}

PACK_MAGIC = b"YFS1"


def first_existing(candidates):
    for candidate in candidates:
        if candidate and Path(candidate).is_dir():
            return Path(candidate)
    return None


def resolve_yetty_build():
    env_dir = os.environ.get("YETTY_WEBASM_BUILD")
    names = [
        "build-webasm-ytrace-release",
        "build-webasm-yinfo-release",
        "build-webasm-ytrace-debug",
        "build-webasm-yinfo-debug",
    ]
    return first_existing([env_dir] + [REPO_ROOT / name for name in names])


def resolve_yos_result():
    env_dir = os.environ.get("YOS_RESULT")
    return first_existing(
        [env_dir, REPO_ROOT / "yos" / "result", REPO_ROOT.parent / "yos" / "result"])


def resolve_engine_dirs():
    """Primary engine dir plus fallbacks for untracked build artifacts
    (lua/liblua.wasm lives only where build-liblua.sh was run)."""
    env_dir = os.environ.get("YOS_ENGINE_DIR")
    dirs = []
    for candidate in [env_dir,
                      REPO_ROOT / "yos" / "src" / "yos" / "platform" / "web",
                      REPO_ROOT.parent / "yos" / "src" / "yos" / "platform" / "web"]:
        if candidate and Path(candidate).is_dir():
            dirs.append(Path(candidate))
    return dirs


YETTY_BUILD_DIR = resolve_yetty_build()
YOS_RESULT_DIR = resolve_yos_result()
ENGINE_DIRS = resolve_engine_dirs()

pack_cache = None


def build_share_pack(share_dir):
    """Pack a directory tree into the YFS1 blob parsePack() reads:
    magic(4) | u32le index-length | index JSON | blobs. Follows symlinks
    (nix store trees are symlink-heavy). Index offsets are relative to
    the end of the index."""
    index = []
    blobs = []
    offset = 0
    for dirpath, _dirnames, filenames in os.walk(share_dir, followlinks=True):
        for filename in sorted(filenames):
            full = Path(dirpath) / filename
            try:
                data = full.read_bytes()
            except OSError:
                continue
            relative = full.relative_to(share_dir).as_posix()
            index.append({"p": relative, "o": offset, "s": len(data)})
            blobs.append(data)
            offset += len(data)
    index_bytes = json.dumps(index, separators=(",", ":")).encode()
    return b"".join([PACK_MAGIC, struct.pack("<I", len(index_bytes)),
                     index_bytes] + blobs)


def safe_child(base_dir, relative):
    """base/relative with a lexical traversal guard. Deliberately does NOT
    resolve symlinks — the nix umbrella's libexec/<tool> entries are
    symlinks into other store paths and must be followed when read."""
    normalized = os.path.normpath(relative)
    if os.path.isabs(normalized) or normalized.startswith(".."):
        return None
    return base_dir / normalized


class ShowcaseHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def send_body(self, body, content_type):
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        # Cross-origin isolation: not required by the cooperative engine,
        # but matches the yos dev server so Worker/SharedArrayBuffer
        # experiments keep working on the same origin.
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
        self.end_headers()
        self.wfile.write(body)

    def send_file(self, path):
        try:
            body = path.read_bytes()
        except OSError:
            return self.fail(404, "not found: " + str(path))
        content_type = MIME_TYPES.get(path.suffix, "application/octet-stream")
        self.send_body(body, content_type)

    def fail(self, code, message):
        body = message.encode()
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        global pack_cache
        url = self.path.split("?", 1)[0]

        if url == "/":
            return self.send_file(SHOWCASE_DIR / "index.html")

        if (url in ("/yetty.js", "/yetty.wasm", "/yetty.data", "/favicon.ico")
                or url.startswith("/yetty-assets/")):
            if not YETTY_BUILD_DIR:
                return self.fail(503, "webasm yetty build not found - run "
                                      "`make build-webasm-ytrace-release` first")
            target = safe_child(YETTY_BUILD_DIR, url.lstrip("/"))
            if not target or not target.is_file():
                return self.fail(404, "not found in yetty build: " + url)
            return self.send_file(target)

        if url.startswith("/engine/"):
            relative = url[len("/engine/"):]
            for engine_dir in ENGINE_DIRS:
                target = safe_child(engine_dir, relative)
                if target and target.is_file():
                    return self.send_file(target)
            return self.fail(404, "engine file not found: " + relative)

        if url == "/tools/list.json":
            if not YOS_RESULT_DIR:
                return self.fail(503, "yos result not found - run "
                                      "`nix build .#all` in the yos tree")
            names = sorted(entry.name for entry
                           in (YOS_RESULT_DIR / "libexec").iterdir()
                           if entry.is_file())
            return self.send_body(json.dumps(names).encode(), "application/json")

        if url.startswith("/tools/") and url.endswith(".wasm"):
            if not YOS_RESULT_DIR:
                return self.fail(503, "yos result not found")
            tool_name = os.path.basename(url)[:-len(".wasm")]
            target = safe_child(YOS_RESULT_DIR / "libexec", tool_name)
            if not target or not target.is_file():
                return self.fail(404, "no such tool: " + tool_name)
            return self.send_body(target.read_bytes(), "application/wasm")

        if url == "/fs/pack.bin":
            if not YOS_RESULT_DIR:
                return self.fail(503, "yos result not found")
            if pack_cache is None:
                pack_cache = build_share_pack(YOS_RESULT_DIR / "share")
            return self.send_body(pack_cache, "application/octet-stream")

        # Anything else: a static file from the showcase directory
        # (selftest.html, future assets).
        target = safe_child(SHOWCASE_DIR, url.lstrip("/"))
        if target and target.is_file():
            return self.send_file(target)

        return self.fail(404, "not found")

    def log_message(self, fmt, *args):
        sys.stderr.write("[serve] %s\n" % (fmt % args))


def ensure_tls_cert():
    """Self-signed cert for the HTTPS listener. WebGPU only exists on
    secure origins — http://<lan-ip> is NOT one (only localhost is
    exempt), so LAN access needs https. Generated once into tmp/."""
    cert_dir = REPO_ROOT / "tmp"
    cert_dir.mkdir(exist_ok=True)
    cert = cert_dir / "yos-serve-cert.pem"
    key = cert_dir / "yos-serve-key.pem"
    if cert.is_file() and key.is_file():
        return cert, key
    result = subprocess.run(
        ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
         "-days", "3650", "-keyout", str(key), "-out", str(cert),
         "-subj", "/CN=192.168.1.10",
         "-addext", "subjectAltName=IP:192.168.1.10,IP:127.0.0.1,DNS:localhost"],
        capture_output=True)
    if result.returncode != 0:
        print("openssl cert generation failed: %s" %
              result.stderr.decode(errors="replace")[-300:], flush=True)
        return None, None
    return cert, key


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8100
    bind_address = sys.argv[2] if len(sys.argv) > 2 else "0.0.0.0"
    https_port = port + 1
    print("yetty + yos showcase server", flush=True)
    print("  showcase dir : %s" % SHOWCASE_DIR, flush=True)
    print("  yetty build  : %s" % (YETTY_BUILD_DIR or
                                   "MISSING - make build-webasm-ytrace-release"),
          flush=True)
    print("  yos result   : %s" % (YOS_RESULT_DIR or
                                   "MISSING - nix build .#all in the yos tree"),
          flush=True)
    print("  engine dirs  : %s" % ", ".join(str(d) for d in ENGINE_DIRS),
          flush=True)
    cert, key = ensure_tls_cert()
    if cert:
        https_server = http.server.ThreadingHTTPServer((bind_address, https_port),
                                                       ShowcaseHandler)
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(certfile=str(cert), keyfile=str(key))
        https_server.socket = tls.wrap_socket(https_server.socket, server_side=True)
        threading.Thread(target=https_server.serve_forever, daemon=True).start()
        print("serving on https://192.168.1.10:%d/  <- USE THIS ONE for WebGPU "
              "(self-signed: accept the browser warning once)" % https_port,
              flush=True)
    print("serving on http://%s:%d/   (WebGPU works here only via "
          "http://localhost:%d/)  (Ctrl-C to stop)" % (bind_address, port, port),
          flush=True)
    server = http.server.ThreadingHTTPServer((bind_address, port), ShowcaseHandler)
    server.serve_forever()


if __name__ == "__main__":
    main()
