// Browser-engine callback regression test (epic #32).
//
// Guards the wasm table-export patch (wasm_patch.mjs). yos's tools don't export
// their function table, so the JS process engine could not invoke a guest
// function pointer — every callback-based libc call (qsort/bsearch/mergesort/
// heapsort comparators, signal handlers, pthread_once) silently did nothing.
// Most visible symptom: `ps` printed "pid: keyword not found" because its
// keyword bsearch returned NULL. The patch adds a `__indirect_function_table`
// export at load so the host can call comparators.
//
// Runs the SAME ps.wasm desktop runs, through the browser process engine, and
// asserts the keyword bsearch works (a proper header, no "keyword not found").
//
// Run: node callback_test.mjs   (or `make test-browser-callbacks`)
import { runYos } from "./yos_run.mjs";
import { compileGuest } from "./wasm_patch.mjs";
import { patchWasmTableExport } from "./wasm_patch.mjs";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const libexec = join(here, "..", "..", "..", "..", "result", "libexec");

const failures = [];
const check = (name, cond, detail) => { if (!cond) failures.push(`${name}: ${detail}`); };

// 1. The patcher actually adds a table export to a tool that lacks one.
{
	const raw = readFileSync(join(libexec, "ps"));
	const patched = patchWasmTableExport(raw);
	const before = WebAssembly.Module.exports(new WebAssembly.Module(raw)).some((e) => e.kind === "table");
	const after = WebAssembly.Module.exports(new WebAssembly.Module(patched)).some((e) => e.kind === "table");
	check("patch adds table export", !before && after, `before=${before} after=${after}`);
}

// 2. ps: keyword bsearch works — a header row, no "keyword not found".
{
	const mod = await compileGuest(readFileSync(join(libexec, "ps")));
	let out = "", err = "";
	await runYos(mod, ["ps"], { onOutput: (fd, t) => { if (fd === 2) err += t; else out += t; } });
	check("ps no keyword error", !/keyword not found/.test(out + err), `saw: ${(out + err).split("\n")[0]}`);
	check("ps prints header", /\bPID\b/.test(out) && /\bCOMMAND\b/.test(out), `header missing, got: ${out.split("\n")[0]}`);
}

if (failures.length) {
	process.stderr.write("callback_test: FAIL\n" + failures.map((f) => "  - " + f).join("\n") + "\n");
	process.exit(1);
}
process.stderr.write("callback_test: PASS — guest function-pointer callbacks (ps bsearch) work via the table-export patch\n");
process.exit(0);
