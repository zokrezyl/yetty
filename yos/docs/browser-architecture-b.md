# Architecture A performance + Architecture B research (epic #33, issue #41)

> Decision record. Architecture B (native guest sharing imported memory with a C
> bridge module) is investigated here **only** because Architecture A now gives a
> correctness baseline with real numbers. No production code depends on B.

## 1. Architecture A — measured performance

Architecture A (shipped, `src/yos/platform/web/host/yos-host.wasm`) runs the
guest under the **wasm3 interpreter, which is itself compiled to wasm** and run
by the browser/Node engine. A guest opcode therefore costs "wasm3 dispatch,
interpreted by V8" rather than "wasm3 dispatch, native". The overhead of that
extra interpretation layer is the one deliberate trade.

Measured with `src/yos/platform/web/host/arch_a_perf.mjs` (Node, same guest tool
artifacts as desktop; `ms/run`, lower is better):

| case | native yos | host wasm (A) | ratio |
|---|---:|---:|---:|
| `echo hi` (setup/instantiate baseline) | 14.3 | 16.6 | **1.2×** |
| `cat big.txt` (20k lines, I/O-bound) | 14.3 | 31.8 | **2.2×** |
| `wc -l big.txt` (20k lines, scan-bound) | 24.6 | 84.4 | **3.4×** |

**Reading the numbers.**
- The `echo` baseline (~1.2×) is dominated by fixed setup on both sides
  (process spawn vs. Emscripten module instantiation + wasm3 runtime), so it
  isolates roughly the fixed cost, not interpreter cost.
- I/O-bound work (`cat`) is ~2×: each `read`/`write` bridge call crosses the
  guest→wasm3→bridge boundary, all interpreted.
- Scan-bound work (`wc`, which touches every byte in guest code) is ~3.4×: this
  is where the interpreter-under-interpreter tax is largest.

**Verdict for A:** 2–3.4× on coreutils is **fine for a shell and its tools** —
the interactive path is human-latency-bound, not throughput-bound. The open
question the numbers do NOT answer is `nvim`: a large, compute-heavy guest where
a 3× tax on hot loops may be user-visible. That measurement (boot + edit-loop
latency for `nvim.wasm` under A) is the go/no-go input for B and is left as the
next measurement once the fork/process model (#38) lets `nvim` run in the host.

Mitigations for A that stay interpreter-level (and therefore iOS/tvOS-safe, no
JIT): runtime wasm-bytecode generation, wasm3 op-stream synthesis, and AOT of
hot guests (e.g. lua→wasm). All keep native code out of the sandbox.

## 2. Architecture B — the shared-memory dual-module model

Architecture B runs the guest on the **browser's native wasm JIT** (fast) and
gives the Emscripten C bridge module the **same `WebAssembly.Memory`** as the
guest (`--import-memory`), so a guest pointer and a bridge pointer are the same
address. This is the eventual performance answer — and a **separate research
track**, not a phase after A.

### A proof slice already exists

`src/yos/platform/web/build.sh` builds the B primitive today:
`guest.wasm` and `bridge.wasm` both `--import-memory` a single `Memory`, so the
bridge reads the guest's pointers directly (see `src/yos/platform/web/README.md`).
It proves the *pointer contract* holds across two modules sharing one memory.
What it does **not** prove is the hard part below.

### Why B is not a straightforward next step

The single shared linear memory must host **both** modules' worlds at once:

1. **Static data / stack / heap / TLS collision.** The guest and the Emscripten
   C bridge each want their own `__data`, `__stack_pointer`, `__heap_base`,
   malloc arena, and (under pthreads) TLS. In one memory these must be
   partitioned to non-overlapping regions — Emscripten's `GLOBAL_BASE` /
   `STACK_SIZE` / `INITIAL_MEMORY` on one side, the guest's `--global-base` /
   stack-size on the other — and kept from ever growing into each other. The
   `build.sh` slice sidesteps this by parking the bridge's near-empty data at a
   fixed high offset; a real bridge (impl+vfs+wasm3-free B bridge) is not empty.
2. **Function tables are not shared.** Indirect calls (function pointers,
   callbacks like `qsort`, signal handlers) resolve through a module's own
   table; the two modules have separate tables, so a guest function pointer is
   meaningless to the bridge and vice versa. Any callback-taking libc call needs
   an explicit table-sharing or trampoline scheme.
3. **malloc/brk/mmap ownership must be split.** Exactly one allocator can own a
   given region. yos's model puts the guest heap in the lower half and mmap in
   the upper half of the guest memory; the bridge's own malloc must live in a
   third, disjoint region and never hand the guest a pointer into bridge-owned
   space (or vice versa).
4. **asyncify across two modules = two suspension domains.** yos's fork and
   setjmp use asyncify to unwind/rewind the wasm stack. With the guest on the
   native engine and the bridge in Emscripten, a fork that must snapshot the
   guest and rewind both sides spans two independent asyncify state machines —
   there is no single stack to unwind.
5. **pthread/shared-memory constraints.** A `SharedArrayBuffer`-backed shared
   memory (needed for threads) constrains how/when memory can be created and
   imported and requires COOP/COEP; combining that with `--import-memory` across
   two modules is unproven here.

### B viability conclusion

The pointer contract (the thing people doubt) is **proven** by the `build.sh`
slice. The blockers are all in the **single-memory partitioning + table sharing
+ two-domain asyncify** layer, which is real engineering with no de-risking
prototype yet. B is therefore **documented as viable-in-principle but not
scheduled**: it is worth pursuing only if the `nvim`-under-A measurement shows
the interpreter is too slow for the target experience. The bridge/impl/vfs C is
unchanged between A and B — only *how the guest runs* differs — so A→B is not
throwaway.

## 3. Recommendation

- **Ship A** (done: `yos-host.wasm`, parity harness) as the correctness baseline.
  Coreutils/shell are comfortably within budget at 2–3.4×.
- **Measure `nvim` under A** as the single go/no-go datum for B (blocked on #38).
- **Do not introduce any production dependency on B** until a prototype resolves
  the memory/table/asyncify model above. The `build.sh` slice stays as the
  standing B experiment.
