// Browser-engine nvim / Lua-library test (epic #33 — full library surface).
//
// nvim imports 85 lua_*/luaL_* functions: it expects a Lua runtime from the
// host (desktop links native liblua). This test proves the browser provides
// the SAME Lua via liblua.wasm — compiled from Lua 5.1 source, sharing nvim's
// linear memory + function table, with pcall/error via native wasm exceptions.
//
// Runs the SAME nvim.wasm desktop runs, through the browser process engine.
//
// Run: node nvim_test.mjs   (or `make test-browser-nvim`)
import { runYos } from "../yos_run.mjs";
import { compileGuest } from "../wasm_patch.mjs";
import { loadLiblua } from "./lua_bridge.mjs";
import { readFileSync, existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const nvimWasm = join(here, "..", "..", "..", "..", "..", "result", "libexec", "nvim");
const liblua = join(here, "liblua.wasm");

const fail = (m) => { process.stderr.write(`nvim_test: FAIL — ${m}\n`); process.exit(1); };

if (!existsSync(liblua)) fail("liblua.wasm not built — run lua/build-liblua.sh (or `make browser-liblua`)");
if (!existsSync(nvimWasm)) fail("nvim.wasm not found (run `make nvim-build` / `make all`)");

await loadLiblua();

const mod = await compileGuest(readFileSync(nvimWasm));
let out = "", err = "";
let code;
try {
	code = await runYos(mod, ["nvim", "--version"], { onOutput: (fd, t) => { if (fd === 2) err += t; else out += t; } });
} catch (e) {
	fail(`nvim trapped: ${e?.message ?? e}${err ? " | " + err.slice(0, 120) : ""}`);
}

const exit = code?.exitCode ?? code;
if (exit !== 0) fail(`nvim --version exited ${JSON.stringify(exit)} (expected 0); out=${JSON.stringify(out.slice(0, 80))}`);
if (!/NVIM v/.test(out)) fail(`no version banner; got ${JSON.stringify(out.slice(0, 80))}`);
if (!/Lua 5\.1/.test(out)) fail(`Lua not reported; got ${JSON.stringify(out.slice(0, 120))}`);

process.stdout.write(out.split("\n").slice(0, 3).join("\n") + "\n");
process.stderr.write("nvim_test: PASS — nvim runs its Lua runtime in the browser via liblua.wasm (shared memory + table)\n");
process.exit(0);
