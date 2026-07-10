#!/usr/bin/env bash
# nvim — neovim built for wasm32 against yos musl + the deps we already
# staged (lua, libuv, msgpack-c, unibilium, libvterm, tree-sitter, lpeg,
# lua-mpack, luv).
#
# Cross-compile note: neovim's build runs Lua at build time to generate
# headers/sources (the "vim_version" generator etc.). Cross builds need a
# HOST lua AND the wasm32 lua we already built. We install both and let
# CMake's NVIM_GENERATE_PROC find the host lua.

set -euo pipefail

NAME=nvim
VERSION=0.10.4
URL="https://github.com/neovim/neovim/archive/refs/tags/v${VERSION}.tar.gz"
SHA256="10413265a915133f8a853dc757571334ada6e4f0aa15f4c4cc8cc48341186ca2"
DEPS="lua libuv msgpack-c unibilium libvterm tree-sitter lpeg lua-mpack luv"

: "${ROOT:?}"; : "${PREFIX:?}"; : "${WASM_CC:?}"; : "${WASM_SYSROOT:?}"
: "${DEP_PREFIXES:?}"
: "${WORK:=$ROOT/build-linux/wasm-pkgs/${NAME}-${VERSION}}"

mkdir -p "$WORK" "$PREFIX/bin"

TARBALL="$WORK/${NAME}-${VERSION}.tar.gz"
[[ -f "$TARBALL" ]] || curl -fsSL "$URL" -o "$TARBALL"
echo "${SHA256}  $TARBALL" | sha256sum -c - > /dev/null

SRC="$WORK/src"
if [[ ! -f "$SRC/.extracted" ]]; then
    rm -rf "$SRC"; mkdir -p "$SRC"
    tar -xzf "$TARBALL" -C "$SRC" --strip-components=1

    # Patches for cross-compile-to-wasm32:
    # 1. Drop the PO (translations) subdirectory; needs gettext on host.
    sed -i 's|^add_subdirectory(po)|# add_subdirectory(po) # disabled for wasm32 build|' \
        "$SRC/src/nvim/CMakeLists.txt"
    # 2. nlua0 is normally a MODULE (host shared lib loaded by build-time
    #    Lua). wasm-ld can't make a runnable shared module, so we force it
    #    to STATIC — nothing links the .a, but the target's $<TARGET_FILE>
    #    expression still resolves (preload.lua's require('nlua0') is served
    #    from package.preload by lua-codegen, see below).
    sed -i 's|^add_library(nlua0 MODULE)|add_library(nlua0 STATIC)|' \
        "$SRC/src/nvim/CMakeLists.txt"

    # 3. wasm-ld doesn't support `--no-undefined` (and we want late-bound
    #    host imports anyway: musl/yos symbols resolve at wasm3 load time).
    sed -i 's|target_link_libraries(nvim_bin PRIVATE -Wl,--no-undefined)|# disabled for wasm32: target_link_libraries(nvim_bin PRIVATE -Wl,--no-undefined)|' \
        "$SRC/src/nvim/CMakeLists.txt"

    # 3b. Drop the `-lm` / `-lutil` UNIX-block links — musl wasm32 bundles
    #     libm and util into libc, and we link musl-as-libc statically into
    #     the final wasm via the yos runtime.
    sed -i 's|target_link_libraries(main_lib INTERFACE m)|# wasm32: m is in musl libc|' \
        "$SRC/src/nvim/CMakeLists.txt"
    sed -i 's|target_link_libraries(main_lib INTERFACE util)|# wasm32: util is in musl libc|' \
        "$SRC/src/nvim/CMakeLists.txt"

    # 4b. Disable -fstack-protector{,-strong} for wasm32. clang's stack
    #     protector for wasm32 emits canary checks that load from
    #     memory[0..3] (the wasm-libc thread-pointer slot). Because that
    #     same slot is updated by libc whenever it allocates a new
    #     thread struct (e.g. libuv's post-fork bookkeeping in
    #     channel_job_start), the canary at function entry no longer
    #     matches at function exit and __stack_chk_fail trips on a
    #     false positive. wasm bounds-checks every memory access so
    #     stack canaries add nothing useful here; turn them off.
    sed -i 's|target_compile_options(main_lib INTERFACE -fstack-protector-strong)|target_compile_options(main_lib INTERFACE -fno-stack-protector) # wasm32: see build.sh patch 4b|' \
        "$SRC/src/nvim/CMakeLists.txt"
    sed -i 's|target_link_libraries(main_lib INTERFACE -fstack-protector-strong)|# wasm32: see build.sh patch 4b|' \
        "$SRC/src/nvim/CMakeLists.txt"
    sed -i 's|target_compile_options(main_lib INTERFACE -fstack-protector --param ssp-buffer-size=4)|target_compile_options(main_lib INTERFACE -fno-stack-protector) # wasm32: see build.sh patch 4b|' \
        "$SRC/src/nvim/CMakeLists.txt"
    sed -i 's|target_link_libraries(main_lib INTERFACE -fstack-protector --param ssp-buffer-size=4)|# wasm32: see build.sh patch 4b|' \
        "$SRC/src/nvim/CMakeLists.txt"

    # 5. cjson's fpconv_update_locale runs `snprintf(buf, 8, "%g", 0.5)` at
    #    startup and expects "0.5". On wasm32 yos the printf_core scan pass
    #    consistently returns -1 for that exact call site (the standalone
    #    musl snprintf works fine — there's some interaction with nvim's
    #    runtime state we haven't pinned down). The test is a paranoid
    #    sanity-check, not a correctness gate; just skip it. The static
    #    `locale_decimal_point` defaults to '.' which is right for every
    #    locale we actually load.
    sed -i 's|fpconv_update_locale();|/* yos wasm32: skip self-test */ (void)0;|' \
        "$SRC/src/cjson/fpconv.c"

    # 4z. Inject the libuv include path directly into the codegen
    #     (`gen_cflags`) inside src/nvim/CMakeLists.txt. The codegen
    #     `walks` BUILDSYSTEM_TARGETS in its own directory; libuv is
    #     IMPORTED in the top-level CMakeLists, so its include never
    #     reaches the codegen step on a fresh cross build. Append a
    #     hard `-I${LIBUV_INCLUDE_DIR}` after the BUILDSYSTEM_TARGETS
    #     loop so every gen_declarations.lua run sees uv.h.
    sed -i 's|^list(REMOVE_DUPLICATES gen_cflags)|list(APPEND gen_cflags "-I${LIBUV_INCLUDE_DIR}")\nlist(REMOVE_DUPLICATES gen_cflags)|' \
        "$SRC/src/nvim/CMakeLists.txt"

    # 4. Replace FindLibuv.cmake — upstream version runs `check_library_exists`
    #    against the HOST toolchain (glibc), so it appends -ldl -lrt -lkstat
    #    -lkvm -lnsl -lperfstat -lsendfile to LIBUV_LIBRARIES. None of those
    #    exist for wasm32; musl bundles dl/rt/util/m into libc. Stub it down to
    #    a plain (include-dir, library) pair driven by the env we pass in.
    cat > "$SRC/cmake/FindLibuv.cmake" <<'CMAKE'
