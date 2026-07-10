# Browser runtime — status and convergence plan

> Revision 2 — incorporates the Codex review (`tmp/cdx-to-cld.md`). The main
> correction: the browser must reuse yos's **FreeBSD-shaped C runtime** for all
> guest-visible semantics; Emscripten supplies only the host-side C runtime and a
> storage substrate *beneath* yos's bridge/impl/VFS — it does **not** provide the
> guest ABI.

## TL;DR / feasibility verdict

**Yes, this is feasible.** The goal — *run the browser on the same C code as
desktop, compiled to wasm, with JavaScript reduced to the irreducible glue
(fork/process orchestration, threads, terminal I/O, browser leaves)* — is
achievable and is the direction an existing proof of concept
(`src/yos/platform/web/build.sh`) already points at.

The single enabling fact: **yos's whole bridge model is "translate a guest
pointer with `host = memory_base + guest_offset`, then apply the FreeBSD⇄host
shape conversion."** That works in the browser as long as the C bridge code and
the guest share **one linear memory**. What we are deleting is the parallel
**JavaScript** reimplementation of libc/syscalls (`yos_proc.mjs`), not swapping
the guest ABI for Emscripten's.

Two real caveats set the honest expectations:

1. **Cost:** guest execution speed under Architecture A is interpreter-class
   (wasm3), not the browser JIT the current JS engine enjoys. This is the one
   deliberate trade.
2. **It is a port, not a recompile.** "Same C runtime with a browser platform
   backend" — not "the desktop binary translated to wasm." There is a small but
   real platform layer to write (pthreads/Workers, blocking, signals, event
   readiness, FS routing).

---

## 1. Where we are today (the problem)

Two **completely separate host runtimes**:

| | Desktop | Browser (today) |
|---|---|---|
| Host runtime | C `yos` binary (`src/yos/`) | `src/yos/platform/web/yos_proc.mjs` — a hand-written **JavaScript reimplementation** |
| Guest execution | **wasm3** interpreter (C), embedded | the browser's **native WebAssembly** engine |
| libc (`env.*`) | `yos_bridge.c` (generated) + `impl/*.c` + `vfs/*.c` → host glibc, in **FreeBSD shape** | ~5 000 lines of JS re-deriving `open`/`read`/`stat`/`fork`/`regcomp`/… |
| Threads | host pthreads | Web Workers + SharedArrayBuffer (`mt_engine.mjs`) |

**The guest wasm executables are byte-identical** across the two — `ls.wasm`,
`ps.wasm`, `cat.wasm` are the same file the desktop runs (verified by md5). So
any behavioural difference is **entirely the host runtime**, i.e. the JS
reimplementation.

That is why the browser shell is broken where desktop is not:

- `ls -alrt` → `Error 2` (ENOENT) per entry: the JS host's `lstat`/`fstatat`/
  `readdir` (and their FreeBSD `struct stat`/`dirent` shaping) are incomplete.
- `ps` → "keyword not found": the JS host's `sysctl(KERN_PROC,…)` surface is
  incomplete.
- Two implementations of the same ~500-function FreeBSD ABI will drift forever.
  The "browser sed produced different output than desktop sed" episode was this
  drift — same `sed.wasm`, two hosts, two answers. **The divergence is the bug.**

A related trap: because the JS host matches `regcomp`/`ctype` via **JS RegExp**,
it is *accidentally more correct* than desktop for `grep`/`sed` (desktop hits the
shared rune-locale `<_ctype.h>` gap and matches nothing). The case table now
marks those with `knownNativeGap`. Converging on the C host will make the browser
**inherit** that rune-locale bug — which is the point: one implementation, one
bug, fixed once in C, instead of a JS engine that papers over it and drifts.

Two more current-state facts:

- The interactive browser fetches tools from `src/yos/platform/web/tools/`, which
  is **stale and incomplete** (has `ls`/`ps`/`cat`/`echo`/`pwd`; missing
  `sed`/`sort`/`tr`/`cut`/`grep`/`wc`). Desktop uses `result/libexec/`. These
  must become **one** artifact source.
- `build.sh` already contains a **proof slice** of the shared-memory idea: a
  `guest.wasm` and a `bridge.wasm` that both `--import-memory` a single `Memory`,
  so the bridge reads the guest's pointers directly.

