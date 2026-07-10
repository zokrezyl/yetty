// Host side of real threads: pthread_* implemented over Web Workers /
// worker_threads + Atomics on a shared linear memory.
//
//   pthread_create → spawn a real worker that runs the start routine
//                    against the SAME shared memory (true parallelism)
//   pthread_mutex  → Atomics futex on the mutex word in shared memory
//   pthread_join   → Atomics.wait on the thread's done-flag word
//
// A process is a worker with PRIVATE memory; a thread is a worker with
// SHARED memory — same primitive, the memory decides which.
import { Worker } from "node:worker_threads";

const enc = new TextEncoder();
const dec = new TextDecoder();

// Thread done-flags live in a region the tiny guest never touches.
const FLAG_BASE = 8 * 1024 * 1024;
const workerURL = new URL("./thread_worker.mjs", import.meta.url);

export function makeThreadEnv(memory, module) {
  const ia = new Int32Array(memory.buffer);
  const u8 = new Uint8Array(memory.buffer);
  const state = { nextTid: 1, threads: new Map() };

  return {
    memory,
    pthread_create: (threadOut, attr, fnIdx, arg) => {
      const tid = state.nextTid++;
      const flagAddr = FLAG_BASE + tid * 4;
      Atomics.store(ia, flagAddr >> 2, 0); // not done
      ia[threadOut >> 2] = tid;            // guest pthread_t = int tid
      const worker = new Worker(workerURL, { workerData: { module, memory, fnIdx, arg, flagAddr } });
      worker.on("error", (e) => { console.error("thread error:", e.message); });
      state.threads.set(tid, { worker, flagAddr });
      return 0;
    },
    pthread_join: (tid, retval) => {
      const t = state.threads.get(tid);
      if (!t) return -1;
      const idx = t.flagAddr >> 2;
      // Block until the thread stores 1 — real wait, real parallelism.
      while (Atomics.load(ia, idx) === 0) Atomics.wait(ia, idx, 0);
      t.worker.terminate();
      state.threads.delete(tid);
      return 0;
    },
    pthread_mutex_lock: (m) => {
      const idx = m >> 2;
      for (;;) {
        if (Atomics.compareExchange(ia, idx, 0, 1) === 0) return 0; // acquired
        Atomics.wait(ia, idx, 1); // contended: sleep until unlocked
      }
    },
    pthread_mutex_unlock: (m) => {
      const idx = m >> 2;
      Atomics.store(ia, idx, 0);
      Atomics.notify(ia, idx, 1);
      return 0;
    },
    write: (fd, buf, count) => { process.stdout.write(dec.decode(u8.subarray(buf, buf + count))); return count; },
  };
}

// Run an exported entry (run_racy / run_locked) on a fresh shared memory.
export function runThreaded(module, entry) {
  const memory = new WebAssembly.Memory({ initial: 256, maximum: 1024, shared: true });
  const env = makeThreadEnv(memory, module);
  const inst = new WebAssembly.Instance(module, { env });
  return inst.exports[entry]();
}
