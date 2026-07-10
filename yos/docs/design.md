# yos design

This document explains the **why** and **how** behind yos's design:
the FreeBSD-libc surface contract, the codegen pipeline that
auto-derives most of the bridge code, the small set of subsystems we
hand-write and why, and how the Nix packaging brings it all together.

If you only want to use yos, [README.md](../README.md) is enough. This
file is for people changing yos itself or porting another wasm
program.

## 1. The contract: wasm guest sees FreeBSD libc

Every libc function the wasm guest references becomes a wasm import
on `env.<name>`. The `.wasm` file embeds **no** libc bodies — only
declarations annotated with import attributes:

```c
__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t n);
```

clang's wasm32 back end emits the import; wasm-ld's `--allow-undefined`
keeps the link happy. yos's host runtime resolves all of those imports
at module load time.

The shapes — function signatures, struct layouts, errno values, flag
constants — are all **FreeBSD's**, specifically the FreeBSD-i386 ABI:
32-bit pointers, 32-bit `time_t` and `long` and `size_t`, FreeBSD's
`struct stat` field order, FreeBSD's `O_*` flag values. We picked
i386 because:

- wasm32 is a 32-bit address space; matching pointer width is free.
- iOS / tvOS / macOS share BSD heritage; FreeBSD-shape structs port
  there with the smallest delta vs the host.
- The FreeBSD source tree provides a complete reference for every
  function we'd want to compile to wasm later (Tier 2).

We explicitly do **not** target Linux libc shapes, WASI, or musl.
Linux's syscall ABI is kernel-coupled and Apple platforms forbid it
in shipping apps. WASI's surface is too narrow for shells/editors.
musl drags in Linux-isms.

## 2. The 3-tier resolution ladder

For each FreeBSD libc function the wasm guest imports, yos picks the
**lowest** tier that gives correct behaviour:

### Tier 1 — direct host-libc passthrough

The bridge body translates wasm pointer args to host pointers (via
`ctx->memory + offset`), calls the host's libc with the same name,
returns. Errno is read after the call and remapped from host →
FreeBSD code via `yos_remap_errno_h2g`.

This is most pure / stateless functions: `strlen`, `memcpy`, `qsort`,
`strtol`, `sin`, `read`, `write`, `close`, `lseek`, hundreds more. The
generator emits these mechanically from the type-extracted YAML — see
§ 3.

### Tier 2 — FreeBSD source compiled to wasm32

Some functions can't be served by host libc directly because the
shape is genuinely different (variadic `printf`-family, FreeBSD-only
locale machinery). For those we pull the corresponding `.c` files
from the FreeBSD source tree, cross-compile them to wasm32, and
package the result into a `libc-pure.wasm` byte array embedded in the
yos host binary. yos loads it at startup into a *secondary wasm3
instance*. When the guest imports a Tier-2 function, the bridge
dispatches into this sidecar.

The Tier-2 module is allowed to import its own lower-level libc
functions (`printf` calls `write`); those resolve through the **same**
yos import surface, landing in Tier 1 or Tier 3 as appropriate. So
the sidecar runs in a layered way without yos baking in any
assumptions about it.

We do **not** vendor a full FreeBSD libc. Only the specific `.c`
files we need, listed under `from_freebsd_src:` in
`src/yos/codegen/hooks.yaml`.

### Tier 3 — yos hand-written implementation

State-bound operations live in `src/yos/impl/<area>.c`:

- **`fork`/`vfork`/`execve`/`waitpid`/`kill`** (`impl/proc.c`) — proc
  table, asyncify-based fork (snapshot wasm linear memory + globals →
  spawn a host pthread → both rewind to the fork point), `_exit`,
  pthread-based child lifecycle.
- **`pthread_*`** (`impl/pthread.c`) — host-pthread-backed; per-process
  TLS arena pool; clone-with-CLONE_SETTLS shape; mutex/cond/key.
- **`sigaction` / `sigsuspend` / `sigprocmask` / `kill`-signal-side**
  (`impl/sig.c`) — yos doesn't have async signal delivery, so
  `sigaction` records the wasm-side handler and `sigsuspend`
  synchronously dispatches SIGCHLD when a child of the calling proc
  zombies (this is what makes zsh's `wait_for_processes` see
  `STAT_DONE`).
- **`malloc`/`free`/`realloc`/`calloc`** (planned `impl/alloc.c`) —
  mimalloc-over-wasm-linear-memory.
- **`mmap`/`munmap`/`brk`** (`impl/mem.c`) — wasm-linear-memory
  bookkeeping. Lower half = brk-heap, upper half (anchored at
  `memory_size/2`) = mmap arena. Best-fit free list.