---

## 2. What we want

```
                         browser tab
  ┌───────────────────────────────────────────────────────────┐
  │  thin JS shell (xterm.js, Worker spawns, COOP/COEP)         │  ← minimal JS
  ├───────────────────────────────────────────────────────────┤
  │  browser platform backend (small C+JS boundary)            │  ← thin port layer
  ├───────────────────────────────────────────────────────────┤
  │  yos host  =  THE SAME C CODE AS DESKTOP, → wasm via emcc   │  ← all C, FreeBSD ABI
  │     yos_bridge.c  +  impl/*.c  +  vfs/*.c  +  wasm3         │
  ├───────────────────────────────────────────────────────────┤
  │  Emscripten C runtime + storage substrate (musl, WASMFS)   │  ← below yos, not the ABI
  ├───────────────────────────────────────────────────────────┤
  │  guest wasm (ls / sed / zsh / nvim) — unchanged, identical  │  ← same file as desktop
  └───────────────────────────────────────────────────────────┘
```

- **All guest-visible semantics stay in yos's C** — the generated bridge +
  `impl/` + `vfs/` provide FreeBSD `struct stat`, `dirent`, errno values,
  `sysctl(KERN_PROC,…)`, ioctl/termios, procfs, SCM_RIGHTS, etc. This is the code
  that makes `ls -alrt`/`ps`/`tmux` behave; it is **reused, not replaced**.
- **Emscripten sits below that**: it supplies the host-side C runtime (musl) and
  a storage substrate (WASMFS/MEMFS/IDBFS) that yos's `impl/` leaf calls bottom
  out in — the same role glibc plays on desktop. Emscripten's POSIX/musl
  semantics are **never** exposed to the guest directly.
- **JavaScript shrinks to orchestration only**: Worker spawns, terminal bytes ↔
  xterm.js, cross-origin isolation, browser-device leaves.

---

## 3. Why it works — the shared-memory pointer contract

Every yos bridge is:

```c
ssize_t yos_write(struct yos_exec_ctx *ctx, uint32_t guest_ptr, uint32_t len) {
    const void *host_ptr = ctx->memory + guest_ptr;   /* the whole trick */
    return host_write(1, host_ptr, len);              /* FreeBSD⇄host shaping around this */
}
```

For this to run in the browser, the C bridge must compute `ctx->memory +
guest_ptr` and land on the guest's bytes. Two ways to guarantee it:

1. **Guest lives inside the host's memory** (wasm3-in-wasm). wasm3 allocates the
   guest's linear memory as a buffer inside the emscripten host module. Identical
   to desktop. **The guest's `env.*` imports are served by the same generated
   `m3ApiRawFunction` bridge wrappers as desktop** — so the FreeBSD ABI adapter
   is reused *verbatim*, by construction.

2. **Guest and host import the *same* `WebAssembly.Memory`** (`--import-memory`).
   The guest runs on the browser engine; its `env.*` is wired to the emscripten
   bridge's exports; shared memory makes `ptr` valid on both sides. This is what
   `build.sh` prototypes — but the bridge needs an *export* shim (today's bridges
   are wasm3 raw-function wrappers), and the two modules must share one memory
   safely (§ Architecture B).

Either way, JS never touches libc semantics.

---

## Two target architectures

### Architecture A — wasm3-in-wasm (recommended first)

Compile the yos host — `wasm3` + `yos_bridge.c` + `impl/*.c` + `vfs/*.c` — to one
wasm module with Emscripten, plus a **browser platform backend** (below). Guest
wasm bytes go to the embedded wasm3, as on desktop.

- **Pros**
  - Maximum reuse: the `m3ApiRawFunction` bridge wrappers, the FreeBSD⇄host shape
    conversions, fork/asyncify, and the VFS are reused **as-is** — no new linkage.
  - Guest and host trivially share memory (guest memory is a host buffer); the
    pointer contract holds with zero new plumbing.
  - Deletes the duplicated JS libc; fastest route to *one* correct implementation.
