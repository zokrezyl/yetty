// Regression: a thread created by a fork() CHILD must run against the
// child's own linear memory, not the root/parent address space the worker
// pool was first bound to (issue #23, blocker #1).
//
// Two guests, both on the real-thread engine (node worker_threads):
//   fork_thread_demo     — one fork child spawns a thread. Correct output:
//       t7 (thread saw the child's marker 7, not the root's 100), c7, p42.
//       Before the fix the child's thread ran in a worker bound to the ROOT
//       memory, so the child printed t0 (never t7).
//   fork_thread_siblings — two sibling children reuse the engine's per-depth
//       memory and EACH spawn a thread. Correct output: t7,r1,t8,r1,p42 —
//       each sibling's thread sees its own marker (7 then 8) and runs exactly
//       once (r1). Guards that the second sibling's pool does not collide with
//       the first sibling's asynchronously-terminated workers on a reused
//       control region (a double-wake would show r2).
import { Worker } from "node:worker_threads";
import { readFile } from "node:fs/promises";
import { runMtProgram } from "./mt_engine.mjs";

const workerURL = new URL("./mt_pool_worker_node.mjs", import.meta.url);

async function runGuest(wasmFile, argv0) {
  const module = await WebAssembly.compile(await readFile(new URL(`./${wasmFile}`, import.meta.url)));
  let out = "";
  const res = await runMtProgram(module, [argv0], {
    onOutput: (fd, t) => { out += t; },
    onUnimpl: (n) => process.stderr.write(`[unimpl ${n}]\n`),
    spawnPoolWorker: ({ module: mod, memory, slot }) =>
      new Worker(workerURL, { workerData: { module: mod, memory, slot } }),
  });
  return { lines: out.split("\n").map((l) => l.trim()).filter(Boolean), res };
}

let failed = 0;
const check = (label, ok) => { if (!ok) failed++; console.log(`  ${ok ? "ok  " : "FAIL"} ${label}`); };

{
  const { lines, res } = await runGuest("fork_thread_demo.wasm", "fork_thread_demo");
  const has = (t) => lines.includes(t);
  console.log(`fork_thread_demo → ${JSON.stringify(lines)} (procs ${res.procs}${res.error ? ", error " + res.error : ""})`);
  check("child thread saw child memory (t7)", has("t7"));
  check("child main saw child memory (c7)", has("c7"));
  check("parent kept its own memory (p42)", has("p42"));
  check("exit 0", res.exitCode === 0);
}

{
  const { lines, res } = await runGuest("fork_thread_siblings.wasm", "fork_thread_siblings");
  const has = (t) => lines.includes(t);
  console.log(`fork_thread_siblings → ${JSON.stringify(lines)} (procs ${res.procs}${res.error ? ", error " + res.error : ""})`);
  check("sibling 0 thread saw its own memory (t7)", has("t7"));
  check("sibling 1 thread saw its own memory (t8)", has("t8"));
  check("no thread double-execution (r1, not r2)", has("r1") && !has("r2"));
  check("parent kept its own memory (p42)", has("p42"));
  check("exit 0", res.exitCode === 0);
}

console.log(failed ? `\nFAIL — ${failed} check(s) failed` : `\nPASS fork-child thread memory ownership`);
process.exit(failed ? 1 : 0);