- **fd table & file machinery** (`impl/vfs.c`, `impl/file.c`,
  `impl/env.c`) — the wasm-side fd numbers map to host fds; FILE\*
  handles are small ints into a host FILE\* table; environ is mirrored
  into wasm linear memory so the guest's getenv finds inherited vars.
- **callback adapters** (`impl/callback.c`) — `qsort`/`bsearch` etc.
  take a guest function pointer (table index); we run the algorithm
  host-side and call back into wasm via `m3_CallV(table0[idx], …)`.

Selection rule, restated: Tier 1 if host libc has a compatible
function. Else Tier 2 if a FreeBSD source file gives the right
behaviour cross-compiled. Else Tier 3.

**Strictly forbidden libc sources:** musl, wasi-libc, newlib. Tier 2
is FreeBSD only.

## 3. Codegen pipeline — auto-generate from headers, hand-write as last resort

The bridge is mostly mechanical translation. We do that translation
*from* structured data extracted by libclang — never by hand-coding
"what `struct stat` looks like".

### 3.1 extract.py — libclang reads the headers

`src/yos/codegen/extract.py` walks two header trees and emits YAML:

- **Guest** — the FreeBSD-i386 sysroot under
  `build-tools/freebsd/headers-i386/`. Extracts every function
  declaration (with arg names + types), every typedef, every struct
  layout (field offsets + sizes, recursively), every enum, every
  integer constant. Output:
  `src/yos/codegen/guest-api-i386-freebsd.yaml`.
- **Host** — the host's libc include path, discovered by
  `clang -E -v /dev/null` (see `build-tools/host-libc/snapshot.py`).
  Output: `src/yos/codegen/host-api.yaml`.

The output is YAML so it's diffable, reviewable, and trivially
reloadable from any other language.

### 3.2 compare.py — structured deltas, not heuristics

`compare.py` walks every function arg-by-arg + the return, recursing
into structs and pointers, and emits per-arg deltas:

- `widen` / `narrow` — scalar size mismatch (e.g. wasm `time_t` is 4B,
  glibc 8B). Generator emits a cast.
