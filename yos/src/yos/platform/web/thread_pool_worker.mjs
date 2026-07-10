// A pooled thread worker. Pre-spawned during init (while the event loop
// is free), it instantiates the module against the shared memory once,
// marks itself ready, then parks on an Atomics futex waiting for work.
// pthread_create assigns work by writing the slot and notifying — which
// reaches this already-running worker even while the coordinator is
// blocked in pthread_join. This is why a pool is required in browsers:
// a worker spawned by an about-to-block thread never starts.
const POOL_BASE = 7 * 1024 * 1024;
const POOL_SIZE = 8;
const READY_I = (POOL_BASE >> 2) + POOL_SIZE * 4;

self.onmessage = (event) => {
  const { module, memory, slot } = event.data;
  const ia = new Int32Array(memory.buffer);
  const u8 = new Uint8Array(memory.buffer);
  const dec = new TextDecoder();
  const env = {
    memory,
    pthread_create: () => 0, pthread_join: () => 0,
    pthread_mutex_lock: (m) => { const i = m >> 2; for (;;) { if (Atomics.compareExchange(ia, i, 0, 1) === 0) return 0; Atomics.wait(ia, i, 1); } },
    pthread_mutex_unlock: (m) => { const i = m >> 2; Atomics.store(ia, i, 0); Atomics.notify(ia, i, 1); return 0; },
    write: (fd, buf, n) => { self.postMessage({ out: dec.decode(u8.slice(buf, buf + n)) }); return n; },
  };
  let inst;
  try { inst = new WebAssembly.Instance(module, { env }); }
  catch (e) { self.postMessage({ error: "pool worker instantiate: " + e.message }); return; }
  const table = inst.exports.__indirect_function_table;

  const slotI = (POOL_BASE >> 2) + slot * 4; // [work, fnIdx, arg, flagAddr]
  Atomics.add(ia, READY_I, 1); // signal ready

  for (;;) {
    Atomics.wait(ia, slotI, 0);            // park until assigned work
    if (Atomics.load(ia, slotI) === 0) continue;
    const fnIdx = Atomics.load(ia, slotI + 1);
    const arg = Atomics.load(ia, slotI + 2);
    const flagAddr = Atomics.load(ia, slotI + 3);
    try { table.get(fnIdx)(arg); }
    catch (e) { self.postMessage({ error: "thread run: " + e.message }); }
    Atomics.store(ia, flagAddr >> 2, 1);   // thread done
    Atomics.notify(ia, flagAddr >> 2);
    Atomics.store(ia, slotI, 0);           // slot idle again
  }
};
