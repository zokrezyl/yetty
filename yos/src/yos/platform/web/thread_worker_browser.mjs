// One real thread in a browser Web Worker. Instantiates the same module
// against the same shared memory and runs the start routine by table
// index, then signals the joiner via an Atomics futex.
self.onmessage = async (event) => {
 try {
  const { module, memory, fnIdx, arg, flagAddr, shared } = event.data;
  if (!(memory.buffer instanceof SharedArrayBuffer)) { self.postMessage({ error: `memory not shared in thread worker (coordinator saw shared=${shared})` }); return; }
  const ia = new Int32Array(memory.buffer);
  Atomics.add(ia, (8 * 1024 * 1024 - 4) >> 2, 1); // "workers started" diagnostic counter
  const u8 = new Uint8Array(memory.buffer);
  const dec = new TextDecoder();

  // A thread can lock mutexes / write. (No nested thread spawn in this
  // demo, so pthread_create here is a no-op safeguard.)
  const env = {
    memory,
    pthread_create: () => 0,
    pthread_join: () => 0,
    pthread_mutex_lock: (m) => { const i = m >> 2; for (;;) { if (Atomics.compareExchange(ia, i, 0, 1) === 0) return 0; Atomics.wait(ia, i, 1); } },
    pthread_mutex_unlock: (m) => { const i = m >> 2; Atomics.store(ia, i, 0); Atomics.notify(ia, i, 1); return 0; },
    write: (fd, buf, n) => { self.postMessage({ out: dec.decode(u8.subarray(buf, buf + n)) }); return n; },
  };
  const inst = new WebAssembly.Instance(module, { env });
  inst.exports.__indirect_function_table.get(fnIdx)(arg);

  Atomics.store(ia, flagAddr >> 2, 1);
  Atomics.notify(ia, flagAddr >> 2);
  self.postMessage({ done: true });
 } catch (e) { self.postMessage({ error: String(e && e.stack || e) }); }
};