# wasm32 stub — overrides the upstream FindLibuv.cmake. Honors the cache
# vars LIBUV_INCLUDE_DIR / LIBUV_LIBRARY passed in by build.sh and exposes a
# `libuv` IMPORTED target with no transitive system libs.
set(LIBUV_LIBRARIES ${LIBUV_LIBRARY})
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libuv DEFAULT_MSG LIBUV_LIBRARY LIBUV_INCLUDE_DIR)
mark_as_advanced(LIBUV_INCLUDE_DIR LIBUV_LIBRARY)
add_library(libuv UNKNOWN IMPORTED GLOBAL)
set_target_properties(libuv PROPERTIES
    IMPORTED_LOCATION "${LIBUV_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBUV_INCLUDE_DIR}")
# nvim's src/nvim/CMakeLists.txt builds gen_cflags (used to preprocess
# .c files for header generation) by walking BUILDSYSTEM_TARGETS in
# its own directory and pulling INTERFACE_INCLUDE_DIRECTORIES. Without
# the cmake-side stub being GLOBAL the codegen step doesn't see uv.h.
include_directories("${LIBUV_INCLUDE_DIR}")
CMAKE

    touch "$SRC/.extracted"
fi

# ----------------------------------------------------------------------------
# Pass 1 — host helper for build-time codegen.
#
# neovim's build invokes Lua scripts (preload.lua + generators) that
# require('nlua0'), normally a .so loaded via dlopen. Cross-compiling to
# wasm32 makes a runnable .so impossible, so we build a tiny native
# `lua-codegen` binary with everything statically linked, register
# luaopen_nlua0 in package.preload, and point LUA_PRG at it.
# ----------------------------------------------------------------------------
HOST_HELPER="$WORK/lua-codegen"
HOST_BUILD="$WORK/host-build"

