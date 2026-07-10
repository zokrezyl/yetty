// Browser-parity test harness (issue #21, milestone 2).
//
// Runs a curated set of small wasm guests against BOTH backends and
// compares stdout + exit code:
//   - DESKTOP: the native yos host binary (the source of truth);
//   - BROWSER/NODE: the JS process engine in yos_proc.mjs.
//
// The desktop result is the contract. A guest "passes parity" when the
// node engine reproduces the desktop stdout and exit code. Guests the
// node engine cannot yet serve fail loudly (no silent return-0), and that
// gap is reported, not hidden.
//
// Run: node parity_runner.mjs            (curated contract; exit non-zero
//                                          if any REQUIRED guest regresses)
//      node parity_runner.mjs --sweep    (broad sweep: every fork-matrix
//                                          guest, desktop vs node, tallied)

import { runProgram } from "./yos_proc.mjs";
import { readFile } from "node:fs/promises";
import { execFile } from "node:child_process";
import { existsSync, readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, "..", "..", "..", "..");
const yosBin = join(repo, "build-linux", "src", "yos", "yos");
const wasmDir = join(repo, "build-linux", "tests", "ut", "libc");

// The contract. `required:true` guests MUST match desktop for the run to
// pass; others are reported as a worklist (known browser gaps).
const CONTRACT = [
  { name: "test_fork_basic", what: "fork: distinct parent/child, parent waits", required: true },
  { name: "test_fork_child_getpid_ne_parent", what: "fork: child pid != parent pid", required: true },
  { name: "test_fork_child_write_stdout", what: "fork: child writes to stdout", required: true },
  { name: "test_fork_child_getenv_inherited", what: "fork: env inherited by child", required: true },
  { name: "test_fork_child_iso_cwd_parent_unchanged", what: "cwd: child chdir does not move parent", required: true },
  { name: "test_fork_child_dup_then_write", what: "dup: dup(fd) then write", required: true },
  { name: "test_dup2_pipe_pair", what: "dup2 + pipe: redirect over a pipe pair", required: true },
  { name: "test_fork_child_pipe_epipe_on_closed_reader", what: "pipe: EPIPE when reader closed", required: true },
  { name: "test_fork_child_fd_cloexec_inherited_across_fork", what: "fd: inheritance across fork", required: true },
  { name: "test_fork_channel_from_stdio", what: "fork: live socketpair channel parent<->child", required: false, note: "needs concurrent fork scheduling + kqueue readiness (M3) — cooperative fork runs the child to completion before the parent's post-fork write" },
  { name: "test_pthread_create_join", what: "pthread: create + join basics", required: true },
  { name: "test_ssh_poll_dev_null", what: "poll: /dev/null readiness", required: true },
  { name: "test_ssh_ppoll", what: "ppoll: readiness", required: true },
];

function pickWasm(name) {
  const a = join(wasmDir, `${name}.wasm`);
  const r = join(wasmDir, `${name}-raw.wasm`);
  if (existsSync(a)) return a;
  if (existsSync(r)) return r;
  return null;
}

