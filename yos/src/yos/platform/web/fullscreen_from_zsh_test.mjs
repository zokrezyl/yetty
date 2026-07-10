// Full-screen tools launched FROM zsh on the browser engine — the exact flow
// of typing `top` / `nvim` at the zsh.html prompt (fork from the interactive
// shell, execve of the tool image, tty handover, exit back to the prompt).
//
// This is deliberately a different code path from top_test.mjs /
// lua/nvim_interactive_test.mjs (which run the tool as the root process):
// the exec'd-from-zsh flow pins engine bugs the direct flow cannot see:
//   - stat/open/access on the EMPTY path must be ENOENT, not the cwd —
//     isdirectory("") == true made netrw hijack nvim's startup buffer into
//     a directory listing of /, and its keymaps shell out on plain letters;
//   - the liblua data+stack window must be reserved in EVERY process
//     (state.reserve): the from-zsh server's heap grows past 256 MiB and
//     without the reservation it allocates straight through Lua state
//     ("unreachable" traps);
//   - the tool inherits the shell's tty and returns it cleanly (the prompt
//     must work after the tool exits).
//
// Run: node fullscreen_from_zsh_test.mjs
import { runInteractive, loadLiblua } from "./yos_proc.mjs";
import { compileGuest } from "./wasm_patch.mjs";
import { entriesFromDir } from "./fs_mount.mjs";
import { readFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const shareDir = join(here, "..", "..", "..", "..", "result", "share");
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const load = async (p) => compileGuest(await readFile(join(here, p)));

if (!existsSync(shareDir)) { process.stderr.write("fullscreen_from_zsh_test: FAIL — result/share missing (run `make all`)\n"); process.exit(1); }

await loadLiblua();
const zsh = await load("zsh.wasm");
const tools = new Map([["sh", zsh], ["zsh", zsh]]);
for (const name of ["nvim", "top", "echo", "ls", "true"]) tools.set(name, await load(`tools/${name}.wasm`));

let raw = "";
const ctl = runInteractive(zsh, ["zsh", "-f", "+o", "promptsp"], {
	onOutput: (fd, text) => { raw += text; },
	tools,
	mounts: [{ at: "/usr/share", entries: await entriesFromDir(shareDir) }],
	env: ["PATH=/bin:/usr/bin", "HOME=/", "TERM=xterm-256color", "PWD=/", "SHELL=/bin/sh", "VIMRUNTIME=/usr/share/nvim/runtime"],
	cols: 80, rows: 24,
});
const plain = () => raw.replace(/\x1b\[[0-9;?$ ]*[A-Za-z]/g, "").replace(/\x1b[P\]^_][^\x1b\x07]*(\x1b\\|\x07)?/g, "").replace(/\x1b[()][A-Z0-9]/g, "").replace(/\x1b[=>]/g, "");
const type = async (s, ms = 700) => { for (const ch of s) { ctl.write(ch); await sleep(40); } await sleep(ms); };
const alive = () => ctl.mgr.procs.filter((p) => !p.exited).map((p) => p.comm).join(",");

let failed = 0;
const check = (name, ok, detail) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${name}${ok ? "" : "  " + (detail || "")}`); if (!ok) failed++; };

await sleep(500);

// --- top ---
raw = "";
await type("top\r", 1500);
check("top paints its screen from zsh", /load averages/.test(raw) && /PID USERNAME/.test(raw), JSON.stringify(plain().slice(0, 100)));
check("top runs alongside zsh", alive() === "zsh,top", alive());
await type("q", 1200);
check("q quits top, back to zsh only", alive() === "zsh", alive());

// --- nvim ---
raw = "";
await type("nvim\r", 1500);
for (let i = 0; i < 40 && !/type\s+:q/.test(plain()); i++) await sleep(500);
check("nvim intro screen paints (NOT a netrw listing)", /type\s+:q<Enter>\s+to exit/.test(plain()) && !/Netrw Directory Listing/.test(plain()), JSON.stringify(plain().slice(-120)));
check("TUI client + embedded server both alive", alive() === "zsh,nvim,nvim", alive());
await type("ihello\x1b", 1200);
raw = "";
await type(":q!\r", 1500);
for (let i = 0; i < 20 && alive() !== "zsh"; i++) await sleep(500);
check(":q! exits both nvim processes", alive() === "zsh", alive());
check("nvim processes exited cleanly", ctl.mgr.procs.filter((p) => p.comm === "nvim").every((p) => p.exitCode === 0),
	ctl.mgr.procs.filter((p) => p.comm === "nvim").map((p) => "x" + p.exitCode).join(","));

// --- the shell survived ---
raw = "";
await type("echo BACK_$((6*7))\r", 1200);
check("zsh prompt still works after nvim", /BACK_42/.test(raw), JSON.stringify(plain().slice(-80)));

ctl.dispose();
console.log(failed ? `\n${failed} FAILED` : "\nALL PASSED — top and nvim run full-screen from the browser zsh and hand the tty back");
process.exit(failed ? 1 : 0);
