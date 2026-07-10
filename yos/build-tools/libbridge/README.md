# libbridge — universal library-bridge generator

Compile a host library's headers into:
- `<libname>_guest.h` — wasm-guest-side declarations (each function
  marked `__attribute__((import_module("env"), import_name("X")))`)
- `<libname>_host.c` — host-side m3ApiRawFunction wrappers that
  decode wasm-ABI args, translate pointers, and call the real library.

## Why

yos's `src/yos/codegen/` already does this for libc (FreeBSD i386
headers in, generated bridges to host glibc/libSystem out). The
codegen there is libc-specific in places:

- Pointer args default to "wasm-memory offset" translation
  (`ctx->memory + offset`).
- Returns named in a hard-coded set (`strdup`, `strchr`, …) get
  special pointer-translation treatment.
- errno is remapped after every call via `yos_remap_errno_h2g`.

For arbitrary host libraries (libpython, libsqlite3, libssl, …)
those defaults are wrong:

- Pointer args are usually **opaque handles** the wasm guest holds —
  the host owns the storage. Passed through as 32-bit handles via
  a host-side handle table.
- Returns are similarly opaque (or NULL).
- No errno remapping — these libraries report errors via their own
  conventions (return value, exception object, …).

`libbridge/` is the generator we run on `Python.h`, `sqlite3.h`,
`openssl/ssl.h`, etc. — same libclang-walk machinery as the libc
extractor, with the policy knobs pointed the other way.

## Per-guest state isolation (THE hard problem)

yos can host multiple wasm guests in one process. If we link
libpython once and two guests each call `import os`, both see the
same `sys.modules` — guest A's monkey-patches poison guest B. Same
class of bug for any library with file-scope state.

Three approaches we considered, and one we landed on:

### ❌ "Blanket `__thread` / `-ftls-model=local-exec` on the library"

Tempting one-line fix. Doesn't work:

1. **`-ftls-model=local-exec` doesn't convert variables.** It only
   changes the ABI of variables *already* declared `__thread` /
   `_Thread_local`. Applied to an unmodified library, every static
   stays process-global. There is no compiler flag that retroactively
   makes file-scope statics thread-local.

2. **Even with a successful source patch, breaks libraries whose
   globals are intentionally shared.** CPython's immortal singletons,
   type-object registry, GIL, allocator arenas; glibc's malloc lock,
   the actual errno storage; any refcount table or lock-free cache.
   Blanket `__thread` treats "leaked instance state" and "deliberately
   shared sync primitive" the same. Result: silent data corruption.

3. **TLS isolation is one level deep.** A TLS slot holds a *pointer*.
   Thread A stores ptr-to-heap-object-X in its slot; X is on the shared
   heap. If any library code walks from one thread's TLS slot into a
   heap structure with cross-pointers (interner, weak-ref table,
   connection pool), thread B reaches A's data through the indirection.

4. **iOS reality:** even if it worked, can't touch Apple system
   frameworks (libobjc, CFNetwork, …). Same constraint that kills
   dlmopen.

So: no, the blanket-TLS approach is not a real solution. It sounds
plausible because the problem (per-guest state) and the mechanism
(TLS) are both legitimate; the gap is semantic — only library authors
know which statics are per-instance vs. shared.

### ❌ `fork()` per guest, or `dlmopen(LM_ID_NEWLM, …)` per guest

Both ruled out by iOS: fork is forbidden inside the app sandbox;
dlmopen is glibc-only and Apple doesn't allow arbitrary dlopen of
third-party .so anyway. They'd work on Linux but yos's primary
target is iOS/tvOS, so they don't count.

### ✅ Per-library multi-state API + Tier annotation

Every library bridged by libbridge declares a tier in its config.

**Tier 1: Library has an explicit multi-state API.**
Use it. One library instance in the host, N per-guest state objects.

| library  | per-guest object        | example                    |
|----------|--------------------------|----------------------------|
| CPython  | `PyThreadState *`        | `Py_NewInterpreter()`      |
| Lua      | `lua_State *`            | `luaL_newstate()`          |
| OpenSSL  | `SSL_CTX *`              | `SSL_CTX_new()`            |
| sqlite3  | `sqlite3 *`              | `sqlite3_open()`           |
| libcurl  | `CURL *`                 | `curl_easy_init()`         |

Bridge layer holds the per-guest object in a `void *` slot of
`struct yos_exec_ctx` (one slot per library). Each bridge swaps to
the guest's object before the host call, swaps back after.

This is the only iOS-compatible answer for libraries we don't own.
It's what every embeddable C library was designed for — they expected
to be linked into apps with multiple instances.

**Tier 2: We build the library ourselves AND its source separates
per-instance state from shared state, OR we patch it to.**
Hand-audit each file-scope static; selectively mark per-instance
ones `_Thread_local`. Not a blanket pass — per-variable judgment.
Combined with yos's "fork = host pthread per guest" model, gives
per-guest copies of the marked statics.

Used for libraries we bundle that don't have a Tier-1 API but where
the source is tractable.

**Tier 3 does not exist.** A library with no multi-state API and no
audit-able source cannot be safely shared across guests on iOS.
Generator REJECTS configs without an explicit Tier 1 or Tier 2
declaration — refusing silently-broken bridges.

This is also a strong filter on which libraries are worth bridging at
all. The well-designed ones (Lua, Python, sqlite, OpenSSL, libcurl,
libxml2, libsodium) are Tier 1. The "C library with file-scope
statics nobody can clean up" cases are exactly the ones to avoid.

## Workflow

1. Author a `<lib>.libbridge.yaml` config describing:
   - which headers to walk (`headers: [Python.h]`)
   - clang search paths (`cflags: [-I/usr/include/python3.12]`)
   - which symbols to bridge (`allowlist: [Py_*, PyRun_*, ...]`)
   - **per-guest tier (REQUIRED)**: T1 with state-create / state-attach
     hook fns, or T2 with the list of audited `_Thread_local` statics.
2. Run `libbridge.py <config>` — emits `<lib>_guest.h` and `<lib>_host.c`.
3. Plug the host .c into yos's host build; ship the guest .h to wasm
   packages.

## Pointer policies (per arg, configurable)

| policy        | guest sees    | host translation                          |
|---------------|---------------|--------------------------------------------|
| `opaque`      | i32 handle    | host-side handle table (default for libs)  |
| `wasm_offset` | i32 offset    | `ctx->memory + offset` (default for libc)  |
| `string_in`   | i32 offset    | read NUL-terminated string from memory     |
| `bytes_in_out`| (off, len)    | read `len` bytes from memory               |

## Status

Proof of concept under `build-tools/libbridge/`. First consumer:
`impl/libpython.c` (hand-written for 3 functions, Tier-1 with
`Py_NewInterpreter()` per ctx — confirmed isolating monkey-patches
across guests). Generator takes over as soon as it emits equivalent
code.
