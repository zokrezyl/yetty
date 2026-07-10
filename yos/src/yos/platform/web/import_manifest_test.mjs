// Self-test for the prototype-boundary import manifest (issue #21,
// milestone 1). Verifies that:
//   1. classifyImports splits a module's env imports into
//      supported / partial / unsupported against a given env;
//   2. strictImportEnv installs a LOUD-FAILURE stub for unsupported
//      imports (a call throws a clear diagnostic) and leaves implemented
//      imports untouched — i.e. there is no silent return-0 fallback.
//
// Run: node import_manifest_test.mjs   (exits non-zero on failure)

import assert from "node:assert/strict";
import {
  classifyImports,
  strictImportEnv,
  describeImports,
  PROTOTYPE_PARTIAL,
} from "./import_manifest.mjs";

// --- Build a minimal valid wasm module that imports three env functions:
//     env.write (implemented), env.poll (partial), env.fopen (unsupported).
//     The module declares the imports but never calls them, which is legal
//     and lets us instantiate it with a hardened env. ----------------------
function leb(n) {
  const out = [];
  do {
    let byte = n & 0x7f;
    n >>>= 7;
    if (n !== 0) byte |= 0x80;
    out.push(byte);
  } while (n !== 0);
  return out;
}
function str(s) {
  const bytes = [...new TextEncoder().encode(s)];
  return [...leb(bytes.length), ...bytes];
}
function section(id, payload) {
  return [id, ...leb(payload.length), ...payload];
}
function funcImport(name) {
  // module "env", field <name>, kind 0x00 (func), type index 0
  return [...str("env"), ...str(name), 0x00, 0x00];
}

const names = ["write", "poll", "fopen"];
const typeSec = section(1, [0x01, 0x60, 0x00, 0x00]); // 1 type: () -> ()
const importPayload = [...leb(names.length), ...names.flatMap(funcImport)];
const importSec = section(2, importPayload);
const wasm = new Uint8Array([
  0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // magic + version
  ...typeSec,
  ...importSec,
]);
const module = new WebAssembly.Module(wasm);

// --- A "real env" that implements write + poll but NOT fopen. ------------
const realEnv = {
  memory: new WebAssembly.Memory({ initial: 1 }),
  write: () => 0,
  poll: () => 0, // poll is in PROTOTYPE_PARTIAL -> classified partial
};

// classifyImports: write=supported, poll=partial, fopen=unsupported.
const info = classifyImports(module, realEnv);
assert.deepEqual(info.supported, ["write"], "write should be supported");
assert.deepEqual(info.partial, ["poll"], "poll should be partial");
assert.deepEqual(info.unsupported, ["fopen"], "fopen should be unsupported");
assert.ok(PROTOTYPE_PARTIAL.has("poll"), "poll listed in PROTOTYPE_PARTIAL");

// strictImportEnv: installs a loud stub for fopen, keeps write/poll as-is.
let reported = null;
const calls = [];
const { env } = strictImportEnv(realEnv, module, {
  label: "test",
  onCall: (name, args) => calls.push([name, args.length]),
  report: (i) => (reported = i),
});
assert.equal(env, realEnv, "env is returned in place");
assert.equal(typeof env.fopen, "function", "fopen now bound to a stub");
assert.ok(env.fopen.yosUnsupported, "fopen stub is tagged unsupported");
assert.ok(!env.write.yosUnsupported, "write impl left untouched");
assert.ok(reported && reported.unsupported.includes("fopen"), "report fired");

// The instance must still build (all imports satisfied by stubs/impls).
const instance = new WebAssembly.Instance(module, { env });
assert.ok(instance, "module instantiates with hardened env");

// Calling the unsupported import fails loudly with a clear diagnostic —
// NOT a silent 0.
assert.throws(
  () => env.fopen(1, 2),
  (err) =>
    err instanceof Error &&
    /unsupported libc import 'env\.fopen'/.test(err.message) &&
    /fails loudly/.test(err.message),
  "fopen stub must throw a clear diagnostic, not return 0",
);
assert.deepEqual(calls, [["fopen", 2]], "onCall observed the loud call");

// strict:true makes it fail at link time instead of call time.
assert.throws(
  () =>
    strictImportEnv({ write: () => 0, poll: () => 0 }, module, {
      label: "test",
      strict: true,
    }),
  /unsupported libc import/,
  "strict:true rejects an unsupported module at link time",
);

console.log("import_manifest_test: PASS —", describeImports(info));
