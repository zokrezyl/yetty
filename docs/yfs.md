# yfs — the web-served lazy filesystem

yfs is a static, HTTP-served filesystem tree deployed with the site
(GitHub Pages, or any static server). It plays the role TinyEMU's
9p-over-HTTP root plays for the temu session type, for two consumers:

1. **The yos web engine** (phase 1, implemented): the guest userspace —
   tool binaries in `/bin`, `/usr/share`, the demo files. Nothing is
   downloaded up front except the shell; every other file and binary is
   fetched on first access.
2. **yetty itself** (phase 2, implemented): the webasm asset set (raw
   fonts, WGSL shaders, images). `yetty-assets-preload.js` fetches a small
   eager boot set and demand-pages the rest. The MSDF font atlases are not
   served at all: yetty builds them from the raw fonts with its GPU
   generator on the first visit and the shim keeps them in the browser's
   Cache Storage (see the consumer section below).

One format, one generator, one deployment — per consumer only the
*backend* that mounts the tree differs.

## Tree layout

```
yfs/current.json            {"version": "<V>"} — the only mutable file
yfs/<V>/guest.yfs           aggregate manifest (all listings in one fetch)
yfs/<V>/guest/              path-mirrored guest tree
    .yfs                    directory listing (one per directory)
    bin/.yfs
    bin/zsh                 small bodies live at their mirrored path…
    usr/share/...
yfs/blob/<sha256>           …large bodies (>= 64 KiB) live here,
                            content-addressed, shared across versions
```

- The `<V>` prefix makes everything under it immutable: browsers and
  CDNs may cache forever; a deploy is a new prefix. `current.json` is
  the version pointer the page fetches first.
- The blob store is *unversioned on purpose*: two releases shipping the
  same nvim binary or font CDB reference the same blob — a redeploy
  re-downloads nothing that did not change.
- Bodies smaller than the blob threshold stay at their path-mirrored
  URL, which keeps the tree browsable and debuggable with curl.

## Listing format

Each directory contains one listing file under the reserved name
`.yfs` (the generator refuses input trees that contain a real file by
that name). JSON:

```json
{"v":1,"entries":[
  {"n":"ycat","t":"f","i":1042,"s":2297137,"m":493,"mt":1752200000,
   "h":"<sha256>","b":1},
  {"n":"demos","t":"d","i":2001,"m":493},
  {"n":"sh","t":"l","i":1043,"tgt":"zsh"}
]}
```

| field | meaning |
|---|---|
| `n` | entry name |
| `t` | `f` file, `d` directory, `l` symlink |
| `i` | inode — assigned by the generator, stable within a build; hardlinks share it |
| `s` | file size in bytes |
| `m` | mode bits (decimal) |
| `mt` | mtime, epoch seconds |
| `h` | sha256 of the body (files only) |
| `b` | present and `1` when the body lives in the blob store (`yfs/blob/<h>`); absent when it lives at the mirrored path |
| `tgt` | symlink target |

`guest.yfs` aggregates every listing into one document —
`{"v":1,"dirs":{"":[...],"bin":[...],...}}` (keys are
slash-separated paths relative to the tree root, `""` is the root).
The per-directory `.yfs` files are the canonical format; the aggregate
is a derived artifact so the web engine can materialize all *metadata*
in one round trip. A future backend that wants lazy metadata (or the
native yos host mounting yfs over HTTP) reads the per-directory files.

## Runtime semantics (yos web engine)

- **Eager metadata, lazy bodies.** At boot the engine fetches
  `guest.yfs` and materializes the whole node tree in its VFS — sizes,
  modes, inodes come from the listings, so `stat`/`readdir`/`access`
  and shell PATH walks are synchronous and never touch the network.
  File *bodies* are absent until first use.
- **Suspend on first open.** `open(2)` of a cold file starts the fetch
  and suspends the process with the same asyncify unwind/rewind used
  for blocking terminal reads; the scheduler resumes it when the bytes
  arrive. After that everything downstream (`read`, `lseek`, `mmap`)
  is as synchronous as before. Consequence: **every guest binary that
  opens yfs-backed files must be asyncify-instrumented** (all shipped
  tools are — the instrumentation is the same one fork needs).
  `O_TRUNC` opens skip the fetch (the old bytes are dead anyway).
