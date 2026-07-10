# yos — what this project is and what NOT to do

## ABSOLUTELY FORBIDDEN — ASSISTANT ATTRIBUTION

**Never write the word "Claude", "Anthropic", "Opus", "Sonnet",
"Haiku", "claude.ai", "AI assistant", "AI", or any assistant /
model name into this repository in ANY form, for ANY reason.
STRICTLY FORBIDDEN.**

This applies to:
- Source code (comments, string literals, log/format strings,
  symbol names).
- Headers, build files, config / data files (YAML, JSON, TOML,
  INI, shell, CMake, meson, Nix).
- Documentation (`*.md`, `*.rst`, READMEs, ADRs).
- **Commit messages, PR titles, PR descriptions, branch names.**
- **Co-Authored-By lines, sign-offs, "Generated with …" footers.**
- Templates, scaffolding, generated code.

There is **no** "Co-Authored-By" trailer of any kind on commits in
this repo. Author/committer is the user. End of story.

Citing internal rules by name is also forbidden — write the
constraint, not its source. ❌ `// per CLAUDE.md` / ❌ `// as the
assistant guidelines say` — just state the rule.

**Why this matters:** every leak compromises the credibility of
the project. The user audits commits and code for these strings.
This has happened before; it does not happen again.

If you are unsure whether something counts: it counts. Strip it.

## Vision (one paragraph)

yos is a host-side **libc emulator** for wasm32 guests. The wasm
binary sees a **FreeBSD libc API**: FreeBSD-shaped headers,
FreeBSD struct layouts (`struct stat`, `struct timespec`,
`struct sockaddr`, errno values, flag constants), FreeBSD
function signatures. Each libc call in the guest becomes a wasm
import on `env.<freebsd-libc-name>`. yos resolves those imports
to host-side bridge functions that translate FreeBSD shapes to
the host's libc shape and call **host libc** — glibc on Linux,
libSystem on macOS / iOS / tvOS, libc on FreeBSD (where the
translation is mostly a no-op). The same wasm binary runs
unchanged on all those host platforms because it only ever sees
the FreeBSD ABI.

## NON-NEGOTIABLES

1. **No raw kernel calls. Anywhere. Ever.** The model is:
   wasm app → calls a function on yos's surface (a FreeBSD libc
   function) → bridge → host libc function (glibc / libSystem /
   FreeBSD libc). That's the only path. yos is a userspace libc
   emulator; it does not — and cannot — go below the host libc
   layer, because that layer is what makes it portable across
   Linux, macOS / iOS / tvOS, and FreeBSD. **Do not write the
   word that means "kernel-level call dispatched by number"
   anywhere in this codebase.** If you find yourself reaching
   for the `<sys/...>` header that contains it, stop and pick a
   libc function instead. The legacy yos-private POC used that
   approach; we have explicitly moved away from it. yos-private
   is read-only reference for fork/pthread/asyncify *technique*,
   not for the kernel-dispatch ABI.

2. **Source of truth = `/home/misi/work/my/yos-private/`.** Read
   from there. **Never** look at `yos-sandbox/` — it's an older
   iteration and contains misleading code. **Never write** to
   yos-private (work happens here, in `yos/`).

3. **The wasm guest's view is FreeBSD libc.** Not Linux libc, not
   musl-shaped imports, not WASI. The header surface and ABI are
   FreeBSD's wasm32 view (i386-style 32-bit pointers, FreeBSD
   struct layouts).

4. **One sentence picture, memorise it:**
   "wasm app calls a FreeBSD libc function on yos's surface; yos
   picks the cheapest implementation that gives FreeBSD-correct
   behaviour."
   No layer between bridge and host libc when host libc is enough.
   No kernel-level dispatcher. No number-indexed table.

