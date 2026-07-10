// Multi-thread + fork engine for one process tree. Environment-agnostic:
// the caller supplies spawnPoolWorker (node worker_threads OR browser Web
// Worker) and an output sink.
//
// PROCESS MEMORY OWNERSHIP (issue #23). Every process — the root and every
// fork() child — owns its OWN linear memory. A thread must run against the
// memory of the process that created it: after fork() the child has a
// copied address space, so a thread the child spawns has to see the child's
// bytes, not the root's. We therefore bind a worker pool to EACH process's
// memory. The pool workers park on futexes in that process's control region;
// join/mutex/cond/rwlock are Atomics on the same memory.
//
// The ROOT pool is booted eagerly, awaiting readiness on the event loop
// (awaitPoolReady). Fork/exec children boot their pool lazily on the first
// pthread_create, waiting for readiness synchronously (ensurePool). The sync
// wait works under node's worker_threads (they boot from workerData on their
// own threads) but NOT in the browser: a Web Worker's module load needs an
// event-loop turn the coordinator is not giving it while blocked in
// Atomics.wait. So today, in the browser, only the eagerly-booted ROOT
// process threads; a fork child that calls pthread_create in the browser
// fails loudly ("pool not ready") rather than silently running on the wrong
// memory. Node runs the full fork-child-thread case correctly. Lifting the
// browser limitation needs an asyncify-suspend on pthread_create (boot the
// pool during an async yield, then rewind) — future work, tracked with the
// mt_fork_thread_browser_test.mjs known-gap harness.
//
// FORK IS COOPERATIVE, NOT CONCURRENT (documented limitation, issue #23
// blocker #3). handleFork() snapshots the parent's memory (asyncify) and runs
// the child to completion BEFORE the parent resumes; waitpid() only reaps a
// child that has already exited. This models fork/exec/wait fork-trees and a
// child that runs to completion, but NOT live parent<->child interaction
// (pipes written after fork while the child is still alive, event-loop
// readiness across processes, job-control). A concurrent process model would
// need Worker-backed, independently-runnable processes and is out of scope
// here.
import {
  buildEnv, makeMem, MEM_PAGES, ALLOC_PTR, POOL_BASE, READY_OFF,
  FLAG_BASE, ASYNC_BUF, ASYNC_SZ, POOL_SIZE,
} from "./mt_libc.mjs";
import { format } from "./mt_format.mjs";
import { HEAP_START } from "./mt_libc.mjs";
import { strictImportEnv } from "./import_manifest.mjs";

const ASYNCIFY_REWINDING = 2;
const enc = new TextEncoder();
const dec = new TextDecoder();

// Minimal in-memory VFS shared across the tree (host-side JS).
function makeVfs() {
  const now = Math.floor(Date.now() / 1000);
  return { files: new Map(), fds: new Map(), dirs: new Map(), nextFd: 5, cwd: "/", now };
}

