// Regression test (issue #21): terminal output newline cooking (termios
// OPOST|ONLCR) on the browser/node process engine.
//
// Programs that print bare LF to a tty (ls -1, ps, ordinary stdout) rely on the
// tty's output post-processing to map "\n" -> "\r\n". The engine tracked only
// c_lflag (input flags) and ignored c_oflag, so bare LF reached the terminal
// unchanged and every line "staircased" — continuing from the previous line's
// last column instead of starting at column 0. Fixed by tracking c_oflag and
// cooking LF->CRLF on cooked-mode terminal output (cookTtyOutput in yos_proc).
//
// A correct terminal emulator (libvterm) staircases on bare LF exactly as xterm
// does, so we assert on the libvterm-rendered grid: each output line must start
// at column 0. (Raw-mode apps that clear OPOST — tmux/vim — must NOT be cooked;
// that non-regression is covered by tmux_render_test.mjs / tmux_from_zsh_test.)
//
// Run: node onlcr_test.mjs
import { runInteractive } from "./yos_proc.mjs";
import { renderStreamToGrid } from "./vterm_render.mjs";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const load = async (p) => WebAssembly.compile(await readFile(join(here, p)));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const COLS = 80, ROWS = 24;

const zsh = await load("zsh.wasm");
const tools = new Map([["sh", zsh], ["zsh", zsh]]);
for (const n of ["ls", "ps", "echo", "tmux"]) { try { tools.set(n, await load(`tools/${n}.wasm`)); } catch {} }

let raw = "";
const ctl = runInteractive(zsh, ["zsh", "-f", "+o", "promptsp"], {
  onOutput: (fd, t) => { raw += t; },
  tools,
  env: ["PATH=/bin:/usr/bin", "HOME=/", "TERM=xterm-256color", "PWD=/"],
  cols: COLS, rows: ROWS,
});

let failed = 0;
const check = (name, ok, detail) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${name}${ok ? "" : "  " + (detail || "")}`); if (!ok) failed++; };

await sleep(300);

const noBareLF = (s) => (s.match(/[^\r]\n/g) || []).length;

// The two distinct guest output paths must BOTH be cooked:
//   - write(2)            : `ls` writes directly -> ofdWrite
//   - stdio printf/fprintf : `ps` formats via stdio -> emit/fpWrite -> ofdWrite
// Cooking lives in the single ofdWrite chokepoint, so both get CRLF.
raw = "";
ctl.write("ls -1 /bin\r");
await sleep(900);
check("write(2) path (ls): CRLF, no bare LF", noBareLF(raw) === 0, `${noBareLF(raw)} bare LF`);
// ls entries have no leading whitespace -> each must render flush at column 0.
const lsGrid = renderStreamToGrid(raw, { cols: COLS, rows: ROWS });
const lsLines = lsGrid.rows.filter((l) => l.trim() && !/\$|%|ls -1/.test(l));
const lsCols = lsLines.slice(0, 8).map((l) => l.search(/\S/));
check("ls -1 entries render at column 0 (no staircase)", lsCols.length >= 3 && lsCols.every((c) => c === 0), `columns: ${JSON.stringify(lsCols)}`);

raw = "";
ctl.write("ps\r");
await sleep(900);
check("stdio printf path (ps): CRLF, no bare LF", noBareLF(raw) === 0, `${noBareLF(raw)} bare LF`);
// ps rows are right-aligned (legit leading spaces), so assert separation instead:
// each rendered row sits on its own line and none continues a prior row's column.
const psGrid = renderStreamToGrid(raw, { cols: COLS, rows: ROWS });
const psRows = psGrid.rows.filter((l) => /\bR\b|PID/.test(l));
check("ps rows land on separate lines (no staircase)", psRows.length >= 3, `${psRows.length} rows`);

// 3) the PANE pty path: ps run INSIDE a tmux pane. The pane program's output
// flows through the pty slave, which has its OWN output discipline. A staircase
// here means the pty slave wasn't cooking LF->CRLF. Assert the ps data rows all
// share the same left indent (the PID column) instead of marching rightward.
if (tools.has("tmux")) {
  const typePerKey = async (k, settle = 900) => { for (const c of k) { ctl.write(c); await sleep(45); } await sleep(settle); };
  await typePerKey("tmux new-session\r", 1200);
  raw = "";
  await typePerKey("ps\r", 1000);
  const g = renderStreamToGrid(raw, { cols: COLS, rows: ROWS });
  const dataRows = g.rows.filter((l) => /\d+\s+\d+ [A-Z] /.test(l)); // "  1     0 R zsh"
  const indents = dataRows.map((l) => l.search(/\S/));
  const sameIndent = indents.length >= 3 && indents.every((c) => c === indents[0]);
  check("ps inside tmux pane: rows aligned, no staircase", sameIndent, `indents: ${JSON.stringify(indents)}`);
} else {
  console.log("  SKIP  ps inside tmux pane (tmux.wasm missing)");
}

console.log(failed ? `\n${failed} FAILED` : "\nALL PASSED — terminal output newlines are cooked (LF->CRLF)");
process.exit(failed ? 1 : 0);