- **…except inside liblua — then read synchronously.** liblua.wasm
  (nvim's Lua C API companion) shares the guest's memory and function
  table but is NOT asyncify-instrumented: an unwind across a `lua_*`
  forwarder frame corrupts the rewind into an `unreachable` trap.
  nvim's TUI `require`s `vim/termcap.lua` from inside a `lua_pcall`,
  which killed nvim on the static deploy (issue #724). While
  `luaCallDepth() > 0` a cold open therefore reads the body
  synchronously — the node dir client's `readBodySync`, or the browser
  client's `readBodySyncFallback` (sync XHR; blocks the thread, which
  is acceptable for the rare cold-open-inside-Lua). Any other blocking
  syscall entered at lua depth fails loudly instead of corrupting.
  Pinned by `web/lua/nvim_yfs_async_test.mjs`.
- **Exec on demand.** `execve` of a cold binary parks the process in
  the existing async-boot state ("booting"), fetches the body, compiles
  it, and swaps the image in — the same path oversized modules already
  take. Compiled modules are cached by content hash, so `sh → zsh`
  (symlink, same body) compiles once.
- **Writes stay in RAM.** The HTTP layer is immutable; the engine VFS
  is the overlay. Writes, creates, truncates, unlinks mutate the
  in-memory tree only, exactly as before yfs.
- **Symlinks** whose target resolves inside the tree are aliased to the
  target node at mount time (hardlink semantics — same node object,
  same body cache). `readlink` fidelity is not a phase-1 goal.
- **Failure**: a fetch error marks the node; the suspended open returns
  `EIO`, exec fails with `ENOENT`-style shell diagnostics. A retry is a
  new open.

## Deployment

The generator (`build-tools/yos/make-yos-web-bundle.py`) consumes the
yos nix umbrella (`result/libexec`, `result/share`), the yetty guest
tools (`build-yos-guest/*.wasm`), and the demo files, and emits the
bundle:

```
engine/     the browser engine modules (unchanged)
yfs/        the tree described above
```

The pipeline is unchanged: bundle tarball → workflow artifact →
`stage-yos-web` → webasm build dir (`yos-web/` prefix) → Pages copies
`engine/` and `yfs/` to the site root. `serve.py` serves the same
layout for local dev.

Retired by yfs: `fs/pack.bin` (the monolithic `/usr/share` blob) and
`tools/` + `tools/list.json` (the fetch-all-77-tools boot phase).

## Phase 2 — yetty assets (implemented)

The tree has a sibling root, `yfs/<V>/yetty/`, holding the webasm asset
set (raw fonts, WGSL shaders, configs, demo files under `demos/`). It is
staged at webasm configure time by `build-tools/yos/stage-yetty-yfs.py`
from the `yetty-assets/` manifest into the same yos-web yfs directory the
bundle provides — shared blob store, same version prefix. Two format notes
specific to this producer: bodies stay **brotli'd on the wire** (listing
entries carry `z:"br"`, `s` is the stored/wire size), and every body lives
in the blob store.

The MSDF font atlases are deliberately absent from the tree. yetty builds
them from the served raw fonts with its GPU generator on the first visit
(`ensure_default_font_atlases`, the same step a raw-font desktop install
runs) and hands each finished file to the shim
(`yetty_yplatform_persist_file` → `Module.yettyPersistFile`), which stores
it in Cache Storage stamped with the listing hashes of the source font and
the generator shader; the next visit restores it into MEMFS before `main()`
and rebuilds only when a font or the shader changed.

The consumer is `build-tools/web/yetty-assets-preload.js`:

- **Boot set**: every asset below 256 KiB (configs, shaders, small
  images) is fetched eagerly, in parallel, before `main()` — a few
  hundred KB instead of the old ~43 MB full preload.
- **Everything else is demand-paged**: the shim creates empty MEMFS
  placeholders (existence probes succeed) and wraps the wasm's
  `__syscall_openat` import (`Module.instantiateWasm`); opening a cold
  asset suspends the whole runtime via `Asyncify.handleSleep`, fetches
  the blob, decodes brotli through `DecompressionStream('br')` (linked
  wasm decoder as fallback), writes MEMFS and resumes with the real fd.
  The import is declared in `-sASYNCIFY_IMPORTS` so the instrumented
  wasm can unwind through it. No yetty C changes — an `fopen()` of a
  cold asset costs one network round trip, once.
- Decoded bodies land in Cache Storage keyed by wire sha256, so warm
  starts skip download and decode in both modes.
- **Legacy fallback**: when no `yetty.yfs` is served (e.g. the showcase
  dev server, which generates only the guest tree), the shim falls back
  to the old fetch-everything preload from `yetty-assets/`.

A later refinement can move individual subsystems from block-on-open to
progressive loading (render the fallback, repaint via the dirty path
when the asset lands) — the blocking interception is the semantic
baseline that makes everything correct today.