- `convert_struct` — host and guest struct layouts differ. Generator
  emits a host↔guest conversion call (or routes via Tier 3 if we
  haven't taught it about that struct yet).
- `pointer_descent` — pointer to a type whose pointee differs;
  recursed into.
- `kind_mismatch` — irreducible (e.g. host returns `void *`, guest
  expects `int`). Generator can't emit; falls into `needs_policy`.

`analyse.py` categorises each function:
- **compatible** — zero deltas. Bridge body is one line:
  `return name(...args)`.
- **mechanical** — every delta is a safe widen/narrow/convert.
  Generator emits the bridge body directly.
- **needs_policy** — at least one delta the generator can't decide.
  A human writes a hooks.yaml entry.

Categorisation is *data*, not code. If you change the headers
(updating to a new FreeBSD release, e.g.), the categorisation
recomputes.

### 3.3 bridge.py — emit the C from the data

`bridge.py` reads the three YAML files and writes
`build-linux/src/yos/codegen/yos_bridge.{c,h}`. Per function it emits:

- A `yos_<name>(struct yos_exec_ctx *ctx, …)` body — does the pointer
  translation, scalar casts, optional struct conversion, calls the
  host libc, remaps errno on failure, returns.
- A `m3w_<name>` raw wrapper — pops args from wasm3's stack, calls
  the bridge, pushes the return value.
- An entry in `yos_brg_link_imports(IM3Module)` that binds
  `env.<name>` to `m3w_<name>`.

Same input → same output. Don't hand-edit the generated file.

### 3.4 hooks.yaml — the routing table

`src/yos/codegen/hooks.yaml` is the one place where humans intervene.
Each function name lives in exactly one category:

| Category | Bridge body emits |
|---|---|
| (default — passthrough) | direct host-libc call |
| `custom_proc` / `custom_pthread` / `custom_vfs` / `custom_mem` / `custom_alloc` / `custom_sig` | extern decl only — body lives in `impl/<area>.c` |
| `from_freebsd_src` | tier-2 dispatch into the libc-pure.wasm sidecar |
| `variadic` | bridge skipped entirely; `m3w_<name>` is hand-written (printf family lives in `impl/printf.c`) |
| `struct_convert` | host↔guest struct conversion + host call |
| `stub` | returns -ENOSYS / NULL with a `// stub` comment |
| `runtime_owned` | bridge skipped; `m3w_<name>` linked via a `yos_*_link()` call from `main.c` (qsort/bsearch live here) |

`stub` and `runtime_owned` are the escape hatches for the small
number of cases where the auto-classifier can't pick a category. Each
entry should have a one-line comment saying *why* the function landed
there.

### 3.5 Adding a new libc function

1. Drop the FreeBSD declaration into the guest sysroot (it's already
   there if the function exists in the FreeBSD release we're tracking).
2. `meson compile` — extract → compare → analyse → bridge run.
3. If the function lands in `compatible` or `mechanical`, you're done.
4. If it lands in `needs_policy`, add the function to a `custom_*`
   category in `hooks.yaml` and write the body under `impl/`.

Most ports add zero hand-written code.

## 4. The yos host runtime

`src/yos/main.c` is the binary entry point. It:

1. Loads the wasm guest from disk (or from argv / fork-snapshot).
2. Creates a wasm3 environment + runtime + ctx.
3. Calls `yos_brg_link_imports(module, ctx)` to bind every
   `env.<libc-name>` import.
4. Runs `__wasm_call_ctors` then `_start` (the crt1 we ship in
   `build-tools/sysroot/skel.sh`).

`crt1.o` is a tiny piece of wasm we compile and ship in the sysroot.
Its `_start`:

1. Calls `__yos_argc()` / `__yos_argv_setup()` to populate `argv`.
2. Calls `__yos_envc()` / `__yos_envp_setup()` to populate `environ`.
3. Calls user's `main(argc, argv)`.
4. Calls `exit(rc)`.

Without crt1's env setup, `getenv` inside the guest sees nothing, the
shell falls back to a hard-coded default `PATH`, and command lookup
fails. Without `_start` calling `__wasm_call_ctors`, FreeBSD libc's
ctype init never runs and `isalpha()` always returns 0.

`libyos_stubs.a` (also in the sysroot, see
`nixpkgs/sysroot/default.nix`) provides definitions for symbols that
FreeBSD libc *headers* declare but our `--allow-undefined` link can't
otherwise satisfy: `__stdinp`, `__stdoutp`, `__stderrp`,
`__cap_rights_*`, the `err`/`warn`/`errc` family, `tgetent`/`tgoto`/…

Defining `__stdinp = (FILE *)1`, `__stdoutp = (FILE *)2`,
`__stderrp = (FILE *)3` is a small but load-bearing trick: yos's FILE
handle table treats those small ints as the canonical stream
identifiers. Without these definitions every `fputs(..., stdout)`
goes through a NULL pointer and silently drops bytes.

## 5. The wasm packages: building tools the guest can run

A package is a wasm32 cross-compile of an upstream tarball, against
yos's sysroot, post-processed with `wasm-opt --asyncify`.

### 5.1 build-recipe.nix

`nixpkgs/lib/build-recipe.nix` wraps a recipe under
`build-tools/wasm-pkg/configs/<name>/build.sh` in a Nix derivation.
The recipe runs as plain shell — same script works under
`tools/wasm-pkg.sh` (no Nix) and under the flake.

The wrapper:

- Pre-fetches the upstream tarball via `fetchurl` with a known SHA.
- Sets `ROOT` / `PREFIX` / `WORK` / `WASM_CC` / `WASM_SYSROOT` /
  `WASM_CFLAGS` / `WASM_LDFLAGS` / `DEP_PREFIXES`.
- Runs the recipe, which does its own configure / make / install
  into `$PREFIX`.
- In a `postInstall`, walks `$out/bin/*.wasm`, moves each to
  `$out/libexec/<name>` (suffix stripped), and writes a 2-line
  shell-script runner in `$out/bin/<name>` that does
  `exec ${yos}/bin/yos $out/libexec/<name> "$@"`. So a built package
  has the same shape as the FreeBSD-tools package: clean executable
  names on PATH, the wasm modules tucked under `libexec/`.

### 5.2 The recipe itself — what `<name>/build.sh` does

For autoconf packages (zsh):

- Patches `config.sub` to recognise `wasm32-*-*`.
- Pre-seeds `config.cache` with answers to every `AC_CHECK_FUNC` /
  `AC_SEARCH_LIBS` probe — autoconf's link-tests "always succeed"
  under `--allow-undefined`, so without the cache we'd think every
  function exists and end up with code paths that don't compile.
- Runs `configure --host=wasm32-unknown-unknown ...`.
- Runs `make` against the wasm sysroot.
- `wasm-opt --asyncify` the resulting binary so yos's fork can
  rewind through it.

For cmake packages (nvim, libvterm, tree-sitter, libuv, msgpack-c):

- Sets `CMAKE_TOOLCHAIN_FILE` pointing at a wasm32 toolchain stub.
- `cmake --build` against the wasm sysroot.
- Copies the binary + any runtime tree (nvim's `share/nvim/runtime/`)
  under `$PREFIX`.

For per-tool FreeBSD-base utilities, see
`nixpkgs/pkgs/freebsd-tools/default.nix` — one derivation builds 22
tools out of the shared `freebsd-src` tree, each linked against
`libyos_stubs.a` with selective `from_freebsd_src` includes
(getopt/basename/strsignal/etc.).

### 5.3 The umbrella package

`nixpkgs/default.nix` defines `all` as a `pkgs.symlinkJoin` of the
host yos runtime + every wasm package. Output:

```
$out/bin/<name>          shell runners (one per port)
$out/bin/yos             host runtime
$out/bin/yos-shell       sandbox wrapper — env -i + PATH=$out/libexec
$out/libexec/<name>      bare wasm modules
$out/share/nvim/runtime/ nvim runtime tree
```

`flake.nix`'s `apps.default = bin/yos-shell` so `nix run .#` drops you
into a wasm-zsh shell with `PATH` scoped to `$out/libexec/`. From that
shell every command resolves through yos's wasm process model;
nothing reaches host `/usr/bin`. That's the sandbox boundary.

## 6. Adding a new package

Skeleton, taking "bash" as the example:

1. **Recipe.** Create `build-tools/wasm-pkg/configs/bash/build.sh`
   with the autoconf cross-compile incantation, modelled on
   `configs/zsh/build.sh`. End with `wasm-opt --asyncify`.

2. **Package nix expression.** `nixpkgs/pkgs/bash/default.nix`:

   ```nix
   { buildRecipe }:
   buildRecipe {
     pname = "bash";
     version = "5.2";
     url = "https://ftp.gnu.org/...";
     sha256 = "...";
   }
   ```

3. **Wire it.** Add to `nixpkgs/default.nix`:

   ```nix
   bash = pkgs.callPackage ./pkgs/bash { inherit buildRecipe; };
   ```

   And include in `inherit` block + the umbrella's `paths`.

4. **Build & test.** `nix build .#bash`. Once it builds, add an
   integration test under `tests/integration/<name>/` modelled on the
   zsh suite. Stage the new files (`git add`) — Nix flake source is
   filtered to git-tracked files only.

## 7. Things that are explicitly *not* part of yos

- Linux syscall ABI. yos's bridge surface is FreeBSD libc, not Linux
  kernel. `<linux/*.h>` is forbidden. `syscall(SYS_*)` is forbidden.
- WASI. Different surface, different goals.
- musl, wasi-libc, newlib. Tier 2 is FreeBSD only.
- A debugger / loader / dynamic linker for wasm. yos loads one wasm
  module per ctx (plus the Tier-2 sidecar). `dlopen` is not
  supported; static-link your dependencies into the package.
- A scheduler. The host's pthread scheduler runs per-ctx threads. yos
  doesn't preempt or rate-limit the wasm guest.

## 8. Testing

`tests/integration/` has black-box tests that drive yos with real
wasm guests through a pty:

- `tests/integration/zsh/` — script mode, grammar, env, missing
  externals, runhookdef trap, pipe behaviour, …
- `tests/integration/nvim/` — quit, winsize, input, edit-session.
- `tests/integration/pty/` — yos's pty bridge in isolation.

Each test spawns yos via `pty.fork`, drives it with timed keystrokes,
and checks the captured pty output + exit status. Tests `xfail :
true` are pinned bug regressions: they flip to `UNEXPECTEDPASS` the
moment the underlying bug is fixed, forcing whoever fixed it to flip
the flag in the same change.

## 9. Where to look first

| Question | Path |
|---|---|
| What does yos do for libc fn `X`? | grep `"X"` in `hooks.yaml`; if not there, it's tier-1 passthrough. |
| Where's the autogen output? | `build-linux/src/yos/codegen/yos_bridge.c` after `meson compile`. |
| A bridge is wrong — where do I edit? | If hand-written, `src/yos/impl/<area>.c`. If autogen, the type tables come from `extract.py` — fixing usually means fixing a header or adding a `hooks.yaml` route. |
| How does fork work? | `src/yos/impl/proc.c::yos_fork` + the asyncify-rewind comment block. |
| How is a new package built? | Recipe under `build-tools/wasm-pkg/configs/<name>/build.sh`, wrapped by `nixpkgs/lib/build-recipe.nix`. |
| Why does `echo` print nothing? | It used to — see `nixpkgs/sysroot/default.nix` `__stdinp/__stdoutp/__stderrp` block for the fix. |
