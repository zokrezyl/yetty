// One real OS thread (node worker_threads; the browser uses Web Workers
// identically). Instantiates the SAME wasm module against the SAME shared
// memory, runs the thread's start routine by function-table index, then
// signals completion via an Atomics futex the joiner waits on.
import { parentPort, workerData } from "node:worker_threads";
import { makeThreadEnv } from "./threads_host.mjs";

const { module, memory, fnIdx, arg, flagAddr } = workerData;
const ia = new Int32Array(memory.buffer);

// A thread can itself lock mutexes, write, and spawn more threads.
const env = makeThreadEnv(memory, module);
const inst = new WebAssembly.Instance(module, { env });

// Call start_routine(arg) by its function-table index — real pthread
// semantics (the routine is a function pointer, i.e. a table index).
const table = inst.exports.__indirect_function_table;
table.get(fnIdx)(arg);

// Signal the joiner: store 1 at the flag word and wake any waiter.
Atomics.store(ia, flagAddr >> 2, 1);
Atomics.notify(ia, flagAddr >> 2);
parentPort.postMessage("done");
