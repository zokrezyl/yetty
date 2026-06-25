#!/bin/bash
# Headless Chrome boot test for the WebAssembly yetty.
#
# Drives a real headless Chrome with SOFTWARE WebGPU (lavapipe by default,
# SwiftShader as fallback) against a locally-served webasm build, captures the
# page console over the Chrome DevTools Protocol, and asserts that yetty
# actually boots: WebGPU comes up, the terminal starts, and the in-browser VM
# reaches Linux — with no fatal wasm/WebGPU errors.
#
# Usage:
#   tools/test-chrome-headless.sh [BUILD_DIR|URL] [SERVE_PORT]
#
# Examples:
#   tools/test-chrome-headless.sh build-webasm-ytrace-release
#   tools/test-chrome-headless.sh https://zokrezyl.github.io/yetty/
#
# Environment overrides:
#   WEBGPU_BACKEND=lavapipe|swiftshader   software adapter (default: lavapipe)
#   CAPTURE_SECS=60                       CDP console capture window
#   DEBUG_PORT=9222                       Chrome remote-debugging port
#   TEST_URL_OVERRIDE=...                 full URL to load (skips ?mode=temu)
#
# Why CDP and not `--enable-logging=stderr`: --headless=new does not reliably
# pipe page console.* to stderr, so console output is captured through the
# DevTools Protocol (tools/capture-chrome-console.py) instead.

set -u

BUILD_DIR="${1:-build-webasm-ytrace-release}"
SERVE_PORT="${2:-8199}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
YETTY_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

WEBGPU_BACKEND="${WEBGPU_BACKEND:-lavapipe}"
CAPTURE_SECS="${CAPTURE_SECS:-60}"
DEBUG_PORT="${DEBUG_PORT:-9222}"

CONSOLE_LOG="/tmp/yetty-chrome-console.log"
CHROME_STDERR="/tmp/yetty-chrome-stderr.log"
SERVER_LOG="/tmp/yetty-web-server.log"
PROFILE_DIR="/tmp/yetty-chrome-prof.$$"

SERVER_PID=""
CHROME_PID=""

cleanup() {
    [ -n "$CHROME_PID" ] && kill "$CHROME_PID" 2>/dev/null
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null
    # Chrome may still be flushing its profile dir; ignore a racy rm failure.
    rm -rf "$PROFILE_DIR" 2>/dev/null || true
}
trap cleanup EXIT

