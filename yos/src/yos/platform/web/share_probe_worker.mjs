self.onmessage = (e) => {
  const mem = e.data.memory;
  const ok = mem.buffer instanceof SharedArrayBuffer;
  if (ok) { const ia = new Int32Array(mem.buffer); Atomics.store(ia, 0, 42); }
  self.postMessage({ shared: ok });
};
