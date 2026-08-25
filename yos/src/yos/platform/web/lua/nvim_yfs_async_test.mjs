// Interactive nvim with ASYNC yfs bodies — the deployed-site configuration.
//
// nvim_interactive_test.mjs mounts /usr/share synchronously (every body
// present before boot). The static deploy instead mounts it through yfs
// with LAZY bodies: a cold open suspends the process via asyncify until
// the fetch lands. That is unsound while a lua_* forwarder is on the wasm
// stack — liblua is not asyncify-instrumented, so the unwind skips its
// frames and the rewind traps `unreachable`. nvim's TUI hits exactly that:
// vim/termcap.lua is require'd (opened) from inside a lua_pcall, and on
// yetty.dev the editor died with a silent 139 (issue #724).
//
// This test pins the fix:
//   - cold opens OUTSIDE lua still suspend + resume through the async
//     fetch path (the yfs design working as intended);
//   - cold opens INSIDE lua take the client's synchronous fallback reader
//     (browser: sync XHR; here: readFileSync) instead of suspending;
//   - the TUI client + embedded server survive to the welcome screen and
//     tear down cleanly on :q!.
//
// Run: node nvim_yfs_async_test.mjs   (or `make test-browser-nvim`)
import { createInteractiveEngine, loadLiblua } from "../yos_proc.mjs";
import { luaCallDepth } from "./lua_bridge.mjs";
import { compileGuest } from "../wasm_patch.mjs";
import { readFileSync, readdirSync, statSync, existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const nvimWasm = join(here, "..", "..", "..", "..", "..", "result", "libexec", "nvim");
const shareDir = join(here, "..", "..", "..", "..", "..", "result", "share");
const liblua = join(here, "liblua.wasm");
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

const fail = (m) => { process.stderr.write(`nvim_yfs_async_test: FAIL — ${m}\n`); process.exit(1); };
if (!existsSync(liblua)) fail("liblua.wasm not built — run `make browser-liblua`");
if (!existsSync(nvimWasm)) fail("nvim.wasm not found (run `make all`)");
if (!existsSync(shareDir)) fail("result/share not found (run `make all`)");

// A yfs client shaped like yfs_client.mjs openYfs() (browser: async bodies,
// readBodySync null), backed by result/share on disk. fetchBody defers a
// macrotask so every cold open genuinely suspends; readBodySyncFallback is
// the stand-in for the browser client's sync XHR.
const stats = { asyncFetches: 0, syncFallbackReads: 0, syncFallbackInsideLua: 0 };
function buildShareYfsClient() {
	const dirs = { "": [{ n: "usr", t: "d", i: 2, m: 0o755 }], usr: [{ n: "share", t: "d", i: 3, m: 0o755 }], "usr/share": [] };
	const sources = new Map(); // guest dir path -> entry name -> disk path
	let nextInode = 4;
	const walk = (guestDir, diskDir) => {
		sources.set(guestDir, new Map());
		for (const name of readdirSync(diskDir).sort()) {
			const diskPath = join(diskDir, name);
			const info = statSync(diskPath); // follows symlinks (nix store trees)
			if (info.isDirectory()) {
				dirs[guestDir].push({ n: name, t: "d", i: nextInode++, m: 0o755 });
				dirs[`${guestDir}/${name}`] = [];
				walk(`${guestDir}/${name}`, diskPath);
			} else if (info.isFile()) {
				dirs[guestDir].push({ n: name, t: "f", i: nextInode++, m: 0o644, s: info.size, mt: Math.floor(info.mtimeMs / 1000) });
				sources.get(guestDir).set(name, diskPath);
			}
		}
	};
	walk("usr/share", shareDir);
	const diskPathFor = (dirPath, entry) => {
		const diskPath = sources.get(dirPath) && sources.get(dirPath).get(entry.n);
		if (!diskPath) throw new Error(`yfs: no such entry: ${dirPath}/${entry.n}`);
		return diskPath;
	};
	return {
		version: "test", dirs,
		fetchBody: async (dirPath, entry) => {
			stats.asyncFetches++;
			await sleep(1); // force a real suspension window
			return new Uint8Array(readFileSync(diskPathFor(dirPath, entry)));
		},
		readBodySync: null,
		readBodySyncFallback: (dirPath, entry) => {
			stats.syncFallbackReads++;
			if (luaCallDepth() > 0) stats.syncFallbackInsideLua++;
			return new Uint8Array(readFileSync(diskPathFor(dirPath, entry)));
		},
	};
}

await loadLiblua();
const nvim = await compileGuest(readFileSync(nvimWasm));

let raw = "";
const unimpl = new Set();
const engine = createInteractiveEngine({
	tools: new Map([["nvim", nvim]]),
	yfs: { client: buildShareYfsClient(), compile: compileGuest },
	onUnimpl: (name) => unimpl.add(name),
});
const ctl = engine.startSession(nvim, ["nvim", "-u", "NONE", "-i", "NONE"], {
	cols: 80, rows: 24,
	env: ["PATH=/bin:/usr/bin", "HOME=/", "TERM=xterm-256color", "VIMRUNTIME=/usr/share/nvim/runtime"],
	onOutput: (fd, text) => { raw += text; },
});

let failed = 0;
const check = (name, ok, detail) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${name}${ok ? "" : "  " + (detail || "")}`); if (!ok) failed++; };
const plain = () => raw.replace(/\x1b\[[0-9;?$ ]*[A-Za-z]/g, "").replace(/\x1b[P\]^_][^\x1b\x07]*(\x1b\\|\x07)?/g, "").replace(/\x1b[()][A-Z0-9]/g, "").replace(/\x1b[=>]/g, "");

// Boot: client + server come up, welcome screen paints. Pre-fix, the TUI
// died 139 (`unreachable`) on the first Lua-driven cold open.
for (let i = 0; i < 30 && !/type\s+:q/.test(plain()); i++) await sleep(500);
check("client + embedded server both alive", engine.mgr.procs.filter((p) => !p.exited).length === 2,
	engine.mgr.procs.map((p) => `${p.pid}:${p.comm}:${p.exited ? "x" + p.exitCode + (p.error ? "(" + p.error + ")" : "") : p.sched}`).join(" "));
check("welcome screen painted", /type\s+:q<Enter>\s+to exit/.test(plain()), JSON.stringify(plain().slice(-120)));
check("no unimplemented imports hit", unimpl.size === 0, [...unimpl].join(","));
check("cold opens inside lua used the sync fallback", stats.syncFallbackInsideLua > 0,
	`syncFallbackReads=${stats.syncFallbackReads} insideLua=${stats.syncFallbackInsideLua} (nvim no longer opens runtime files from Lua? re-examine this pin)`);

// :edit a cold runtime file — a C-side buffer read (no lua on the stack),
// so it must take the ASYNC path: suspend on the fetch, resume, paint.
for (const ch of ":e /usr/share/nvim/runtime/delmenu.vim\r") { ctl.write(ch); await sleep(30); }
for (let i = 0; i < 20 && !/delmenu/.test(plain().slice(-400)); i++) await sleep(500);
check("cold open outside lua suspended through the async fetch path", stats.asyncFetches > 0, `asyncFetches=${stats.asyncFetches}`);
check(":e painted the yfs-fetched buffer", /delmenu/.test(plain().slice(-400)), JSON.stringify(plain().slice(-120)));

// :q! must exit BOTH processes.
for (const ch of ":q!\r") { ctl.write(ch); await sleep(60); }
for (let i = 0; i < 20 && !ctl.proc.exited; i++) await sleep(500);
check("root (TUI client) exited 0 on :q!", ctl.proc.exited && ctl.proc.exitCode === 0, `exited=${ctl.proc.exited} code=${ctl.proc.exitCode}`);
check("embedded server exited too", engine.mgr.procs.every((p) => p.exited),
	engine.mgr.procs.map((p) => `${p.pid}:${p.exited ? "x" + p.exitCode : p.sched}`).join(" "));

console.log(`  stats: asyncFetches=${stats.asyncFetches} syncFallbackReads=${stats.syncFallbackReads} insideLua=${stats.syncFallbackInsideLua}`);
ctl.dispose();
console.log(failed ? `\n${failed} FAILED` : "\nALL PASSED — interactive nvim survives async yfs bodies (deployed-site configuration)");
process.exit(failed ? 1 : 0);
