# yos in the browser — architecture proof

This slice proves the disputed point concretely: **no interpreter in the
browser, and the bridge is wasm, not JS.**

The browser is itself a wasm engine, so the guest `.wasm` runs on it
natively. yos's value — the `env.<freebsd-libc-name>` bridge layer plus
the stateful subsystems — also compiles to wasm and **shares the guest's
linear memory**, so a guest pointer and a bridge pointer are the same
address. Calls from guest to bridge are wasm→wasm at engine speed. The
only thing that crosses into JS is the irreducible sandbox edge: effects
wasm physically cannot perform alone (push bytes at a terminal, spawn a
Worker, touch storage, read the clock).

```
guest.wasm  ──env.write / env.strlen──▶  bridge.wasm        (wasm → wasm, native)
   │                                          │
   └──────────── one shared WebAssembly.Memory ──────────────┘
                                              │
                                  host.write_bytes            (the ONLY JS)
```

> **Convergence host (epic #33).** The files in this directory are the
> **Architecture B** shared-memory proof slice (guest on the browser's own
> engine). The **Architecture A** convergence host — the yos C runtime +
> wasm3 compiled to one browser wasm module, which is the correctness
> baseline being built out — lives in [`host/`](host/). Phase 0 is done:
> `make test-browser-host-phase0`. See [`host/README.md`](host/README.md)
> and `docs/browser.md`.

## What runs

- `guest.c` → `guest.wasm` — the app. Imports `env.write`, `env.strlen`,
  `env.memory`. Knows nothing about the bridge.
- `bridge.c` → `bridge.wasm` — the yos-host bridge. Exports `write` /
  `strlen`, imports `host.write_bytes` (the single host effect) and the
  shared `env.memory`. `strlen` is a **pure wasm bridge**: it reads the
  guest string out of shared memory and computes with zero JS. `write`
  is the Tier-1 shape: translate (no-op, shared memory) → dispatch to the
  one host effect → return.
- `run.mjs` — headless harness (`node run.mjs`). Owns the one Memory,
  wires guest↔bridge, asserts the output.
- `index.html` + `serve.sh` — the same wiring in a tab.
- `shell_demo.c` → `shell_demo.wasm` — an **interactive** guest: a
  minimal line shell you type into. Push-driven (`feed_byte` per
  keystroke), so no blocking `read()` yet. NOT zsh — it is the skeleton
  the real guest plugs into.
- `terminal.html` — a real **xterm.js** terminal wired to that guest
  through the wasm bridge. `shell_test.mjs` checks the same guest+bridge
  headlessly.

## Run it

```sh
nix develop ../../../../#default --command ./build.sh         # → *.wasm
nix develop ../../../../#default --command node run.mjs        # headless proof: PASS
nix develop ../../../../#default --command node shell_test.mjs # headless shell: all ok
nix develop ../../../../#default --command ./serve.sh          # browser
#   then open http://127.0.0.1:8099/terminal.html and type:
#   `help`, `echo hi`, `clear`

# Drive the real page in headless Chrome (no npm deps; speaks the
# DevTools Protocol over node's built-in WebSocket). Types into the
# guest, asserts the rendered output. YOS_SHOT=path captures a PNG.
nix develop ../../../../#default --command \
  bash -c 'YOS_CHROME=google-chrome-stable node browser_test.mjs'
```

## Interactive universal zsh in the browser

`zsh.html` runs the **universal** `zsh.wasm` — the exact binary desktop
`yos` runs, no special build — as a **long-lived interactive shell**.
`cd`, variables, history, pipelines and loops persist across commands,
because it is one continuous zsh process, not one-zsh-per-line.

The enabling trick: the universal binaries are fully `--asyncify`
instrumented (for fork). That same suspend/rewind lets `read()`/`poll()`/
`select()` on the terminal **block** — the guest unwinds, yields to the
browser, and the page rewinds it back into the blocked syscall on the next
keystroke. `runInteractive()` in `yos_proc.mjs` drives this; the page just
pipes xterm keystrokes in and guest output out. CR→NL translation mimics a
tty's `ICRNL`; `isatty`/`tcgetattr`/`ioctl(TIOCGWINSZ)` report a real
terminal so zsh enables its line editor.

Proof (real headless Chrome, typing real keystrokes):
`node iterm_check.mjs` — boots zsh.html, asserts `cd`/variables persist and
loops run. `tmux -V` execs the universal tmux (`tmux 3.4`) from the shell.

## Real interactive tmux in the browser

`tmux.html` runs a **full interactive tmux 3.4 session** — the same wasm
binary desktop yos runs — with a real **zsh in the pane**. tmux forks its
server over a named unix socket, hands it the client tty via `sendmsg`
SCM_RIGHTS, spawns the pane shell on a pty, and renders the screen (status
bar, window list, clock) back to the client. Server, client and pane shell
all run **concurrently** under the cooperative scheduler; keystrokes
(including the `C-b` prefix) flow client→server→pty→shell, output flows
back, and it all draws into xterm. `node tmux_browser_test.mjs` proves it in
headless Chrome (status bar renders, a typed command runs in the pane and
draws back). What made this work, in `yos_proc.mjs`: real FreeBSD
`struct __sFILE` so the inlined `getc`/`feof` macros don't trap; a real
`sysconf(_SC_OPEN_MAX)` (without it tmux's imsg layer spins, never reading
the socket); `isatty` over the SCM_RIGHTS-passed tty fd; bidirectional tty
char writes; and a virtual clock so libevent's redraw timers fire under the
synchronous scheduler.

## Real interactive nvim (and top) in the browser

**nvim runs.** Typing `nvim` at the zsh.html prompt boots the full editor:
the TUI client uv_spawns the embedded server (`nvim --embed`), the two talk
msgpack-RPC over a socketpair, the intro screen paints, insert-mode
keystrokes round-trip, and `:q!` tears both processes down and hands the tty
back to zsh. `top` runs full-screen the same way (paint → select() sleep →
repaint → `q`). `make test-browser-nvim`, `make test-browser-top` and
`make test-browser-fullscreen` prove all of it headlessly; the from-zsh test
is the exact keystroke path the page uses.

What made it work, layer by layer:

- **Lua**: nvim imports the whole Lua 5.1 C API (~85 `lua_*`/`luaL_*`) and
  expects the host to provide the VM (desktop bridges to native liblua).
  The browser equivalent is `lua/liblua.wasm` — Lua 5.1 compiled from source
  (C++ + native wasm exceptions for pcall/error, yos sysroot) sharing the
  guest's linear memory AND function table. Its static data parks at a fixed
  `--global-base` (256 MiB) and its element segment at a fixed
  `--table-base` (65536), both above anything the guest owns; the engine
  reserves the data window so the guest heap/mmap can never cross it, and
  `wasm_patch.mjs` lifts the guest table's max so it can grow past the table
  base. Lua's stdio binds the sentinel std streams (fd resolved per call, so
  io.stdout follows nvim's dup2 remap instead of corrupting the RPC socket).
- **Runtime files**: `$VIMRUNTIME` (vim/_defaults.lua, syntax, ftplugin, …)
  mounts into the VFS from `result/share` — one `/fs/pack.bin` blob in the
  browser (serve.sh packs it, `fs_mount.mjs` parses it, file data are
  zero-copy views), lazy per-file reads in node.
- **Engine**: pipe2 O_CLOEXEC honored across exec (libuv's uv_spawn status
  pipe must EOF); fcntl(F_GETFL) reports real access modes (libuv derives
  stream writability from it); kevent grew EVFILT_PROC (how libuv on kqueue
  learns a spawned child died — that's what makes `:q` exit); stat/open on
  the empty path are ENOENT (isdirectory("") == true had netrw hijacking the
  startup buffer); and a module past Chrome's main-thread sync-instantiation
  size limit (nvim is 14 MB) boots asynchronously — the process parks in a
  "booting" state and the scheduler resumes it when the instance resolves.
- **top** needed none of that — just asyncify: the freebsd-tools binaries
  are now wasm-opt --asyncify instrumented like every other universal
  binary, so a tool that blocks in select()/read() suspends instead of
  trapping.

**`:terminal` works too — the full nested chain runs.** `zsh → tmux → nvim
in the pane → :terminal → live shell`, eight processes deep, verified in
node and real Chrome (`make test-browser-nested`; nvim-only :terminal is in
the fullscreen/top suites). What it took: a real `forkpty` (openpty + fork +
login_tty composed in one bridge over the asyncify fork — both sides rewind
through it and each does its own fixup); real `/bin/<tool>` VFS nodes with
exec bits (nvim stats `$SHELL` and refused `:terminal` with "'/bin/sh' is
not executable" when only the exec tool-map knew the name); `getrlimit`
actually filling rlim_cur (nvim's pty spawn loops `fcntl(F_SETFD)` to
RLIMIT_NOFILE — reading stack garbage it spun 2.9M fds into the runaway
guard) with fcntl now EBADF on nonexistent fds; SIGCHLD delivery for forkpty
children (no EVFILT_PROC watches a hand-rolled fork — the parent's sigaction
is how nvim reaps the shell and paints "[Process exited]"); and dup2-style
release of the child's inherited stdio in forkpty/login_tty (a leaked outer
pty slave kept tmux from ever seeing its pane EOF).

Still deferred: `dlopen` (native plugins), `scandir` fills.

## Prototype boundary (issue #21, milestone 1)

The `.mjs` files here that hand-implement a libc/process model in
JavaScript — `mt_libc.mjs`, `mt_engine.mjs`, `yos_proc.mjs`,
`zsh_host.mjs` and the pool workers — are **prototypes**, not the
production yos surface. The production direction is the opposite of
growing this JS libc: a shared yos runtime/bridge layer owns the
FreeBSD/yos ABI semantics (compiled from the existing C subsystems), and
JavaScript only supplies the browser effects wasm cannot perform —
Worker lifecycle, terminal I/O, persistent storage, timers, event
notification.

To stop the prototype boundary from silently drifting, the silent
return-0 fallbacks are gone. There is no longer a catch-all
`new Proxy(env, …)` (was `mt_libc.mjs`) and no "pre-fill every guest
import with a no-op that returns 0" loop (was `yos_proc.mjs` /
`zsh_host.mjs`). Instead every engine hardens its `env` through
`strictImportEnv()` in **`import_manifest.mjs`**:

- imports the prototype implements run normally;
- imports it does **not** implement get a loud-failure stub — the first
  call throws a clear `unsupported libc import 'env.<name>'` diagnostic
  instead of returning a silent 0, so missing semantics are test-visible;
- `PROTOTYPE_PARTIAL` lists the imports that are implemented but with
  known non-faithful semantics (`pipe`, `dup2`, `poll`, `select`, `mmap`,
  `ioctl`/tty, signals, …), so reviewers and parity tests can see exactly
  which behaviours are placeholders.

`import_manifest_test.mjs` is the self-test for this (`node
import_manifest_test.mjs`). A concrete demonstration: `node
mt_test_node.mjs` runs the prototype phases green and then fails loudly
the moment the guest reaches `env.fopen` (unimplemented) — that gap used
to be hidden behind the catch-all.

## Browser-parity harness (issue #21, milestone 2)

`parity_runner.mjs` is the contract: it runs the SAME wasm guests against
both backends — the native `yos` host binary (source of truth) and the
node `yos_proc.mjs` process engine — and compares stdout + exit code. The
desktop result is the baseline; a guest "passes parity" when the browser
engine reproduces it exactly. This is the real proof of process-model
correctness, not the zsh/tmux demos.

```sh
node parity_runner.mjs          # curated contract (fork, pipe, dup2, cwd,
                                # pthread, poll/ppoll, fd inheritance, …)
node parity_runner.mjs --sweep  # broad sweep over every fork-matrix guest
```

To make the contract pass, `yos_proc.mjs` grew a real **per-process I/O
model**, mirroring the desktop runtime rather than faking results:

- a per-process fd table of open file descriptions, shared on `dup`/`dup2`
  and across `fork` (refcounted), copied-not-shared for cwd/env/umask/rand
  so a child cannot perturb the parent;
- real `pipe`/`socketpair` buffers with `EPIPE` on a closed reader and
  `poll`/`ppoll`/`select` readiness;
- a `FILE*` layer (`fopen`/`fread`/`fwrite`/`fgets`/`getline`/`fseek`/…)
  over that fd table;
- cooperative `pthread_create`/`join`/`mutex`/`once` (one JS thread, so a
  created thread runs inline to completion).

Current status: the curated contract passes **12/13** (the one gap,
`fork_channel_from_stdio`, needs concurrent fork scheduling + kqueue
readiness — see below); the broad sweep reproduces **75/91** fork-matrix
guests exactly. The remaining misses are honest, named gaps, not silent
passes: live signal delivery, `mmap`, sockets, `scandir`, `getpwuid`/
`getrusage` struct fills, and anything that needs two processes running at
once (the cooperative engine runs a forked child to completion before the
parent resumes — concurrent scheduling is the M3 Worker-backed model in
`mt_engine.mjs`).

`mt_engine.mjs` is the real-thread engine (Web Workers + SharedArrayBuffer +
Atomics). Each process — the root and every `fork()` child — owns its linear
memory and its own worker pool bound to that memory, so a thread created by a
fork child runs against the child's address space, not the root's (issue #23).
The root pool boots eagerly (async, event-loop friendly); fork/exec children
boot lazily on first `pthread_create`. Node runs the full fork-child-thread
case (`mt_fork_thread_test.mjs`); in the browser a fork child cannot boot its
pool synchronously (a Web Worker's module load needs an event-loop turn the
coordinator won't give while blocked in `Atomics.wait`), so today only the
root threads in-browser and a fork child's `pthread_create` fails loud rather
than running on the wrong memory (`mt_fork_thread_browser_test.mjs` guards that
invariant). Fork itself stays cooperative, not concurrent.

## The gap to real zsh (measured, not guessed)

`nix build .#zsh` produces a 2 MB wasm32 zsh that **imports 169
`env.*` functions** and exports its own memory + the asyncify entry
points + `_start`. Those 169 are the work to boot it in a tab:

| group | count | examples | browser need |
|---|---|---|---|
| pure / stateless | ~50 | `memcpy` `strlen` `qsort` `snprintf` `strtoul` | trivial wasm bridges |
| FILE\* stdio | ~20 | `fopen` `fread` `fwrite` `fflush` `open_memstream` | FILE\* table |
| filesystem | ~25 | `open` `read` `write` `stat` `opendir` `readlink` | virtual FS (no host FS in a tab) |
| process + fork | ~15 | `fork` `execve` `wait3` `kill` `setpgid` | asyncify-fork without wasm3 |
| tty + fd | ~12 | `tcgetattr` `tcsetattr` `ioctl` `dup2` `poll` `select` | a JS PTY |
| signals / creds / time / env | rest | `sigaction` `getpwnam` `clock_gettime` `getenv` | per-area shims |

zsh also exports `memory` (it owns it), so to share memory the
production path either rebuilds zsh with `--import-memory` (embedder owns
the Memory, as this proof does) or has the bridge import zsh's exported
memory via deferred binding. Settle that with the layout decision below.

## How memory is shared (the load-bearing decision)

Both modules are linked `-Wl,--import-memory`, so neither owns the
memory — the embedder creates ONE `WebAssembly.Memory` and hands it to
both. That breaks the instantiation cycle (guest imports the bridge's
exports; the bridge imports the guest's memory would be circular) and is
exactly how Emscripten's dynamic linking shares memory.

The one unsolved-at-scale question this slice **stands in for, does not
solve**: the bridge's own static data + stack must not collide with the
guest's. Here the bridge is parked at a high `--global-base` (8 MiB) and
the guest's footprint is tiny, so they never overlap. The real yos-host
has real state (proc table, fd table, mmap free-list) and the guest
manages its full linear-memory layout (brk heap low, mmap arena high), so
the production answer is one of:

1. **Dynamic-linking memory base** — assign the yos-host module a runtime
   `__memory_base` in a region the guest's allocator is told to avoid
   (Emscripten MAIN/SIDE module model).
2. **Multi-memory** — yos-host keeps memory 0 for its private state and
   addresses the guest's memory as memory 1. Cleaner (no carve-out), and
   now shipping in V8/Firefox, but C/lld support is still young.

Settle that before porting the subsystems. Everything above this line is
proven to work.

## What this slice deliberately omits (the rest of the port, in order)

1. **Real `env.*` surface** — retarget `build-tools/api-generate/bridge.py`
   to emit these wasm bridges instead of native C ones; the
   extract→compare→analyse pipeline is unchanged.
2. **Virtual backends for stateful libc** — a JS/in-wasm filesystem behind
   the existing `src/yos/vfs/` seam; sockets over WebSocket or `ENOSYS`.
3. **Guest threads** — guest compiled with wasm threads/atomics + a
   `shared` Memory; `pthread_create` spawns a Web Worker. Needs the
   COOP/COEP headers `serve.sh` already sends.
4. **fork** — asyncify (already a guest-side transform, runs native) +
   a new Worker with a fresh, non-shared Memory + snapshot copy. Mirrors
   the native split: threads share memory, fork gets its own.
