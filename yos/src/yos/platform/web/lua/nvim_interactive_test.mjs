// Interactive nvim on the browser process engine (epic #33).
//
// nvim_test.mjs proves `nvim --version` (batch). This proves the full
// interactive editor: the TUI client uv_spawns the embedded server
// (`nvim --embed`), the two talk msgpack-RPC over a socketpair, the client
// renders the welcome screen to the tty, insert-mode keystrokes round-trip
// (key → client → server → redraw → tty), and `:q!` tears BOTH processes
// down (server exit observed via kqueue EVFILT_PROC, like libuv on FreeBSD).
//
// Engine behaviours this test pins (each one was a real nvim-blocking bug):
//   - pipe2 O_CLOEXEC honored across execve (libuv's exec-status pipe must
//     EOF in the parent, else uv_spawn never completes);
//   - fcntl(F_GETFL) reports the fd access mode (libuv derives stream
//     writability from it; a bare 0 reads as O_RDONLY and every uv_write
//     fails UV_EPIPE);
//   - kevent EVFILT_PROC fires when a spawned child exits;
//   - liblua.wasm at --table-base parks its element segment above the
//     guest's own function-table entries (no call_indirect corruption);
//   - Lua's io.stdout resolves fd 1 at write time (sentinel FILE), so
//     _defaults.lua's terminal queries follow nvim's dup2(2,1) remap
//     instead of corrupting the RPC socket.
//
// Run: node nvim_interactive_test.mjs   (or `make test-browser-nvim`)
import { runInteractive } from "../yos_proc.mjs";
import { loadLiblua } from "./lua_bridge.mjs";
import { compileGuest } from "../wasm_patch.mjs";
import { entriesFromDir } from "../fs_mount.mjs";
import { readFileSync, existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const nvimWasm = join(here, "..", "..", "..", "..", "..", "result", "libexec", "nvim");
const shareDir = join(here, "..", "..", "..", "..", "..", "result", "share");
const liblua = join(here, "liblua.wasm");
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

const fail = (m) => { process.stderr.write(`nvim_interactive_test: FAIL — ${m}\n`); process.exit(1); };
if (!existsSync(liblua)) fail("liblua.wasm not built — run `make browser-liblua`");
if (!existsSync(nvimWasm)) fail("nvim.wasm not found (run `make all`)");
if (!existsSync(shareDir)) fail("result/share not found (run `make all`)");

await loadLiblua();
const nvim = await compileGuest(readFileSync(nvimWasm));
const shareEntries = await entriesFromDir(shareDir);

let raw = "";
const unimpl = new Set();
const ctl = runInteractive(nvim, ["nvim", "-u", "NONE", "-i", "NONE"], {
	onOutput: (fd, text) => { raw += text; },
	onUnimpl: (name) => unimpl.add(name),
	tools: new Map([["nvim", nvim]]),
	mounts: [{ at: "/usr/share", entries: shareEntries }],
	env: ["PATH=/bin:/usr/bin", "HOME=/", "TERM=xterm-256color", "VIMRUNTIME=/usr/share/nvim/runtime"],
	cols: 80, rows: 24,
});

let failed = 0;
const check = (name, ok, detail) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${name}${ok ? "" : "  " + (detail || "")}`); if (!ok) failed++; };
const plain = () => raw.replace(/\x1b\[[0-9;?$ ]*[A-Za-z]/g, "").replace(/\x1b[P\]^_][^\x1b\x07]*(\x1b\\|\x07)?/g, "").replace(/\x1b[()][A-Z0-9]/g, "").replace(/\x1b[=>]/g, "");

// Boot: client + server come up, welcome screen paints.
for (let i = 0; i < 30 && !/type\s+:q/.test(plain()); i++) await sleep(500);
check("client + embedded server both alive", ctl.mgr.procs.filter((p) => !p.exited).length === 2,
	ctl.mgr.procs.map((p) => `${p.pid}:${p.comm}:${p.exited ? "x" + p.exitCode : p.sched}`).join(" "));
check("welcome screen painted", /type\s+:q<Enter>\s+to exit/.test(plain()), JSON.stringify(plain().slice(-120)));
check("no unimplemented imports hit", unimpl.size === 0, [...unimpl].join(","));

// Insert-mode round trip: every typed char must come back as a redraw.
raw = "";
for (const ch of "ihello") { ctl.write(ch); await sleep(60); }
await sleep(1200);
check("insert-mode text painted back", /h.*e.*l.*l.*o/s.test(plain()), JSON.stringify(plain().slice(-80)));

// :q! must exit BOTH processes (EVFILT_PROC delivers the server's death).
ctl.write("\x1b");
await sleep(500);
for (const ch of ":q!\r") { ctl.write(ch); await sleep(60); }
for (let i = 0; i < 20 && !ctl.proc.exited; i++) await sleep(500);
check("root (TUI client) exited 0 on :q!", ctl.proc.exited && ctl.proc.exitCode === 0, `exited=${ctl.proc.exited} code=${ctl.proc.exitCode}`);
check("embedded server exited too", ctl.mgr.procs.every((p) => p.exited),
	ctl.mgr.procs.map((p) => `${p.pid}:${p.exited ? "x" + p.exitCode : p.sched}`).join(" "));

ctl.dispose();
console.log(failed ? `\n${failed} FAILED` : "\nALL PASSED — interactive nvim (TUI client + embedded server) runs on the browser engine");
process.exit(failed ? 1 : 0);