// --sweep: run EVERY built fork-matrix guest through both backends and
// tally how many the node engine reproduces exactly. A broad regression
// net beyond the curated contract; categorises misses as unimplemented
// imports (loud gaps) vs. behavioural mismatches.
async function runSweep() {
  const names = readdirSync(wasmDir)
    .filter((f) => /^test_fork_child_.*\.wasm$/.test(f) && !/-raw\.wasm$/.test(f))
    .map((f) => f.replace(/\.wasm$/, ""))
    .sort();
  let pass = 0; const gaps = [], mismatch = [];
  for (const name of names) {
    const wasmPath = join(wasmDir, `${name}.wasm`);
    const desktop = await runDesktop(wasmPath);
    let node; try { node = await runNode(wasmPath, name); } catch (e) { node = { out: "", code: "throw", error: e.message }; }
    if (norm(desktop.out) === norm(node.out) && desktop.code === node.code) pass++;
    else if (node.unimpl || node.error) gaps.push(`${name} (${node.unimpl ? "unimpl:" + node.unimpl : (node.error || "").slice(0, 32)})`);
    else mismatch.push(`${name} (desktop=${desktop.code} node=${node.code})`);
  }
  console.log(`\nBroad sweep: ${pass}/${names.length} fork-matrix guests reproduce desktop exactly`);
  console.log(`  ${gaps.length} unimplemented-import gaps, ${mismatch.length} behavioural mismatches\n`);
  if (gaps.length) { console.log("UNIMPLEMENTED (loud gaps — missing libc on the browser engine):"); for (const g of gaps) console.log("  " + g); }
  if (mismatch.length) { console.log("\nMISMATCH (ran but differs — signal-delivery / concurrent-fork / struct-fill gaps):"); for (const m of mismatch) console.log("  " + m); }
  console.log("");
}

function runDesktop(wasmPath) {
  return new Promise((resolve) => {
    execFile(yosBin, [wasmPath], { timeout: 20000, encoding: "buffer" }, (err, stdout) => {
      const code = err && typeof err.code === "number" ? err.code : err ? 1 : 0;
      resolve({ out: stdout.toString("utf8"), code, signal: err && err.signal });
    });
  });
}

async function runNode(wasmPath, name) {
  const mod = await WebAssembly.compile(await readFile(wasmPath));
  let out = "";
  let unimpl = null;
  const r = runProgram(
    mod,
    [name],
    (fd, t) => { out += t; },
    (n) => { if (!unimpl) unimpl = n; },
  );
  return { out, code: typeof r.exitCode === "number" ? r.exitCode : r.exitCode, error: r.error, unimpl };
}

function norm(s) { return s.replace(/\r/g, ""); }

if (process.argv.includes("--sweep")) { await runSweep(); process.exit(0); }

const results = [];
for (const item of CONTRACT) {
  const wasmPath = pickWasm(item.name);
  if (!wasmPath) { results.push({ ...item, status: "NO-WASM" }); continue; }
  const desktop = await runDesktop(wasmPath);
  let node;
  try { node = await runNode(wasmPath, item.name); }
  catch (e) { node = { out: "", code: "throw", error: e.message }; }
  const outMatch = norm(desktop.out) === norm(node.out);
  const codeMatch = desktop.code === node.code;
  const pass = outMatch && codeMatch;
  results.push({ ...item, status: pass ? "PASS" : "FAIL", desktop, node, outMatch, codeMatch });
}

// ---- report ----
let failedRequired = 0;
const pad = (s, n) => String(s).padEnd(n);
console.log("\nBrowser-parity contract (desktop yos vs. node yos_proc engine)\n");
console.log(pad("guest", 50), pad("desktop", 18), pad("node", 18), "verdict");
console.log("-".repeat(100));
for (const r of results) {
  const d = r.desktop ? `exit=${r.desktop.code} out=${JSON.stringify(r.desktop.out).slice(0, 22)}` : "-";
  const n = r.node ? `exit=${r.node.code}${r.node.unimpl ? " unimpl:" + r.node.unimpl : ""}` : "-";
  let verdict = r.status;
  if (r.status === "FAIL") {
    if (r.node && r.node.unimpl) verdict += ` (unimpl:${r.node.unimpl})`;
    if (r.required) { verdict += "  <REQUIRED>"; failedRequired++; }
    else verdict = "KNOWN-GAP";
  }
  console.log(pad(r.name, 50), pad(d, 18), pad(n, 18), verdict);
}
console.log("-".repeat(100));
const passes = results.filter((r) => r.status === "PASS").length;
console.log(`\n${passes}/${results.length} parity, ${failedRequired} required failures`);
for (const r of results) if (r.status === "FAIL" && !r.required && r.note) console.log(`  KNOWN-GAP ${r.name}: ${r.note}`);
console.log("");
process.exit(failedRequired ? 1 : 0);
