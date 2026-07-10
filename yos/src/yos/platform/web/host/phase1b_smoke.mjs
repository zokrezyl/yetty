// Phase 1b smoke test (epic #33, issue #36).
//
// Runs the Phase 1b host — the REAL generated yos bridge + impl/vfs compiled to
// wasm — and asserts a guest's echo (write) and cat (open/read/write/close)
// were served by yos C code with zero JS libc. The file contents can only
// appear if open()/read() went through the generated bridge → yos_open/yos_read
// → the Emscripten MEMFS substrate under yos's VFS.
//
// Run: node phase1b_smoke.mjs   (or `make test-browser-host-phase1b`)
import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const hostMjs = join(here, "yos-host-phase1b.mjs");

const fail = (msg) => {
	process.stderr.write(`phase1b smoke: FAIL — ${msg}\n`);
	process.exit(1);
};

if (!existsSync(hostMjs)) {
	fail("yos-host-phase1b.mjs not found — run build-phase1b.sh (or `make browser-host-phase1b`)");
}

let out = "";
let err = "";
const print = (line) => { out += line + "\n"; };
const printErr = (line) => { err += line + "\n"; };

let createHost;
try {
	({ default: createHost } = await import(hostMjs));
} catch (e) {
	fail(`could not import host module: ${e?.message ?? e}`);
}

try {
	await createHost({ print, printErr });
} catch (e) {
	if (err) process.stderr.write(err);
	fail(`host wasm aborted or trapped: ${e?.message ?? e}`);
}

if (out) process.stdout.write(out);
if (err) process.stderr.write(err);

// echo: write() + getpid() through the generated bridge.
const ECHO = "phase1b: pid=1 echo via generated bridge";
if (!out.includes(ECHO)) {
	fail(`echo did not run through the generated bridge (expected "${ECHO}")`);
}

// cat: open()/read()/write()/close() through the generated bridge + MEMFS.
// This line is the embedded file's contents, surfaced only if the VFS/io path
// worked end to end.
const CAT = "phase1b: cat via generated bridge OK";
if (!out.includes(CAT)) {
	fail(`cat did not run through the generated bridge + VFS (expected "${CAT}")`);
}

process.stderr.write("phase1b smoke: PASS — echo + cat ran through the generated yos bridge + impl/vfs, no JS libc\n");
process.exit(0);