6. **EVERY libc function is a yos `env.<name>` import. No
   exceptions.** The wasm guest's `.wasm` carries no `libc.a`
   bodies — only headers with `__attribute__((import_module
   ("env"), import_name("X"))) <ret> X(<args>);` decls. yos owns
   all libc resolution.

   How yos *implements* each import is a 3-tier ladder; pick the
   lowest tier that yields FreeBSD-correct behaviour:

   - **Tier 1 — direct host libc passthrough.** The bridge body
     translates wasm pointers, calls the host libc function with
     the same name, returns. Used when FreeBSD signature is
     layout-compatible with the host's. Most pure / stateless
     fns: `strlen`, `memcpy`, `qsort`, `strtol`, `sin`, `read`,
     `write`, `close`, `lseek`, …

   - **Tier 2 — FreeBSD source compiled to wasm32 inside yos.**
     yos's build picks specific `.c` files from FreeBSD libc
     source, compiles them to wasm32 with `clang -target wasm32`,
     packages them into a `libc-pure.wasm` byte array embedded in
     the yos host binary. yos loads it at startup into a secondary
     wasm3 instance. When the guest imports a Tier-2 fn, the
     bridge dispatches into this secondary instance. The Tier-2
     code may import lower-level libc fns (e.g. printf calls
     `write`); those resolve through the SAME yos import surface,
     landing in Tier 1 / Tier 3. Used for variadics (printf
     family) and other pure-userspace fns where host libc can't
     serve a faithful FreeBSD-shape implementation directly.

     We do NOT vendor a full FreeBSD libc. We pull in only the
     specific .c files we need, on demand, listed under
     `from_freebsd_src:` in `src/yos/codegen/hooks.yaml`.

   - **Tier 3 — yos hand-written implementation.** The bridge
     dispatches into `yos_<name>(struct yos_exec_ctx *ctx, ...)`
     defined in `src/yos/impl/<area>.c`. Used for state-bound
     operations: fork, vfork, execve, wait*, kill, exit (proc
     table); pthread_* (host pthread + TLS pool); malloc/free
     (mimalloc-over-linear-memory, future); mmap/munmap/brk
     (wasm-linear-memory bookkeeping); open/read/write/close/etc
     (only because of fd virtualisation across forks).

   Selection rule restated: Tier 1 if host libc has a compatible
   function. Else Tier 2 if FreeBSD source for it is suitable.
   Else Tier 3.

   **Strictly forbidden libc sources**: musl, wasi-libc, newlib.
   Tier 2 is FreeBSD only — primary platform is iOS/tvOS, which
   is BSD-derived; staying close to FreeBSD libc keeps that path
   short. musl is Linux-kernel-coupled; we explicitly do not
   want it.

5. **The legacy yos-private surface is removed, not retained.**
   No `env.__yos_syscall` binding, no `generated/syscall_*.c`,
   no `<linux/*.h>` UAPI headers in the build, no
   `syscall(SYS_*, ...)` sites anywhere. Not behind a meson
   option, not in a compat shim, not "kept for the regression
   net". Removed.

   The reason is **two independent constraints that both point
   the same way**:
   (a) Cross-platform: macOS / iOS / tvOS / FreeBSD don't have
       Linux's syscall numbers; Apple platforms forbid direct
       kernel calls in shipping apps. The only portable
       contract is host libc.
   (b) Licensing: yos ships under BSL. The legacy path drags
       in Linux UAPI headers (GPL-2.0-WITH-Linux-syscall-note)
       and `syscall(SYS_*)` sites. The Linux-syscall-note
       exception means there is no *legal* GPL contamination,
       but every reviewer asks. With the legacy path gone, the
       dependency tree is wasm3 (MIT) + mimalloc (MIT) +
       FreeBSD libc *headers* (BSD-2-Clause) + host libc at
       runtime — zero GPL involvement, clean BSL story.

   Until the libc-by-import path covers nvim's full surface,
   the regression net for ongoing work is the libc-by-import
   demos we build out incrementally — NOT yos-private's
   `__yos_syscall`-using wasm tests.

## Architecture

