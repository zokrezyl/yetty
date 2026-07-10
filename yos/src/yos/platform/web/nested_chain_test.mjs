// The full nested interactive chain on the browser engine — the scenario a
// user actually types at zsh.html:
//
//   zsh → tmux (zsh in the pane) → nvim inside the pane → :terminal inside
//   nvim (forkpty) → a live shell → run a command → unwind every layer back
//   to the outer zsh prompt.
//
// Eight processes deep at peak: zsh, tmux client, (daemonize fork), tmux
// server, pane zsh, nvim TUI client, nvim embedded server, :terminal shell.
// The command output travels :terminal pty → nvim grid → msgpack redraw →
// TUI → pane pty → tmux server render → client tty → xterm.
//
// Engine behaviours this pins (each was a real blocker):
//   - forkpty (openpty + fork + login_tty in one bridge; the child wires the
//     slave as stdio/controlling tty, the parent keeps the master);
//   - /bin/<tool> VFS nodes with 0755 exec bits (nvim stats $SHELL via
//     os_can_exe and refuses :terminal with "'/bin/sh' is not executable"
//     when only the tool map knows the name);
//   - getrlimit writing a real RLIMIT_NOFILE (nvim's pty spawn loops
//     fcntl(F_SETFD) up to that limit — stack garbage made it spin 2.9M fds
//     into the runaway guard) and fcntl EBADF on nonexistent fds;
//   - SIGCHLD delivery for forkpty children (no EVFILT_PROC watches a
//     hand-rolled fork; the parent's sigaction is how nvim reaps the shell).
//
// Run: node nested_chain_test.mjs
import { runInteractive, loadLiblua } from "./yos_proc.mjs";
import { compileGuest } from "./wasm_patch.mjs";
import { entriesFromDir } from "./fs_mount.mjs";
import { renderStreamToGrid } from "./vterm_render.mjs";
import { readFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const shareDir = join(here, "..", "..", "..", "..", "result", "share");
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const load = async (p) => compileGuest(await readFile(join(here, p)));

if (!existsSync(shareDir)) { process.stderr.write("nested_chain_test: FAIL — result/share missing (run `make all`)\n"); process.exit(1); }

await loadLiblua();
const zsh = await load("zsh.wasm");
const tools = new Map([["sh", zsh], ["zsh", zsh]]);
for (const name of ["tmux", "nvim", "echo", "ls", "true"]) tools.set(name, await load(`tools/${name}.wasm`));

let allRaw = "";
const ctl = runInteractive(zsh, ["zsh", "-f", "+o", "promptsp"], {
	onOutput: (fd, text) => { allRaw += text; },
	tools,
	mounts: [{ at: "/usr/share", entries: await entriesFromDir(shareDir) }],
	env: ["PATH=/bin:/usr/bin", "HOME=/", "TERM=xterm-256color", "PWD=/", "SHELL=/bin/sh", "VIMRUNTIME=/usr/share/nvim/runtime"],
	cols: 80, rows: 24,
});
const gridText = () => renderStreamToGrid(allRaw, { cols: 80, rows: 24 }).text;
const type = async (s, ms = 800) => { for (const ch of s) { ctl.write(ch); await sleep(45); } await sleep(ms); };
const waitFor = async (re, tries = 30) => { for (let i = 0; i < tries; i++) { if (re.test(gridText())) return true; await sleep(500); } return re.test(gridText()); };
const alive = () => ctl.mgr.procs.filter((p) => !p.exited).map((p) => p.comm).join(",");

let failed = 0;
const check = (name, ok, detail) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${name}${ok ? "" : "  " + (detail || "")}`); if (!ok) failed++; };

await sleep(500);

// layer 1: tmux
await type("tmux new-session\r", 1500);
check("tmux session up (status bar)", await waitFor(/\[0\] 0:/), JSON.stringify(gridText().slice(-100)));

// layer 2: nvim in the pane
await type("nvim\r", 1500);
check("nvim intro inside the tmux pane", await waitFor(/type  :q<Enter>\s+to exit/, 40), alive());

// layer 3: :terminal inside nvim
await type(":terminal\r", 1500);
check(":terminal opens a live shell (term:// statusline)", await waitFor(/term:\/\/.*\/bin\/sh/, 24), JSON.stringify(gridText().slice(0, 160)));
check("shell process spawned via forkpty", ctl.mgr.procs.some((p) => !p.exited && p.viaForkpty), alive());

// layer 4: run a command through the whole stack
await type("i", 900);
await type("echo NESTED_$((6*7))\r", 1500);
check("command output painted through pty→nvim→tmux→tty", await waitFor(/NESTED_42/, 16), JSON.stringify(gridText().replace(/\s+/g, " ").slice(0, 160)));

// unwind: shell → nvim → pane → back to the outer zsh prompt
await type("exit\r", 2000);
await type("\x1c\x0e", 500); // CTRL-\ CTRL-N — leave terminal-mode if still in it
await type(":q!\r", 2000);
check("nvim gone after :q!", !ctl.mgr.procs.some((p) => !p.exited && p.comm === "nvim"), alive());
await type("exit\r", 2500);
await sleep(2500);
check("tmux session ended after pane exit", !ctl.mgr.procs.some((p) => !p.exited && p.comm === "tmux"), alive());

allRaw = "";
await type("echo BACK_$((6*7))\r", 1500);
check("outer zsh prompt alive at the end", /BACK_42/.test(allRaw), JSON.stringify(allRaw.slice(-80)));

ctl.dispose();
console.log(failed ? `\n${failed} FAILED` : "\nALL PASSED — zsh → tmux → nvim → :terminal → shell, and back out");
process.exit(failed ? 1 : 0);