if [[ ! -x "$HOST_HELPER" ]]; then
    mkdir -p "$HOST_BUILD"
    LUA_SRC_DIR="$ROOT/build-linux/wasm-pkgs/lua-5.1.5/src/src"
    LPEG_SRC_DIR="$ROOT/build-linux/wasm-pkgs/lpeg-1.1.0/src"
    if [[ ! -f "$LUA_SRC_DIR/lua.h" || ! -f "$LPEG_SRC_DIR/lpvm.c" ]]; then
        echo "[$NAME] need lua and lpeg sources extracted in build-linux/wasm-pkgs first." >&2
        exit 1
    fi

    echo "[$NAME] building host lua-codegen helper (statically links nlua0)"
    # nvim's mpack/lmpack.c includes nvim/macros_defs.h → auto/config.h.
    # The latter is normally generated by nvim's CMake. For the host helper
    # build we only care about EXTERN/INIT plumbing in macros_defs, none of
    # the HAVE_* flags — provide an empty stub.
    mkdir -p "$HOST_BUILD/auto"
    : > "$HOST_BUILD/auto/config.h"
    (
        cd "$HOST_BUILD"
        # Lua + lpeg + nvim mpack + nvim nlua0 + bit + our wrapper.
        # `cc`, not `gcc`: darwin nix-stdenv (clang-wrapper) ships only
        # `cc`; Linux nix-stdenv (gcc-wrapper) ships both `cc` and `gcc`
        # as symlinks to the same wrapped compiler — picking `cc` keeps
        # behavior identical on Linux while making the script work on
        # darwin. Avoid `$CC`: the wasm toolchain on PATH symlinks
        # *unwrapped* clang as `bin/clang`, which shadows the stdenv
        # wrapper and loses the resource-dir (no stdarg.h).
        cc -O2 -DLUA_USE_POSIX -DLUA_ANSI -DMAKE_LIB \
            -I"$HOST_BUILD" \
            -I"$LUA_SRC_DIR" -I"$LPEG_SRC_DIR" \
            -I"$SRC/src" -I"$SRC/src/mpack" \
            -o "$HOST_HELPER" \
            "$LUA_SRC_DIR"/{lapi,lcode,ldebug,ldo,ldump,lfunc,lgc,llex,lmem,lobject,lopcodes,lparser,lstate,lstring,ltable,ltm,lundump,lvm,lzio,lauxlib,lbaselib,ldblib,liolib,lmathlib,loslib,ltablib,lstrlib,loadlib,linit}.c \
            "$LPEG_SRC_DIR"/{lpcap,lpcode,lpcset,lpprint,lptree,lpvm}.c \
            "$SRC/src/mpack"/{mpack_core,conv,object,lmpack,rpc}.c \
            "$SRC/src/bit.c" \
            "$SRC/src/nlua0.c" \
            "$ROOT/build-tools/wasm-pkg/configs/nvim/lua-codegen.c" \
            -lm
    )
    # Smoke test: each preloaded native module loads without dlopen.
    cat > "$WORK/smoke.lua" <<'LUA'
-- nlua0 expects _G.vim to exist (real preload.lua does `_G.vim = require'vim.shared'`
-- first). Provide a stub so we can verify it loads cleanly.
_G.vim = {}
local mods = { 'bit', 'mpack', 'lpeg', 'nlua0' }
for _, m in ipairs(mods) do
    local ok, err = pcall(require, m)
    if not ok then error('preload failed for '..m..': '..tostring(err)) end
end
print("[lua-codegen] OK: bit/mpack/lpeg/nlua0 preload works")
LUA
    "$HOST_HELPER" "$WORK/smoke.lua"
fi

# Walk DEP_PREFIXES into named per-package vars.
read LUA_P LIBUV_P MSGPACK_P UNIBILIUM_P LIBVTERM_P TREESITTER_P LPEG_P LUAMPACK_P LUV_P <<<"$DEP_PREFIXES"

CFLAGS_W="--target=wasm32-unknown-unknown -nostdlib -nostdinc -O2 \
    -fno-stack-protector \
    $WASM_CFLAGS -D_GNU_SOURCE -D__FreeBSD__=14 \
    -I$LUA_P/include -I$LIBUV_P/include -I$MSGPACK_P/include \
    -I$UNIBILIUM_P/include -I$LIBVTERM_P/include -I$TREESITTER_P/include \
    -I$LUV_P/include \
    -L$WASM_SYSROOT/usr/lib"
# -fno-stack-protector: clang's -fstack-protector for wasm32 emits
# canary checks that load from address 0. nvim's wasi/freebsd-libc
# uses the SAME slot (memory[0..3]) as its thread-struct pointer, so
# any libc operation that updates the thread struct (libuv's post-
# fork bookkeeping in particular) writes a fresh heap pointer there
# and trips a false-positive canary smash on exit of any function
# whose frame straddled the write — channel_job_start being the
# first long-lived caller after our asyncify-fork. Stack protectors
# don't add safety in this build (everything's in linear memory and
# wasm bounds-checks already), so disabling them is the right call.