```
+--------------------+   wasm imports on env.<freebsd-libc-name>
|  wasm guest        |   (e.g. env.open, env.write, env.fork, env.pthread_create)
|  built against     |
|  FreeBSD libc      |
|  headers, wasm32   |
+---------+----------+
          |
          v
+--------------------+   per-import m3ApiRawFunction wrapper:
|  yos_brg_<name>    |   - pop wasm-ABI args
|  (auto-generated   |   - translate wasm offsets -> host pointers
|  from bridge.py)   |   - convert FreeBSD struct layout -> host layout
+---------+----------+   - dispatch to subsystem (state) or host libc (pure)
          |
   +------+------+
   |             |
   v             v
+----------+  +-------------+
| yos      |  | host libc   |
| subsystem|  | (glibc /    |
| (yos-    |  |  libSystem /|
|  private |  |  FreeBSD    |
|  origin) |  |  libc)      |
+----------+  +-------------+
```

The yos subsystem owns state (fd table, process table, mmap free
list, pthread TLS pool, asyncify fork mechanics) and lives in
`src/yos/`. Source taken verbatim from yos-private:

- `types.h` — struct yos_runtime / yos_proc / yos_exec_ctx
- `yos-mem.{c,h}` — brk + mmap2 inside wasm linear memory; lower
  half = brk-heap, upper half (anchored at `memory_size/2`) = mmap
  arena. Best-fit free list. `mprotect`/`mlock`/`madvise` are
  advisory no-ops because wasm has one flat memory.
- `yos-proc.{c,h}` — process table, asyncify-based fork (snapshot
  linear memory + globals → spawn host pthread → both rewind to
  fork point), wait, exec.
- `yos-pthread.{c,h}` — host-side L1 pthread implementation,
  per-process TLS arena pool, clone(CLONE_SETTLS, CLONE_CHILD_-
  CLEARTID), mutex/cond/key.
- `yos-sig.{c,h}` — signal stubs.
- `yos-f128.c` — __float128 helpers.
- `yos-procfs.h` + `vfs/procfs.c` — synthetic /proc.
- `vfs/mount.{c,h}`, `vfs/file.{c,h}` — mount table, virtual fd.
- `include/yos/ytrace/ytrace.h` — switchable trace-point macros
  (`ydebug`/`yinfo`/`ywarn`/`yerror`/`ytrace`); gated by `YTRACE_DEFAULT_ON=yes`.

Auto-generated bridges live in `build-tools/api-generate/`:

- `extract.py` walks FreeBSD-32 + host libc headers via libclang,
  produces `guest-api-i386-freebsd.yaml` and `host-api.yaml`.
- `compare.py` + `analyse.py` produce structured deltas.
- `bridge.py` emits `yos_bridge.{c,h}`: per-fn `yos_<libc-name>`
  bridge that does pointer translation + scalar widen/narrow +
  errno remap, plus per-fn wasm3 raw wrapper `m3w_<name>`, plus
  `yos_brg_link_imports(IM3Module)` registering them all.

wasm3 is **vendored** (not fetched per-build) at
`build-tools/wasm3/vendored/` — yos-private's fork with
`m3_NewSiblingRuntime` + atomics. Update via a future
`tools/sync-wasm3.sh` (TBD).

## Codegen strategy — auto-generate from headers, hand-write as last resort

1. **libclang walks the headers, not us.** `extract.py` uses
   `clang.cindex` to parse the FreeBSD-32 guest headers and the
   host libc headers into structured yaml — every function
   signature, struct layout (field offsets + sizes, recursively),
   typedef, scalar width, enum, integer constant. Nothing in the
   bridge generator hand-codes "what `struct stat` looks like"; it
   reads the layout from yaml.

2. **The diff between guest and host is also extracted, not
   declared.** `compare.py` walks each function arg-by-arg + the
   return, recursing into structs and pointers, and emits a
   structured delta (`widen`, `narrow`, `convert_struct`,
   `pointer_descent`, `kind_mismatch`, …). `analyse.py`
   categorises functions as `compatible` (no delta),
   `mechanical` (every delta is a safe cast), or `needs_policy`
   (a human has to decide). Categorisation is data, not code.

