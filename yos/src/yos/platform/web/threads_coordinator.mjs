// Browser coordinator. Runs the guest entry (run_racy/run_locked) here —
// a Worker, where Atomics.wait is allowed. Threads are a PRE-SPAWNED
// pool of Web Workers (Emscripten model): they're created during async
// init while the event loop is free, so they're alive and parked before
// the guest's synchronous pthread_create/join blocks anything.
const POOL_BASE = 7 * 1024 * 1024;
const POOL_SIZE = 8;
const READY_I = (POOL_BASE >> 2) + POOL_SIZE * 4;
const FLAG_BASE = 8 * 1024 * 1024;
const dec = new TextDecoder();

function makeMainEnv(memory, state) {
  const i32 = new Int32Array(memory.buffer);
  const u8 = new Uint8Array(memory.buffer);
  return {
    memory,
    pthread_create: (threadOut, attr, fnIdx, arg) => {
      const tid = state.nextTid++;
      const slot = (tid - 1) % POOL_SIZE; // distinct slots → distinct live workers
      const flagAddr = FLAG_BASE + tid * 4;
      Atomics.store(i32, flagAddr >> 2, 0);
      i32[threadOut >> 2] = tid;
      const slotI = (POOL_BASE >> 2) + slot * 4;
      Atomics.store(i32, slotI + 1, fnIdx);
      Atomics.store(i32, slotI + 2, arg);
      Atomics.store(i32, slotI + 3, flagAddr);
      Atomics.store(i32, slotI, 1);        // hand work to the pooled worker
      Atomics.notify(i32, slotI);
      state.threads.set(tid, { flagAddr });
      return 0;
    },
    pthread_join: (tid) => {
      const t = state.threads.get(tid);
      const idx = t.flagAddr >> 2;
      while (Atomics.load(i32, idx) === 0) Atomics.wait(i32, idx, 0);
      return 0;
    },
    pthread_mutex_lock: (m) => { const i = m >> 2; for (;;) { if (Atomics.compareExchange(i32, i, 0, 1) === 0) return 0; Atomics.wait(i32, i, 1); } },
    pthread_mutex_unlock: (m) => { const i = m >> 2; Atomics.store(i32, i, 0); Atomics.notify(i32, i, 1); return 0; },
    write: (fd, buf, n) => { self.postMessage({ out: dec.decode(u8.slice(buf, buf + n)) }); return n; },
  };
}

self.onmessage = async () => {
  try {
    const module = await WebAssembly.compile(await (await fetch("./threads_demo.wasm")).arrayBuffer());
    const memory = new WebAssembly.Memory({ initial: 256, maximum: 1024, shared: true });
    const i32 = new Int32Array(memory.buffer);

    // Pre-spawn the pool and wait (async, event loop free) until every
    // worker has instantiated and parked — only then run the guest.
    const pool = [];
    for (let slot = 0; slot < POOL_SIZE; slot++) {
      const w = new Worker(new URL("./thread_pool_worker.mjs", import.meta.url), { type: "module" });
      w.onmessage = (e) => { if (e.data.out) self.postMessage({ out: e.data.out }); if (e.data.error) self.postMessage({ error: e.data.error }); };
      w.postMessage({ module, memory, slot });
      pool.push(w);
    }
    for (let i = 0; i < 100 && Atomics.load(i32, READY_I) < POOL_SIZE; i++) await new Promise((r) => setTimeout(r, 50));
    if (Atomics.load(i32, READY_I) < POOL_SIZE) { self.postMessage({ error: `pool not ready: ${Atomics.load(i32, READY_I)}/${POOL_SIZE}` }); return; }

    const state = { nextTid: 1, threads: new Map() };
    const inst = new WebAssembly.Instance(module, { env: makeMainEnv(memory, state) });
    const racy = inst.exports.run_racy();
    state.nextTid = 1; state.threads.clear();
    const locked = inst.exports.run_locked();
    self.postMessage({ result: { racy, locked, expected: 4 * 200000 } });
  } catch (e) {
    self.postMessage({ error: String(e && e.stack || e) });
  }
};