BLD="$WORK/cmake-build"
rm -rf "$BLD"; mkdir -p "$BLD"
cd "$BLD"

# yos_libc_init.c — initialises the FreeBSD `_DefaultRuneLocale`,
# `_CurrentRuneLocale`, `__mb_sb_limit` globals that the inline ctype
# macros in <ctype.h> need. Without this Lua's tokenizer thinks every
# letter is "unexpected" and nvim aborts with E970 on first init.
# Compile once into an .o that nvim's link picks up.
LIBC_INIT_C="$ROOT/build-tools/wasm-pkg/configs/nvim/yos_libc_init.c"
LIBC_INIT_O="$BLD/yos_libc_init.o"
"$WASM_CC" $CFLAGS_W -c "$LIBC_INIT_C" -o "$LIBC_INIT_O"

# Link flags for yos's libc-by-import surface: no crt1.o, no -lc, no
# musl objects. `--allow-undefined` makes wasm-ld auto-import any
# unresolved fn ref as `env.<name>`; yos resolves those at module
# load. `--export-all` exposes the function table (pthread_create
# needs it) and `--stack-first` puts the wasm shadow stack at the
# bottom of linear memory.
# Stack size: libuv's uv__io_poll allocates `struct kevent events[1024]`
# on the stack — 1024 × 64 B = 64 KiB by itself, so a 64 KiB stack is
# fully consumed before any other local fits. Empirically nvim also
# wants room for vimscript parsing, ex_getln, regex ops, etc., so
# bump to 1 MiB. (Real i386 + the FreeBSD 11 kevent shape would fit
# in 32 KiB, but our wasm32 ABI 8-aligns int64_t and gives kevent a
# 64-byte footprint — see src/yos/impl/kqueue.c for the layout
# table.) The `--stack-first` flag still places the stack at the
# low end of linear memory growing downward.
LDFLAGS_W="-Wl,--no-entry -Wl,--export=_start -Wl,--export-all \
    -Wl,--allow-undefined -Wl,--stack-first -Wl,-z,stack-size=1048576 \
    $WASM_SYSROOT/usr/lib/crt1.o $LIBC_INIT_O"

# nvim's wasm-side lua_* / luaL_* references should resolve as wasm
# imports against yos's host-bridged liblua (src/yos/impl/libc/liblua.c)
# rather than being statically linked from the wasm32 lua-5.1.5 build.
# Stand up an EMPTY archive and point CMake's LUA_LIBRARY at it; the
# wasm32 lua headers stay (compile-time only). wasm-ld with
# --allow-undefined then emits env.lua_* imports for every undefined
# reference, exactly the way ssh.wasm imports env.SSL_*.
#
# lpeg / lua-mpack / luv stay as real wasm32 archives — they don't
# DEFINE the lua_* symbols, they only REFERENCE them, so each of those
# references also becomes an env import once liblua's bodies are gone.
EMPTY_LUA="$BLD/empty-liblua.a"
mkdir -p "$BLD"
echo "" | "$WASM_CC" -target wasm32-unknown-unknown -c -x c - -o "$BLD/empty-liblua.o"
llvm-ar rcs "$EMPTY_LUA" "$BLD/empty-liblua.o"