3. **Code is emitted from those tables.** `bridge.py` reads
   `guest-api*.yaml` + `host-api.yaml` + `analyse-report.yaml`
   and writes `yos_bridge.c` mechanically — pointer translation
   comes from the `kind=pointer` tag, scalar casts from widen /
   narrow deltas, struct field-by-field conversion (when we add
   it) from `convert_struct` deltas, errno + flag remap tables
   from `constants_remap`. Same input → same output, no hand-
   edits to the generated file.

4. **Hand-written code is the explicit exception, not the
   default.** Only stateful subsystems (fork / asyncify dance,
   pthread / TLS, mmap free-list, FILE* table) are hand-written,
   because their behaviour can't be derived from a header. The
   hand-written list is small and lives in `src/yos/`; everything
   else lives in `build-tools/api-generate/` output and is
   regenerated from headers on every build.

5. **Adding a new libc function = adding a header, not C code.**
   Drop the FreeBSD declaration into the guest sysroot, re-run
   extract → compare → analyse → generate, and the new bridge
   appears. If it lands in `needs_policy`, write a one-line
   routing entry; otherwise it's free. That's why the pipeline is
   built this way.

## Bridge body — the only shape every bridge has

```
yos_<freebsd-libc-fn>(ctx, wasm_args...)
{
    1. translate each wasm pointer arg (uint32 offset)
       to a host pointer via   ctx->memory + offset
    2. convert FreeBSD argument shapes to host shapes
       where they differ (struct stat, sockaddr, scalar
       widen/narrow, flag remap)
    3. call host libc:   <freebsd-libc-fn>(host_args...)
    4. convert the host return value + errno back to
       the FreeBSD shape (errno remap via
       yos_remap_errno_h2g, struct out-params
       layout-converted)
    5. return the FreeBSD-ABI value to the wasm guest
}
```

Steps 2 and 4 are no-ops for primitive types and any case
where FreeBSD == host shape; otherwise they're the per-type
renderer (currently emitted for scalars, pending for structs).
**The bridge never goes anywhere except into the host libc.**
No layer below it. No kernel-level dispatcher. No
number-indexed table. Just a function call.

## What's done

- Host runtime backbone copied from yos-private.
- wasm3 vendored from yos-private.
- bridge.py generates 178+ libc-name bridges + wasm3 wrappers +
  `yos_brg_link_imports`.
- Build produces a `yos` binary; **all 17 yos-private wasm unit
  tests pass (135/135)** — fork, pthread, setjmp, procfs, vfs,
  brk, etc. — confirming the runtime backbone works.