export async function runMtProgram(module, argv, opts) {
  const { onOutput, spawnPoolWorker, onUnimpl } = opts;
  const memory = makeMem();
  const ia = new Int32Array(memory.buffer);
  Atomics.store(ia, ALLOC_PTR >> 2, HEAP_START); // heap starts at 1 MiB, above static data
  const vfs = makeVfs();
  let nextPid = 1;
  const procs = [];

  // The shared build has no crt _start — it exports __main_argc_argv and
  // __wasm_init_memory. fresh procs (root, exec) init memory + ctors and
  // build argv; fork children inherit those via the copied memory.
  const spawnProc = (mod, av, ppid, mem, fresh) => {
    if (procs.length > 1500) throw new Error("runaway fork: " + procs.length + " procs");
    const pid = nextPid++;
    const proc = { pid, ppid, argv: av, comm: (av[0] || "?").replace(/.*\//, ""), exited: false, reaped: false, exitCode: 0, mem, mod, pool: null, nextTid: 0, pia: null, forkReturn: 0, forkPending: false, pendingRewind: false, argc: 0, argvPtr: 0 };
    procs.push(proc);
    proc.ctx = makeCtx(proc, mod);
    const { env } = strictImportEnv(buildEnv(proc.mem, proc.ctx), mod, { label: "mt", onCall: proc.ctx.unimpl });
    proc.inst = new WebAssembly.Instance(mod, { env });
    proc.ctx.inst = proc.inst;
    proc.ctx.errnoPtr = allocIn(proc.mem, 4);
    if (fresh) {
      if (proc.inst.exports.__wasm_init_memory) proc.inst.exports.__wasm_init_memory();
      if (proc.inst.exports.__wasm_call_ctors) proc.inst.exports.__wasm_call_ctors();
      // build argv array in the process memory
      const v = new DataView(proc.mem.buffer), u8 = new Uint8Array(proc.mem.buffer);
      const arr = allocIn(proc.mem, (av.length + 1) * 4);
      for (let i = 0; i < av.length; i++) { const b = enc.encode(av[i] + "\0"); const p = allocIn(proc.mem, b.length, 1); u8.set(b, p); v.setUint32(arr + i * 4, p, true); }
      v.setUint32(arr + av.length * 4, 0, true);
      proc.argc = av.length; proc.argvPtr = arr;
    }
    return proc;
  };
  const allocIn = (mem, n, align = 8) => {
    const a = new Int32Array(mem.buffer);
    for (;;) { const cur = Atomics.load(a, ALLOC_PTR >> 2); const base = (cur + (align - 1)) & ~(align - 1); if (Atomics.compareExchange(a, ALLOC_PTR >> 2, cur, base + n) === cur) return base; }
  };

  // Spawn a worker pool bound to THIS process's memory. A fork child inherits
  // the parent's copied control region, so reset the readiness counter + slot
  // words before its own workers boot.
  function spawnPoolWorkers(proc) {
    const pia = new Int32Array(proc.mem.buffer);
    Atomics.store(pia, READY_OFF >> 2, 0);
    for (let slot = 0; slot < POOL_SIZE; slot++) Atomics.store(pia, (POOL_BASE >> 2) + slot * 4, 0);
    proc.pool = [];
    for (let slot = 0; slot < POOL_SIZE; slot++) proc.pool.push(spawnPoolWorker({ module: proc.mod, memory: proc.mem, slot }));
  }
  // Async readiness wait — yields to the event loop so a browser Web Worker
  // can actually load + boot. Used for the ROOT pool, booted eagerly before
  // the synchronous run starts.
  async function awaitPoolReady(proc) {
    const pia = new Int32Array(proc.mem.buffer);
    const idx = READY_OFF >> 2;
    for (let i = 0; i < 400 && Atomics.load(pia, idx) < POOL_SIZE; i++) await new Promise((r) => setTimeout(r, 25));
    if (Atomics.load(pia, idx) < POOL_SIZE) throw new Error(`pool not ready: ${Atomics.load(pia, idx)}/${POOL_SIZE}`);
  }
  // Ensure THIS process has a pool, booting it lazily on the first
  // pthread_create (most fork children never thread, so they pay nothing).
  // Readiness is a synchronous Atomics wait: node worker_threads boot from
  // workerData on their own threads and bump READY_OFF (with an
  // Atomics.notify), so blocking here does not deadlock. NOTE: a browser Web
  // Worker cannot boot while the coordinator is blocked in Atomics.wait (its
  // module load needs an event-loop turn), so this lazy path serves the node
  // engine; in the browser only the eagerly-booted root pool threads today
  // (see the file header). The root already has a pool, so ensurePool() is a
  // no-op for it and the sync wait is reached only by fork/exec children.
  function ensurePool(proc) {
    if (proc.pool) return;
    spawnPoolWorkers(proc);
    const pia = new Int32Array(proc.mem.buffer);
    const idx = READY_OFF >> 2;
    for (let guard = 0; Atomics.load(pia, idx) < POOL_SIZE; guard++) {
      if (guard > 800) throw new Error(`pool not ready: ${Atomics.load(pia, idx)}/${POOL_SIZE}`);
      Atomics.wait(pia, idx, Atomics.load(pia, idx), 25);
    }
  }
  // Tear down a process's pool once it has exited (its threads are joined, so
  // the workers are parked). Frees the workers and lets a pooled child memory
  // be reused by the next sibling without two pools racing on one region.
  function killPool(proc) {
    if (!proc.pool) return;
    for (const worker of proc.pool) { try { worker.terminate(); } catch { /* best effort */ } }
    proc.pool = null;
  }

  function makeCtx(proc, mod) {
    const mem = proc.mem;
    const v = new DataView(mem.buffer), u8 = new Uint8Array(mem.buffer), pia = new Int32Array(mem.buffer);
    proc.pia = pia; // this process's atomics view, used by fork/thread bookkeeping
    const cstr = (p) => { if (!p) return ""; let e = p; while (u8[e]) e++; return dec.decode(u8.slice(p, e)); };
    const ctx = {
      pid: proc.pid, ppid: proc.ppid, argv: proc.argv, envp: ["PATH=/bin", "HOME=/", "TERM=xterm", "PWD=/", "TMPDIR=/tmp"],
      errnoPtr: 0, now: () => (vfs.now * 1000 + (typeof performance !== "undefined" ? performance.now() : Date.now()) % 1e9),
      format, unimpl: onUnimpl, emit: (fd, t) => onOutput(fd, t),
      callMain: (argc, argv) => proc.inst.exports.main(argc, argv),
      callIndirect: (fnIdx, arg) => proc.inst.exports.__indirect_function_table.get(fnIdx)(arg),
      // ---- VFS ----
      write: (fd, p, n) => {
        const f = vfs.fds.get(fd);
        if (f && f.file) { const at = f.append ? f.file.data.length : f.off; const m = new Uint8Array(Math.max(f.file.data.length, at + n)); m.set(f.file.data); m.set(u8.subarray(p, p + n), at); f.file.data = m; f.off = at + n; return n; }
        onOutput(fd, dec.decode(u8.slice(p, p + n))); return n;
      },
      open: (path, fl) => {
        let file = vfs.files.get(path);
        if (!file) { if (fl & 0x200) { file = { data: new Uint8Array(0) }; vfs.files.set(path, file); } else { v.setUint32(ctx.errnoPtr, 2, true); return -1; } }
        if (fl & 0x400) file.data = new Uint8Array(0);
        const fd = vfs.nextFd++; vfs.fds.set(fd, { file, off: (fl & 8) ? file.data.length : 0, append: !!(fl & 8) }); return fd;
      },
      read: (fd, b, n) => { const f = vfs.fds.get(fd); if (!f || !f.file) return 0; const end = Math.min(f.off + n, f.file.data.length); u8.set(f.file.data.subarray(f.off, end), b); const g = end - f.off; f.off = end; return g; },
      close: (fd) => { vfs.fds.delete(fd); vfs.dirs.delete(fd); return 0; },
      lseek: (fd, o, w) => { const f = vfs.fds.get(fd); if (!f) return -1; f.off = w === 2 ? f.file.data.length + o : w === 1 ? f.off + o : o; return f.off; },
      // dup/dup2 share the SAME open file description (same entry object), so
      // the file offset is shared — sequential writes via dup'd fds append.
      dup: (fd) => { const f = vfs.fds.get(fd); if (!f) return -1; const n = vfs.nextFd++; vfs.fds.set(n, f); return n; },
      dup2: (oldfd, newfd) => { const f = vfs.fds.get(oldfd); if (!f) return -1; vfs.fds.set(newfd, f); return newfd; },
      chdir: (path) => { vfs.cwd = path; return 0; }, getcwd: () => vfs.cwd,
      unlink: (path) => { vfs.files.delete(path); return 0; },
      stat: (path, b) => { const f = vfs.files.get(path); u8.fill(0, b, b + 128); if (f) { v.setUint16(b + 24, 0o100644, true); v.setUint32(b + 96, f.data.length, true); return 0; } if (path === "/" || path === "/tmp" || path === ".") { v.setUint16(b + 24, 0o040755, true); return 0; } v.setUint32(ctx.errnoPtr, 2, true); return -1; },
      fstat: (fd, b) => { u8.fill(0, b, b + 128); const f = vfs.fds.get(fd); if (f && f.file) { v.setUint16(b + 24, 0o100644, true); v.setUint32(b + 96, f.file.data.length, true); } else { v.setUint16(b + 24, 0o020666, true); } return 0; },
      access: (path) => (vfs.files.has(path) ? 0 : -1),
      dupfd: () => vfs.nextFd++,
      opendir: (path) => { const names = [...vfs.files.keys()].filter((p) => p.startsWith((path === "/" ? "/" : path + "/")) && !p.slice((path === "/" ? 1 : path.length + 1)).includes("/")).map((p) => p.slice(path === "/" ? 1 : path.length + 1)); const h = vfs.nextFd++; vfs.dirs.set(h, { names: [".", "..", ...names], idx: 0, buf: 0 }); return h; },
      fdopendir: (fd) => fd,
      readdir: (h) => { const d = vfs.dirs.get(h); if (!d || d.idx >= d.names.length) return 0; if (!d.buf) d.buf = ctx.alloc280(); const name = d.names[d.idx++]; u8.fill(0, d.buf, d.buf + 280); v.setUint16(d.buf + 16, 280, true); u8[d.buf + 18] = 0; const nb = enc.encode(name); v.setUint16(d.buf + 20, nb.length, true); u8.set(nb, d.buf + 24); return d.buf; },
      alloc280: () => { for (;;) { const cur = Atomics.load(pia, ALLOC_PTR >> 2); const base = (cur + 7) & ~7; if (Atomics.compareExchange(pia, ALLOC_PTR >> 2, cur, base + 280) === cur) return base; } },
      sysctl: (namePtr, namelen, oldp, oldlenp) => {
        const mib = []; for (let i = 0; i < namelen; i++) mib.push(v.getInt32(namePtr + i * 4, true));
        if (mib[0] === 1 && mib[1] === 14) {
          const sel = mib[2], pidArg = mib[3];
          const live = procs.filter((p) => !p.reaped && (sel !== 1 || p.pid === pidArg));
          const need = live.length * 768; const buflen = oldlenp ? v.getUint32(oldlenp, true) : 0;
          if (!oldp || buflen < need) { if (oldlenp) v.setUint32(oldlenp, need, true); if (!oldp) return 0; v.setUint32(ctx.errnoPtr, 12, true); return -1; }
          let o = oldp; for (const p of live) { u8.fill(0, o, o + 768); v.setInt32(o, 768, true); v.setInt32(o + 40, p.pid, true); v.setInt32(o + 44, p.ppid, true); v.setInt32(o + 48, p.pid, true); v.setInt32(o + 56, p.pid, true); v.setInt32(o + 516, 1, true); v.setInt32(o + 520, p.pid, true); u8[o + 308] = p.exited ? 5 : 2; const c = enc.encode(p.comm.slice(0, 19)); u8.set(c, o + 367); o += 768; }
          if (oldlenp) v.setUint32(oldlenp, need, true); return 0;
        }
        if (oldlenp) v.setUint32(oldlenp, 0, true); return 0;
      },
      // ---- process ----
      fork: () => doFork(proc),
      waitpid: (pid, st) => { const k = procs.find((p) => p.ppid === proc.pid && p.exited && !p.reaped && (pid <= 0 || p.pid === pid)); if (!k) return -1; k.reaped = true; if (st) v.setUint32(st, (k.exitCode & 0xff) << 8, true); return k.pid; },
      execve: (path, argvPtr) => { const name = path.replace(/.*\//, ""); const m = opts.tools && opts.tools.get(name); if (!m) { v.setUint32(ctx.errnoPtr, 2, true); return -1; } const args = []; for (let p = argvPtr; ; p += 4) { const sp = v.getUint32(p, true); if (!sp) break; args.push(cstr(sp)); } const em = makeMem(); Atomics.store(new Int32Array(em.buffer), ALLOC_PTR >> 2, HEAP_START); const child = spawnProc(m, args.length ? args : [name], proc.pid, em, true); runProc(child, 100); killPool(child); child.reaped = true; child.inst = null; const e = new Error("exec"); e.isExit = true; e.code = child.exitCode; throw e; },
      kill: (pid, sig) => { const p = procs.find((q) => q.pid === pid); if (p && !p.exited) { p.exited = true; p.exitCode = 128 + sig; } return 0; },
      // ---- threads ----
      // All slot/flag words live in THIS process's memory (pia), and the pool
      // is bound to THIS process's memory — so a thread the process creates
      // runs against the right address space even after fork() (issue #23).
      threadCreate: (out, fnIdx, arg) => {
        ensurePool(proc);
        // 8 bytes per thread in the flag region: [done flag @0, return value @4]
        // so pthread_join can hand the thread's return value back to the guest.
        const tid = ++proc.nextTid; const slot = (tid - 1) % POOL_SIZE; const flagAddr = FLAG_BASE + tid * 8;
        Atomics.store(pia, flagAddr >> 2, 0); Atomics.store(pia, (flagAddr >> 2) + 1, 0); v.setUint32(out, tid, true);
        const slotI = (POOL_BASE >> 2) + slot * 4;
        Atomics.store(pia, slotI + 1, fnIdx); Atomics.store(pia, slotI + 2, arg); Atomics.store(pia, slotI + 3, flagAddr);
        Atomics.store(pia, slotI, 1); Atomics.notify(pia, slotI);
        proc.threads = proc.threads || new Map(); proc.threads.set(tid, flagAddr); return 0;
      },
      threadJoin: (tid, retvalPtr) => { const fa = proc.threads && proc.threads.get(tid); if (fa == null) return -1; const idx = fa >> 2; let n = 0; while (Atomics.load(pia, idx) === 0) { Atomics.wait(pia, idx, 0, 4000); if (++n > 4) break; } if (retvalPtr) v.setUint32(retvalPtr, Atomics.load(pia, idx + 1) >>> 0, true); proc.threads.delete(tid); return 0; },
    };
    return ctx;
  }

  function getAsyncifyState(proc) { const f = proc.inst.exports.asyncify_get_state; return f ? f() : -1; }

  // A process's threads are "live" until joined. Used to keep fork() safe.
  function liveThreadCount(proc) {
    if (!proc.threads) return 0;
    const pia = proc.pia;
    let n = 0;
    for (const fa of proc.threads.values()) if (Atomics.load(pia, fa >> 2) === 0) n++;
    return n;
  }

  function doFork(parent) {
    const st = getAsyncifyState(parent);
    if (((typeof process!=='undefined'&&process.env.MT_DEBUG))) console.error(`doFork pid=${parent.pid} state=${st} (procs=${procs.length})`);
    if (st < 0) return -1;
    // SAFETY (codex review #4): fork() in a multithreaded process is only safe
    // if the OTHER threads are quiescent. A worker mid-write would make the
    // memory snapshot torn, and POSIX says the child must come up with ONLY the
    // calling thread. We don't preempt running workers, so rather than silently
    // snapshot inconsistent memory we refuse with EAGAIN while any sibling
    // thread is still live (the common cases — fork before any pthread_create,
    // or after the threads are joined — are unaffected).
    if (st === 0 && liveThreadCount(parent) > 0) return -11; // EAGAIN
    // soft cap: too many live procs → fork fails with EAGAIN (the guest
    // handles fork-fail), rather than throwing and killing the run.
    if (st === 0 && procs.filter((p) => !p.reaped).length > 64) return -11;
    if (st === ASYNCIFY_REWINDING) { parent.inst.exports.asyncify_stop_rewind(); return parent.forkReturn; }
    const buf = ASYNC_BUF;
    const pv = new DataView(parent.mem.buffer);
    pv.setUint32(buf, buf + 8, true); pv.setUint32(buf + 4, buf + ASYNC_SZ, true);
    parent.forkPending = true;
    parent.inst.exports.asyncify_start_unwind(buf);
    return 0;
  }

  // Cooperative fork at recursion depth D needs only ONE live child
  // memory at that depth — reuse a per-depth pool so a 110-deep tree
  // holds ~3 memories, not 110. (Shared Memory reserves full address
  // space and isn't GC'd during the synchronous run.)
  const memPool = [];
  function handleFork(parent, depth) {
    parent.forkPending = false;
    const mem = memPool[depth] || (memPool[depth] = makeMem());
    new Uint8Array(mem.buffer).set(new Uint8Array(parent.mem.buffer)); // copy parent → child
    const child = spawnProc(module, parent.argv, parent.pid, mem, false);
    child.argc = parent.argc; child.argvPtr = parent.argvPtr;
    child.forkReturn = 0; child.pendingRewind = true;
    runProc(child, depth + 1);
    const childThreaded = !!child.pool;
    killPool(child); // free the child's workers before its memory is reused
    // If the child ran threads, DON'T let the next sibling reuse this memory:
    // the child's pool workers are parked on this memory's control region and
    // worker.terminate() is asynchronous, so a sibling booting a fresh pool on
    // the SAME control region could double-wake a not-yet-dead worker on a
    // shared slot. Dropping the pooled memory gives the next sibling an
    // isolated address space (threading fork children are rare, so this costs
    // at most a few extra live memories in one synchronous burst).
    if (childThreaded) memPool[depth] = null;
    child.inst = null; // memory is pooled (reused by the next sibling)
    parent.forkReturn = child.pid; parent.pendingRewind = true;
  }

  function runProc(proc, depth) {
    for (;;) {
      try {
        if (proc.pendingRewind) { proc.pendingRewind = false; proc.inst.exports.asyncify_start_rewind(ASYNC_BUF); }
        proc.inst.exports.__main_argc_argv(proc.argc, proc.argvPtr); // no crt _start; call main directly
      } catch (e) { if (e.isExit) { proc.exited = true; proc.exitCode = e.code; return; } proc.exited = true; proc.exitCode = 139; proc.error = e.message; return; }
      if (proc.forkPending) { handleFork(proc, depth); continue; }
      proc.exited = true; return;
    }
  }

  const root = spawnProc(module, argv, 0, memory, true);
  // Boot the root pool eagerly, awaiting readiness on the event loop — this is
  // the only spawn point with an async context, so it is the one that works in
  // the browser too (a Web Worker needs an event-loop turn to load). Fork/exec
  // children boot lazily and synchronously (node only; see ensurePool).
  spawnPoolWorkers(root);
  await awaitPoolReady(root);
  runProc(root, 0);
  for (const proc of procs) killPool(proc); // release any remaining pool workers
  return { exitCode: typeof root.exitCode === "number" ? root.exitCode : 0, error: root.error, procs: procs.length };
}
