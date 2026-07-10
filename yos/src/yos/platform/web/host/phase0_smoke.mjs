// Phase 0 smoke test (epic #33, issue #34).
//
// Loads the Emscripten-built host (yos-host-phase0.mjs), which runs the wasm3
// interpreter INSIDE a browser-targeted wasm module against a trivial embedded
// guest, and asserts the guest actually executed under wasm3.
//
// This is the gate the issue calls for: "a test that fails if wasm3-in-wasm
// cannot execute the trivial guest." It goes red on every failure mode —
//   * missing artifact (build never ran)
//   * host wasm abort/trap (factory promise rejects)
//   * wasm3 parse/instantiate/call error (no output marker)
//   * wrong computed value (marker present but != 42)
//
// Run: node phase0_smoke.mjs   (or `make test-browser-host-phase0`)
import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const hostMjs = join(here, "yos-host-phase0.mjs");

const fail = (msg) => {
	process.stderr.write(`phase0 smoke: FAIL — ${msg}\n`);
	process.exit(1);
};

if (!existsSync(hostMjs)) {
	fail("yos-host-phase0.mjs not found — run build-phase0.sh (or `make browser-host-phase0`)");
}

// Minimal JS output hook: the ONLY JavaScript in the guest-execution path.
// It captures, it does not implement libc. The host's printf → musl write →
// these callbacks.
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

// A wasm trap / abort inside the host rejects the factory promise — catch it
// so a hard failure is a red test, never an unhandled rejection.
try {
	await createHost({ print, printErr });
} catch (e) {
	if (err) process.stderr.write(err);
	fail(`host wasm aborted or trapped: ${e?.message ?? e}`);
}

// Surface whatever the host emitted, for eyeballing.
if (out) process.stdout.write(out);
if (err) process.stderr.write(err);

const EXPECT = "phase0: wasm3 executed guest, compute()=42";
if (!out.includes(EXPECT)) {
	fail(`expected marker not found in host output: "${EXPECT}"`);
}

process.stderr.write("phase0 smoke: PASS — wasm3-in-wasm executed the embedded guest\n");
process.exit(0);