- BUT those tests reach the kernel through yos-private's
  **Linux-only** number-indexed dispatcher (the legacy surface
  documented as removed in non-negotiable #5). That whole layer
  goes away as part of "What needs to be done"; nothing in the
  shipped binary uses it. The libc-name bridge surface is what
  we keep.

## What needs to be done (in order)

### 1. Drop the kernel-bypass dispatcher

- Remove `src/yos/generated/syscall_handlers.c` and
  `syscall_dispatch.c` from the build. They are the Linux
  number-indexed kernel-call table; nothing in the new design
  calls into them.
- Remove the `env.__yos_syscall` binding from `src/yos/main.c`
  along with `m3_yos_syscall` / `m3_syscall_cp_asm`.
- Remove every yos-private guest-side wasm test that imports
  `env.__yos_syscall` from the regression set — they'll be
  replaced by libc-by-import demos.

### 2. Route bridges through the subsystem for stateful libc fns

For each FreeBSD libc name, decide:

- **Stateful** (open/read/write/close/fork/exec/wait/kill,
  pthread_*, mmap/munmap/brk, gettimeofday with ctx-aware time,
  procfs paths): bridge calls into `yos_<area>_<fn>(ctx, ...)`
  from yos-private (yos_vfs_open, yos_proc_fork, yos_pthread_-
  create, yos_mem_brk, …). bridge.py needs a routing table.
- **Pure / stateless** (strlen, memcpy, snprintf-formatting,
  sin/sqrt, strtol, getenv): bridge calls host libc directly.
  Already does this.

### 3. Replace every kernel-bypass site inside the subsystem with libc

Audit yos-vfs.c, yos-proc.c, yos-pthread.c — anywhere they
bypass libc to talk to the kernel directly, replace with the
host libc function:

- futex wake/wait → `pthread_cond_signal` / `pthread_cond_wait`
  on a host cond-var keyed by the address.
- `getdents64` direct → `fdopendir(dup(fd))` + `readdir`,
  re-encoding the entries into the FreeBSD-shape buffer.
- `process_vm_readv/writev` → drop, or use `pread/pwrite` on
  `/proc/<pid>/mem` **only on Linux**, behind `#ifdef __linux__`.
- AIO (`io_setup`/`io_submit`/...) → ENOSYS until libaio (Linux)
  / kqueue (BSD) / GCD (Darwin) wrappers exist.

Goal: the runtime compiles and links cleanly on macOS and
FreeBSD, returning ENOSYS for items that genuinely have no
portable libc analogue.

### 4. Build a guest-side libc-wasm

The current yos-private guest tests use musl-wasm + a
`__yos_syscall` thunk that goes through the dispatcher. We
replace that with a **FreeBSD-shaped libc** where each function
declares itself as a wasm import:

```c
__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t n);
```

That declaration alone is enough — clang `-target wasm32` emits
the import. App code does `#include <unistd.h>` + `write(...)`
and the .wasm imports `env.write`. yos's bridges resolve it.

Source for the FreeBSD-shaped headers: already extracted in
`build-tools/freebsd/headers-i386/usr/include/`. Add a
`build-tools/sysroot/` step that bolts import attributes onto
each declaration (or a small wrapper layer that does it
selectively). Output: a wasm32 sysroot apps can compile against
with `clang -target wasm32 --sysroot=...`.

For state-having things (errno-thread-local, FILE* table,
malloc) the guest libc has a small body. The bulk of the surface
is import declarations.

### 5. Allocator

`malloc`/`free`/`calloc`/`realloc` exported by yos. Implementation
sits **on the host** but allocates inside the **guest's wasm
linear memory** (which is just a host `uint8_t*` we own).
mimalloc is the chosen allocator: vendored at
`build-tools/mimalloc/`. Tell mimalloc the guest's linear-memory
blob is its arena via `mi_manage_os_memory`. Constrain it to
the **lower half** of linear memory (don't trample mmap_top in
the upper half, don't trample TLS pool which has a reserved
region). One mimalloc heap per `yos_exec_ctx_t` (per fork).
Bridges return wasm offsets, not host pointers.

### 6. Variadics, FILE*, errno

- **Variadic libc** (printf, fprintf, snprintf, scanf, syslog,
  execl, …): wasm has no `...`. The guest libc packs `...` into
  a typed-tag array struct; the host bridge unpacks → vsnprintf →
  write through an `env.write`-style call. Same convention
  yos-private's POC uses (see `yos-runtime.h` VarArgPack /
  VFUNC_PRINTF in yos-sandbox for the shape — yos-private has the
  finished version somewhere; verify before reimplementing).
- **FILE\***: opaque on the host. Guest sees a small-int handle;
  yos keeps a host FILE* table indexed by it.
- **errno**: thread-local. Either an `env.__errno_location`
  import returning a wasm-memory offset that the bridge writes to
  after each call, or every bridge return-value already carries
  errno (negative-return convention; existing code does this).
  Pick one and stick with it.

## Build / dev rules

