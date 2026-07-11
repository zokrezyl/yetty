#!/bin/bash
# Comprehensive WebAssembly build verification
# Run after build to catch issues BEFORE browser testing

set -e

BUILD_DIR="${1:-build-webasm-dawn-release}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
YETTY_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

ERRORS=0
WARNINGS=0

error() {
    echo -e "${RED}ERROR: $1${NC}"
    ((ERRORS++))
}

warn() {
    echo -e "${YELLOW}WARNING: $1${NC}"
    ((WARNINGS++))
}

ok() {
    echo -e "${GREEN}OK: $1${NC}"
}

info() {
    echo -e "INFO: $1"
}

echo "=============================================="
echo "WebAssembly Build Verification"
echo "Build directory: $BUILD_DIR"
echo "=============================================="

cd "$YETTY_ROOT"

if [ ! -d "$BUILD_DIR" ]; then
    error "Build directory does not exist: $BUILD_DIR"
    exit 1
fi

cd "$BUILD_DIR"

#----------------------------------------------
# 1. Check required output files exist
#----------------------------------------------
echo ""
echo "=== 1. Checking output files ==="

for f in yetty.js yetty.wasm index.html serve.py; do
    if [ -f "$f" ]; then
        SIZE=$(ls -lh "$f" | awk '{print $5}')
        ok "$f exists ($SIZE)"
    else
        error "$f missing"
    fi
done

for f in toybox.js toybox.wasm; do
    if [ -f "$f" ]; then
        SIZE=$(ls -lh "$f" | awk '{print $5}')
        ok "$f exists ($SIZE)"
    else
        warn "$f missing (toybox optional)"
    fi
done

#----------------------------------------------
# 2. Check assets directory structure
#----------------------------------------------
echo ""
echo "=== 2. Checking assets directory ==="

for d in assets assets/fonts-cdb assets/shaders assets/shaders/cards assets/shaders/lib; do
    if [ -d "$d" ]; then
        COUNT=$(ls -1 "$d" 2>/dev/null | wc -l)
        ok "$d/ exists ($COUNT items)"
    else
        error "$d/ missing"
    fi
done

#----------------------------------------------
# 3. Check font files
#----------------------------------------------
echo ""
echo "=== 3. Checking font files ==="

FONTS="DejaVuSansMNerdFontMono-Regular DejaVuSansMNerdFontMono-Bold DejaVuSansMNerdFontMono-Oblique DejaVuSansMNerdFontMono-BoldOblique"

for font in $FONTS; do
    TTF="assets/${font}.ttf"
    CDB="assets/fonts-cdb/${font}.cdb"

    if [ -f "$TTF" ]; then
        SIZE=$(ls -lh "$TTF" | awk '{print $5}')
        ok "$TTF ($SIZE)"
    else
        error "$TTF missing"
    fi

    if [ -f "$CDB" ]; then
        SIZE=$(ls -lh "$CDB" | awk '{print $5}')
        ok "$CDB ($SIZE)"
    else
        error "$CDB missing"
    fi
done

#----------------------------------------------
# 4. Check shader files
#----------------------------------------------
echo ""
echo "=== 4. Checking shader files ==="

SHADERS="gpu-screen.wgsl cursor.wgsl msdf_gen.wgsl"
for shader in $SHADERS; do
    if [ -f "assets/shaders/$shader" ]; then
        ok "assets/shaders/$shader"
    else
        error "assets/shaders/$shader missing"
    fi
done

