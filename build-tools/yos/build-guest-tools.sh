#!/usr/bin/env bash
# Build yetty client tools (yecho, ycat) as yos wasm32 guests.
#
# The tools are compiled with the yos wasm toolchain against the FreeBSD
# wasm32 sysroot — the same compile/link shape every yos guest uses (see
# yos/nixpkgs/pkgs/perf-stress). libc resolves at load time through yos's
# env.<libc-name> import bridges (`-Wl,--allow-undefined` + empty sysroot
# libc.a), so the only bodies in the module are yetty code plus the
# third-party C libraries compiled here from source (libyaml, lz4, and
# for ycat: pdfio, freetype, stb, yxml, cdb; zlib comes from the yos
# nix tree's wasm32 build).
#
# No assets are embedded: fonts and any other files the tools read are
# regular open()/read() calls that go through yos's VFS at runtime.
#
# Outputs land in build-yos-guest/<tool>.wasm. Run them under the yos
# host runtime:
#
#   ./yos/tmp/guest-yos/bin/yos build-yos-guest/yecho.wasm --osc hello
#
# Prerequisites: nix (flake support). The script builds the yos
# toolchain / sysroot / host runtime out-links on first use.

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
YOS_ROOT="$REPO_ROOT/yos"
OUT_DIR="$REPO_ROOT/build-yos-guest"
GEN_DIR="$OUT_DIR/gen"
OBJ_DIR="$OUT_DIR/obj"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yetty-3rdparty}"