- **PORTABILITY: every host-side change must compile and run on
  Linux, macOS (darwin), iOS, and tvOS.** Apple platforms are the
  primary deployment target — the whole reason yos exists is to run
  one wasm binary across them via `env.<libc-name>` bridges. Before
  you reach for a Linux-specific header (`<linux/...>`, `<sys/epoll.h>`,
  `<sys/inotify.h>`, `<sys/eventfd.h>`, `bits/...`), `<asm/...>`),
  a Linux-only syscall (`syscall(SYS_*)`, `pidfd_*`, `splice`,
  `tee`, `vmsplice`, `process_vm_*`, `getrandom`, `memfd_create`,
  `name_to_handle_at`, `copy_file_range`'s direct syscall flavour),
  or a glibc-ism (`canonicalize_file_name`, `mempcpy`, `__error()`
  in the wrong place, `strerror_l` with NULL locale, `program_invocation_name`,
  `__progname`), STOP and find the POSIX equivalent — or guard with
  `#if defined(__linux__)` and provide the darwin/BSD branch
  explicitly. The same rule applies to `clock_gettime`'s
  Linux-specific clocks (`CLOCK_BOOTTIME`, `CLOCK_TAI`,
  `CLOCK_MONOTONIC_COARSE`), to `MAP_POPULATE` / `MAP_HUGETLB` /
  `O_TMPFILE` / `O_PATH`, to `signalfd` / `timerfd` / `eventfd`,
  and to anything that ends in `_np`. Linker side: don't add
  `-lrt` / `-lresolv` / `-lcrypt` to host-side flags; macOS doesn't
  have those. Use `dispatch_*` / `kqueue` / `pthread_setname_np`'s
  darwin signature when there's no portable answer; else gate the
  whole block behind a platform `#ifdef`. Test:
  `ssh macbook 'cd ~/work/my/<repo>--cc && git pull && nix build .#yos'`
  AND `ssh rb00 'cd ~/work/my/<repo>--cc && git pull && nix build .#yos'`
  if you touched something portability-flavoured (the rb00 build is
  aarch64-linux but the same constraints apply since iOS/tvOS are
  also aarch64).
- Build dir is `build-<host_os>` (e.g. `build-linux`,
  `build-darwin`, `build-freebsd`). Refuse in-source builds.
- Long output goes to `tmp/` files, not stdout. Don't pipe
  through `tee` / `grep` — let the command finish, then read the
  log file.
- Python helpers run via the project venv: `uv venv && uv sync`
  populates `.venv/`; meson picks `.venv/bin/python3` if present.
- **MANDATORY: before claiming any change is "done" or "fixed",
  run `./tools/yos.sh zsh -c true` (or any quick `./tools/yos.sh
  …` invocation) yourself.** This forces a `nix build` of the
  full umbrella, which is what the user actually runs. `meson
  test -C build-linux` works on the local working tree but does
  NOT validate the nix build path. `nix build` uses `src = self`
  and only sees git-TRACKED files: any new file you wrote but
  didn't `git add` will make nix bail with `"File X does not
  exist."` even though meson is green. So:
    1. `git add` every new file you created (tests, headers,
       sources, meson refs).
    2. Run `./tools/yos.sh zsh -c true` and confirm it exits 0.
    3. Only then say the change is shipped.
  Don't make the user run the wrapper to discover your build is
  broken.

## Diagnostic / trace output discipline

- **Default runs are quiet.** A normal invocation
  (`./yos some.wasm`) must print *only* the wasm guest's own
  output and yos's actual user-visible errors (a wasm trap, a
  failed file open, an unresolved import). No diagnostic chatter.
- **All trace / informational prints go through `ydebug()`** (or
  the matching level macros `yinfo`/`ywarn`/`yerror`/`ytrace`) from
  `<yos/ytrace/ytrace.h>`. They are gated at runtime by the
  project-wide `YTRACE_DEFAULT_ON=yes` env var (matches the
  convention used across the rest of the toolchain).
- **Never write `fprintf(stderr, "yos: ...")` directly** for
  anything that isn't a fatal user-facing error. Even helpful
  things like "argv_setup: ptr=0x…" go through `ydebug()` —
  otherwise every nvim run drowns the user's own output in yos
  internals.
