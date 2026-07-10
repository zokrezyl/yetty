// Pooled thread worker (browser Web Worker). Same role as the node
// version: instantiate the module against the shared memory, park on the
// slot futex, run assigned thread functions by table index.
import { buildEnv, POOL_BASE, READY_OFF, ALLOC_PTR } from "./mt_libc.mjs";
import { format } from "./mt_format.mjs";
import { strictImportEnv } from "./import_manifest.mjs";

self.onmessage = (event) => {
  const { module, memory, slot } = event.data;
  const ia = new Int32Array(memory.buffer);
  const v = new DataView(memory.buffer);
  const u8 = new Uint8Array(memory.buffer);
  const dec = new TextDecoder(), enc = new TextEncoder();

  const vfs = { files: new Map(), fds: new Map(), nextFd: 1000 + slot * 100 };
  const cstr = (p) => { if (!p) return ""; let e = p; while (u8[e]) e++; return dec.decode(u8.slice(p, e)); };
  const errnoPtr = (() => { for (;;) { const c = Atomics.load(ia, ALLOC_PTR >> 2); const b = (c + 7) & ~7; if (Atomics.compareExchange(ia, ALLOC_PTR >> 2, c, b + 8) === c) return b; } })();

  const ctx = {
    pid: 1000 + slot, ppid: 1, argv: ["thread"], envp: [], errnoPtr,
    now: () => Date.now(), format, unimpl: () => {},
    emit: (fd, t) => self.postMessage({ out: t }),
    callMain: () => 0, callIndirect: (fn, a) => inst.exports.__indirect_function_table.get(fn)(a),
    write: (fd, p, n) => { const f = vfs.fds.get(fd); if (f && f.file) { const at = f.append ? f.file.data.length : f.off; const m = new Uint8Array(Math.max(f.file.data.length, at + n)); m.set(f.file.data); m.set(u8.subarray(p, p + n), at); f.file.data = m; f.off = at + n; return n; } self.postMessage({ out: dec.decode(u8.slice(p, p + n)) }); return n; },
    open: (path, fl) => { let f = vfs.files.get(path); if (!f) { if (fl & 0x200) { f = { data: new Uint8Array(0) }; vfs.files.set(path, f); } else { v.setUint32(errnoPtr, 2, true); return -1; } } if (fl & 0x400) f.data = new Uint8Array(0); const fd = vfs.nextFd++; vfs.fds.set(fd, { file: f, off: (fl & 8) ? f.data.length : 0, append: !!(fl & 8) }); return fd; },
    read: (fd, b, n) => { const f = vfs.fds.get(fd); if (!f) return 0; const end = Math.min(f.off + n, f.file.data.length); u8.set(f.file.data.subarray(f.off, end), b); const g = end - f.off; f.off = end; return g; },
    close: (fd) => { vfs.fds.delete(fd); return 0; },
    lseek: (fd, o, w) => { const f = vfs.fds.get(fd); if (!f) return -1; f.off = w === 2 ? f.file.data.length + o : w === 1 ? f.off + o : o; return f.off; },
    unlink: (p) => { vfs.files.delete(p); return 0; },
    stat: (p, b) => { u8.fill(0, b, b + 128); const f = vfs.files.get(p); if (f) { v.setUint16(b + 24, 0o100644, true); v.setUint32(b + 96, f.data.length, true); return 0; } v.setUint32(errnoPtr, 2, true); return -1; },
    fstat: (fd, b) => { u8.fill(0, b, b + 128); v.setUint16(b + 24, 0o020666, true); return 0; },
    access: () => -1, dupfd: () => vfs.nextFd++, opendir: () => 0, fdopendir: (fd) => fd, readdir: () => 0, sysctl: () => 0,
    fork: () => -1, vfork: () => -1, waitpid: () => -1, execve: () => -1, kill: () => 0,
    threadCreate: () => 11, threadJoin: () => 0,
  };

  let inst;
  try { const { env } = strictImportEnv(buildEnv(memory, ctx), module, { label: "mt-pool", onCall: ctx.unimpl }); inst = new WebAssembly.Instance(module, { env }); }
  catch (e) { self.postMessage({ error: "pool instantiate: " + e.message }); return; }
  const table = inst.exports.__indirect_function_table;
  const slotI = (POOL_BASE >> 2) + slot * 4;
  // Signal readiness and wake the engine's synchronous ensurePool() wait.
  Atomics.add(ia, READY_OFF >> 2, 1); Atomics.notify(ia, READY_OFF >> 2);

  for (;;) {
    Atomics.wait(ia, slotI, 0);
    if (Atomics.load(ia, slotI) === 0) continue;
    const fnIdx = Atomics.load(ia, slotI + 1), arg = Atomics.load(ia, slotI + 2), flagAddr = Atomics.load(ia, slotI + 3);
    let rv = 0;
    try { rv = table.get(fnIdx)(arg) >>> 0; } catch (e) { if (e.isThreadExit) rv = e.code | 0; else self.postMessage({ error: "thread: " + e.message }); }
    Atomics.store(ia, (flagAddr >> 2) + 1, rv >>> 0);   // return value for pthread_join
    Atomics.store(ia, flagAddr >> 2, 1); Atomics.notify(ia, flagAddr >> 2);
    Atomics.store(ia, slotI, 0);
  }
};
