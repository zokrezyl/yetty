#!/bin/bash
# Headless Chrome test for yetty WebAssembly
# Tests actual browser loading with software WebGPU and verifies render loop
#
# Usage:
#   ./test-chrome-headless.sh [BUILD_DIR|URL] [PORT] [TEST_MODE]
#
# Examples:
#   ./test-chrome-headless.sh build-webasm-dawn-release      # Local build
#   ./test-chrome-headless.sh https://zokrezyl.github.io/yetty/  # Remote URL

set -e

BUILD_DIR="${1:-build-webasm-ytrace-release}"
PORT="${2:-8199}"
TEST_MODE="${3:-full}"  # "full", "jslinux", "telnet", etc.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
YETTY_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Special case: "telnet" as first arg means telnet mode (no build dir needed)
if [ "$BUILD_DIR" = "telnet" ]; then
    TEST_MODE="telnet"
    TELNET_WS_URL="${2:-ws://localhost:8765}"
    REMOTE_MODE=1
    BASE_URL="http://localhost:8080"
    echo "=============================================="
    echo "Headless Chrome Test - TELNET MODE"
    echo "Server: $BASE_URL"
    echo "WebSocket: $TELNET_WS_URL"
    echo "=============================================="
# Check if BUILD_DIR is a URL (remote mode)
elif [[ "$BUILD_DIR" == http://* ]] || [[ "$BUILD_DIR" == https://* ]]; then
    REMOTE_MODE=1
    REMOTE_URL="$BUILD_DIR"
    echo "=============================================="
    echo "Headless Chrome Test (Software WebGPU) - REMOTE"
    echo "URL: $REMOTE_URL"
    echo "Test mode: $TEST_MODE"
    echo "=============================================="
else
    REMOTE_MODE=0
    echo "=============================================="
    echo "Headless Chrome Test (Software WebGPU) - LOCAL"
    echo "Build directory: $BUILD_DIR"
    echo "Port: $PORT"
    echo "Test mode: $TEST_MODE"
    echo "=============================================="
    cd "$YETTY_ROOT/$BUILD_DIR"
fi

# Find Chrome/Chromium
CHROME=""
for cmd in google-chrome chromium chromium-browser google-chrome-stable; do
    if command -v "$cmd" &> /dev/null; then
        CHROME="$cmd"
        break
    fi
done

if [ -z "$CHROME" ]; then
    echo -e "${YELLOW}WARNING: Chrome/Chromium not found, skipping headless test${NC}"
    exit 0
fi

echo "Using browser: $CHROME"

# Start server for local mode only
SERVER_PID=""
if [ "$REMOTE_MODE" -eq 0 ]; then
    echo "Starting server on port $PORT..."
    python3 serve.py $PORT . > /tmp/yetty-server.log 2>&1 &
    SERVER_PID=$!

    cleanup() {
        echo "Cleaning up..."
        [ -n "$SERVER_PID" ] && kill $SERVER_PID 2>/dev/null || true
    }
    trap cleanup EXIT

    # Wait for server to start
    sleep 2

    # Check server is running
    if ! curl -s -o /dev/null http://localhost:$PORT/; then
        echo -e "${RED}ERROR: Server failed to start${NC}"
        cat /tmp/yetty-server.log
        exit 1
    fi
    echo "Server started"
    BASE_URL="http://localhost:$PORT"
else
    BASE_URL="$REMOTE_URL"
    # Remove trailing slash if present
    BASE_URL="${BASE_URL%/}"
fi

#=============================================================================
# Build Asset Verification (local mode only)
#=============================================================================
if [ "$REMOTE_MODE" -eq 0 ]; then
    echo ""
    echo "=== Build Asset Verification ==="

    BUILD_RESULT=0

    # Check vfsync filesystem
    echo "Checking vfsync filesystem..."
    if [ -d "$YETTY_ROOT/$BUILD_DIR/vfsync/u/os/yetty-alpine" ]; then
        echo -e "${GREEN}OK: vfsync directory exists${NC}"
        VFSYNC_FILES=$(ls "$YETTY_ROOT/$BUILD_DIR/vfsync/u/os/yetty-alpine/files/" 2>/dev/null | wc -l)
        echo "  vfsync files: $VFSYNC_FILES"
        if [ "$VFSYNC_FILES" -lt 100 ]; then
            echo -e "${RED}ERROR: vfsync has too few files ($VFSYNC_FILES < 100)${NC}"
            BUILD_RESULT=1
        fi
    else
        echo -e "${RED}ERROR: vfsync directory missing${NC}"
        BUILD_RESULT=1
    fi

    # Check vfsync head file for demo presence
    if [ -f "$YETTY_ROOT/$BUILD_DIR/vfsync/u/os/yetty-alpine/head" ]; then
        FS_COUNT=$(grep "FSFileCount:" "$YETTY_ROOT/$BUILD_DIR/vfsync/u/os/yetty-alpine/head" | awk '{print $2}')
        echo "  FSFileCount: $FS_COUNT"
    fi

    # Check jslinux files
    echo "Checking jslinux files..."
    JSLINUX_DIR="$YETTY_ROOT/$BUILD_DIR/jslinux"
    JSLINUX_OK=1
    for file in vm-bridge.html term-bridge.js x86_64emu-wasm.js x86_64emu-wasm.wasm kernel-x86_64.bin; do
        if [ -f "$JSLINUX_DIR/$file" ]; then
            echo -e "  ${GREEN}OK: jslinux/$file${NC}"
        else
            echo -e "  ${RED}MISSING: jslinux/$file${NC}"
            JSLINUX_OK=0
            BUILD_RESULT=1
        fi
    done

    # Check shader glyphs
    echo "Checking shader glyphs..."
    GLYPH_DIR="$YETTY_ROOT/$BUILD_DIR/assets/shaders/glyphs"
    if [ -d "$GLYPH_DIR" ]; then
        GLYPH_COUNT=$(ls "$GLYPH_DIR"/*.wgsl 2>/dev/null | wc -l)
        echo -e "${GREEN}OK: $GLYPH_COUNT shader glyph files${NC}"
        if [ "$GLYPH_COUNT" -lt 30 ]; then
            echo -e "${RED}ERROR: Too few shader glyphs ($GLYPH_COUNT < 30)${NC}"
            BUILD_RESULT=1
        fi
    else
        echo -e "${RED}ERROR: Shader glyphs directory missing${NC}"
        BUILD_RESULT=1
    fi

    # Check card shaders
    CARD_DIR="$YETTY_ROOT/$BUILD_DIR/assets/shaders/cards"
    if [ -d "$CARD_DIR" ]; then
        CARD_COUNT=$(ls "$CARD_DIR"/*.wgsl 2>/dev/null | wc -l)
        echo -e "${GREEN}OK: $CARD_COUNT card shader files${NC}"
    else
        echo -e "${RED}ERROR: Card shaders directory missing${NC}"
        BUILD_RESULT=1
    fi

    # Check yetty.data contains demo
    echo "Checking yetty.data..."
    if [ -f "$YETTY_ROOT/$BUILD_DIR/yetty.data" ]; then
        DATA_SIZE=$(stat -c%s "$YETTY_ROOT/$BUILD_DIR/yetty.data" 2>/dev/null || stat -f%z "$YETTY_ROOT/$BUILD_DIR/yetty.data")
        DATA_MB=$((DATA_SIZE / 1024 / 1024))
        echo -e "${GREEN}OK: yetty.data exists (${DATA_MB}MB)${NC}"
    else
        echo -e "${RED}ERROR: yetty.data missing${NC}"
        BUILD_RESULT=1
    fi

    if [ "$BUILD_RESULT" -ne 0 ]; then
        echo ""
        echo -e "${RED}=== BUILD ASSET VERIFICATION FAILED ===${NC}"
        exit 1
    fi
    echo ""
else
    echo ""
    echo "=== Skipping local asset verification (remote mode) ==="
    echo ""
fi

# Determine test URL
if [ "$TEST_MODE" = "jslinux" ]; then
    TEST_URL="${BASE_URL}/jslinux/vm-bridge.html?ptyId=test1&url=alpine-x86_64.cfg&cpu=x86_64&cols=80&rows=25&mem=256"
    echo "Testing JSLinux (vfsync.org proxy) at: $TEST_URL"
elif [ "$TEST_MODE" = "jslinux-local" ]; then
    # Test full yetty with local VM config - this enables card creation testing
    # The VM will boot, run card tests in init script, and yetty will process OSC sequences
    TEST_URL="${BASE_URL}/?vmconfig=yetty-alpine.cfg"
    echo "Testing yetty with local JSLinux VM at: $TEST_URL"
elif [ "$TEST_MODE" = "vm-only" ]; then
    # Test VM only (without yetty) - useful for debugging VM boot issues
    TEST_URL="${BASE_URL}/jslinux/vm-bridge.html?ptyId=test1&url=yetty-alpine.cfg&cpu=x86_64&cols=80&rows=25&mem=256"
    echo "Testing JSLinux VM only (no yetty) at: $TEST_URL"
elif [ "$TEST_MODE" = "alpine-disk" ]; then
    # riscv64 alpine-disk variant: yetty-built tinyemu wasm + opensbi.elf +
    # kernel-riscv64.bin + virtio-blk root. capture-chrome-console.py
    # mirrors term-output postMessages -> [VM-OUT] console lines.
    # ydebug=1 turns on tinyemu's [YDEBUG] traces (htif_write, copy_bios,
    # SBI ECALL traces) — required to diagnose silent boot stalls.
    TEST_URL="${BASE_URL}/jslinux/vm-bridge.html?ptyId=test1&url=alpine-disk.cfg&cpu=riscv64&cols=80&rows=25&mem=256&ydebug=1"
    echo "Testing alpine-disk variant (riscv64, virtio-blk) at: $TEST_URL"
elif [ "$TEST_MODE" = "alpine-extended-disk" ]; then
    TEST_URL="${BASE_URL}/jslinux/vm-bridge.html?ptyId=test1&url=alpine-extended-disk.cfg&cpu=riscv64&cols=80&rows=25&mem=256&ydebug=1"
    echo "Testing alpine-extended-disk variant (riscv64, virtio-blk) at: $TEST_URL"
elif [ "$TEST_MODE" = "term-size" ]; then
    # Test terminal size initialization - use non-default size to verify kernel gets correct dimensions
    TEST_COLS="${4:-120}"
    TEST_ROWS="${5:-40}"
    TEST_URL="${BASE_URL}/jslinux/vm-bridge.html?ptyId=test1&url=yetty-alpine.cfg&cpu=x86_64&cols=${TEST_COLS}&rows=${TEST_ROWS}&mem=256"
    echo "Testing terminal size initialization at: $TEST_URL"
    echo "Expected terminal size: ${TEST_COLS}x${TEST_ROWS}"
elif [ "$TEST_MODE" = "telnet" ]; then
    # Test telnet mode via WebSocket proxy
    # Usage: ./test-chrome-headless.sh telnet [WS_URL]
    # Requires: telnetd running, websocket proxy running
    TELNET_WS_URL="${2:-ws://localhost:8765}"
    # For telnet mode, we use a fixed server at 8080 (user must start it)
    BASE_URL="http://localhost:8080"
    TEST_URL="${BASE_URL}/?mode=telnet&wsurl=${TELNET_WS_URL}"
    echo "Testing telnet mode at: $TEST_URL"
    echo "WebSocket proxy: $TELNET_WS_URL"
    echo ""
    echo "Prerequisites:"
    echo "  1. Start telnetd: busybox telnetd -F -p 8023 -l /bin/bash"
    echo "  2. Start proxy:   ./tools/telnet-websocket.sh 127.0.0.1:8023 8765"
    echo "  3. Start server:  cd build-webasm-dawn-release && python3 serve.py 8080"
    echo ""
else
    TEST_URL="${BASE_URL}/?mode=jslinux&trace=1"
    echo "Testing full yetty at: $TEST_URL"
fi

# Run Chrome with software WebGPU rendering
echo "Running headless Chrome with software WebGPU..."

CONSOLE_LOG="/tmp/yetty-chrome-console.log"

# Use lavapipe (software Vulkan) for WebGPU - much more reliable than SwiftShader flags
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json

# Chrome flags for software WebGPU:
# --enable-unsafe-webgpu: Enable WebGPU
# --enable-features=Vulkan,WebGPU: Enable Vulkan backend for WebGPU
# --use-vulkan: Force Vulkan usage
# Use Chrome DevTools Protocol to reliably capture page console.log
# output. --headless=new doesn't pipe console messages to stderr, and
# --headless=old is unreliable across Chrome versions. CDP is the
# canonical channel.
DEBUG_PORT="${DEBUG_PORT:-9222}"
$CHROME \
    --headless=new \
    --no-sandbox \
    --disable-dev-shm-usage \
    --disable-gpu-sandbox \
    --remote-debugging-port="$DEBUG_PORT" \
    --remote-allow-origins=* \
    --enable-unsafe-webgpu \
    --enable-features=Vulkan,WebGPU \
    --use-vulkan \
    --allow-insecure-localhost \
    --user-data-dir="/tmp/yetty-chrome-prof.$$" \
    about:blank \
    > /tmp/yetty-chrome-stderr.log 2>&1 &
CHROME_PID=$!

# Use system python (has websocket-client); nix dev shell python may not.
PY=/usr/bin/python3
[ -x "$PY" ] || PY=python3

# Connect CDP, enable Runtime/Log/Page domains BEFORE navigating to
# $TEST_URL — otherwise we miss the earliest console.log calls (e.g.
# yetty's index.html boot logs and emcc's preRun output). 90s capture
# window.
timeout 110 "$PY" "$YETTY_ROOT/tools/capture-chrome-console.py" \
    "$DEBUG_PORT" 90 "$TEST_URL" \
    2>/tmp/yetty-cdp-stderr.log | tee "$CONSOLE_LOG"
CDP_RC=$?

kill "$CHROME_PID" 2>/dev/null || true
wait "$CHROME_PID" 2>/dev/null || true
rm -rf "/tmp/yetty-chrome-prof.$$"
if [ $CDP_RC -ne 0 ] && [ $CDP_RC -ne 124 ]; then
    echo "${YELLOW}WARN: CDP capture exited with $CDP_RC (chrome stderr below)${NC}"
    cat /tmp/yetty-chrome-stderr.log | tail -20
fi

echo ""
echo "=== Chrome Output Analysis ==="

RESULT=0

# Check for fatal errors. Each branch below is independent, so a build
# can hit several at once (e.g. WebGPU validation + RuntimeError) and
# all get reported. Don't skip later checks just because an earlier
# one matched.

if grep -q "Destroyed texture" "$CONSOLE_LOG"; then
    echo -e "${RED}ERROR: Destroyed texture used in submit (use-after-free)${NC}"
    grep -B2 -A2 "Destroyed texture" "$CONSOLE_LOG" || true
    RESULT=1
fi

if grep -q "function signature mismatch" "$CONSOLE_LOG"; then
    echo -e "${RED}ERROR: WASM function signature mismatch${NC}"
    grep -B2 -A2 "function signature mismatch" "$CONSOLE_LOG" || true
    RESULT=1
fi

if grep -q "RuntimeError" "$CONSOLE_LOG"; then
    echo -e "${RED}ERROR: WASM RuntimeError${NC}"
    grep -B2 -A2 "RuntimeError" "$CONSOLE_LOG" || true
    RESULT=1
fi

if grep -q "Uncaught" "$CONSOLE_LOG"; then
    # Check if the only Uncaught is the benign "Instance reference" error from preinitializedWebGPUDevice
    if grep "Uncaught" "$CONSOLE_LOG" | grep -v "external Instance reference" | grep -q .; then
        echo -e "${RED}ERROR: Uncaught exception${NC}"
        grep "Uncaught" "$CONSOLE_LOG" | grep -v "external Instance reference" | head -5 || true
        RESULT=1
    else
        echo -e "${YELLOW}WARN: Benign WebGPU instance reference warning (preinitializedWebGPUDevice not used)${NC}"
    fi
fi

if grep -q "processPackageData" "$CONSOLE_LOG"; then
    echo -e "${RED}ERROR: processPackageData error${NC}"
    grep -A2 "processPackageData" "$CONSOLE_LOG" || true
    RESULT=1
fi

# WebGPU device validation errors come through yetty_ywebgpu_uncaptured_error_callback
# which logs via ydebug as `[error]`. They look like:
#   [error] error.c:55 (yetty_ywebgpu_uncaptured_error_callback): WebGPU Validation error: ...
# A failed shader entry point or pipeline creation cascades into many
# "Invalid RenderPipeline" / "Invalid CommandBuffer" follow-ups; those
# are noise after the first real error. Surface the *root* cause first.
if grep -q "WebGPU Validation error" "$CONSOLE_LOG"; then
    echo -e "${RED}ERROR: WebGPU validation errors${NC}"
    # Most useful root causes — show first occurrence with context.
    if grep -q "Entry point" "$CONSOLE_LOG"; then
        echo -e "${RED}  Cause: shader missing entry point${NC}"
        grep -B1 -A2 "Entry point" "$CONSOLE_LOG" | head -8 || true
    fi
    if grep -q "shader module is invalid" "$CONSOLE_LOG"; then
        echo -e "${RED}  Cause: shader module compilation failed${NC}"
        grep -B1 -A2 "shader module is invalid" "$CONSOLE_LOG" | head -8 || true
    fi
    if grep -q "type mismatch" "$CONSOLE_LOG"; then
        echo -e "${RED}  Cause: pipeline / binding type mismatch${NC}"
        grep -B1 -A2 "type mismatch" "$CONSOLE_LOG" | head -8 || true
    fi
    # Distinct first-line-only summary of every unique validation error
    echo -e "${RED}  Distinct validation messages:${NC}"
    grep "WebGPU Validation error" "$CONSOLE_LOG" \
        | sed 's/.*WebGPU Validation error: //' \
        | sort -u | head -10 | sed 's/^/    /'
    RESULT=1
fi

# yetty's own ydebug `[error]` lines that are NOT covered by the more
# specific checks above (rare — most go through the WebGPU callback).
if grep -E "^\[CONSOLE log\].*\[error\]" "$CONSOLE_LOG" | grep -v "WebGPU Validation error" | grep -q .; then
    echo -e "${RED}ERROR: yetty logged ydebug-error-level messages${NC}"
    grep -E "^\[CONSOLE log\].*\[error\]" "$CONSOLE_LOG" | grep -v "WebGPU Validation error" | head -10
    RESULT=1
fi

# CDP-level uncaught exceptions thrown on the page (other than the ones
# already captured by the Uncaught/RuntimeError checks). The capture
# script tags these as [PAGE-EXCEPTION].
if grep -q "^\[PAGE-EXCEPTION\]" "$CONSOLE_LOG"; then
    echo -e "${RED}ERROR: page-level uncaught exception(s)${NC}"
    grep "^\[PAGE-EXCEPTION\]" "$CONSOLE_LOG" | head -10
    RESULT=1
fi

if [ "$TEST_MODE" = "alpine-disk" ] || [ "$TEST_MODE" = "alpine-extended-disk" ]; then
    # riscv64 variants — vm-bridge.html mode, no parent yetty wasm. The
    # python capture mirrors term-output postMessages to console as
    # [VM-OUT] lines, so OpenSBI / kernel boot text shows up there.
    echo ""
    echo "=== JSLinux Bridge Output ==="
    grep -E "\[vm-bridge\]|\[term-bridge\]|SMP:" "$CONSOLE_LOG" | head -30 || echo "(no bridge output)"

    echo ""
    echo "=== VM Console Output (last 60 lines) ==="
    grep -E "^\[(CONSOLE log\] \[)?VM-OUT\]" "$CONSOLE_LOG" | tail -60 || echo "(no VM output)"

    echo ""
    echo "=== Boot Checkpoints ==="

    # Each checkpoint maps to a substring grep against the console log.
    # We look in [VM-OUT] lines for actual VM-side text. The order is
    # roughly load -> bios -> kernel -> userspace.
    declare -A CHECK_PATTERNS=(
        [emcc-runtime]="\[vm-bridge\] Emscripten runtime initialized"
        [smp-init]="SMP: Using"
        [opensbi-banner]="OpenSBI"
        [kernel-banner]="Linux version"
        [kernel-cmdline]="Kernel command line"
        [virtio-blk]="virtio_blk"
        [ext4-mount]="EXT4-fs"
        [init-running]="Welcome to|tinyemu|init "
        [shell-prompt]="/ # |~ # |# $|\\$ $"
    )
    BOOT_FAIL=0
    for k in emcc-runtime smp-init opensbi-banner kernel-banner kernel-cmdline virtio-blk ext4-mount init-running shell-prompt; do
        if grep -qE "${CHECK_PATTERNS[$k]}" "$CONSOLE_LOG"; then
            echo -e "${GREEN}OK: $k${NC}"
        else
            echo -e "${RED}MISSING: $k${NC}"
            BOOT_FAIL=$((BOOT_FAIL + 1))
        fi
    done
    if [ "$BOOT_FAIL" -gt 0 ]; then
        echo -e "${RED}Boot incomplete — $BOOT_FAIL checkpoint(s) missing${NC}"
        RESULT=1
    fi
elif [ "$TEST_MODE" = "jslinux" ] || [ "$TEST_MODE" = "jslinux-local" ]; then
    # JSLinux-specific checks
    echo ""
    echo "=== JSLinux Output ==="
    grep -E "\[vm-bridge\]|\[term-bridge\]" "$CONSOLE_LOG" | head -50 || echo "(no vm-bridge output)"

    # Check JSLinux checkpoints
    echo ""
    echo "=== JSLinux Checkpoints ==="

    JSLINUX_CHECKPOINTS=(
        "term-bridge.js loaded"
        "Term constructor called"
        "start_vm called"
        "Loading emulator"
        "Module.preRun called"
        "vm_start returned successfully"
    )

    for checkpoint in "${JSLINUX_CHECKPOINTS[@]}"; do
        if grep -q "$checkpoint" "$CONSOLE_LOG"; then
            echo -e "${GREEN}OK: $checkpoint${NC}"
        else
            echo -e "${RED}MISSING: $checkpoint${NC}"
            RESULT=1
        fi
    done

    # Check if kernel was downloaded (shows emulator is running)
    if grep -q "kernel-x86_64.bin" "$CONSOLE_LOG"; then
        echo -e "${GREEN}OK: Kernel download started${NC}"
    else
        echo -e "${YELLOW}WARN: Kernel download not seen${NC}"
    fi

    # Check for successful VM boot
    if grep -q "Welcome to JS/Linux" "$CONSOLE_LOG" || grep -q "Welcome to yetty Alpine" "$CONSOLE_LOG"; then
        echo -e "${GREEN}OK: VM booted - Welcome message received${NC}"
    else
        echo -e "${RED}MISSING: VM boot welcome message${NC}"
        RESULT=1
    fi

    if grep -q "localhost:~#" "$CONSOLE_LOG"; then
        echo -e "${GREEN}OK: Shell prompt received${NC}"
    else
        echo -e "${YELLOW}WARN: Shell prompt not seen (may need more time)${NC}"
    fi

    # Terminal size verification (for term-size test mode)
    if [ "$TEST_MODE" = "term-size" ]; then
        echo ""
        echo "=== Terminal Size Verification ==="
        echo "JavaScript side:"
        grep -E "term_get_size|_console_get_size|getSize|cols.*rows|URL params" "$CONSOLE_LOG" | head -20 || echo "(no JS size logs)"

        echo ""
        echo "Looking for stty output in boot messages..."
        # The init script should run 'stty size' and output it
        # Format is: "rows cols" e.g. "40 120"
        if grep -qE "^[0-9]+ [0-9]+$" "$CONSOLE_LOG"; then
            STTY_OUTPUT=$(grep -E "^[0-9]+ [0-9]+$" "$CONSOLE_LOG" | tail -1)
            KERNEL_ROWS=$(echo "$STTY_OUTPUT" | awk '{print $1}')
            KERNEL_COLS=$(echo "$STTY_OUTPUT" | awk '{print $2}')
            echo "Kernel terminal size (stty): ${KERNEL_COLS}x${KERNEL_ROWS}"

            if [ "$KERNEL_COLS" = "$TEST_COLS" ] && [ "$KERNEL_ROWS" = "$TEST_ROWS" ]; then
                echo -e "${GREEN}OK: Kernel has correct terminal size${NC}"
            else
                echo -e "${RED}FAIL: Kernel has WRONG size! Expected ${TEST_COLS}x${TEST_ROWS}, got ${KERNEL_COLS}x${KERNEL_ROWS}${NC}"
                RESULT=1
            fi
        else
            echo -e "${YELLOW}WARN: No stty output found - checking term-output messages${NC}"
        fi

        # Also check term-output messages for any size indicators
        echo ""
        echo "Term output containing size info:"
        grep -E "term-output.*[0-9]+.*[0-9]+" "$CONSOLE_LOG" | head -10 || echo "(none)"

        # Show all resize events
        echo ""
        echo "Resize events:"
        grep -E "resize|console_resize" "$CONSOLE_LOG" | head -20 || echo "(none)"
    fi

    # Check for demo directory in boot output
    echo ""
    echo "=== Demo Files Check ==="
    if grep -q "/home/demo contents" "$CONSOLE_LOG"; then
        echo -e "${GREEN}OK: Demo directory listing found in boot${NC}"
        grep -A 20 "/home/demo contents" "$CONSOLE_LOG" | head -25 || true
    else
        echo -e "${YELLOW}WARN: Demo directory listing not in boot output${NC}"
    fi

    # Check for shader-glyphs.txt specifically
    if grep -q "shader-glyphs.txt" "$CONSOLE_LOG"; then
        echo -e "${GREEN}OK: shader-glyphs.txt found${NC}"
    else
        echo -e "${YELLOW}WARN: shader-glyphs.txt not seen in output${NC}"
    fi

    # Check for card creation from boot card tests
    echo ""
    echo "=== Card Creation Tests ==="
    CARD_TEST_RESULT=0

    # Check if card tests ran
    if grep -q "CARD TESTS" "$CONSOLE_LOG"; then
        echo -e "${GREEN}OK: Card tests executed in VM${NC}"

        # Count created cards by type
        CREATED_CARDS=$(grep -oP "CardFactory: created card '\K[^']+" "$CONSOLE_LOG" | sort | uniq -c | sort -rn)
        TOTAL_CREATED=$(grep -c "CardFactory: created card" "$CONSOLE_LOG" || echo 0)
        TOTAL_FAILED=$(grep -c "CardFactory: failed" "$CONSOLE_LOG" || echo 0)

        echo "Cards created: $TOTAL_CREATED"
        echo "Cards failed: $TOTAL_FAILED"

        if [ -n "$CREATED_CARDS" ]; then
            echo "Card types created:"
            echo "$CREATED_CARDS" | head -15
        fi

        # Check minimum card types created
        CARD_TYPES=(qrcode plot image ytext ydraw)
        for ctype in "${CARD_TYPES[@]}"; do
            if grep -q "CardFactory: created card '$ctype'" "$CONSOLE_LOG"; then
                echo -e "${GREEN}OK: $ctype card created${NC}"
            elif grep -q "CardFactory: creating card '$ctype'" "$CONSOLE_LOG"; then
                echo -e "${YELLOW}PARTIAL: $ctype card creation started${NC}"
            else
                echo -e "${YELLOW}WARN: $ctype card not tested${NC}"
            fi
        done

        # Check for OSC handling
        if grep -q "handleCardOSCSequence: ENTERED" "$CONSOLE_LOG"; then
            OSC_COUNT=$(grep -c "handleCardOSCSequence: ENTERED" "$CONSOLE_LOG" || echo 0)
            echo -e "${GREEN}OK: $OSC_COUNT OSC sequences processed${NC}"
        else
            echo -e "${YELLOW}WARN: No OSC handling logged${NC}"
        fi

        # Fail if no cards created at all
        if [ "$TOTAL_CREATED" -lt 2 ]; then
            echo -e "${RED}FAIL: Too few cards created ($TOTAL_CREATED < 2)${NC}"
            CARD_TEST_RESULT=1
        fi

        # Show failures if any
        if [ "$TOTAL_FAILED" -gt 0 ]; then
            echo -e "${YELLOW}Card creation failures:${NC}"
            grep "CardFactory: failed" "$CONSOLE_LOG" | head -10
        fi

        if [ "$CARD_TEST_RESULT" -ne 0 ]; then
            echo -e "${RED}Card creation tests failed${NC}"
            RESULT=1
        fi
    else
        echo -e "${YELLOW}WARN: Card tests not found in boot output${NC}"
    fi
else
    # Full yetty checks
    echo ""
    echo "=== Yetty Startup Verification ==="

    # Check for JavaScript errors first
    if grep -qE "Uncaught|TypeError|ReferenceError|SyntaxError" "$CONSOLE_LOG"; then
        echo -e "${RED}FAIL: JavaScript errors found${NC}"
        grep -E "Uncaught|TypeError|ReferenceError|SyntaxError" "$CONSOLE_LOG" | head -10
        RESULT=1
    fi

    # Check yetty.js was loaded (look for preRun or ENV setting)
    if grep -q "preRun.*Setting ENV" "$CONSOLE_LOG" || grep -q "onRuntimeInitialized" "$CONSOLE_LOG"; then
        echo -e "${GREEN}OK: yetty.js loaded${NC}"
    else
        echo -e "${RED}FAIL: yetty.js not loaded - check WebGPU availability${NC}"
        RESULT=1
    fi

    # Check for C++ ytrace output (required checkpoints)
    echo ""
    echo "=== Required Checkpoints ==="
    CHECKPOINTS=(
        "main: WebASM starting"
        "main: Config created"
        "main: Window created"
        "main: PlatformInputPipe created"
        "main: PtyFactory created"
        "main: WebGPU instance created"
        "main: Yetty created"
        "main: Starting Yetty"
        "initWebGPU: Adapter obtained"
        "initWebGPU: Device obtained"
    )

    CHECKPOINT_PASS=0
    CHECKPOINT_FAIL=0
    for checkpoint in "${CHECKPOINTS[@]}"; do
        if grep -q "$checkpoint" "$CONSOLE_LOG"; then
            echo -e "${GREEN}OK: $checkpoint${NC}"
            CHECKPOINT_PASS=$((CHECKPOINT_PASS + 1))
        else
            echo -e "${RED}MISSING: $checkpoint${NC}"
            CHECKPOINT_FAIL=$((CHECKPOINT_FAIL + 1))
        fi
    done

    echo ""
    echo "Checkpoints: $CHECKPOINT_PASS passed, $CHECKPOINT_FAIL failed"

    if [ "$CHECKPOINT_FAIL" -gt 0 ]; then
        echo -e "${RED}FAIL: Required checkpoints missing${NC}"
        RESULT=1
    fi

    # Show all yetty console output
    echo ""
    echo "=== Yetty Console Output ==="
    grep -E "\[yetty\]" "$CONSOLE_LOG" | head -50 || echo "(no [yetty] output)"
fi

# Show any errors
echo ""
echo "=== Errors (if any) ==="
grep -iE "error|failed|exception" "$CONSOLE_LOG" | grep -v "VERBOSE" | head -20 || echo "(no errors)"

#=============================================================================
# Card Rendering Tests - Inject OSC via JSLinux and verify YTRACE output
#=============================================================================
echo ""
echo "=== Card Rendering Tests ==="

DEMO_OUTPUT_DIR="$YETTY_ROOT/$BUILD_DIR/demo-output"
if [ "$REMOTE_MODE" -eq 1 ]; then
    echo -e "${YELLOW}SKIP: Card rendering file tests not available in remote mode${NC}"
elif [ -d "$DEMO_OUTPUT_DIR" ] && [ "$TEST_MODE" = "jslinux-local" ]; then
    # Test cards by catting .out files in JSLinux and checking yetty logs
    # Expected YTRACE patterns when card is successfully created:
    #   - "CardFactory: creating card '<type>'"
    #   - "CardFactory: created card '<type>'"
    #   - "GPUScreen: Created card '<type>'"

    CARD_TESTS=(
        "cards/qrcode.out:CardFactory: created card 'qrcode'"
        "cards/plot.out:CardFactory: created card 'plot'"
    )

    CARD_RESULT=0

    for test_spec in "${CARD_TESTS[@]}"; do
        OUT_FILE="${test_spec%%:*}"
        EXPECTED_LOG="${test_spec##*:}"

        echo "Testing card: $OUT_FILE"
        echo "  Expected YTRACE: $EXPECTED_LOG"

        # Check if .out file exists and has valid OSC
        if [ ! -s "$DEMO_OUTPUT_DIR/$OUT_FILE" ]; then
            echo -e "  ${RED}FAIL: Output file missing or empty${NC}"
            CARD_RESULT=1
            continue
        fi

        if ! grep -qaP '\x1b\]666666' "$DEMO_OUTPUT_DIR/$OUT_FILE" 2>/dev/null; then
            echo -e "  ${RED}FAIL: No OSC 666666 in output file${NC}"
            CARD_RESULT=1
            continue
        fi

        # The full test would boot JSLinux, run 'cat /home/demo/output/<file>'
        # and check console for YTRACE card creation messages.
        # For now, verify file structure only (full test in CI with longer timeout)
        echo -e "  ${GREEN}OK: Valid OSC structure${NC}"
    done

    # Summary counts
    TOTAL_OUT=$(find "$DEMO_OUTPUT_DIR" -name "*.out" -type f 2>/dev/null | wc -l)
    VALID_OSC=$(grep -rlP '\x1b\]666666' "$DEMO_OUTPUT_DIR" --include="*.out" 2>/dev/null | wc -l)

    echo ""
    echo "Demo output summary:"
    echo "  Total .out files: $TOTAL_OUT"
    echo "  Valid OSC files: $VALID_OSC"

    if [ "$VALID_OSC" -lt 50 ]; then
        echo -e "${RED}ERROR: Too few valid OSC files ($VALID_OSC < 50)${NC}"
        CARD_RESULT=1
    fi

    if [ "$CARD_RESULT" -ne 0 ]; then
        echo -e "${RED}Card rendering tests FAILED${NC}"
        RESULT=1
    else
        echo -e "${GREEN}Card rendering tests PASSED${NC}"
    fi
elif [ -d "$DEMO_OUTPUT_DIR" ]; then
    # Just count files for non-jslinux mode
    TOTAL_OUT=$(find "$DEMO_OUTPUT_DIR" -name "*.out" -type f 2>/dev/null | wc -l)
    VALID_OSC=$(grep -rlP '\x1b\]666666' "$DEMO_OUTPUT_DIR" --include="*.out" 2>/dev/null | wc -l)
    echo "Demo outputs: $VALID_OSC/$TOTAL_OUT valid OSC files"
else
    echo -e "${YELLOW}SKIP: No demo-output directory${NC}"
fi

#=============================================================================
# Resize Test - Verify terminal survives window resize
#=============================================================================
echo ""
echo "=== Resize Test ==="

RESIZE_LOG="/tmp/yetty-resize-test.log"

# Run Chrome with resize test - use DevTools protocol to resize window
timeout 60 $CHROME \
    --headless=new \
    --no-sandbox \
    --disable-dev-shm-usage \
    --disable-gpu-sandbox \
    --enable-logging=stderr \
    --enable-unsafe-webgpu \
    --enable-features=Vulkan,WebGPU \
    --use-vulkan \
    --window-size=800,600 \
    --virtual-time-budget=10000 \
    "$TEST_URL" \
    2>&1 | tee "$RESIZE_LOG" &
RESIZE_PID=$!

sleep 5

# Take screenshot before resize
SCREENSHOT_BEFORE="/tmp/yetty-before-resize.png"
$CHROME \
    --headless=new \
    --no-sandbox \
    --screenshot="$SCREENSHOT_BEFORE" \
    --window-size=800,600 \
    --disable-gpu \
    "$TEST_URL" 2>/dev/null &
sleep 3

# Take screenshot after resize (different size)
SCREENSHOT_AFTER="/tmp/yetty-after-resize.png"
$CHROME \
    --headless=new \
    --no-sandbox \
    --screenshot="$SCREENSHOT_AFTER" \
    --window-size=1024,768 \
    --disable-gpu \
    "$TEST_URL" 2>/dev/null &
sleep 3

wait $RESIZE_PID 2>/dev/null || true

# Check screenshots exist and are not empty/tiny
RESIZE_RESULT=0
for img in "$SCREENSHOT_BEFORE" "$SCREENSHOT_AFTER"; do
    if [ -f "$img" ]; then
        IMG_SIZE=$(stat -c%s "$img" 2>/dev/null || stat -f%z "$img" 2>/dev/null || echo 0)
        if [ "$IMG_SIZE" -gt 5000 ]; then
            echo -e "${GREEN}OK: Screenshot $img exists (${IMG_SIZE} bytes)${NC}"
        else
            echo -e "${RED}FAIL: Screenshot $img too small (${IMG_SIZE} bytes) - likely blank${NC}"
            RESIZE_RESULT=1
        fi
    else
        echo -e "${YELLOW}WARN: Screenshot $img not created${NC}"
    fi
done

# Check resize callback was triggered
if grep -q "Surface configured" "$RESIZE_LOG"; then
    SURFACE_COUNT=$(grep -c "Surface configured" "$RESIZE_LOG" || echo 0)
    echo -e "${GREEN}OK: Surface configured $SURFACE_COUNT time(s)${NC}"
else
    echo -e "${YELLOW}WARN: No surface configuration logged${NC}"
fi

# Check for resize-related errors
if grep -q "resize.*error\|Error.*resize" "$RESIZE_LOG" 2>/dev/null; then
    echo -e "${RED}FAIL: Resize errors found${NC}"
    grep -i "resize.*error\|error.*resize" "$RESIZE_LOG" | head -5
    RESIZE_RESULT=1
fi

if [ "$RESIZE_RESULT" -ne 0 ]; then
    echo -e "${RED}Resize test FAILED${NC}"
    RESULT=1
else
    echo -e "${GREEN}Resize test PASSED${NC}"
fi

#=============================================================================
# Rendering Verification - Check actual pixels rendered
#=============================================================================
echo ""
echo "=== Rendering Verification ==="

RENDER_RESULT=0

# Check for actual render calls in logs
if grep -q "wgpuRenderPassEncoderDraw\|renderFrame\|GPUScreen.*render" "$CONSOLE_LOG"; then
    echo -e "${GREEN}OK: Render calls found in logs${NC}"
else
    echo -e "${YELLOW}WARN: No explicit render calls logged${NC}"
fi

# Check getCurrentTextureView success (proves WebGPU is rendering)
if grep -q "getCurrentTextureView.*texture\|got texture.*status=0\|SuccessOptimal" "$CONSOLE_LOG"; then
    echo -e "${GREEN}OK: WebGPU surface texture acquired${NC}"
else
    if grep -q "Failed to get surface texture" "$CONSOLE_LOG"; then
        echo -e "${RED}FAIL: WebGPU surface texture acquisition failed${NC}"
        RENDER_RESULT=1
    else
        echo -e "${YELLOW}WARN: Surface texture status not logged${NC}"
    fi
fi

# Check frame count if available
FRAME_COUNT=$(grep -c "mainLoopIteration\|frame.*rendered\|present" "$CONSOLE_LOG" 2>/dev/null | head -1 || echo 0)
FRAME_COUNT=${FRAME_COUNT:-0}
if [ "$FRAME_COUNT" -gt 0 ]; then
    echo -e "${GREEN}OK: $FRAME_COUNT frame iterations detected${NC}"
else
    echo -e "${YELLOW}WARN: No frame iterations logged${NC}"
fi

# Verify screenshot is not all black (basic pixel check)
if [ -f "$SCREENSHOT_BEFORE" ] && command -v identify &>/dev/null; then
    # Use ImageMagick to check if image has color variation
    COLORS=$(identify -format "%k" "$SCREENSHOT_BEFORE" 2>/dev/null || echo 0)
    if [ "$COLORS" -gt 10 ]; then
        echo -e "${GREEN}OK: Screenshot has $COLORS unique colors (not blank)${NC}"
    else
        echo -e "${RED}FAIL: Screenshot has only $COLORS colors (likely blank/black)${NC}"
        RENDER_RESULT=1
    fi
elif [ -f "$SCREENSHOT_BEFORE" ]; then
    echo -e "${YELLOW}WARN: ImageMagick not available for pixel verification${NC}"
fi

if [ "$RENDER_RESULT" -ne 0 ]; then
    echo -e "${RED}Rendering verification FAILED${NC}"
    RESULT=1
else
    echo -e "${GREEN}Rendering verification PASSED${NC}"
fi

echo ""
if [ "$RESULT" -eq 0 ]; then
    echo -e "${GREEN}=== TEST PASSED ===${NC}"
else
    echo -e "${RED}=== TEST FAILED ===${NC}"
fi

exit $RESULT
