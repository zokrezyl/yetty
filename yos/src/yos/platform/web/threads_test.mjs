// Prove real threads: run_racy (no lock) must lose updates under genuine
// parallelism; run_locked (Atomics mutex) must hit the exact total. A
// cooperative/sequential fake would get the exact total BOTH times — the
// race is the proof the threads truly run at once.
import { readFile } from "node:fs/promises";
import { runThreaded } from "./threads_host.mjs";

const module = await WebAssembly.compile(await readFile(new URL("./threads_demo.wasm", import.meta.url)));
const EXPECTED = 4 * 200000;

console.log("--- run_racy (4 threads, no lock) ---");
const racy = runThreaded(module, "run_racy");
console.log("--- run_locked (4 threads, Atomics mutex) ---");
const locked = runThreaded(module, "run_locked");

const racedLost = racy < EXPECTED;
const lockedExact = locked === EXPECTED;
console.log(`\nracy=${racy} (lost ${EXPECTED - racy} updates), locked=${locked}, expected=${EXPECTED}`);
console.log(`${racedLost ? "ok  " : "FAIL"} real parallelism: racy lost updates`);
console.log(`${lockedExact ? "ok  " : "FAIL"} mutex correctness: locked exact`);
process.exit(racedLost && lockedExact ? 0 : 1);
