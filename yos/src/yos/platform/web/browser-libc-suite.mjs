// Run the ENTIRE yos:libc unit-test suite against the BROWSER process engine.
//
// The native suite is `meson test -C build-linux --suite yos:libc`: each test
// parses an `Expected: exit <code>[, stdout contains "..."]` header from its .c
// and runs the .wasm through the host `yos` binary. This driver runs the exact
// same tests with the exact same pass criterion (tests/ut/libc/run_libc_test.py)
// but points it at run-wasm-node.mjs — the browser engine (yos_proc.mjs) on the
// command line — instead of the native binary.
//
// It discovers the test set + each test's (src, wasm) from `meson introspect`,
// so it always matches what meson actually runs. For every test that FAILS on
// the browser engine it re-runs the SAME test on the native yos binary to
// classify it:
//   - BROWSER-GAP : native passes, browser fails  → a real browser-engine gap
//   - BOTH-FAIL   : native fails too               → xfail/known (not a regression)
//
// Usage:
//   node browser-libc-suite.mjs [--build build-linux] [--filter <substr>] [--quiet]
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { fileURLToPath } from "node:url";
import { dirname, join, isAbsolute } from "node:path";
import { existsSync } from "node:fs";
const exec = promisify(execFile);

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, "..", "..", "..", "..");
const argOf = (flag, def) => { const i = process.argv.indexOf(flag); return i >= 0 ? process.argv[i + 1] : def; };
const BUILD = argOf("--build", "build-linux");
const FILTER = argOf("--filter", "");
const quiet = process.argv.includes("--quiet");
const buildDir = join(repo, BUILD);
const shim = join(here, "run-wasm-node.mjs");
const nativeYos = join(buildDir, "src", "yos", "yos");

const resolveWasm = (w) => (isAbsolute(w) ? w : join(buildDir, w));

// Discover the suite from meson, so we run exactly what `meson test` runs.
const introspect = JSON.parse((await exec("meson", ["introspect", buildDir, "--tests"], { maxBuffer: 64 << 20 })).stdout);
const tests = [];
for (const t of introspect) {
  if (!(t.suite || []).some((s) => s.includes("yos:libc"))) continue;
  const cmd = t.cmd;
  const runnerIdx = cmd.findIndex((a) => a.endsWith("run_libc_test.py"));
  if (runnerIdx < 0) continue; // skip non-standard runners (e.g. run_libc_coverage.py)
  const get = (flag) => { const i = cmd.indexOf(flag); return i >= 0 ? cmd[i + 1] : null; };
  // Pass src/wasm to run_libc_test.py EXACTLY as meson does (some are relative
  // to the build dir — the fork-matrix .c/.wasm are generated INTO the build
  // tree), and run the runner with cwd=buildDir so those relative paths
  // resolve. Keep an absolute wasm path only for the "is it built?" check.
  let wasm = get("--wasm");
  const src = get("--src"), timeout = get("--timeout") || "60";
  const pyRunner = cmd[runnerIdx];
  const python = cmd[0];
  if (!src || !wasm) continue;
  if (FILTER && !t.name.includes(FILTER)) continue;
  // Prefer a shared-memory "-mt" build when one exists: it IMPORTS its memory,
  // so the shim routes it to the REAL-thread engine (mt_engine) instead of the
  // cooperative one. This is how perf_stress (a real producer/consumer condvar)
  // passes in the browser — native keeps running the host-threaded build.
  const mtWasm = wasm.replace(/(-raw)?\.wasm$/, "-mt.wasm");
  if (mtWasm !== wasm && existsSync(resolveWasm(mtWasm))) wasm = mtWasm;
  tests.push({ name: t.name, src, wasm, wasmAbs: resolveWasm(wasm), timeout, pyRunner, python });
}
tests.sort((a, b) => a.name.localeCompare(b.name));

function runRunner(t, yosBin) {
  // run_libc_test.py exits 0 on PASS, 1 on FAIL. Run it from the build dir with
  // the paths verbatim — exactly as meson does — so build-relative src/wasm
  // resolve. Capture output for diagnostics.
  return exec(t.python, [t.pyRunner, "--yos", yosBin, "--src", t.src, "--wasm", t.wasm, "--timeout", t.timeout],
    { cwd: buildDir, maxBuffer: 16 << 20, timeout: (Number(t.timeout) + 30) * 1000 })
    .then(() => ({ ok: true, out: "" }))
    .catch((e) => ({ ok: false, out: ((e.stdout || "") + (e.stderr || "")).trim() }));
}

console.log(`\nyos:libc unit tests against the BROWSER engine (run-wasm-node.mjs)`);
console.log(`build=${BUILD}  tests=${tests.length}${FILTER ? `  filter=${FILTER}` : ""}\n`);
if (!existsSync(nativeYos)) console.log(`(note: native yos not at ${nativeYos}; failures won't be classified)\n`);

const browserGaps = [], bothFail = [], missingWasm = [];
let pass = 0;
const t0 = Date.now();
for (const t of tests) {
  if (!existsSync(t.wasmAbs)) { missingWasm.push(t.name); if (!quiet) console.log(`  MISS  ${t.name}  (wasm not built: ${t.wasmAbs})`); continue; }
  const br = await runRunner(t, shim);
  if (br.ok) { pass++; if (!quiet) console.log(`  PASS  ${t.name}`); continue; }
  // Classify against native.
  let cls = "BROWSER-FAIL";
  if (existsSync(nativeYos)) {
    const nat = await runRunner(t, nativeYos);
    cls = nat.ok ? "BROWSER-GAP" : "BOTH-FAIL";
    (nat.ok ? browserGaps : bothFail).push({ name: t.name, detail: br.out.split("\n").slice(0, 4).join(" | ") });
  } else browserGaps.push({ name: t.name, detail: br.out.split("\n").slice(0, 4).join(" | ") });
  if (!quiet) console.log(`  FAIL  ${t.name}  [${cls}]`);
}

const secs = Math.round((Date.now() - t0) / 1000);
console.log(`\n────────────────────────────────────────────────────────`);
console.log(`browser-engine PASS : ${pass}/${tests.length}`);
console.log(`BROWSER-GAP (native passes, browser fails) : ${browserGaps.length}`);
console.log(`BOTH-FAIL   (native fails too; xfail/known) : ${bothFail.length}`);
if (missingWasm.length) console.log(`MISSING wasm (not built) : ${missingWasm.length}`);
console.log(`elapsed : ${secs}s\n`);
if (browserGaps.length) {
  console.log("BROWSER-GAP details (these are real browser-engine gaps):");
  for (const g of browserGaps) console.log(`  • ${g.name.padEnd(40)} ${g.detail.slice(0, 90)}`);
  console.log("");
}
if (bothFail.length && !quiet) {
  console.log("BOTH-FAIL (fail on native yos too — not a browser regression):");
  for (const g of bothFail) console.log(`  • ${g.name}`);
  console.log("");
}
// Exit non-zero only on genuine browser-engine gaps (BOTH-FAIL == native parity).
process.exit(browserGaps.length ? 1 : 0);