- Errors that abort or trap *are* allowed to use `fprintf(stderr,
  "yos: …")` — they're rare and the user wants to see them.
- When debugging, run with `YTRACE_DEFAULT_ON=yes ./yos …` to
  light up every trace point at once.

## Common mistakes I have made and must not repeat

- **Forgetting that the primary deployment target is iOS / tvOS
  and treating "performance" as if any JIT is on the table.**
  Apple's app sandbox forbids `mmap(PROT_EXEC)` / `MAP_JIT` in
  shipping apps. Wasmtime, wasmer, LuaJIT, V8 — every native
  code-generating runtime is OFF the table. wasm3 is in the tree
  *because* it's an interpreter; that's the whole reason. When
  the user asks how to speed something up, do NOT suggest:
  swapping wasm3 for a JITting wasm runtime, using LuaJIT,
  using LLVM-backed JIT'd Lua (Ravi etc.), anything that emits
  host machine code at runtime, or anything that says "Cranelift"
  / "Liftoff" / "Baseline JIT". The legitimate speedup paths
  are: (a) generate WASM BYTECODE at runtime and load via
  `m3_LoadModule` — wasm-interpreter sees more wasm, no native
  code escapes the sandbox; (b) synthesise `M3Function->compiled`
  op streams directly (we own wasm3), skipping the bytecode
  parse — still pure data + statically-compiled op handlers,
  Apple-store-safe; (c) AOT compile slow guests (e.g. lua-to-wasm)
  so the inner interpreter goes away. All three live entirely
  inside wasm bytecode / wasm3 op-pointer data; none ever calls
  `mprotect(PROT_EXEC)`. Internalise this — the user has had to
  re-explain it more than once.
- **Reading from yos-sandbox.** That tree is older and the
  layout (no mmap_top anchor, no TLS pool, no procfs, no real
  fork) is wrong. Always read from yos-private.
- **Reaching for kernel-level dispatch because yos-private
  does.** yos-private is Linux-only and uses that approach
  internally — we explicitly do not. The chain is
  *wasm app → FreeBSD libc function → bridge → host libc*.
  Anything that bypasses the host libc layer is wrong.
- **Treating `__yos_syscall(nr, ...)` as a contract.** It's
  not. It's an internal yos-private convention being phased out.
  The contract is **per-libc-function imports** on `env.<name>`.
- **Conflating `yos_<libc-fn>` (bridge, wasm-ABI args) with
  `yos_<area>_<fn>` (subsystem, host-translated args).** They're
  different layers; the bridge calls the subsystem.
- **Vomiting code before agreeing on direction.** When in doubt,
  3-sentence summary first, then ask.

## File map quick-ref

| Path | Origin | Role |
|------|--------|------|
| `src/yos/types.h` | yos-private (verbatim) | runtime structs |
| `src/yos/yos-{mem,vfs,proc,pthread,sig,f128}.{c,h}` | yos-private | subsystems |
| `src/yos/vfs/` | yos-private | mount table + procfs |
| `src/yos/generated/` | yos-private | **TO BE DROPPED** — Linux-only kernel-bypass dispatcher |
| `src/yos/main.c` | yos-private (to be rewritten — drop the dispatcher binding, keep setjmp/pthread/f128 link calls) | yos host entry |
| `include/yos/{types,yos_pthread}.h` | yos-private | wasm32-side surface decls |
| `build-tools/api-extract/` | mine | libclang ABI extractor |
| `build-tools/api-compare/` | mine | layout-driven type compare + analyse |
| `build-tools/api-generate/bridge.py` | mine | emits `yos_bridge.{c,h}` |
| `build-tools/freebsd/` | mine | fetch FreeBSD source, curate UAPI tree |
| `build-tools/host-libc/` | mine | snapshot host libc include search path |
| `build-tools/wasm3/vendored/` | yos-private (verbatim) | wasm3 fork |
| `build-tools/mimalloc/` | (TBD) | host-side allocator over linear memory |