- **Cons**
  - Guests run under the **wasm3 interpreter**, not the browser JIT — slower.
    Coreutils are fine; `nvim` needs measurement. Mitigations are the
    already-blessed, JIT-free ones (runtime wasm-bytecode generation, wasm3
    op-stream synthesis, AOT of hot guests) — all iOS/tvOS-safe.
  - **Not an unchanged recompile.** A browser platform backend must be written
    (next box). Audit at least:
    - host pthread usage in `impl/proc`, `impl/pthread` (→ Emscripten pthreads /
      Workers)
    - blocking waits vs. Worker restrictions (main-thread cannot block on Atomics)
    - signal delivery assumptions (SIGCHLD/SIGWINCH/…)
    - fd / event-loop readiness (poll/select/kqueue backing)
    - `impl/` leaf filesystem calls → WASMFS/MEMFS or a yos VFS backend
    - any direct POSIX API unavailable or semantically different under Emscripten

#### Browser platform backend (the thin port layer)

The small, explicit C/JS boundary that lets the unchanged yos C run in a tab:

- terminal bytes out → xterm.js; keystrokes in
- storage: yos VFS leaf ops → WASMFS/MEMFS (+ IDBFS for persistence)
- clock/random/env
- Worker spawn + SharedArrayBuffer wiring for threads and parallel processes
- COOP/COEP so SharedArrayBuffer is available
- ENOSYS for genuinely-unimplementable leaves (same policy as desktop portability)

### Fork under A — cross-process state is a first-class design item

Snapshot/rewind is the easy half: the guest's linear memory is a host-owned
buffer, so fork = copy the buffer + a second wasm3 context, conceptually the
desktop path, with **no** shared-memory split.

The hard half is the **process object model**:

- **Cooperative, single host instance (correctness-first).** All processes live
  in one host-wasm module, cooperatively scheduled in C — like today's JS engine's
  cooperative fork, but in the reused C. fd tables, pipes, socketpairs, procfs,
  SIGCHLD/wait, terminal ownership, and shared open file descriptions all stay in
  one C runtime — **no cross-worker marshalling.** This is the recommended first
  target and sidesteps the hard part.
- **Parallel, multi-Worker (later).** If separate Workers run separate host-wasm
  instances, each process has its own host module + guest memory, and the items
  above need an **explicit cross-worker host object model** (shared open file
  descriptions, SCM_RIGHTS across Workers, wait/SIGCHLD, terminal ownership).
  This is what replaces today's JS scheduler and must be designed deliberately —
  it is the genuinely hard part of parallel fork, deferred until the cooperative
  model is correct.

### Architecture B — shared-memory dual-module (post-A research track)

Guest runs on the **browser's native wasm JIT** (fast); the emscripten C bridge
imports the **same** `Memory` as the guest. This is the `build.sh` direction and
the eventual performance answer — but it is a **separate research/optimization
track, not a straightforward phase after A**, because:

- guest and Emscripten host both want static data, stack, heap, TLS, and
  sometimes tables/globals — the single memory must be partitioned perfectly
- function pointers / tables are not automatically shared between modules
- malloc/brk/mmap ownership must be split cleanly
- asyncify across two modules gets ugly (two suspension domains)
- `pthread` shared-memory builds constrain memory creation/import and require
  COOP/COEP

Do not schedule B until A gives a correctness baseline **and** we have real
`nvim` numbers proving the interpreter is too slow. The bridge/impl/VFS C is
unchanged between A and B — only *how the guest is run* differs — so A→B is not
throwaway.

### Recommendation

**A first (cooperative process model), B only if measured need.** A yields one
correct implementation with the least new code, fixes the `ls`/`ps` class of bugs
by reusing yos's FreeBSD C, and unifies `grep`/`sed` onto the real (rune-locale)
behavior so the fix lands once. B waits for numbers.

---

## 4. What legitimately stays JavaScript

The "except the glue" the request calls out — keep it small and boring:

1. **Process/fork orchestration** — Worker spawns for parallelism, the scheduler
   choosing which runnable process advances (cooperative variant can be C under
   A). Today's `mt_engine.mjs` / `mt_*_worker_*.mjs` / `mt_coordinator_browser.mjs`
   are the surviving skeleton.
2. **Threads** — Emscripten pthreads = Workers + SharedArrayBuffer; JS is the
   Worker bootstrap + SAB wiring, not pthread semantics (those are `impl/` C).
