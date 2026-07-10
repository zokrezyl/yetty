// Regression test (issue #21): fd lifecycle in the browser/node process engine.
//
// Pins two engine bugs that leaked descriptors in the JS fd model:
//
//  1. execve did not honor close-on-exec. prepareExec() swapped the wasm image
//     in place but kept EVERY entry in proc.pio.fds, so a descriptor opened with
//     O_CLOEXEC / F_DUPFD_CLOEXEC / F_SETFD(FD_CLOEXEC) stayed visible — and
//     refcounted — after exec. A shell/tmux internal pipe or saved fd then
//     leaked into the exec'd program. Fixed by closing every cloexec fd before
//     instantiating the replacement image.
//
//  2. The non-scheduler exit path (run()/doFork()/runChildProgram()) never
//     released a process's fds. Only the interactive scheduler's onProcExit()
//     did. So a forked child that exited without explicitly closing an inherited
//     pipe/socket end left it refcounted, and the peer never saw EOF/EPIPE.
//     Fixed by funnelling run()'s exit through the same releaseProcFds() cleanup.
//
// Run: node fd_lifecycle_test.mjs
import { Manager } from "./yos_proc.mjs";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const load = async (p) => WebAssembly.compile(await readFile(join(here, p)));

const trueMod = await load("tools/true.wasm"); // exits 0 immediately
const tools = new Map([["true", trueMod]]);

let failed = 0;
const check = (name, ok, detail) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${name}${ok ? "" : "  " + (detail || "")}`); if (!ok) failed++; };

const newMgr = () => new Manager({ onOutput: () => {}, onUnimpl: () => {} }, tools);

// A throwaway file-backed open file description with an explicit refcount.
const fileOfd = () => ({ kind: "file", node: { type: "file", data: new Uint8Array(0) }, off: 0, append: false, refs: 1 });

// ── Issue 1: prepareExec() closes close-on-exec descriptors ──────────────────
{
  const mgr = newMgr();
  const proc = mgr.spawn(trueMod, ["true"], 0);

  // A descriptor marked close-on-exec, and one that is NOT — both backed by
  // distinct open file descriptions so we can watch their refcounts.
  const cloexecOfd = fileOfd();
  const keepOfd = fileOfd();
  proc.pio.fds.set(10, { ofd: cloexecOfd, cloexec: true });
  proc.pio.fds.set(11, { ofd: keepOfd, cloexec: false });

  mgr.prepareExec(proc, trueMod, ["true"]);

  check("cloexec fd removed from the table on exec", !proc.pio.fds.has(10));
  check("cloexec ofd refcount dropped to 0", cloexecOfd.refs === 0, `refs=${cloexecOfd.refs}`);
  check("non-cloexec fd survives exec", proc.pio.fds.has(11));
  check("non-cloexec ofd refcount untouched", keepOfd.refs === 1, `refs=${keepOfd.refs}`);
  check("inherited std fds (0,1,2) survive exec", proc.pio.fds.has(0) && proc.pio.fds.has(1) && proc.pio.fds.has(2));
}

// A pipe-end ofd whose LAST holder is a child: on the child's exit the engine
// must drop the ref and mark the pipe's write side closed, which is exactly the
// EOF a reader on the other end observes (bufReadable -> writeClosed).
const pipeWriteOfd = (pipe) => ({ kind: "pipe", end: "w", pipe, refs: 1 });
const newPipe = () => ({ chunks: [], total: 0, readClosed: false, writeClosed: false });

// ── Issue 2a: run() releases fds on a forked-child-style exit ────────────────
{
  const mgr = newMgr();
  const child = mgr.spawn(trueMod, ["true"], 1);

  // Hand the child the SOLE write end of a pipe (as if the parent already closed
  // its own copy). The child runs true.wasm and exits without closing it.
  const pipe = newPipe();
  const wOfd = pipeWriteOfd(pipe);
  child.pio.fds.set(5, { ofd: wOfd, cloexec: false });

  mgr.run(child);

  check("child process exited", child.exited === true, `exited=${child.exited}`);
  check("run() emptied the exited child's fd table", child.pio.fds.size === 0, `size=${child.pio.fds.size}`);
  check("inherited pipe write end refcount dropped to 0", wOfd.refs === 0, `refs=${wOfd.refs}`);
  check("pipe peer sees EOF (writeClosed) after child exit", pipe.writeClosed === true, `writeClosed=${pipe.writeClosed}`);
}

// ── Issue 2b: runChildProgram() children release their fds too ───────────────
{
  const mgr = newMgr();
  const before = mgr.procs.length;
  mgr.runChildProgram(trueMod, ["true"], 0);
  const child = mgr.procs[before]; // the spawned child

  check("runChildProgram child exited", child.exited === true);
  check("runChildProgram child fd table emptied on exit", child.pio.fds.size === 0, `size=${child.pio.fds.size}`);
}

// ── Issue 2c: a real fork tree leaves no exited proc holding fds ─────────────
// forkdemo exercises the genuine asyncify fork/doFork path; after it completes,
// every process that exited must have released its descriptors.
{
  const forkdemo = await load("tools/forkdemo.wasm").catch(() => null);
  if (forkdemo) {
    const mgr = new Manager({ onOutput: () => {}, onUnimpl: () => {} }, new Map([["forkdemo", forkdemo]]));
    const root = mgr.spawn(forkdemo, ["forkdemo"], 0);
    mgr.run(root);
    const leaked = mgr.procs.filter((p) => p.exited && p.pio && p.pio.fds.size > 0);
    check("real fork tree: no exited process leaks fds", leaked.length === 0, `leaked pids=${leaked.map((p) => p.pid).join(",")}`);
  } else {
    console.log("  SKIP  real fork tree (forkdemo.wasm missing)");
  }
}

console.log(failed ? `\n${failed} FAILED` : "\nALL PASSED");
process.exit(failed ? 1 : 0);
