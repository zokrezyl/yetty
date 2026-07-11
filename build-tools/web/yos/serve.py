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
  /yfs/<path>           -> the lazy web filesystem (docs/yfs.md),
                           generated on first request from the live
                           nix umbrella + guest tools + demos — the
                           same tree make-yos-web-bundle.py packs

Directory resolution (override via environment):
  YETTY_WEBASM_BUILD  webasm build dir holding yetty.js/.wasm/.data
  YOS_RESULT          the yos nix umbrella result (libexec/ + share/)
  YOS_ENGINE_DIR      the yos browser engine sources

Usage:  ./serve.py [port] [bind-address]
        (default port 8100; binds 0.0.0.0 so the page is reachable
         over the LAN, e.g. http://192.168.1.10:<port>/)
"""

import http.server
import importlib.util
import os
import ssl
import subprocess
import sys
import tempfile
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

# yetty client tools (ycat, yecho) built as yos guests by
# build-tools/yos/build-guest-tools.sh — placed into /bin alongside the
# umbrella tool set, same as make-yos-web-bundle.py packs them.
GUEST_TOOLS_DIR = Path(os.environ.get("YOS_GUEST_TOOLS",
                                      REPO_ROOT / "build-yos-guest"))

# /yfs is generated on first request from the live inputs — by the SAME
# builder the bundle packer uses (imported from make-yos-web-bundle.py,
# so dev serving and the deployed tarball cannot drift). The tree lands
# in a temp dir and is served statically from there.
YFS_DEV_VERSION = "dev"
yfs_dev_dir = None
yfs_dev_lock = threading.Lock()


def load_bundle_module():
    script = REPO_ROOT / "build-tools" / "yos" / "make-yos-web-bundle.py"
    spec = importlib.util.spec_from_file_location("yos_web_bundle", script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def ensure_yfs_tree():
    """Generate the yfs tree once, lazily. Returns its root dir or None
    when the yos result is missing."""
    global yfs_dev_dir
    with yfs_dev_lock:
        if yfs_dev_dir:
            return yfs_dev_dir
        if not YOS_RESULT_DIR:
            return None
        bundle = load_bundle_module()
        builder = bundle.YfsBuilder()
        builder.add_tree("bin", YOS_RESULT_DIR / "libexec", force_mode=0o755)
        builder.add_tree("usr/share", YOS_RESULT_DIR / "share")
        for wasm in (sorted(GUEST_TOOLS_DIR.glob("*.wasm"))
                     if GUEST_TOOLS_DIR.is_dir() else []):
            builder.add_file(f"bin/{wasm.stem}", wasm, force_mode=0o755)
        for name, source in bundle.demo_entries():
            builder.add_file(f"usr/share/yetty/demos/{name}", source)
        staging = Path(tempfile.mkdtemp(prefix="yos-yfs-dev-"))
        files, blobs, total = builder.emit(staging, YFS_DEV_VERSION)
        sys.stderr.write(f"[serve] yfs generated: {files} files "
                         f"({total / 1e6:.1f} MB), {blobs} blobs\n")
        yfs_dev_dir = staging / "yfs"
        return yfs_dev_dir


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
        url = self.path.split("?", 1)[0]

        if url == "/":
            return self.send_file(SHOWCASE_DIR / "index.html")

        if (url in ("/yetty.js", "/yetty.wasm", "/favicon.ico")
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

        if url.startswith("/yfs/"):
            yfs_root = ensure_yfs_tree()
            if not yfs_root:
                return self.fail(503, "yos result not found - run "
                                      "`nix build .#all` in the yos tree")
            target = safe_child(yfs_root, url[len("/yfs/"):])
            if not target or not target.is_file():
                return self.fail(404, "yfs: not found: " + url)
            return self.send_file(target)

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
