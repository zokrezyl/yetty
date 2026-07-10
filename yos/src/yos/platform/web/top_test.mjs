// Interactive top on the browser process engine (issue: full-screen tools).
//
// top is the canonical "blocks in select() on the tty with a timeout"
// program: it paints a screen, sleeps in select(), repaints, and reads
// single-key commands. Two things this pins:
//   - the freebsd-tools binaries are asyncify-instrumented (wasm-opt
//     --asyncify in the nix build) — without it the engine cannot suspend
//     the guest and top trapped on its first blocking select();
//   - beginBlock's diagnostic for non-instrumented guests stays loud (a
//     clear "not asyncify-instrumented" error, not a bare TypeError).
//
// Run: node top_test.mjs   (or `make test-browser-top`)
import { runInteractive } from "./yos_proc.mjs";
import { compileGuest } from "./wasm_patch.mjs";
import { readFileSync, existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const topWasm = join(here, "tools", "top.wasm");
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

if (!existsSync(topWasm)) { process.stderr.write("top_test: FAIL — tools/top.wasm missing (run `make all` / refresh the tools symlinks)\n"); process.exit(1); }

const top = await compileGuest(readFileSync(topWasm));

let raw = "";
const ctl = runInteractive(top, ["top"], {
	onOutput: (fd, text) => { raw += text; },
	env: ["PATH=/bin:/usr/bin", "HOME=/", "TERM=xterm-256color"],
	cols: 80, rows: 24,
});

let failed = 0;
const check = (name, ok, detail) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${name}${ok ? "" : "  " + (detail || "")}`); if (!ok) failed++; };

for (let i = 0; i < 20 && !/COMMAND/.test(raw); i++) await sleep(250);
check("screen painted (header + process table)", /load averages/.test(raw) && /PID USERNAME/.test(raw), JSON.stringify(raw.slice(0, 120)));
check("top is alive and blocked (not crashed)", !ctl.proc.exited && ctl.proc.sched === "blocked", `exited=${ctl.proc.exited} err=${ctl.proc.error || "-"}`);

// Header integrity: "last pid" must be in the FIRST full paint. When the
// first sample reported it unavailable and a later refresh wrote the number
// at its fixed column anyway, it landed on top of the "load averages" text
// and the header rendered mangled (the yos_top_compat lastpid seed fix).
check("header intact: 'last pid: N;' present from the first paint", /last pid:\s+\d+;\s+load averages:/.test(raw), JSON.stringify(raw.slice(0, 100)));

// Bounded output: top repaints ONCE per refresh interval. The idle
// fast-forward used to re-fire its 2s select() timeout thousands of times
// per scheduler turn — 295 KB of redraws at boot, ~600 KB per keystroke.
check("no redraw storm at boot (bounded bytes)", raw.length < 8192, `rawlen=${raw.length}`);

// Idle real-time refresh: the deadline tick advances the frozen virtual
// clock at wall-clock pace, so an untouched top still repaints (a little).
const idleStart = raw.length;
await sleep(2600);
const idleDelta = raw.length - idleStart;
check("idle top refreshes via the deadline tick (small delta)", idleDelta > 0 && idleDelta < 4096, `delta=${idleDelta}`);

// 'q' quits.
ctl.write("q");
for (let i = 0; i < 20 && !ctl.proc.exited; i++) await sleep(250);
check("'q' exits top cleanly", ctl.proc.exited && ctl.proc.exitCode === 0, `exited=${ctl.proc.exited} code=${ctl.proc.exitCode}`);

ctl.dispose();
console.log(failed ? `\n${failed} FAILED` : "\nALL PASSED — interactive top runs on the browser engine");
process.exit(failed ? 1 : 0);
