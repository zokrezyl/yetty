# yos Nix package set

This directory holds the Nix scaffolding for building yos itself, its
wasm32 sysroot, and wasm32 packages that target it. The flake at the
repo root (`../flake.nix`) wires it up.

The build uses the **existing source tree as-is** — no copies, no
vendoring. Each derivation reads from `src/yos/`, `src/wasm3/`,
`build-tools/`, `include/` directly. Move a file in the repo and the
derivations pick it up.

## Layered foundation

```
              ┌──────────────────────────────────────────────┐
   layer 5    │   pkgs/{nvim, lua, libuv, msgpack-c, …}      │  buildRecipe consumers
              │   pkgs/{echo, true, false, pwd, cat}         │  buildFreebsdTool consumers
              └──────────┬───────────────────────────────────┘
              ┌──────────┴───────────────┐
   layer 4    │   lib/build-yos-package  │   `mkYosPackage` (single-file)
              │   lib/build-recipe       │   `buildRecipe` (drives wasm-pkg.sh recipes)
              │   lib/build-freebsd-tool │   `buildFreebsdTool` (per-tool srcs from FreeBSD tree)
              └──────────┬───────────────┘
        ┌────────────────┼────────────────┐
   ┌────┴────┐    ┌──────┴───────┐    ┌──┴──────────┐    ┌──────────────┐
   │  yos    │    │  toolchain   │    │ sysroot     │    │ freebsd-src  │
   │ (host)  │    │ (wasm-clang) │    │ (wasm32 sr) │←───┤ (.txz extract)│
   └─────────┘    └──────────────┘    └─────────────┘    └──────────────┘
   layer 1        layer 3              layer 2 (+stubs)   layer 1.5
```

| Layer | Drv | What it produces | Source |
| --- | --- | --- | --- |
| 1 | `yos` | `$out/bin/yos` — host runtime binary | `src/yos/`, `src/wasm3/`, `meson.build` |
| 2 | `sysroot` | `$out/usr/{include,lib}` — wasm32 sysroot (FreeBSD-i386 headers, empty stub libs, yos crt1.o) | `build-tools/freebsd/install_includes.py`, `build-tools/sysroot/skel.sh` + FreeBSD release tarball |
| 3 | `toolchain` | `$out/bin/{wasm-clang,clang,wasm-ld,wasm-opt,llvm-ar,…}` | `build-tools/wasm-clang` |
| 4 | `lib.buildRecipe` | function — wraps a `build-tools/wasm-pkg/configs/<name>/build.sh` recipe | `build-tools/wasm-pkg/configs/<name>/` |
| 4 | `lib.mkYosPackage` | function — single-file wasm packages | n/a |
| 5 | `pkgs.nvim` | `$out/bin/nvim.wasm` — neovim 0.10.4 wasm | nvim 0.10.4 release + the 9 deps |
| 5 | `pkgs.{lua,libuv,…}` | `$out/lib/lib<X>.a` + headers | upstream releases |
| 5 | `pkgs.{echo,true,false,pwd,cat}-tool` | `$out/bin/<name>{,.wasm}` — FreeBSD-base ports | usr/src/{bin,usr.bin}/<name>/<name>.c |

## Usage

```sh
# build the whole thing
nix build .#yos        # host runtime
nix build .#nvim       # neovim wasm + transitive deps

# build just one wasm dep
nix build .#libuv      # → $out/lib/libuv.a

# get a dev shell with the wasm toolchain
nix develop            # `wasm-clang` etc. on PATH

# run nvim
./build-linux/src/yos/yos $(nix eval --raw .#nvim)/bin/nvim.wasm
```

## When a recipe pulls a secondary tarball

`buildRecipe` accepts an `extraTarballs = [{ name; url; sha256; }]`
list — each entry is pre-fetched by Nix and dropped into `$WORK`
before the recipe runs. Use this for recipes that grab a vendored
companion archive (`luv` → `lua-compat-5.3`; `lua-mpack` → `libmpack`).

## Adding a port

### Tarball-driven (autotools / cmake / Make build with build.sh)

1. Write `build-tools/wasm-pkg/configs/<name>/build.sh` (the same
   recipe that `tools/wasm-pkg.sh <name>` would run).
2. Create `nixpkgs/pkgs/<name>/default.nix`:

   ```nix
   { buildRecipe, …optional deps… }:
   buildRecipe {
     pname = "<name>";
     version = "…";
     url = "…tarball URL…";
     sha256 = "…";
     deps = [ … ];   # optional, in positional order the recipe expects
   }
   ```
3. Add to `nixpkgs/default.nix` and `flake.nix`'s `packages`.

### FreeBSD-base tool (bin/, usr.bin/, sbin/, …)

For tools whose source lives in the FreeBSD release tree, skip the
recipe and use `buildFreebsdTool`:

```nix
{ buildFreebsdTool }:
buildFreebsdTool {
  pname        = "cat";
  srcDir       = "bin/cat";        # under usr/src/
  srcs         = [ "cat.c" ];      # basenames in srcDir
  extraSrcDirs = [ "bin/foo" ];    # extra paths to search for srcs (FreeBSD .PATH)
  extraCflags  = [ "-DBOOTSTRAP_CAT" ];
  libcExtras   = [ "getopt" ];     # well-known libc helpers (see table in build-freebsd-tool.nix)
}
```

`extraCflags` is for tool-specific defines/includes. `libcExtras` is
for libc helpers yos doesn't bridge cleanly (currently `getopt`,
`err`); the entry pulls the matching .c from `usr/src/lib/libc/<sub>/`
and adds the libc-private include dir.

Tools always link `libyos_stubs.a` from the sysroot, which carries:
- `__cap_rights_*` no-op stubs for FreeBSD's Capsicum macros
- `err`/`warn`/`errc`/`warnc`/`errx`/`warnx`/`getprogname`/`setprogname`
  bare implementations (sidesteps FreeBSD libc's `err.c` namespace
  rename trick that doesn't survive being compiled in isolation)

## Pin synchronisation

Two files carry pinned upstream versions/sha256:

- `nixpkgs/sysroot/default.nix` — `freebsdVersion`, `freebsdSha256`
- `nixpkgs/yos/default.nix`     — same pair, plus mimalloc commit

These mirror `meson_options.txt` (FreeBSD) and
`build-tools/mimalloc/meson.build` (mimalloc commit). **Keep them in
sync manually** — a pin mismatch means `nix build` and `meson compile`
reach for different bytes and produce different artifacts.
