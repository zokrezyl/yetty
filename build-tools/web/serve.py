#!/usr/bin/env python3
"""
Simple HTTP server for yetty web demo.
Serves static files like GitHub Pages (query params ignored), plus the
dynamic endpoints the yos session mode needs (lifted from
build-tools/web/yos/serve.py):

  /engine/<path>     the yos browser engine (yos_proc.mjs & friends),
                     with fallbacks so the untracked lua/liblua.wasm is
                     found in a sibling yos checkout too
  /tools/list.json   JSON array of every wasm tool in the yos umbrella
  /tools/<name>.wasm that tool's binary (result/libexec/<name> — a nix
                     store symlink; followed when read, never realpath'd)
  /fs/pack.bin       the guest /usr/share tree as one YFS1 blob

Resolution (override via environment): YOS_RESULT, YOS_ENGINE_DIR.
Without a yos checkout these endpoints answer 503 and every other mode
works as before.
"""

import http.server
import json
import socketserver
import struct
import sys
import os
from functools import partial
from pathlib import Path

# Resolved from the script location at import time (before main() chdirs).
# The script runs from two places: build-tools/web/ (repo root two levels
# up) or a staged copy in the build dir (repo root one level up). Probe
# both, plus a sibling `yos` checkout next to the repo root.
SCRIPT_DIR = Path(__file__).resolve().parent

PACK_MAGIC = b"YFS1"


def _first_existing_dir(candidates):
    for candidate in candidates:
        if candidate and Path(candidate).is_dir():
            return Path(candidate)
    return None


def _yos_root_candidates():
    roots = []
    for base in (SCRIPT_DIR.parent, SCRIPT_DIR.parent.parent,
                 SCRIPT_DIR.parent.parent.parent):
        for root in (base / "yos", base.parent / "yos"):
            if root not in roots:
                roots.append(root)
    return roots


def resolve_yos_result():
    env_dir = os.environ.get("YOS_RESULT")
    return _first_existing_dir(
        [env_dir] + [root / "result" for root in _yos_root_candidates()])


def resolve_engine_dirs():
    """Primary engine dir plus fallbacks for untracked build artifacts
    (lua/liblua.wasm lives only where build-liblua.sh was run)."""
    env_dir = os.environ.get("YOS_ENGINE_DIR")
    dirs = []
    for candidate in ([env_dir] +
                      [root / "src" / "yos" / "platform" / "web"
                       for root in _yos_root_candidates()]):
        if candidate and Path(candidate).is_dir():
            dirs.append(Path(candidate))
    return dirs


YOS_RESULT_DIR = resolve_yos_result()
YOS_ENGINE_DIRS = resolve_engine_dirs()

yos_pack_cache = None


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


class CORSRequestHandler(http.server.SimpleHTTPRequestHandler):
    """HTTP handler with CORS headers and proper WASM MIME type."""

    def end_headers(self):
        # Add CORS headers for cross-origin isolation (required for SharedArrayBuffer)
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')

        # Cache control for development
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')

        super().end_headers()

    def guess_type(self, path):
        """Override to ensure correct MIME types."""
        mimetype = super().guess_type(path)

        if path.endswith('.wasm'):
            return 'application/wasm'
        # .mjs must be a JS MIME or browsers refuse the module import.
        if path.endswith('.js') or path.endswith('.mjs'):
            return 'application/javascript'

        return mimetype

    def do_OPTIONS(self):
        """Handle CORS preflight requests."""
        self.send_response(200)
        self.end_headers()

    # ---- yos dynamic endpoints ------------------------------------

    def send_dynamic(self, body, content_type):
        self.send_response(200)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_dynamic_error(self, code, message):
        body = message.encode()
        self.send_response(code)
        self.send_header('Content-Type', 'text/plain; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_dynamic_file(self, path, content_type):
        try:
            body = path.read_bytes()
        except OSError:
            return self.send_dynamic_error(404, 'not found: ' + str(path))
        self.send_dynamic(body, content_type)

    def do_GET(self):
        global yos_pack_cache
        url = self.path.split('?', 1)[0]

        if url.startswith('/engine/'):
            relative = url[len('/engine/'):]
            for engine_dir in YOS_ENGINE_DIRS:
                target = safe_child(engine_dir, relative)
                if target and target.is_file():
                    return self.send_dynamic_file(
                        target, self.guess_type(str(target)))
            return self.send_dynamic_error(
                404 if YOS_ENGINE_DIRS else 503,
                'yos engine file not found: ' + relative)

        if url == '/tools/list.json':
            if not YOS_RESULT_DIR:
                return self.send_dynamic_error(
                    503, 'yos result not found - run `nix build .#all` '
                         'in the yos tree (or set YOS_RESULT)')
            names = sorted(entry.name for entry
                           in (YOS_RESULT_DIR / 'libexec').iterdir()
                           if entry.is_file())
            return self.send_dynamic(json.dumps(names).encode(),
                                     'application/json')

        if url.startswith('/tools/') and url.endswith('.wasm'):
            if not YOS_RESULT_DIR:
                return self.send_dynamic_error(503, 'yos result not found')
            tool_name = os.path.basename(url)[:-len('.wasm')]
            target = safe_child(YOS_RESULT_DIR / 'libexec', tool_name)
            if not target or not target.is_file():
                return self.send_dynamic_error(404, 'no such tool: ' + tool_name)
            return self.send_dynamic(target.read_bytes(), 'application/wasm')

        if url == '/fs/pack.bin':
            if not YOS_RESULT_DIR:
                return self.send_dynamic_error(503, 'yos result not found')
            if yos_pack_cache is None:
                yos_pack_cache = build_share_pack(YOS_RESULT_DIR / 'share')
            return self.send_dynamic(yos_pack_cache,
                                     'application/octet-stream')

        return super().do_GET()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    # Resolve BEFORE chdir — a relative directory would otherwise be
    # applied twice (chdir + the handler's own directory join).
    directory = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else '.')

    os.chdir(directory)

    handler = partial(CORSRequestHandler, directory=directory)

    # SO_REUSEADDR — without this, restarting the test within the OS's
    # TIME_WAIT window (~60s) fails with "Address already in use".
    socketserver.TCPServer.allow_reuse_address = True

    with socketserver.TCPServer(("0.0.0.0", port), handler) as httpd:
        print(f"\n  yetty web demo server")
        print(f"  Serving at: http://localhost:{port}/")
        print(f"  Directory:  {os.path.abspath(directory)}")
        print(f"  yos result: {YOS_RESULT_DIR or 'MISSING (yos mode disabled)'}")
        print(f"  yos engine: "
              f"{', '.join(str(d) for d in YOS_ENGINE_DIRS) or 'MISSING'}")
        print(f"\n  Press Ctrl+C to stop.\n")

        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n  Server stopped.")
            sys.exit(0)


if __name__ == "__main__":
    main()