cmake "$SRC" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=wasm32 \
    -DCMAKE_C_COMPILER="$WASM_CC" \
    -DCMAKE_C_COMPILER_TARGET=wasm32-unknown-unknown \
    -DCMAKE_DL_LIBS= \
    -DCMAKE_AR="$(command -v llvm-ar)" \
    -DCMAKE_RANLIB="$(command -v llvm-ranlib)" \
    -DCMAKE_C_FLAGS="$CFLAGS_W" \
    -DCMAKE_EXE_LINKER_FLAGS="$LDFLAGS_W" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
    -DCMAKE_PREFIX_PATH="$LUA_P;$LIBUV_P;$MSGPACK_P;$UNIBILIUM_P;$LIBVTERM_P;$TREESITTER_P;$LUV_P" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DPREFER_LUA=ON \
    -DUSE_BUNDLED=OFF \
    -DENABLE_LIBINTL=OFF \
    -DENABLE_LIBICONV=OFF \
    -DLUA_INCLUDE_DIR="$LUA_P/include" \
    -DLUA_LIBRARY="$EMPTY_LUA" \
    -DLUA_LIBRARIES="$EMPTY_LUA" \
    -DLUA_MATH_LIBRARY="" \
    -DCMAKE_DL_LIBS="" \
    -DLUA_PRG="$HOST_HELPER" \
    -DCOMPILE_LUA=OFF \
    -DICONV_INCLUDE_DIR="$WASM_SYSROOT/usr/include" \
    -DICONV_LIBRARY="" \
    -DLIBUV_INCLUDE_DIR="$LIBUV_P/include" \
    -DLIBUV_LIBRARY="$LIBUV_P/lib/libuv.a" \
    -DMSGPACK_INCLUDE_DIR="$MSGPACK_P/include" \
    -DMSGPACK_LIBRARY="$MSGPACK_P/lib/libmsgpack-c.a" \
    -DUNIBILIUM_INCLUDE_DIR="$UNIBILIUM_P/include" \
    -DUNIBILIUM_LIBRARY="$UNIBILIUM_P/lib/libunibilium.a" \
    -DLIBVTERM_INCLUDE_DIR="$LIBVTERM_P/include" \
    -DLIBVTERM_LIBRARY="$LIBVTERM_P/lib/libvterm.a" \
    -DTreeSitter_INCLUDE_DIR="$TREESITTER_P/include" \
    -DTreeSitter_LIBRARY="$TREESITTER_P/lib/libtree-sitter.a" \
    -DLPEG_LIBRARY="$LPEG_P/lib/liblpeg.a" \
    -DMPACK_LIBRARY="$LUAMPACK_P/lib/liblua-mpack.a" \
    -DLUV_LIBRARY="$LUV_P/lib/libluv.a" \
    || { echo "[$NAME] cmake configure failed; full log:"; tail -50 "$BLD/CMakeFiles/CMakeOutput.log" 2>/dev/null; exit 1; }

# Build only the binary target. The umbrella `nvim` target also tries to
# invoke the freshly-built wasm to regenerate :help tags, which fails on the
# host with "Exec format error" — we don't care, we just want the wasm.
cmake --build . --parallel --target nvim_bin

# Asyncify pass — required for our setjmp/longjmp host imports to actually
# unwind. Without it, after `_longjmp` returns from the host call the wasm
# bytecode falls into the `unreachable` instruction (clang emits this
# because longjmp is noreturn) and traps for real, instead of cooperatively
# unwinding. Lua's pcall depends on this for every error path.
mkdir -p "$PREFIX/bin"
wasm-opt --asyncify -O2 "$BLD/bin/nvim" -o "$PREFIX/bin/nvim.wasm"

# nvim's binary references its runtime tree at $PREFIX/share/nvim/runtime/
# (set at compile time via CMAKE_INSTALL_PREFIX). Without this tree
# nvim crashes loading vim/_init_packages.lua and friends — every
# init.lua errors out with "attempt to index a nil value", and any
# user script that references syntax/colors/etc. fails too.
#
# The canonical runtime layout is in the source tree ($SRC/runtime).
# The cmake-build dir has additional generated files (help tags etc.)
# we want to merge in. Use `cmake --install` to perform nvim's own
# install rules, then prune the empty/aux files cmake leaves behind.
echo "[$NAME] copying runtime tree → $PREFIX/share/nvim/runtime/"
# Direct copy of the source runtime/ — that's the canonical layout
# (syntax/, lua/, autoload/, plugin/, colors/, ftplugin/, doc/, …).
# `cmake --install runtime` only triggers a small subset of install
# rules (pack/dist/* mostly) so we can't rely on it. Merge any
# generated files from cmake-build/runtime/ on top (help tags etc.)
# but skip cmake's own bookkeeping (CMakeFiles/, Makefile, …).
mkdir -p "$PREFIX/share/nvim/runtime"
cp -r "$SRC/runtime/." "$PREFIX/share/nvim/runtime/"
if [ -d "$BLD/runtime" ]; then
    # Pick up cmake-generated runtime artefacts (help tags, package
    # tags etc.) without dragging in cmake's own metadata files.
    for sub in doc syntax pack; do
        [ -d "$BLD/runtime/$sub" ] && cp -r "$BLD/runtime/$sub/." \
            "$PREFIX/share/nvim/runtime/$sub/" 2>/dev/null || true
    done
fi

cat > "$PREFIX/manifest.txt" <<EOF
name=nvim
version=${VERSION}
prefix=${PREFIX}
binary=${PREFIX}/bin/nvim.wasm
deps=${DEPS}
EOF

echo "[$NAME] installed → $PREFIX/bin/nvim.wasm ($(du -h "$PREFIX/bin/nvim.wasm" | cut -f1))"