# --- Mode: local build dir vs remote URL ------------------------------------
if [[ "$BUILD_DIR" == http://* ]] || [[ "$BUILD_DIR" == https://* ]]; then
    REMOTE_MODE=1
    BASE_URL="${BUILD_DIR%/}"
    echo "=============================================="
    echo "Headless webasm-yetty boot test — REMOTE"
    echo "URL:     $BASE_URL"
else
    REMOTE_MODE=0
    BASE_URL="http://localhost:$SERVE_PORT"
    echo "=============================================="
    echo "Headless webasm-yetty boot test — LOCAL"
    echo "Build:   $BUILD_DIR"
    echo "Backend: $WEBGPU_BACKEND (software WebGPU)"
fi
echo "=============================================="

# --- Locate Chrome ----------------------------------------------------------
CHROME=""
for cmd in google-chrome chromium chromium-browser google-chrome-stable; do
    if command -v "$cmd" &>/dev/null; then CHROME="$cmd"; break; fi
done
if [ -z "$CHROME" ]; then
    echo -e "${YELLOW}WARNING: Chrome/Chromium not found — skipping headless test${NC}"
    exit 0
fi
echo "Browser: $CHROME"

# --- Python runner for the CDP capture (needs websocket-client) -------------
# Prefer a plain python3 that already has the module (nix dev shell, CI);
# otherwise fall back to `uv run --with websocket-client`.
CDP_PY=(python3)
if ! python3 -c 'import websocket' 2>/dev/null; then
    if command -v uv &>/dev/null; then
        CDP_PY=(uv run --with websocket-client python3)
        echo "websocket-client absent — using: uv run --with websocket-client"
    else
        echo -e "${RED}ERROR: python3 lacks 'websocket' and 'uv' is unavailable;"
        echo -e "       install websocket-client or uv to capture the console.${NC}"
        exit 1
    fi
fi

# --- Local: serve the build dir + verify the modern asset layout ------------
if [ "$REMOTE_MODE" -eq 0 ]; then
    BUILD_PATH="$YETTY_ROOT/$BUILD_DIR"
    if [ ! -d "$BUILD_PATH" ]; then
        echo -e "${RED}ERROR: build dir not found: $BUILD_PATH${NC}"; exit 1
    fi

    echo ""
    echo "=== Asset verification ==="
    ASSET_FAIL=0
    # Current webasm layout: yetty.{js,wasm,data}, the JS-preload manifest under
    # yetty-assets/, the VM iframe wasm tinyemu.{js,wasm}, and the VM guest
    # bundle (kernel/opensbi/rootfs) the iframe fetches at runtime from
    # tinyemu-assets/.
    for f in yetty.js yetty.wasm yetty.data \
             yetty-assets/manifest.json \
             tinyemu.js tinyemu.wasm tinyemu-iframe.html \
             tinyemu-assets/manifest.json \
             tinyemu-assets/kernel-riscv64.bin.br \
             tinyemu-assets/yetty-rootfs-riscv.img.br \
             index.html serve.py; do
        if [ -s "$BUILD_PATH/$f" ]; then
            sz=$(stat -c%s "$BUILD_PATH/$f" 2>/dev/null || echo 0)
            echo -e "${GREEN}OK:${NC} $f ($((sz/1024)) KiB)"
        else
            echo -e "${RED}MISSING:${NC} $f"; ASSET_FAIL=1
        fi
    done
    if [ "$ASSET_FAIL" -ne 0 ]; then
        echo -e "${RED}=== ASSET VERIFICATION FAILED — build the webasm target first ===${NC}"
        exit 1
    fi

    echo ""
    echo "Starting server on :$SERVE_PORT …"
    ( cd "$BUILD_PATH" && python3 serve.py "$SERVE_PORT" . ) >"$SERVER_LOG" 2>&1 &
    SERVER_PID=$!
    sleep 2
    if ! curl -s -o /dev/null "http://localhost:$SERVE_PORT/index.html"; then
        echo -e "${RED}ERROR: server failed to start${NC}"; tail -20 "$SERVER_LOG"; exit 1
    fi
    echo "Server up (pid $SERVER_PID)"
fi

# index.html shows a transport picker unless ?mode= pins the choice; ?trace=1
# turns on YTRACE_DEFAULT_ON so C-side ydebug checkpoints also reach the
# console. Default to the in-browser VM (temu).
TEST_URL="${TEST_URL_OVERRIDE:-${BASE_URL}/?mode=temu&trace=1}"
echo ""
echo "Loading: $TEST_URL"

# --- Software WebGPU adapter flags ------------------------------------------
CHROME_GPU_FLAGS=(--enable-unsafe-webgpu --enable-features=Vulkan,WebGPU)
case "$WEBGPU_BACKEND" in
    lavapipe)
        for icd in /usr/share/vulkan/icd.d/lvp_icd.json \
                   /usr/share/vulkan/icd.d/lvp_icd.x86_64.json; do
            [ -f "$icd" ] && export VK_ICD_FILENAMES="$icd" && break
        done
        CHROME_GPU_FLAGS+=(--use-vulkan)
        ;;
    swiftshader)
        CHROME_GPU_FLAGS+=(--use-webgpu-adapter=swiftshader --disable-vulkan-surface)
        ;;
    *)
        echo -e "${RED}ERROR: unknown WEBGPU_BACKEND '$WEBGPU_BACKEND'${NC}"; exit 1 ;;
esac

# --- Launch headless Chrome (CDP enabled) -----------------------------------
echo "Launching headless Chrome ($WEBGPU_BACKEND) on debug port $DEBUG_PORT …"
rm -rf "$PROFILE_DIR"; mkdir -p "$PROFILE_DIR"
"$CHROME" \
    --headless=new --no-sandbox --disable-dev-shm-usage --disable-gpu-sandbox \
    --remote-debugging-port="$DEBUG_PORT" --remote-allow-origins='*' \
    --allow-insecure-localhost --disable-web-security \
    "${CHROME_GPU_FLAGS[@]}" \
    --user-data-dir="$PROFILE_DIR" \
    about:blank >"$CHROME_STDERR" 2>&1 &
