// Unified browser-yos entry point — ONE runtime path (issue #21). The wasm's
// memory model decides the engine, so callers stop choosing between two
// engines by hand (codex review: "stop maintaining fake pthreads in one runtime
// and a real-thread engine elsewhere; unify them").
//
//   - wasm that IMPORTS its memory was built --shared-memory / -pthread → run on
//     the REAL-thread engine (mt_engine: Web Workers + SharedArrayBuffer +
//     Atomics). True pthreads (blocking mutex/cond/rwlock), fork via an
//     asyncify memory snapshot. This is the foundation going forward.
//   - wasm that EXPORTS its own (non-shared) memory is the host-threaded/legacy
//     model → run on the single-process engine (yos_proc.mjs): full libc,
//     cooperative fork; pthreads run inline — create/join/mutex work, and a
//     blocking producer/consumer cond_wait fails LOUDLY rather than pretending.
//
// Detection: a memory IMPORT is the signal (every shared-memory build in this
// tree links `--import-memory --shared-memory`; the `.type.shared` flag isn't
// exposed by WebAssembly.Module.imports, but the import's presence is enough).
import { runProgram, loadLiblua } from "./yos_proc.mjs";
import { runMtProgram } from "./mt_engine.mjs";

const isNode = typeof process !== "undefined" && !!(process.versions && process.versions.node);

// True when the module expects a host-provided (shared) memory — i.e. it was
// built for real threads.
export function wasmWantsRealThreads(module) {
  return WebAssembly.Module.imports(module).some((i) => i.kind === "memory");
}

// Run a wasm guest on whichever engine its memory model calls for. Always async
// (the real-thread engine is); the cooperative engine's sync result is wrapped.
//   opts: { onOutput(fd,text), onUnimpl(name), tools, env, spawnPoolWorker }
export async function runYos(module, argv = ["prog"], opts = {}) {
  if (!wasmWantsRealThreads(module)) {
    await loadLiblua().catch(() => {}); // Lua C API for nvim (shared-memory liblua.wasm)
    return runProgram(module, argv, opts.onOutput, opts.onUnimpl, opts);
  }
  let spawnPoolWorker = opts.spawnPoolWorker;
  if (!spawnPoolWorker) {
    if (!isNode) {
      // Browsers forbid Atomics.wait on the main thread, so the real-thread
      // engine must run inside a coordinator Worker (see
      // mt_coordinator_browser.mjs). Callers there supply opts.spawnPoolWorker.
      throw new Error("runYos: shared-memory wasm needs a real-thread worker pool; in a browser run mt_engine from a coordinator Worker (mt_coordinator_browser.mjs) or pass opts.spawnPoolWorker");
    }
    const { Worker } = await import("node:worker_threads");
    const url = new URL("./mt_pool_worker_node.mjs", import.meta.url);
    spawnPoolWorker = ({ module: mod, memory, slot }) => new Worker(url, { workerData: { module: mod, memory, slot } });
  }
  return runMtProgram(module, argv, { onOutput: opts.onOutput, onUnimpl: opts.onUnimpl, tools: opts.tools, spawnPoolWorker });
}
