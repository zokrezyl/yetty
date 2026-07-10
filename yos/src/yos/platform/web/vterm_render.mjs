// libvterm-backed rendering oracle for the browser-runtime tests (issue #21).
//
// The browser engine (yos_proc.mjs) feeds a raw terminal byte stream to xterm.js
// via onOutput(). To ASSESS whether that stream renders correctly — independent
// of xterm.js — we run the exact same bytes through libvterm (a second,
// independent terminal emulator) and read back the final screen grid. The grid
// is authoritative: if it's wrong, the guest/engine produced bad output; if it's
// right but the page looks wrong, the problem is in xterm.js/CSS.
//
// NOTE on the tool: libvterm's bundled `unterm` prints blank cells as
// zero-width, which collapses columns and makes layout/cursor positions
// unmeasurable (a glyph cursor-addressed to col 42 looks like col 4). So we use
// our own tiny libvterm client `vterm_grid.c`, compiled straight from the
// libvterm sources, which emits a faithful rows x cols grid (a space per blank
// cell, every line padded to `cols`). The vendored libvterm tree (src/libvterm)
// is left untouched.
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { writeFileSync, existsSync, mkdtempSync, readdirSync } from "node:fs";
import { tmpdir } from "node:os";

const here = dirname(fileURLToPath(import.meta.url));
// src/yos/platform/web -> src/libvterm
const LIBVTERM = join(here, "..", "..", "..", "libvterm");
const GRID_SRC = join(here, "vterm_grid.c");
const GRID_BIN = join(LIBVTERM, "bin", "vterm_grid");

// Compile vterm_grid once if it's missing, straight from the libvterm sources
// (no libtool). Returns the path, or throws with the compile log.
export function ensureGrid() {
  if (existsSync(GRID_BIN)) return GRID_BIN;
  const srcDir = join(LIBVTERM, "src");
  const cfiles = readdirSync(srcDir).filter((f) => f.endsWith(".c")).map((f) => join(srcDir, f));
  const cc = process.env.CC || "cc";
  const args = ["-O2", "-std=c99", "-I", join(LIBVTERM, "include"), "-o", GRID_BIN, GRID_SRC, ...cfiles];
  const r = spawnSync(cc, args, { cwd: LIBVTERM, encoding: "utf8" });
  if (r.status !== 0 || !existsSync(GRID_BIN))
    throw new Error("failed to compile vterm_grid:\n" + (r.stdout || "") + (r.stderr || ""));
  return GRID_BIN;
}

// Render a terminal byte stream into the final fixed-width screen grid.
//   bytes : Uint8Array | Buffer | string (utf8) — the raw stream xterm.js saw
//   cols, rows
// Returns { text, rows } where rows is an array of exactly `rows` strings, each
// padded to `cols` columns (blank cells are spaces), and text joins them by "\n".
export function renderStreamToGrid(bytes, { cols = 80, rows = 24 } = {}) {
  ensureGrid();
  const buf = typeof bytes === "string" ? Buffer.from(bytes, "utf8") : Buffer.from(bytes);
  const dir = mkdtempSync(join(tmpdir(), "yos-vterm-"));
  const file = join(dir, "stream.term");
  writeFileSync(file, buf);
  const r = spawnSync(GRID_BIN, ["-c", String(cols), "-l", String(rows), file], { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
  if (r.status !== 0) throw new Error("vterm_grid failed: " + (r.stderr || r.status));
  const lines = r.stdout.replace(/\n$/, "").split("\n");
  while (lines.length < rows) lines.push("");
  return { text: lines.join("\n"), rows: lines.slice(0, rows) };
}

// Pretty-print a grid with a ruler + row numbers for eyeballing in test output.
export function frameGrid(rows, cols = 80) {
  const ruler = "    +" + "-".repeat(cols) + "+";
  const body = rows.map((ln, i) => `${String(i).padStart(3)} |${ln.padEnd(cols).slice(0, cols)}|`);
  return [ruler, ...body, ruler].join("\n");
}
