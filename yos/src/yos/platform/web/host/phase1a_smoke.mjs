// Phase 1a smoke test (epic #33, issue #35).
//
// Loads the Phase 1a host and asserts the guest's env.write / env.getpid /
// env.exit imports were served by the C bridge — pointer translation and all —
// with zero JS libc.
//
// It fails on every failure mode: missing artifact, host abort, and — the
// point of Phase 1a — if the C bridge did NOT read the guest's memory
// correctly (the pid-bearing line would be wrong/absent) or if env.exit did
// not carry its code through the C wrapper (the exit line would be wrong).
//
// The distinguishing check vs. "JS implemented the call": the message is built
// inside guest memory and only comes out right if the C wrapper translated the
// guest offset and read those bytes; the pid (4242) is host state returned by
// the C getpid wrapper, not a JS value.
//
// Run: node phase1a_smoke.mjs   (or `make test-browser-host-phase1a`)
import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const hostMjs = join(here, "yos-host-phase1a.mjs");

const fail = (msg) => {
	process.stderr.write(`phase1a smoke: FAIL — ${msg}\n`);
	process.exit(1);
};

if (!existsSync(hostMjs)) {
	fail("yos-host-phase1a.mjs not found — run build-phase1a.sh (or `make browser-host-phase1a`)");
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

// 1. The guest's write() went through the C bridge, which read the message the
//    guest built in its own memory — including the host-provided pid.
const WROTE = "phase1a: pid=4242 via C bridge";
if (!out.includes(WROTE)) {
	fail(`guest write() did not surface through the C bridge (expected "${WROTE}")`);
}

// 2. env.exit carried its code through the C exit wrapper.
const EXITED = "phase1a: guest exited via env.exit, code=7";
if (!out.includes(EXITED)) {
	fail(`env.exit did not carry its code through the C bridge (expected "${EXITED}")`);
}

process.stderr.write("phase1a smoke: PASS — write/getpid/exit served by the C bridge, guest pointers read from guest memory\n");
process.exit(0);