CHROME_PID=$!
sleep 2

# --- Capture the page console over CDP while navigating to the test URL -----
echo "Capturing console for ${CAPTURE_SECS}s …"
timeout "$((CAPTURE_SECS + 20))" \
    "${CDP_PY[@]}" "$SCRIPT_DIR/capture-chrome-console.py" \
        "$DEBUG_PORT" "$CAPTURE_SECS" "$TEST_URL" \
    >"$CONSOLE_LOG" 2>/tmp/yetty-cdp-stderr.log
CDP_RC=$?
if [ "$CDP_RC" -ne 0 ] && [ "$CDP_RC" -ne 124 ]; then
    echo -e "${YELLOW}WARN: CDP capture exited $CDP_RC; chrome stderr tail:${NC}"
    tail -15 "$CHROME_STDERR"
fi
echo "Captured $(wc -l <"$CONSOLE_LOG") console lines."

# ============================================================================
# Analysis
# ============================================================================
RESULT=0

echo ""
echo "=== Fatal error scan ==="
# Each check is independent so several can report at once.
fatal() { echo -e "${RED}FATAL: $1${NC}"; grep -m3 -B1 -A1 "$2" "$CONSOLE_LOG" | sed 's/^/    /'; RESULT=1; }

grep -q "RuntimeError"                "$CONSOLE_LOG" && fatal "wasm RuntimeError" "RuntimeError"
grep -q "function signature mismatch" "$CONSOLE_LOG" && fatal "wasm function signature mismatch" "function signature mismatch"
grep -qE "Aborted\(|abort\("         "$CONSOLE_LOG" && fatal "wasm abort()" "bort("
grep -q "^\[PAGE-EXCEPTION\]"         "$CONSOLE_LOG" && fatal "page-level uncaught exception" "^\[PAGE-EXCEPTION\]"
grep -qE "\[FATAL\] WebGPU"           "$CONSOLE_LOG" && fatal "yetty WebGPU fail-fast (_Exit)" "\[FATAL\] WebGPU"
grep -q "WebGPU Validation error"     "$CONSOLE_LOG" && fatal "WebGPU validation error" "WebGPU Validation error"
# "Uncaught" excluding the known-benign preinitializedWebGPUDevice warning.
if grep "Uncaught" "$CONSOLE_LOG" | grep -vq "external Instance reference"; then
    fatal "uncaught exception" "Uncaught"
fi
[ "$RESULT" -eq 0 ] && echo -e "${GREEN}none${NC}"

echo ""
echo "=== Boot milestones ==="
# Reliable, refactor-stable markers emitted by index.html's [yetty-status]
# channel plus the VM iframe. Each MUST be present for a pass.
declare -a MILESTONES=(
    "yetty.js + WebGPU up|\[yetty-status\] WebGPU ready"
    "yetty terminal started|\[yetty-status\] starting yetty terminal"
    "yetty terminal running|\[yetty-status\] yetty terminal running"
    "in-browser VM reached Linux|vm-iframe\] | Linux version"
)
for m in "${MILESTONES[@]}"; do
    label="${m%%|*}"; pat="${m#*|}"
    if grep -qE "$pat" "$CONSOLE_LOG"; then
        echo -e "${GREEN}OK:${NC} $label"
    else
        echo -e "${RED}MISSING:${NC} $label"; RESULT=1
    fi
done

echo ""
echo "=== Warnings (non-fatal) ==="
WARNED=0
if grep -q "createBuffer threw" "$CONSOLE_LOG"; then
    n=$(grep -c "createBuffer threw" "$CONSOLE_LOG")
    echo -e "${YELLOW}WARN:${NC} 'createBuffer threw' x$n — software-WebGPU (lavapipe/SwiftShader)"
    echo "      mappedAtCreation quirk on the tiny quad VB; non-fatal, not seen on real GPU."
    WARNED=1
fi
[ "$WARNED" -eq 0 ] && echo "none"

echo ""
if [ "$RESULT" -eq 0 ]; then
    echo -e "${GREEN}=== webasm yetty BOOT TEST PASSED ===${NC}"
else
    echo -e "${RED}=== webasm yetty BOOT TEST FAILED ===${NC}"
    echo "Full console: $CONSOLE_LOG"
fi
exit $RESULT