3. **Terminal I/O** — guest stdout/stderr → xterm.js and keystrokes back
   (`zsh_main.mjs`-scale).
4. **Cross-origin isolation** — COOP/COEP headers (dev server + prod).
5. **Browser-device / unimplementable leaves** — ENOSYS, same policy as desktop.

Everything else — `open`/`read`/`write`/`stat`/`readdir`/`mmap`/`malloc`, fork
mechanics, `regcomp`, `sysctl`, procfs, FILE\*, termios/ioctl — is **C**.

---

## 5. Convergence plan (phased, layer-localized)

Phase 1 is deliberately split into small proof gates so failures localize by
layer instead of one "compile yos-host.wasm" cliff.

- **Phase 0 — toolchain + wasm3-in-wasm smoke. ✅ done (issue #34).** Emscripten
  target added; wasm3 compiled to a browser wasm module runs a trivial guest
  fully inside the host, output captured via a JS `print` hook, zero JS libc.
  Lives in `src/yos/platform/web/host/` (`build-phase0.sh`, `phase0_host.c`,
  `phase0_smoke.mjs`); `make test-browser-host-phase0` is the gate.
  - **Toolchain decision (was open): nix-packaged Emscripten, not a pinned
    emsdk.** The build runs under `nix develop` (flake dev shell now includes
    `emscripten`, nixpkgs 24.11 → 3.1.64); the guest is compiled with system
    `clang`. Link is `-O2`, not `-O3`: the packaged frontend emits an `-O3`-only
    binaryen flag (`--no-stack-ir`) its pinned `wasm-opt` rejects, and `-O2`
    gives the same sibling-call dispatch wasm3 needs. (The unrelated dev-profile
    `emcc` 4.0.21 is skewed against its own LLD 21 — `--no-stack-first` — so the
    flake-pinned emscripten is the supported one.)
- **Phase 1a — minimal bridge path. ✅ done (issue #35).** A guest that imports
  `env.write` / `env.getpid` / `env.exit` runs through the host wasm; those
  imports are served by hand-written C bridge wrappers in the exact desktop
  shape (`m3_GetUserData(runtime)` → ctx, pop the wasm3 stack slots, translate
  the guest pointer with `ctx->memory + offset`, call host libc). No JS
  implements them. `phase1a_host.c` / `bridge_guest.c`;
  `make test-browser-host-phase1a` is the gate (asserts the guest's write went
  through pointer translation, getpid returned host state, exit carried its
  code). These wrappers are the minimal proof of the shape; Phase 1b swaps them
  for the generated bridge.
- **Phase 1b — generated bridge + impl/vfs. ✅ core done (issue #36).** The REAL
  generated `yos_bridge.c` + the yos `impl/` and `vfs/` source trees compile to
  the host wasm under emcc (all ~40 core files build clean; the bridge links
  with `-sERROR_ON_UNDEFINED_SYMBOLS=0` so un-compiled leaves become inert
  stubs). A guest's `echo` (`write`) and `cat` (`open`/`read`/`write`/`close`)
  run through yos C — pointer translation, `fd_map`, errno remap, VFS — with
  Emscripten MEMFS as the storage substrate. `phase1b_host.c` / `build-phase1b.sh`;
  `make test-browser-host-phase1b` is the gate. Scoped-out for now:
  `impl/io/file.c` (`FILE*` table — six wide-char defaults collide with the
  bridge; wasm LLD lacks `--allow-multiple-definition`; raw-fd path doesn't need
  it), and running the real `ls`/`ps` tool wasm (argv + malloc + `struct stat`
  conversion + procfs/sysctl over a bigger impl surface — folds into Phase 1d /
  Phase 2 tool-artifact work).
- **Phase 1c.** *(largely covered by 1b — VFS/io `open`/`read`/`close` + MEMFS
  work; `stat`/`readdir`/`/proc` breadth lands with the real tool artifacts.)*
- **Phase 1d / Phase 2 — same artifacts + shared oracle. ✅ core done (issue #37).**
  A general runner (`yos_host_run.c` → `yos-host.mjs`) executes an arbitrary
  desktop tool artifact from `result/libexec` (the **same** artifact desktop
  runs; the stale `web/tools/` copies are no longer the source of truth), and a
  parity harness (`host_parity.mjs`, `make test-browser-host-parity`) runs each
  command through **native yos** and the **host wasm** and classifies MATCH /
  HOST-GAP / NATIVE-GAP / BOTH-FAIL. `echo`/`basename`/`cat`/`wc` MATCH
  byte-for-byte with zero JS libc; `grep` shows as a **NATIVE-GAP** (the
  rune-locale bug — host is correct, native diverges, tracked for #40);
  `sort`/`cut` and `ls`/`ps` remain HOST-GAPs (asprintf/heapsort, `struct stat`
  conversion, procfs/sysctl breadth). A HOST-GAP means "emscripten host diverges
  from native" and trends to zero because it is the same C.
- **Phase 3 — fork + threads glue. ⏳ not yet functional (issue #38).**
  `impl/proc/proc.c` is compiled into `yos-host.wasm` and `fork`/`waitpid`/`_exit`
  are linked, so a fork guest runs — but `fork()` returns failure: the
  single-instance host runner does not yet wire the cooperative snapshot/rewind
  scheduler (run child inline to completion, then resume parent). See
  `src/yos/platform/web/host/README.md` (Phase 3 status) for the exact pieces
  outstanding. Success target unchanged: `zsh` runs a pipeline, `tmux` splits
  panes.
- **Phase 4 — freeze, then retire the JS host (test-gated, not intent-gated).**
  1. **Freeze `yos_proc.mjs` now**: it stays as the legacy runner, but **no more
     semantic patches** — only test-harness compatibility. (This means the recent
     regex/feof/ungetc patches stay as legacy; we add nothing more.)
  2. Parity-run **both** hosts until the emscripten host passes the agreed matrix:
     - browser `yos:libc` parity has **no** native-pass/browser-fail gaps for
       covered tests
     - browser `zsh` runs real tools from the **same build output** as desktop
     - `ls -alrt`, `ps`, `sed`, `sort`, `tr`, `cut`, `grep`, `wc` match desktop
     - fork/wait, pipes, dup/close/cloexec, pty, socketpair/SCM_RIGHTS, termios,
       procfs all have parity tests that pass
     - tmux smoke passes (if we claim multiplexer support)
  3. Only then mark the browser-host wasm as **default**, and delete
     `yos_proc.mjs`'s `buildLibc`.
- **Phase 5 (optional) — Architecture B.** Only with A as baseline and real perf
  numbers.

---

## 6. Risks and open decisions

- **Guest speed (the real decision).** wasm3-in-wasm is an interpreter under a
  JIT. Fine for a shell; unknown for `nvim`. Decide A-only vs. A→B by measuring.
- **Platform port scope.** "Same C runtime, browser backend" still means porting
  pthreads→Workers, blocking waits, signals, event readiness, FS routing. Not a
  pure recompile.
- **Cross-worker process model.** The hard part of *parallel* fork under A;
  deferred by doing the cooperative single-instance model first, but required for
  real parallelism/threads.
- **Architecture B memory model.** Partitioned, imported, shared memory with
  Emscripten musl + pthreads is unproven here; keep it off the critical path.
- **Inherited native bugs.** Converging makes the browser adopt desktop's real
  bugs (e.g. rune-locale `grep`/`sed`). Correct and intended — fix them once in
  C — but expect currently-"passing" browser cases to flip until the shared fix
  lands. The `knownNativeGap` flags track exactly these.
- **Unix domain sockets** (tmux imsg) must run through yos's C VFS, not host
  AF_UNIX (which Emscripten does not fully provide).
- **Toolchain footprint / binary size.** Emscripten SDK in CI; wasm3 + bridges +
  musl is larger than the JS engine — watch mobile web.

---

## 7. Non-goals

- Reaching parity by **growing** `yos_proc.mjs`. That is the thing we are
  removing; every function added to it is future divergence.
- Letting **Emscripten/POSIX/musl semantics reach the guest**. The guest ABI is
  FreeBSD, provided by yos's C bridge/impl/VFS; Emscripten is strictly the
  substrate beneath it.
- A browser-only libc. There is one libc surface — the C bridges — and the
  browser must use it.
- JIT-based guest execution as a *requirement*. Speedups stay
  wasm-bytecode/interpreter-level so the same approach holds on iOS/tvOS.
