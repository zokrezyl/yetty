// Integration test (issue #21): assess the CORRECTNESS of tmux's rendering on
// the browser/node process engine, using libvterm as the oracle.
//
// The browser engine feeds a raw terminal byte stream to xterm.js. Rather than
// regex-matching that escape-laden stream (brittle, and blind to cursor moves /
// overwrites / clears), we capture the EXACT bytes xterm.js receives and render
// them through libvterm — a second, independent terminal emulator — into a
// faithful 80x24 grid (see vterm_grid.c / vterm_render.mjs). Then we assert on
// the grid the way a human reading the screen would: the status bar is on the
// bottom row, a vertical split is a STRAIGHT divider down the centre, a
// horizontal split draws a divider row, a second window shows in the status bar.
//
// SCOPE: this drives the engine at 80x24 (cols/rows below), the size tmux also
// defaults to — so it validates the layout AT A MATCHED SIZE. It does NOT catch
// the separate bug where tmux's window fails to track a DIFFERENT terminal size
// (status bar stranded mid-screen, "two status bars" on resize); that is pinned
// by tmux_xterm_render_test.mjs in real xterm.js.
//
// Run: node tmux_render_test.mjs   (compiles the libvterm grid tool on first run)
import { runInteractive } from "./yos_proc.mjs";
import { renderStreamToGrid, frameGrid } from "./vterm_render.mjs";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const load = async (p) => WebAssembly.compile(await readFile(join(here, p)));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const COLS = 80, ROWS = 24;

const zsh = await load("zsh.wasm");
const tmux = await load("tools/tmux.wasm");
const tools = new Map([["sh", zsh], ["zsh", zsh], ["tmux", tmux]]);
for (const n of ["echo", "ls", "true", "pwd", "cat"]) { try { tools.set(n, await load(`tools/${n}.wasm`)); } catch {} }

// Capture the raw stream xterm.js would receive (the decoded onOutput text — the
// exact strings term.write() gets — re-encoded to utf8 bytes for libvterm).
let raw = "";
const ctl = runInteractive(zsh, ["zsh", "-f", "+o", "promptsp"], {
  onOutput: (fd, t) => { raw += t; },
  tools,
  env: ["PATH=/bin:/usr/bin", "HOME=/", "TERM=xterm-256color", "PWD=/", "SHELL=/bin/sh"],
  cols: COLS, rows: ROWS,
});
const type = async (keys, pauseMs = 700) => { ctl.write(keys); await sleep(pauseMs); };
const typePerKey = async (keys, perKeyMs = 45, settleMs = 900) => { for (const ch of keys) { ctl.write(ch); await sleep(perKeyMs); } await sleep(settleMs); };
const grid = () => renderStreamToGrid(raw, { cols: COLS, rows: ROWS });

let failed = 0;
const check = (name, ok, detail) => { console.error(`  ${ok ? "PASS" : "FAIL"}  ${name}${ok ? "" : "  " + (detail || "")}`); if (!ok) failed++; };

// Columns at which a vertical divider │ (U+2502) appears, per body row (rows
// 0..ROWS-2, excluding the status bar). A correct vertical split is a STRAIGHT
// line: one column, identical on every body row it spans.
const dividerCols = (rows) => {
  const cols = [];
  for (let r = 0; r < ROWS - 1; r++) { const idx = (rows[r] || "").indexOf("│"); if (idx >= 0) cols.push(idx); }
  return cols;
};
const statusRow = (rows) => rows[ROWS - 1] || "";
const show = (label, g) => { console.error(`\n=== ${label} — libvterm render (80x24) ===`); console.error(frameGrid(g.rows, COLS)); };

await sleep(300);

// ── 1) single window: status bar pinned to the bottom, command output in pane ──
await type("tmux new-session\r", 1200);
await typePerKey("echo RENDER_OK\r");
await sleep(400);
let g = grid();
show("after new-session + echo", g);
check("status bar on bottom row (window list)", /^\[\d+\]\s+\d+:/.test(statusRow(g.rows)), JSON.stringify(statusRow(g.rows).slice(0, 30)));
check("command output rendered in the pane", g.rows.some((ln) => ln.includes("RENDER_OK")));

// ── 2) vertical split: a STRAIGHT │ divider down the centre, content both sides ──
await type('\x02%', 1000);            // C-b %  : split-window -h
await typePerKey("echo LEFTPANE\r");
await sleep(400);
g = grid();
show("after C-b % (vertical split)", g);
const dv = dividerCols(g.rows);
const dvUniq = [...new Set(dv)];
check("vertical divider spans the body", dv.length >= ROWS - 2, `${dv.length} rows`);
check("vertical divider is a straight line (one column)", dvUniq.length === 1, `columns=${dvUniq.join(",")}`);
check("vertical divider is centred (~col 40)", dvUniq.length === 1 && dvUniq[0] >= 36 && dvUniq[0] <= 43, `col=${dvUniq[0]}`);
check("left pane keeps its content", g.rows.some((ln) => ln.slice(0, dvUniq[0] || 40).includes("RENDER_OK")));
check("right pane shows new content", g.rows.some((ln) => ln.slice((dvUniq[0] || 40) + 1).includes("LEFTPANE")));

// ── 3) horizontal split: a ─ divider row, vertical divider still straight ──
await type('\x02"', 1000);            // C-b "  : split-window -v
await sleep(500);
g = grid();
show('after C-b " (horizontal split)', g);
const hbarRow = g.rows.findIndex((ln, i) => i < ROWS - 1 && /─{16,}/.test(ln));
check("horizontal split drew a divider row of ─", hbarRow >= 0, `row=${hbarRow}`);
check("vertical divider survived the horizontal split (still one column)", [...new Set(dividerCols(g.rows))].length === 1, `columns=${[...new Set(dividerCols(g.rows))].join(",")}`);

// ── 4) second window appears in the status bar ──
await type('\x02c', 1000);            // C-b c  : new-window
await typePerKey("echo WINDOW2\r");
await sleep(400);
g = grid();
show("after C-b c (new window) + echo", g);
check("status bar lists two windows", /\b0:.*\b1:/.test(statusRow(g.rows)), JSON.stringify(statusRow(g.rows).slice(0, 40)));
check("new window's command output rendered", g.rows.some((ln) => ln.includes("WINDOW2")));

// ── diagnostic (not a hard failure): the status-bar clock ──
// tmux refreshes the clock on its 15s status-interval timer, which the engine's
// deterministic virtual clock does not fast-forward, so a short scripted run can
// show the epoch. Reported for visibility, not asserted.
const clock = (statusRow(g.rows).match(/\b(\w{3} \w{3}\s+\d+ \d\d:\d\d:\d\d \d{4})\b/) || [])[1] || (statusRow(g.rows).match(/\d\d:\d\d/) || [])[0] || "(none)";
console.error(`\n  [diagnostic] status-bar clock reads: ${clock}` + (/1970/.test(clock) ? "  (epoch — time source / 15s status-interval timer, tracked separately)" : ""));

console.error(failed ? `\n${failed} FAILED` : "\nALL PASSED — tmux output renders correctly through libvterm");
process.exit(failed ? 1 : 0);
