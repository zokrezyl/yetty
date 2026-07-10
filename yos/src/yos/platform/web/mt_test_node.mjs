// Run the multi-thread perfstress in node with real worker_threads.
import { Worker } from "node:worker_threads";
import { readFile } from "node:fs/promises";
import { runMtProgram } from "./mt_engine.mjs";

const module = await WebAssembly.compile(await readFile(new URL("./tools/perfstress_mt.wasm", import.meta.url)));
const workerURL = new URL("./mt_pool_worker_node.mjs", import.meta.url);

const argv = process.argv.slice(2);
const args = argv.length ? ["perfstress", ...argv] : ["perfstress"];

let out = "";
const res = await runMtProgram(module, args, {
  onOutput: (fd, t) => { out += t; process.stdout.write(t); },
  onUnimpl: (n) => { if (!globalThis.__seen) globalThis.__seen = new Set(); if (!globalThis.__seen.has(n)) { globalThis.__seen.add(n); process.stderr.write(`[unimpl ${n}]\n`); } },
  spawnPoolWorker: ({ module, memory, slot }) => new Worker(workerURL, { workerData: { module, memory, slot } }),
});
// The full perf-stress workload (fork + real worker-thread mutex/cond/rwlock +
// file/stat/dup/dir/mmap/time/rand) must finish with the "perf-stress ok"
// verdict — this is the regression net for real threads in the browser engine.
const ok = out.includes("perf-stress ok");
console.error(`\n=== exit ${res.exitCode}, procs ${res.procs}${res.error ? ", error " + res.error : ""} — ${ok ? "PASS perf-stress ok" : "FAIL"} ===`);
// pool workers keep the event loop alive; exit explicitly.
process.exit(ok ? 0 : 1);