TOOLS=("$@")
if [ ${#TOOLS[@]} -eq 0 ]; then
    TOOLS=(yecho)
fi

mkdir -p "$OUT_DIR" "$GEN_DIR" "$OBJ_DIR" "$CACHE_DIR"

#-----------------------------------------------------------------------------
# yos toolchain + sysroot (nix out-links under yos/tmp/)
#-----------------------------------------------------------------------------

nix_outlink() {
    # nix_outlink <attr> <link-path>
    local attr="$1" link="$2"
    if [ ! -e "$link" ]; then
        (cd "$YOS_ROOT" && nix build ".#$attr" --out-link "$link")
    fi
}

mkdir -p "$YOS_ROOT/tmp"
nix_outlink toolchain "$YOS_ROOT/tmp/guest-toolchain"
nix_outlink sysroot   "$YOS_ROOT/tmp/guest-sysroot"
nix_outlink yos       "$YOS_ROOT/tmp/guest-yos"
nix_outlink zlib      "$YOS_ROOT/tmp/guest-zlib"

TOOLCHAIN="$(readlink -f "$YOS_ROOT/tmp/guest-toolchain")"
SYSROOT="$(readlink -f "$YOS_ROOT/tmp/guest-sysroot")"
ZLIB_DIR="$(readlink -f "$YOS_ROOT/tmp/guest-zlib")"
WASM_CC="$TOOLCHAIN/bin/wasm-clang"
WASM_OPT="$TOOLCHAIN/bin/wasm-opt"
WASM_AR="$TOOLCHAIN/bin/llvm-ar"
WASM_RANLIB="$TOOLCHAIN/bin/llvm-ranlib"

#-----------------------------------------------------------------------------
# Third-party sources compiled into the guests (from the pinned versions
# under build-tools/3rdparty/<name>/version).
#-----------------------------------------------------------------------------

fetch_source() {
    # fetch_source <name> <url> <tarball-cache-name> <unpack-marker>
    local url="$2" tarball="$CACHE_DIR/$3" marker="$OUT_DIR/src/$4"
    if [ ! -d "$marker" ]; then
        if [ ! -f "$tarball" ]; then
            curl -fL --retry 5 --retry-delay 3 -o "$tarball.part" "$url"
            mv "$tarball.part" "$tarball"
        fi
        mkdir -p "$OUT_DIR/src"
        tar -xzf "$tarball" -C "$OUT_DIR/src"
    fi
}

LIBYAML_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/build-tools/3rdparty/libyaml/version")"
LZ4_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/build-tools/3rdparty/lz4/version")"
PDFIO_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/build-tools/3rdparty/pdfio/version")"
FREETYPE_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/build-tools/3rdparty/freetype/version")"
# The version file may carry a packaging revision (e.g. 2.13.2-1) that names
# the prebuilt tarball; the upstream source tag uses only the component
# before the first dash — same convention as libssh2/mimalloc.
FREETYPE_UPSTREAM_VERSION="${FREETYPE_VERSION%%-*}"
YXML_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/build-tools/3rdparty/yxml/version")"
CDB_VERSION="4.3.2"   # howerj/cdb, same as build-tools/yetty/libs/cdb.cmake

fetch_source libyaml \
    "https://github.com/yaml/libyaml/archive/refs/tags/${LIBYAML_VERSION}.tar.gz" \
    "libyaml-src-${LIBYAML_VERSION}.tar.gz" \
    "libyaml-${LIBYAML_VERSION}"
fetch_source lz4 \
    "https://github.com/lz4/lz4/archive/refs/tags/v${LZ4_VERSION}.tar.gz" \
    "lz4-src-${LZ4_VERSION}.tar.gz" \
    "lz4-${LZ4_VERSION}"

LIBYAML_SRC="$OUT_DIR/src/libyaml-${LIBYAML_VERSION}"
LZ4_SRC="$OUT_DIR/src/lz4-${LZ4_VERSION}"

fetch_ycat_sources() {
    fetch_source pdfio \
        "https://github.com/michaelrsweet/pdfio/archive/refs/tags/v${PDFIO_VERSION}.tar.gz" \
        "pdfio-src-${PDFIO_VERSION}.tar.gz" \
        "pdfio-${PDFIO_VERSION}"
    local freetype_tag="VER-$(echo "$FREETYPE_UPSTREAM_VERSION" | tr '.' '-')"
    fetch_source freetype \
        "https://github.com/freetype/freetype/archive/refs/tags/${freetype_tag}.tar.gz" \
        "freetype-src-${FREETYPE_UPSTREAM_VERSION}.tar.gz" \
        "freetype-${freetype_tag}"
    fetch_source cdb \
        "https://github.com/howerj/cdb/archive/refs/tags/v${CDB_VERSION}.tar.gz" \
        "cdb-src-${CDB_VERSION}.tar.gz" \
        "cdb-${CDB_VERSION}"
    fetch_source stb \
        "https://github.com/nothings/stb/archive/refs/heads/master.tar.gz" \
        "stb-src-master.tar.gz" \
        "stb-master"

    # yxml comes as two plain files from upstream cgit, pinned by SHA —
    # same endpoint build-tools/3rdparty/yxml/_build.sh uses.
    local yxml_dir="$OUT_DIR/src/yxml-${YXML_VERSION}"
    if [ ! -d "$yxml_dir" ]; then
        mkdir -p "$yxml_dir"
        local file
        for file in yxml.c yxml.h; do
            curl -fL --retry 5 --retry-delay 3 \
                -o "$yxml_dir/$file.part" \
                "https://g.blicky.net/yxml.git/plain/$file?id=${YXML_VERSION}"
            mv "$yxml_dir/$file.part" "$yxml_dir/$file"
        done
    fi

    PDFIO_SRC="$OUT_DIR/src/pdfio-${PDFIO_VERSION}"
    FREETYPE_SRC="$OUT_DIR/src/freetype-${freetype_tag}"
    CDB_SRC="$OUT_DIR/src/cdb-${CDB_VERSION}"
    STB_SRC="$OUT_DIR/src/stb-master"
    YXML_SRC="$yxml_dir"
}

#-----------------------------------------------------------------------------
# Generated headers
#-----------------------------------------------------------------------------

python3 "$REPO_ROOT/src/yetty/yfont/gen-shader-glyph-table.py" \
    "$REPO_ROOT/src/yetty/yfont/glyph-shaders" \
    "$GEN_DIR/shader-glyph-table.gen.h" > /dev/null

# Tree-sitter is off for the yos guests (no grammar archives yet) —
# emit the same empty-stub query header tools/ycat/CMakeLists.txt
# writes when YETTY_ENABLE_LIB_TREESITTER is disabled.
gen_ts_queries_stub() {
    local header="$GEN_DIR/ts_queries_generated.h"
    {
        echo "// Auto-generated — tree-sitter disabled"
        echo "#pragma once"
        echo
        local name
        for name in c cpp python javascript typescript rust go java bash \
                    json yaml toml html xml markdown; do
            echo "static const char TS_QUERY_${name}[] = \"\";"
        done
    } > "$header"
}

gen_stb_impl() {
    cat > "$GEN_DIR/stb_impl.c" <<'STB_EOF'
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
STB_EOF
}

#-----------------------------------------------------------------------------
# Compile flags — identical shape to the other yos guests.
#-----------------------------------------------------------------------------

COMMON_CFLAGS=(
    -target wasm32-unknown-unknown -nostdlib
    --sysroot="$SYSROOT"
    -isystem "$SYSROOT/usr/include"
    -D__i386__=1 -D__yos__=1
    -O2
)

YETTY_CFLAGS=(
    "${COMMON_CFLAGS[@]}"
    -I"$REPO_ROOT/include"
    -I"$REPO_ROOT/src"
    -I"$GEN_DIR"
    -I"$LZ4_SRC/lib"
    -I"$LIBYAML_SRC/include"
    -DYAML_DECLARE_STATIC
)

LIBYAML_CFLAGS=(
    "${COMMON_CFLAGS[@]}"
    -I"$LIBYAML_SRC/include"
    -DYAML_DECLARE_STATIC
    -DYAML_VERSION_MAJOR=0 -DYAML_VERSION_MINOR=2 -DYAML_VERSION_PATCH=5
    -DYAML_VERSION_STRING="\"${LIBYAML_VERSION}\""
)

LDFLAGS=(
    -L"$SYSROOT/usr/lib"
    -Wl,--no-entry
    -Wl,--allow-undefined
    -Wl,--export=_start
    -Wl,--export=main
    -Wl,--export-all
    # 1 MiB stack, placed at the bottom of linear memory (same flags as
    # the nvim recipe). The tools keep 64 KiB I/O chunk buffers on the
    # stack; wasm-ld's default 64 KiB stack overflows into the data
    # segments and silently corrupts globals. --stack-first turns any
    # future overflow into a hard trap instead of silent corruption.
    -Wl,--stack-first
    -Wl,-z,stack-size=1048576
)

#-----------------------------------------------------------------------------
# Source lists
#-----------------------------------------------------------------------------

# Client-side yetty core shared by every tool.
CORE_SOURCES=(
    src/yetty/ycore/result.c
    src/yetty/ycore/types.c
    src/yetty/ycore/util.c
    src/yetty/ytrace/ytrace.c
    src/yetty/yplatform/getopt.c
    src/yetty/yplatform/thread/default.c
    src/yetty/yplatform/term/default.c
    src/yetty/ydraw-list/drawable-list.c
    src/yetty/ydraw-list/cmds.c
    src/yetty/ydraw-list/complex.c
    src/yetty/ydraw-list/drawable-list-registry.c
    src/yetty/ydraw-list/font-resource.c
    src/yetty/ydraw-list/drawable-iterator.c
    src/yetty/ydraw-list/text-drawable-list.c
    src/yetty/ysdf/funcs.gen.c
    src/yetty/ysdf/aabb.gen.c
    src/yetty/ysdf/yaml-factory.gen.c
    src/yetty/ysdf/merge.c
    src/yetty/yface/yface.c
    src/yetty/ywire/wire-statemachine.c
    src/yetty/yfont/shader-glyph.c
)

YECHO_SOURCES=(
    tools/yecho/main.c
    src/yetty/yecho/yecho.c
    src/yetty/yexpr/yexpr.c
    src/yetty/yfsvm/compiler.c
    src/yetty/yplot/yplot.c
    src/yetty/yplot/yplot-gen-wire.c
    src/yetty/yplot/yplot-yaml.c
)

# ycat + the client-side libraries it renders through. Optional handlers
# (mermaid, chart, video, lottie, music, circuit) and optional libs
# (tree-sitter, libcurl, libmagic) stay off — same shape as a minimal
# desktop configure.
YCAT_SOURCES=(
    tools/ycat/main.c
    src/yetty/ycat/ycat.c
    src/yetty/ycat/detect.c
    src/yetty/ycat/dcs.c
    src/yetty/ycat/fetch.c
    src/yetty/ycat/ts-grammars.c
    src/yetty/ycat/ts-highlight.c
    src/yetty/ycat/handler-markdown.c
    src/yetty/ycat/handler-pdf.c
    src/yetty/ycat/handler-image.c
    src/yetty/ycat/handler-svg.c
    src/yetty/ycat/handler-shadertoy.c
    src/yetty/ymarkdown/ymarkdown.c
    src/yetty/ypdf/pdf-content-parser.c
    src/yetty/ypdf/pdf-renderer.c
    src/yetty/ysvg/ysvg-parse.c
    src/yetty/ysvg/ysvg-attrs.c
    src/yetty/ysvg/ysvg-style.c
    src/yetty/ysvg/ysvg-css.c
    src/yetty/ysvg/ysvg-path.c
    src/yetty/ysvg/ysvg-fill.c
    src/yetty/ysvg/ysvg-paint.c
    src/yetty/ysvg/ysvg.c
    src/yetty/yimage/yimage.c
    src/yetty/yimage/yimage-gen-wire.c
    src/yetty/yshadertoy/prim-wire.c
    src/yetty/ycdb/ycdb.c
    src/yetty/yfont/ms-raster-font.c
    src/yetty/yfont/ms-msdf-font.c
    src/yetty/yfont/msdf-font.c
    src/yetty/yfont/raster-font.c
    src/yetty/yfont/font-cache.c
    src/yetty/yrender/types.c
    src/yetty/yrender/font-dispatcher.c
)

PDFIO_SOURCES=(
    pdfio-aes.c pdfio-array.c pdfio-common.c pdfio-content.c
    pdfio-crypto.c pdfio-dict.c pdfio-file.c pdfio-md5.c
    pdfio-object.c pdfio-page.c pdfio-rc4.c pdfio-sha256.c
    pdfio-stream.c pdfio-string.c pdfio-token.c pdfio-value.c
)

LIBYAML_SOURCES=(
    src/api.c src/dumper.c src/emitter.c src/loader.c
    src/parser.c src/reader.c src/scanner.c src/writer.c
)

LZ4_SOURCES=(
    lib/lz4.c lib/lz4hc.c lib/lz4frame.c lib/xxhash.c
)

#-----------------------------------------------------------------------------
# Compile + link
#-----------------------------------------------------------------------------

compile() {
    # compile <flags-array-name> <src-root> <obj-subdir> <sources...>
    local -n flags_ref="$1"
    local src_root="$2" obj_subdir="$3"
    shift 3
    local objects=()
    local source_file object_file
    for source_file in "$@"; do
        object_file="$OBJ_DIR/$obj_subdir/${source_file//\//_}.o"
        mkdir -p "$(dirname "$object_file")"
        if [ ! -f "$object_file" ] || [ "$src_root/$source_file" -nt "$object_file" ]; then
            "$WASM_CC" "${flags_ref[@]}" -c "$src_root/$source_file" -o "$object_file"
        fi
        objects+=("$object_file")
    done
    COMPILED_OBJECTS+=("${objects[@]}")
}

link_tool() {
    # link_tool <name> <asyncify:yes|no> [extra link inputs...]
    local name="$1" asyncify="$2"
    shift 2

    # yos_libc_init.o carries the FreeBSD C-locale rune table
    # (_DefaultRuneLocale / _CurrentRuneLocale) that the inline
    # <ctype.h> macros dereference — data can't arrive via env
    # imports, so it must be linked into the guest.
    "$WASM_CC" "${COMMON_CFLAGS[@]}" "${LDFLAGS[@]}" \
        "$SYSROOT/usr/lib/crt1.o" \
        "$SYSROOT/usr/lib/yos_libc_init.o" \
        "${COMPILED_OBJECTS[@]}" \
        "$@" \
        -lc -lyos_stubs \
        -o "$OUT_DIR/$name.raw.wasm"

    # -g keeps the name section — yos's trap backtraces are useless
    # without it, and the size cost is a bundling concern, not a
    # correctness one.
    if [ "$asyncify" = yes ]; then
        "$WASM_OPT" -g --asyncify -O2 "$OUT_DIR/$name.raw.wasm" -o "$OUT_DIR/$name.wasm"
    else
        "$WASM_OPT" -g -O2 "$OUT_DIR/$name.raw.wasm" -o "$OUT_DIR/$name.wasm"
    fi
    if [ "${KEEP_RAW:-}" != "1" ]; then
        rm -f "$OUT_DIR/$name.raw.wasm"
    fi
    echo "built $OUT_DIR/$name.wasm"
}

build_yecho() {
    COMPILED_OBJECTS=()
    compile LIBYAML_CFLAGS "$LIBYAML_SRC" libyaml "${LIBYAML_SOURCES[@]}"
    compile COMMON_CFLAGS  "$LZ4_SRC"     lz4     "${LZ4_SOURCES[@]}"
    compile YETTY_CFLAGS   "$REPO_ROOT"   yetty   "${CORE_SOURCES[@]}"
    compile YETTY_CFLAGS   "$REPO_ROOT"   yetty   "${YECHO_SOURCES[@]}"
    # asyncify: any guest that opens yfs-backed files must be able to
    # suspend on a cold body (docs/yfs.md) — yecho reads piped stdin and
    # can be pointed at files, so it gets the instrumentation too.
    link_tool yecho yes
}

# Freetype cross-builds through its own CMake — pure C, no OS deps once
# zlib/bzip2/png/harfbuzz/brotli are disabled (internal zlib serves the
# gzip module). Cached: reconfigures only when the lib is missing.
build_freetype() {
    FREETYPE_LIB="$OUT_DIR/freetype/libfreetype.a"
    if [ -f "$FREETYPE_LIB" ]; then
        return 0
    fi
    local build_dir="$OUT_DIR/freetype"
    cmake -S "$FREETYPE_SRC" -B "$build_dir" -G Ninja \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_SYSTEM_NAME=Generic \
        -DCMAKE_SYSTEM_PROCESSOR=wasm32 \
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
        -DCMAKE_C_COMPILER="$WASM_CC" \
        -DCMAKE_AR="$WASM_AR" \
        -DCMAKE_RANLIB="$WASM_RANLIB" \
        -DCMAKE_C_FLAGS="${COMMON_CFLAGS[*]} -DFT_CONFIG_OPTION_NO_ASSEMBLER" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DFT_DISABLE_ZLIB=ON \
        -DFT_DISABLE_BZIP2=ON \
        -DFT_DISABLE_PNG=ON \
        -DFT_DISABLE_HARFBUZZ=ON \
        -DFT_DISABLE_BROTLI=ON \
        > "$build_dir-configure.log" 2>&1
    cmake --build "$build_dir" > "$build_dir-build.log" 2>&1
}

build_ycat() {
    fetch_ycat_sources
    gen_ts_queries_stub
    gen_stb_impl
    build_freetype

    local ycat_cflags=(
        "${YETTY_CFLAGS[@]}"
        -I"$PDFIO_SRC"
        -I"$FREETYPE_SRC/include"
        -I"$CDB_SRC"
        -I"$STB_SRC"
        -I"$YXML_SRC"
        -I"$ZLIB_DIR/include"
        -DYETTY_USE_HOWERJ_CDB=1
    )
    local pdfio_cflags=(
        "${COMMON_CFLAGS[@]}"
        -I"$PDFIO_SRC"
        -I"$ZLIB_DIR/include"
    )
    local cdb_cflags=(
        "${COMMON_CFLAGS[@]}"
        -I"$CDB_SRC"
        -DYETTY_USE_HOWERJ_CDB=1
    )
    local stb_cflags=(
        "${COMMON_CFLAGS[@]}"
        -I"$STB_SRC"
    )
    local yxml_cflags=(
        "${COMMON_CFLAGS[@]}"
        -I"$YXML_SRC"
    )

    COMPILED_OBJECTS=()
    compile LIBYAML_CFLAGS "$LIBYAML_SRC" libyaml "${LIBYAML_SOURCES[@]}"
    compile COMMON_CFLAGS  "$LZ4_SRC"     lz4     "${LZ4_SOURCES[@]}"
    compile pdfio_cflags   "$PDFIO_SRC"   pdfio   "${PDFIO_SOURCES[@]}"
    compile cdb_cflags     "$CDB_SRC"     cdb     cdb.c
    compile yxml_cflags    "$YXML_SRC"    yxml    yxml.c
    compile stb_cflags     "$GEN_DIR"     stb     stb_impl.c
    compile ycat_cflags    "$REPO_ROOT"   yetty-ycat "${CORE_SOURCES[@]}"
    compile ycat_cflags    "$REPO_ROOT"   yetty-ycat "${YCAT_SOURCES[@]}"

    # asyncify: freetype's rasterizer drives band overflow via
    # setjmp/longjmp, which yos implements through asyncify rewinds.
    link_tool ycat yes "$FREETYPE_LIB" "$ZLIB_DIR/lib/libz.a"
}

for tool in "${TOOLS[@]}"; do
    case "$tool" in
    yecho)
        build_yecho
        ;;
    ycat)
        build_ycat
        ;;
    *)
        echo "unknown tool: $tool (supported: yecho, ycat)" >&2
        exit 1
        ;;
    esac
done
