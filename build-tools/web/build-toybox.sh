#!/bin/bash
#
# Build Toybox for WebAssembly using Emscripten
# Run this from within the nix develop .#web environment
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
TOYBOX_SRC="$PROJECT_ROOT/tmp/toybox"
BUILD_DIR="$PROJECT_ROOT/build-webasm-release"

if [ ! -d "$TOYBOX_SRC" ]; then
    echo "Error: Toybox source not found at $TOYBOX_SRC"
    echo "Clone it with: git clone https://github.com/landley/toybox tmp/toybox"
    exit 1
fi

# Check if we're in the Emscripten environment
if ! command -v emcc &> /dev/null; then
    echo "Error: emcc not found. Run this script via:"
    echo "  nix develop .#web --command bash build-tools/web/build-toybox.sh"
    exit 1
fi

echo "Building Toybox for WebAssembly..."
echo "  Emscripten: $(emcc --version | head -1)"
echo "  Source: $TOYBOX_SRC"
echo "  Output: $BUILD_DIR"

mkdir -p "$BUILD_DIR"
cd "$TOYBOX_SRC"

# Clean previous build
make clean 2>/dev/null || true
rm -f .config generated/config.h 2>/dev/null || true

# Create a minimal config for Emscripten-compatible commands
# These commands don't need fork/exec and work with virtual filesystem
cat > .config << 'EOF'
#
# Toybox minimal config for Emscripten/WebAssembly
# Only includes commands that work without fork/exec
#

# Core functionality
CONFIG_TOYBOX=y
CONFIG_TOYBOX_FLOAT=y
CONFIG_TOYBOX_HELP=y
CONFIG_TOYBOX_HELP_DASHDASH=y
CONFIG_TOYBOX_FREE=y

# Basic I/O commands (work with Emscripten virtual FS)
CONFIG_BASENAME=y
CONFIG_CAT=y
CONFIG_CP=y
CONFIG_DIRNAME=y
CONFIG_ECHO=y
CONFIG_ENV=y
CONFIG_FALSE=y
CONFIG_HEAD=y
CONFIG_LS=y
CONFIG_MKDIR=y
CONFIG_MV=y
CONFIG_PRINTENV=y
CONFIG_PWD=y
CONFIG_RM=y
CONFIG_RMDIR=y
CONFIG_SLEEP=y
CONFIG_SORT=y
CONFIG_TAIL=y
CONFIG_TEE=y
CONFIG_TOUCH=y
CONFIG_TRUE=y
CONFIG_UNIQ=y
CONFIG_WC=y
CONFIG_YES=y

# Text processing (pure computation)
CONFIG_BASE64=y
CONFIG_CMP=y
CONFIG_COMM=y
CONFIG_CUT=y
CONFIG_EXPAND=y
CONFIG_FOLD=y
CONFIG_NL=y
CONFIG_OD=y
CONFIG_PASTE=y
CONFIG_REV=y
CONFIG_SEQ=y
CONFIG_STRINGS=y
CONFIG_TAC=y
CONFIG_TR=y
CONFIG_UNEXPAND=y
CONFIG_XXD=y

# Hash commands
CONFIG_SHA1SUM=y
CONFIG_SHA256SUM=y
CONFIG_MD5SUM=y

# Date/time
CONFIG_CAL=y
CONFIG_DATE=y

# Other useful commands
CONFIG_FACTOR=y
CONFIG_PRINTF=y
CONFIG_TEST=y
CONFIG_TEST_GLUE=y

EOF

echo "Configuration created."

# Set up Emscripten build environment
export CC="emcc"
export HOSTCC="cc"
export CFLAGS="-Os -DTOYBOX_VERSION='\"wasm\"'"
export LDFLAGS="-sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1"
export OPTIMIZE="-Os"
export NOSTRIP=1
# Disable library detection - Emscripten doesn't have these
export LIBRARIES=""

# Skip kconfig - we have our .config already
# Generate the necessary headers manually
mkdir -p generated/unstripped

echo "Generating headers..."

# Create a minimal generated/config.h from .config
echo "// Auto-generated config.h for Emscripten build" > generated/config.h
grep "^CONFIG_" .config | sed 's/=y$/ 1/' | sed 's/^CONFIG_/#define CONFIG_/' >> generated/config.h
echo "#define CFG_TOYBOX 1" >> generated/config.h
echo "#define USE_TOYBOX(...) __VA_ARGS__" >> generated/config.h

# Build toybox
echo "Compiling with Emscripten..."

# First build the host tools we need
$HOSTCC scripts/mkflags.c -o generated/unstripped/mkflags 2>/dev/null || true
$HOSTCC scripts/mktags.c -o generated/unstripped/mktags 2>/dev/null || true

# Try to use the toybox build system
make generated/Config.in HOSTCC=cc 2>/dev/null || echo "Config.in generation skipped"

# Now try the full build with Emscripten as CC
echo "Running make with CC=emcc LIBRARIES=''..."
make V=1 CC=emcc HOSTCC=cc NOSTRIP=1 LIBRARIES="" 2>&1 | tee "$BUILD_DIR/toybox-build.log" || {
    echo ""
    echo "Standard build failed. Check $BUILD_DIR/toybox-build.log for details."
    echo ""
}

# Check output
if [ -f "generated/unstripped/toybox" ]; then
    cp generated/unstripped/toybox "$BUILD_DIR/toybox-wasm.js"
    [ -f "generated/unstripped/toybox.wasm" ] && cp generated/unstripped/toybox.wasm "$BUILD_DIR/"
    echo ""
    echo "Toybox built successfully!"
    ls -la "$BUILD_DIR"/toybox*
else
    echo ""
    echo "Build output not found. Toybox may need patches for Emscripten compatibility."
    echo "See $BUILD_DIR/toybox-build.log for details."
    ls -la generated/unstripped/ 2>/dev/null || true
fi

echo ""
echo "Done!"