CARD_SHADERS=$(ls assets/shaders/cards/*.wgsl 2>/dev/null | wc -l)
if [ "$CARD_SHADERS" -gt 5 ]; then
    ok "Card shaders: $CARD_SHADERS files"
else
    error "Card shaders: only $CARD_SHADERS files (expected >5)"
fi

# Sections 5/6 (yetty.data package integrity) retired: /demo and /src
# ship as lazy yfs subtrees now (docs/yfs.md) — no data package.

#----------------------------------------------
# 7. Check for zero-byte files in package
#----------------------------------------------
echo ""
echo "=== 7. Checking for problematic files ==="

# Extract file entries and check for zero-byte files
ZERO_FILES=$(grep -oE '\{filename:"[^"]+",start:([0-9]+),end:\1\}' yetty.js | wc -l)
if [ "$ZERO_FILES" -eq 0 ]; then
    ok "No zero-byte files in package"
else
    warn "Zero-byte files in package: $ZERO_FILES"
    grep -oE '\{filename:"[^"]+",start:([0-9]+),end:\1\}' yetty.js | head -5
fi

#----------------------------------------------
# 8. Test with Node.js (if available)
#----------------------------------------------
echo ""
echo "=== 8. Node.js smoke test ==="

if command -v node &> /dev/null; then
    # Create a test script that loads the module
    cat > /tmp/test-yetty-load.js << 'EOF'
const fs = require('fs');
const path = require('path');

// Mock browser globals
global.window = { location: { pathname: '/' } };
global.document = {
    getElementById: () => ({ getContext: () => null }),
    createElement: () => ({})
};
global.navigator = { userAgent: 'node' };
global.WebAssembly = WebAssembly;
global.fetch = async (url) => {
    const filePath = path.join(process.cwd(), url);
    const data = fs.readFileSync(filePath);
    return {
        ok: true,
        arrayBuffer: async () => data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength),
        headers: { get: () => data.length }
    };
};

// Try to parse the JS file for obvious syntax errors
try {
    const jsContent = fs.readFileSync('yetty.js', 'utf8');
    // Check for the file metadata
    const filesMatch = jsContent.match(/files:\[\{.*?\}\]/s);
    if (filesMatch) {
        console.log('JS metadata structure: OK');
    } else {
        console.log('JS metadata structure: NOT FOUND');
        process.exit(1);
    }

    // Try to extract and validate file entries
    const fileEntries = jsContent.match(/\{filename:"[^"]+",start:\d+,end:\d+\}/g);
    if (fileEntries && fileEntries.length > 0) {
        console.log('File entries found:', fileEntries.length);

        // Validate first and last entries
        const first = fileEntries[0];
        const last = fileEntries[fileEntries.length - 1];
        console.log('First:', first.substring(0, 80) + '...');
        console.log('Last:', last);
    }

    process.exit(0);
} catch (e) {
    console.error('Error:', e.message);
    process.exit(1);
}
EOF

    if node /tmp/test-yetty-load.js 2>&1; then
        ok "Node.js parsing test passed"
    else
        error "Node.js parsing test failed"
    fi
    rm -f /tmp/test-yetty-load.js
else
    warn "Node.js not available, skipping"
fi

#----------------------------------------------
# 9. Check WASM file
#----------------------------------------------
echo ""
echo "=== 9. Checking WASM file ==="

WASM_SIZE=$(stat -c%s yetty.wasm 2>/dev/null || stat -f%z yetty.wasm 2>/dev/null)
if [ "$WASM_SIZE" -gt 1000000 ]; then
    ok "WASM file size: $WASM_SIZE bytes"
else
    error "WASM file too small: $WASM_SIZE bytes"
fi

# Check WASM magic number (0x00 0x61 0x73 0x6d = '\0asm')
MAGIC=$(xxd -p -l 4 yetty.wasm)
if [ "$MAGIC" = "0061736d" ]; then
    ok "WASM magic number valid"
else
    error "WASM magic number invalid: $MAGIC (expected 0061736d)"
fi

#----------------------------------------------
# 10. Check data file structure
#----------------------------------------------
echo ""
echo "=== 10. Checking data file structure ==="

# Get the last file's end offset from metadata
LAST_END=$(grep -oE 'end:[0-9]+' yetty.js | tail -1 | cut -d: -f2)
if [ -n "$LAST_END" ] && [ "$LAST_END" -le "$ACTUAL_SIZE" ]; then
    ok "Last file offset ($LAST_END) <= data size ($ACTUAL_SIZE)"
else
    error "Last file offset ($LAST_END) > data size ($ACTUAL_SIZE)"
fi

# Sample check: verify first file starts at 0
FIRST_START=$(grep -oE 'start:[0-9]+' yetty.js | head -1 | cut -d: -f2)
if [ "$FIRST_START" = "0" ]; then
    ok "First file starts at offset 0"
else
    error "First file starts at offset $FIRST_START (expected 0)"
fi

#----------------------------------------------
# Summary
#----------------------------------------------
echo ""
echo "=============================================="
echo "SUMMARY"
echo "=============================================="
echo -e "Errors:   ${RED}$ERRORS${NC}"
echo -e "Warnings: ${YELLOW}$WARNINGS${NC}"

if [ "$ERRORS" -gt 0 ]; then
    echo ""
    echo -e "${RED}BUILD VERIFICATION FAILED${NC}"
    exit 1
else
    echo ""
    echo -e "${GREEN}BUILD VERIFICATION PASSED${NC}"
    exit 0
fi
