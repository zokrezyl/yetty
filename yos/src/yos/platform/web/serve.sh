#!/usr/bin/env bash
# Serve the browser runtime. The COOP/COEP headers are not needed for the
# single-threaded slice, but are sent anyway because guest threads (Workers +
# SharedArrayBuffer) require cross-origin isolation.
#
# Tools are served DYNAMICALLY straight from the nix bin directory
# (result/libexec) — no hardcoded tool list, no symlink/manifest step. The
# server exposes:
#   GET /tools/list.json        → JSON array of every tool in the bin dir
#   GET /tools/<name>.wasm       → that tool's wasm, read from the bin dir
# so the browser shell has exactly the same command set as desktop, and any
# tool added to result/libexec shows up on the next page load with no code
# change. Override the bin dir with YOS_BIN_DIR.
set -euo pipefail
cd "$(dirname "$0")"
[ -f guest.wasm ] || ./build.sh
# Build liblua.wasm (the Lua C API for nvim) if missing — served at ./lua/.
[ -f lua/liblua.wasm ] || ./lua/build-liblua.sh || echo "warn: liblua build failed; nvim will lack Lua" >&2

BIN_DIR=${YOS_BIN_DIR:-"$(cd ../../../.. && pwd)/result/libexec"}
SHARE_DIR=${YOS_SHARE_DIR:-"$(cd ../../../.. && pwd)/result/share"}
PORT=${1:-8099}
echo "serving on http://127.0.0.1:${PORT}/  (tools: $BIN_DIR)  (share: $SHARE_DIR)  (Ctrl-C to stop)"
exec env PORT="$PORT" BIN_DIR="$BIN_DIR" SHARE_DIR="$SHARE_DIR" node --input-type=module -e '
import { createServer } from "node:http";
import { readFile, readdir } from "node:fs/promises";
import { extname, join, basename } from "node:path";
import { packFromDir } from "./fs_mount.mjs";
const port = Number(process.env.PORT || 8099);
const binDir = process.env.BIN_DIR;
const shareDir = process.env.SHARE_DIR;
// /fs/pack.bin — the guest /usr/share tree (nvim runtime, zsh functions) as
// ONE fetchable blob (see fs_mount.mjs). Built lazily, cached for the
// server lifetime.
let sharePack = null;
// .css MUST be served as text/css — browsers refuse to apply a stylesheet sent
// with any other MIME type, which leaves xterm.css unapplied.
const types = { ".html": "text/html", ".wasm": "application/wasm", ".mjs": "text/javascript", ".css": "text/css", ".js": "text/javascript", ".json": "application/json", ".svg": "image/svg+xml" };
const isolate = (res) => {
  res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
  res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
  // never cache — a stale cached .mjs/.wasm is the classic "I edited it but the
  // browser runs the old one" trap.
  res.setHeader("Cache-Control", "no-store, no-cache, must-revalidate");
};
createServer(async (req, res) => {
  const url = req.url.split("?")[0];
  try {
    // Dynamic tool listing straight from the bin dir.
    if (url === "/tools/list.json") {
      let names = [];
      try { names = (await readdir(binDir)).sort(); } catch {}
      isolate(res); res.setHeader("Content-Type", "application/json"); res.end(JSON.stringify(names)); return;
    }
    // Serve any /tools/<name>.wasm from the bin dir (name is the bare tool name
    // in the nix libexec; the ".wasm" suffix is the browser-side convention).
    if (url.startsWith("/tools/") && url.endsWith(".wasm")) {
      const name = basename(url).replace(/\.wasm$/, "");
      const body = await readFile(join(binDir, name));
      isolate(res); res.setHeader("Content-Type", "application/wasm"); res.end(body); return;
    }
    if (url === "/fs/pack.bin") {
      if (!sharePack) sharePack = await packFromDir(shareDir);
      isolate(res); res.setHeader("Content-Type", "application/octet-stream"); res.end(sharePack); return;
    }
    const path = "." + (url === "/" ? "/index.html" : url);
    const body = await readFile(path);
    isolate(res); res.setHeader("Content-Type", types[extname(path)] || "application/octet-stream"); res.end(body);
  } catch {
    res.statusCode = 404;
    res.end("not found");
  }
}).listen(port, "127.0.0.1");
'
