# Browser convergence host (`yos-host.wasm`) — Phase 0

This directory holds the **converged browser host**: the yos C runtime compiled
to a browser-targeted wasm module with Emscripten, so the browser runs the same
FreeBSD-shaped C code as desktop instead of the hand-written JavaScript libc in
[`../yos_proc.mjs`](../yos_proc.mjs). It implements epic
[#33](https://github.com/zokrezyl/yos/issues/33), following `docs/browser.md`
(Architecture A — wasm3-in-wasm).

It is kept **separate** from the legacy `../*.mjs` prototypes on purpose: those
are frozen; this is the path that replaces them once parity gates pass.

## Phase 0 (issue [#34](https://github.com/zokrezyl/yos/issues/34)) — done

Phase 0 proves the single enabling fact: **the wasm3 interpreter, itself
compiled to a browser wasm module, can execute a guest wasm** — no JavaScript
reimplementation of any guest libc.

- `smoke_guest.c` — a trivial wasm32 guest with **zero imports** that exports
  `int compute(void)` returning 42. Zero imports is the point: there is nothing
  for JavaScript to satisfy, so the guest cannot be secretly served by JS.
- `phase0_host.c` — the Emscripten host. `main()` drives wasm3 (parse →
  load → find `compute` → call → read result) over the guest embedded as a C
  byte array, and prints the result. Every wasm3 `M3Result` is checked, so a
  failure to execute returns non-zero.
- `phase0_yperf_stub.c` — no-op definitions of the three `yperf_*` profiling
  symbols the vendored wasm3 fork references unconditionally (disabled by
  default; a later phase can swap in the real `src/yos/yperf/yperf.c`).
- `build-phase0.sh` — builds the guest, embeds it (`smoke_guest_wasm.h`), and
  emcc-compiles `phase0_host.c` + the vendored wasm3 sources into
  `yos-host-phase0.mjs` (+ `.wasm`).
- `phase0_smoke.mjs` — the gate: loads the host in Node, captures its output
  through a minimal `print` hook, and fails the build unless wasm3 actually ran
  the guest and returned 42.

### Build & test

```sh
make browser-host-phase0        # build the Phase 0 host artifact
make test-browser-host-phase0   # build + run the Node smoke test
```

Expected smoke output:

```
phase0: wasm3 executed guest, compute()=42
phase0 smoke: PASS — wasm3-in-wasm executed the embedded guest
```

### Toolchain

The host is compiled with **nix-provided Emscripten** (nixpkgs 24.11 →
emscripten 3.1.64), obtained through the flake dev shell — the Makefile targets
run the build under `nix develop`, so no separate emsdk install is required. The
guest is compiled with the system `clang` (any clang with the wasm32 backend).

Two notes for anyone running `build-phase0.sh` by hand:

- Do it inside `nix develop` (or `nix shell github:NixOS/nixpkgs/nixos-24.11#emscripten`)
  so a working `emcc` is on PATH.
- The build links at `-O2`, not `-O3`: the packaged emscripten frontend emits an
  `-O3`-only binaryen flag (`--no-stack-ir`) its pinned `wasm-opt` rejects. `-O2`
  produces the same sibling-call dispatch wasm3 needs and links cleanly.

Overrides honored by the build script: `YOS_WASM_CLANG` (guest compiler,
default `clang`), `EMCC` (default `emcc`), `PYTHON` (default `python3`, used to
generate the embed header).

## Phase 1a (issue [#35](https://github.com/zokrezyl/yos/issues/35)) — done

Phase 1a proves the **bridge pointer contract** in the browser host: a guest's
`env.*` libc imports are served by C wrappers that translate a guest pointer
with `host = ctx->memory + guest_offset` — the one operation the whole yos
bridge model rests on — with zero JS libc.

- `bridge_guest.c` — a real (tiny) yos guest: it imports `env.write`,
  `env.getpid`, `env.exit` and nothing else, builds a message *in guest memory*,
  and writes it.
- `phase1a_host.c` — three hand-written raw-function wrappers in the exact
  desktop `yos_bridge.c` shape: recover the per-call ctx via
  `m3_GetUserData(runtime)`, pop the wasm3 stack slots, bounds-check and
  translate the guest offset, then call host libc. `getpid` returns host-owned
  state (like desktop `yos_getpid`); `exit` traps out cleanly with the guest's
  code (the browser host can't call host `exit()` — that would kill the module).
- `phase1a_smoke.mjs` — asserts the guest's `write` surfaced through pointer
  translation (`pid=4242 via C bridge`) and `exit` carried code 7 through the C
  wrapper.

```sh
make test-browser-host-phase1a
```

Why this can't be faked by JS: the written bytes only come out right if the C
wrapper read the guest's memory at the correct offset, and the pid is host state
the C `getpid` wrapper returned — neither is a JS value.

## Phase 1b (issue [#36](https://github.com/zokrezyl/yos/issues/36)) — generated bridge + impl/vfs

Phase 1b replaces the hand-written wrappers with the **real generated
`yos_bridge.c`** plus the yos **impl** and **vfs** source trees, compiled to
wasm with Emscripten. A guest's `env.*` libc calls now get FreeBSD-shaped
behaviour from the *same C code desktop runs* — pointer translation, `fd_map`,
errno remap, VFS — with Emscripten musl/MEMFS as the storage substrate beneath.

- `phase1b_guest.c` — a guest that does **echo** (`write`) and **cat**
  (`open`/`read`/`write`/`close`) plus `getpid`, all as `env.*` imports.
- `phase1b_host.c` — bootstraps a real `struct yos_exec_ctx` (guest memory +
  `fd_map` via `yos_fd_table_init`), calls the generated
  `yos_brg_link_imports`, and runs the guest.
- `phase1b_bridge_support.c` — minimal definitions of the five bridge tracing
  helpers desktop keeps in `main.c` (which the browser host doesn't compile).
- `build-phase1b.sh` — compiles the generated bridge + ~40 impl/vfs/support
  files + wasm3 (objects cached in `obj/`), links with
  `-sERROR_ON_UNDEFINED_SYMBOLS=0` (the bridge references the full impl surface;
  un-compiled leaves become inert stubs), and MEMFS-embeds the cat test file.

```sh
make test-browser-host-phase1b   # needs `make codegen` first (generated bridge)
```

Expected:

```
phase1b: pid=1 echo via generated bridge
phase1b: cat via generated bridge OK          # ← file contents via open/read through the bridge + MEMFS
```

**What's proven:** `echo` and `cat` run through `yos-host.wasm` on the real
generated bridge + impl/vfs, no JS libc — the core of the #36 acceptance.

**Scoped for now** (honest boundaries, carried by later passes):
- `impl/io/file.c` (the `FILE*` stdio table) is omitted: the generated bridge
  emits default impls for six wide-char stdio functions that collide with it,
  and wasm LLD has no `--allow-multiple-definition`. The raw-fd echo/cat path
  doesn't need `FILE*`. Proper fix is codegen not emitting defaults for
  hand-written functions.
- `ls -alrt` and `ps` need the **real tool wasm artifacts** (argv, malloc,
  `struct stat` conversion, procfs/sysctl) rather than a custom guest — that
  ties into #37's "same tool artifacts" work and a larger impl surface.
- Clean process `exit()` in the browser host (desktop `exit()` would kill the
  module) and fork/exec/wait are Phase 3 (#38).

## Phase 2 (issue [#37](https://github.com/zokrezyl/yos/issues/37)) — same artifacts + parity harness

Phase 2 turns the fixed-guest host into a **general runner** that executes an
arbitrary desktop tool artifact, and adds a **parity harness** that runs the
same artifact through native yos and the host wasm to classify divergence.

- `yos_host_run.c` → **`yos-host.mjs`** — reads the guest wasm from `/guest.wasm`
  in MEMFS (the harness writes it there), ports the host-glue imports the
  desktop keeps in `main.c` (argv/env bootstrap, `__error`, `__main_argc_argv`,
  the variadic printf/scanf family, a browser-safe trapping `exit`), and calls
  the per-module link functions (`yos_freebsd_userland_link`, `yos_strto_link`,
  `yos_callback_link`, `yos_sysctl_link`, …) alongside `yos_brg_link_imports`.
- `host_parity.mjs` → **`make test-browser-host-parity`** — loads tools from
  `result/libexec` (the **desktop** artifact, not the stale `../tools/`), runs a
  focused command set through **both** backends, and classifies:
  `MATCH` / `HOST-GAP` (native passes, host differs) / `NATIVE-GAP` (native
  itself fails) / `BOTH-FAIL`.

Current matrix (the same `echo`/`basename`/`cat`/`wc`/… artifacts desktop runs):

```
  MATCH       echo, basename, cat, wc      # byte-identical through the C host, no JS libc
  NATIVE-GAP  grep                          # host correct; native hits the rune-locale gap (→ #40)
  HOST-GAP    sort, cut                     # asprintf/heapsort + per-tool surface not yet wired
  BOTH-FAIL   head, tr                      # tr needs stdin plumbing; head test-setup
```

`ls`/`ps` need `struct stat` conversion + procfs/sysctl breadth and are the next
HOST-GAPs to close. The **NATIVE-GAP on `grep`** is the inherited native
rune-locale bug the epic predicts — visible here, fixed once in C under #40.

Same-artifact requirement met: the browser and desktop run the identical guest
tool wasm; the only JS is the harness glue (MEMFS writes, output capture).

## Phase 4 freeze + native-gap tracking (issues #39, #40)

- **#39 — freeze the JS host, gate the default.** `../yos_proc.mjs` carries a
  LEGACY/FROZEN banner: no new semantic patches, only harness compatibility.
  `host-select.mjs` picks the backend — default **`js`** (legacy) with the C
  host (`yos-host.wasm`) behind an explicit flag (`?host=c` / `YOS_BROWSER_HOST=c`).
  Switching the default to the C host is **test-gated** on the parity matrix
  (docs/browser.md §5), not done here — the harness shows 4/9 today.
- **#40 — inherited native gaps.** The parity harness marks known native gaps
  (`knownNativeGap`) so they surface as `NATIVE-GAP*` (expected, tracked) and are
  never mistaken for browser regressions; an *unexpected* native gap warns. The
  harness runs both native and host wasm, which is the dual-backend regression
  test. `grep`'s rune-locale gap is flagged; the shared C fix (impl/libc ctype/
  rune init) makes both backends agree once landed.

## Phase 3 fork/process — status (issue #38)

**Not yet functional; status recorded honestly.** `impl/proc/proc.c` (yos's
fork/exec/wait machinery) *is* compiled into `yos-host.wasm`, and the generated
bridge links `fork`/`waitpid`/`_exit`, so a fork guest **runs** — but `fork()`
currently returns failure:

```
$ forkdemo through yos-host.wasm  →  "fork failed"   (graceful, no crash)
```

Why: desktop `yos_proc_fork` snapshots linear memory + the wasm3 runtime state
and rewinds via `m3_NewSiblingRuntime` + a host pthread (asyncify dance). The
browser host runs a **single** wasm3 instance and does not yet wire the
**cooperative** variant the plan calls for (snapshot the guest memory + wasm3
stack, run the child inline to completion, then resume the parent). The pieces
that must land in the host runner:

- a process table + cooperative scheduler (run child to completion, then parent);
- fork = copy the guest's host-owned memory buffer + a second wasm3 context
  (the "easy half" — no shared-memory split, per docs/browser-architecture-b.md);
- shared open file descriptions across fork/dup, `wait`/SIGCHLD/exit lifecycle,
  pipes / socketpair / SCM_RIGHTS / pty / termios / procfs process listing.

Until then the host runs **non-forking** guests (echo/cat/wc/basename — see the
parity matrix); `zsh`/`tmux` need this phase.

## What comes next

- **#38** — wire the cooperative process model above into `yos_host_run.c`.
- Close the remaining HOST-GAPs (`sort`/`cut`, then `ls`/`ps`) and land the
  shared C rune-locale fix so `grep`/`sed` agree on both backends.
