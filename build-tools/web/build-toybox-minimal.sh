#!/bin/bash
#
# Build minimal Toybox for WebAssembly using Emscripten
# Starts with extreme minimum, can add more commands later
#

set -e

cd "$(dirname "$0")/../.."
PROJECT_ROOT="$(pwd)"
TOYBOX_SRC="$PROJECT_ROOT/tmp/toybox"
BUILD_DIR="$PROJECT_ROOT/build-webasm-release"

if [ ! -d "$TOYBOX_SRC" ]; then
    echo "Error: Toybox source not found at $TOYBOX_SRC"
    exit 1
fi

if ! command -v emcc &> /dev/null; then
    echo "Error: emcc not found. Run via: nix develop .#web --command bash $0"
    exit 1
fi

echo "Building minimal Toybox for WebAssembly..."
mkdir -p "$BUILD_DIR"

# Clean
cd "$TOYBOX_SRC"
make distclean 2>/dev/null || true

# Start with allnoconfig (properly defines all options as disabled)
echo "Creating base config with allnoconfig..."
make allnoconfig HOSTCC=cc 2>&1 | tail -1

# Now enable commands using sed
echo "Enabling commands..."

# Core functionality
sed -i 's/# CONFIG_TOYBOX_FLOAT is not set/CONFIG_TOYBOX_FLOAT=y/' .config
sed -i 's/# CONFIG_TOYBOX_HELP is not set/CONFIG_TOYBOX_HELP=y/' .config

# Basic shell commands
sed -i 's/# CONFIG_TRUE is not set/CONFIG_TRUE=y/' .config
sed -i 's/# CONFIG_FALSE is not set/CONFIG_FALSE=y/' .config
sed -i 's/# CONFIG_ECHO is not set/CONFIG_ECHO=y/' .config
sed -i 's/# CONFIG_PRINTF is not set/CONFIG_PRINTF=y/' .config
sed -i 's/# CONFIG_TEST is not set/CONFIG_TEST=y/' .config
sed -i 's/# CONFIG_PWD is not set/CONFIG_PWD=y/' .config
sed -i 's/# CONFIG_PRINTENV is not set/CONFIG_PRINTENV=y/' .config
sed -i 's/# CONFIG_ENV is not set/CONFIG_ENV=y/' .config

# File operations (stubs provided for missing syscalls)
sed -i 's/# CONFIG_LS is not set/CONFIG_LS=y/' .config
sed -i 's/# CONFIG_CAT is not set/CONFIG_CAT=y/' .config
sed -i 's/# CONFIG_HEAD is not set/CONFIG_HEAD=y/' .config
sed -i 's/# CONFIG_TAIL is not set/CONFIG_TAIL=y/' .config
sed -i 's/# CONFIG_TOUCH is not set/CONFIG_TOUCH=y/' .config
sed -i 's/# CONFIG_MKDIR is not set/CONFIG_MKDIR=y/' .config
sed -i 's/# CONFIG_RMDIR is not set/CONFIG_RMDIR=y/' .config
sed -i 's/# CONFIG_RM is not set/CONFIG_RM=y/' .config
sed -i 's/# CONFIG_CP is not set/CONFIG_CP=y/' .config
sed -i 's/# CONFIG_MV is not set/CONFIG_MV=y/' .config
sed -i 's/# CONFIG_FIND is not set/CONFIG_FIND=y/' .config

# Text processing
sed -i 's/# CONFIG_WC is not set/CONFIG_WC=y/' .config
sed -i 's/# CONFIG_SORT is not set/CONFIG_SORT=y/' .config
sed -i 's/# CONFIG_UNIQ is not set/CONFIG_UNIQ=y/' .config
sed -i 's/# CONFIG_CUT is not set/CONFIG_CUT=y/' .config
sed -i 's/# CONFIG_TR is not set/CONFIG_TR=y/' .config
sed -i 's/# CONFIG_REV is not set/CONFIG_REV=y/' .config
sed -i 's/# CONFIG_BASENAME is not set/CONFIG_BASENAME=y/' .config
sed -i 's/# CONFIG_DIRNAME is not set/CONFIG_DIRNAME=y/' .config
sed -i 's/# CONFIG_TEE is not set/CONFIG_TEE=y/' .config

# Fun/utility
sed -i 's/# CONFIG_YES is not set/CONFIG_YES=y/' .config
sed -i 's/# CONFIG_SEQ is not set/CONFIG_SEQ=y/' .config
sed -i 's/# CONFIG_CAL is not set/CONFIG_CAL=y/' .config
sed -i 's/# CONFIG_FACTOR is not set/CONFIG_FACTOR=y/' .config

# Shell (experimental - needs syscall stubs for fork/exec)
sed -i 's/# CONFIG_SH is not set/CONFIG_SH=y/' .config
sed -i 's/# CONFIG_SLEEP is not set/CONFIG_SLEEP=y/' .config
sed -i 's/# CONFIG_DATE is not set/CONFIG_DATE=y/' .config

echo "Commands enabled: $(grep '=y' .config | wc -l)"

# Generate headers
echo "Generating headers..."
NOBUILD=1 make toybox HOSTCC=cc CC=cc 2>&1 | tail -3

# Check flags.h
if [ ! -s generated/flags.h ] || [ $(wc -c < generated/flags.h) -lt 1000 ]; then
    echo "ERROR: flags.h generation failed ($(wc -c < generated/flags.h) bytes)"
    cat generated/flags.h
    exit 1
fi
echo "flags.h: $(wc -c < generated/flags.h) bytes - OK"

# Create stubs for missing headers
mkdir -p generated/wasm-stubs/sys
cat > generated/wasm-stubs/sys/inotify.h << 'EOF'
#ifndef _SYS_INOTIFY_H
#define _SYS_INOTIFY_H
#define IN_MODIFY 0
#define IN_DELETE_SELF 0
#define IN_MOVE_SELF 0
struct inotify_event { int wd; unsigned mask; unsigned cookie; unsigned len; char name[]; };
static inline int inotify_init(void) { return -1; }
static inline int inotify_init1(int f) { return -1; }
static inline int inotify_add_watch(int f, const char *p, unsigned m) { return -1; }
static inline int inotify_rm_watch(int f, int w) { return -1; }
#endif
EOF

cat > generated/wasm-stubs/sys/xattr.h << 'EOF'
#ifndef _SYS_XATTR_H
#define _SYS_XATTR_H
#include <sys/types.h>
static inline ssize_t getxattr(const char *p, const char *n, void *v, size_t s) { return -1; }
static inline ssize_t lgetxattr(const char *p, const char *n, void *v, size_t s) { return -1; }
static inline ssize_t fgetxattr(int f, const char *n, void *v, size_t s) { return -1; }
static inline ssize_t listxattr(const char *p, char *l, size_t s) { return -1; }
static inline ssize_t llistxattr(const char *p, char *l, size_t s) { return -1; }
static inline ssize_t flistxattr(int f, char *l, size_t s) { return -1; }
static inline int setxattr(const char *p, const char *n, const void *v, size_t s, int f) { return -1; }
static inline int lsetxattr(const char *p, const char *n, const void *v, size_t s, int f) { return -1; }
static inline int fsetxattr(int fd, const char *n, const void *v, size_t s, int f) { return -1; }
static inline int removexattr(const char *p, const char *n) { return -1; }
static inline int lremovexattr(const char *p, const char *n) { return -1; }
static inline int fremovexattr(int f, const char *n) { return -1; }
#endif
EOF

# Create C stubs for missing functions
cat > generated/wasm-stubs/wasm_stubs.c << 'EOF'
// Stubs for functions not available in Emscripten/WASM
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

// sendfile stub: sendfile_len(int in, int out, long long len, long long *consumed)
long long sendfile_len(int in, int out, long long len, long long *consumed) {
    errno = ENOSYS;
    if (consumed) *consumed = 0;
    return -1;
}

// xattr stubs (Toybox internal names)
ssize_t xattr_flist(int fd, char *list, size_t size) { return 0; }
ssize_t xattr_fget(int fd, char *name, void *value, size_t size) {
    errno = ENODATA;
    return -1;
}
int xattr_fset(int fd, char *name, void *value, size_t size, int flags) {
    errno = ENOSYS;
    return -1;
}

// xnotify stubs (struct-based inotify wrapper)
struct xnotify { int unused; };
struct xnotify *xnotify_init(int max) { return 0; }
int xnotify_add(struct xnotify *not, int fd, char *path) { return -1; }
int xnotify_wait(struct xnotify *not, char **path) { return -1; }

// Device major/minor/makedev (not meaningful in WASM filesystem)
int dev_major(int dev) { return 0; }
int dev_minor(int dev) { return 0; }
int dev_makedev(int major, int minor) { return 0; }

// fs_type_name stub
struct statfs;
char *fs_type_name(struct statfs *statfs) { return "unknown"; }

// rename_exchange stub
int rename_exchange(const char *old, const char *new) {
    errno = ENOSYS;
    return -1;
}

// settimeofday stub (date command read works, set fails)
struct timeval;
struct timezone;
int settimeofday(const struct timeval *tv, const struct timezone *tz) {
    errno = EPERM;
    return -1;
}

// xfork stub - fork not available in WASM, return child context
int xfork(void) {
    return 0;  // Pretend we're the child (no actual fork)
}

// Signal name/number conversion stubs (for sh)
char *num_to_sig(int sig) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "SIG%d", sig);
    return buf;
}

int sig_to_num(char *name) {
    // Common signals
    if (!name) return 0;
    if (!strcmp(name, "HUP") || !strcmp(name, "SIGHUP")) return 1;
    if (!strcmp(name, "INT") || !strcmp(name, "SIGINT")) return 2;
    if (!strcmp(name, "QUIT") || !strcmp(name, "SIGQUIT")) return 3;
    if (!strcmp(name, "KILL") || !strcmp(name, "SIGKILL")) return 9;
    if (!strcmp(name, "TERM") || !strcmp(name, "SIGTERM")) return 15;
    return 0;
}

// Block device size stub (not meaningful in WASM)
int get_block_device_size(int fd, unsigned long long *size) {
    if (size) *size = 0;
    return 0;
}
EOF

cd "$PROJECT_ROOT"

# Compile
echo ""
echo "Compiling with Emscripten..."
CFLAGS="-Os -I$TOYBOX_SRC/generated/wasm-stubs -I$TOYBOX_SRC -include sys/vfs.h"
CFLAGS="$CFLAGS -Wno-int-conversion -Wno-incompatible-pointer-types -Wno-implicit-function-declaration"

mkdir -p "$TOYBOX_SRC/generated/obj"
SUCCESS=0
FAILED=0

# Compile libs
echo "Libraries:"
for src in "$TOYBOX_SRC"/lib/*.c; do
    name=$(basename "$src" .c)
    echo -n "  $name... "
    if emcc $CFLAGS -c "$src" -o "$TOYBOX_SRC/generated/obj/lib_$name.o" 2>/dev/null; then
        echo "ok"; ((SUCCESS++)) || true
    else
        echo "FAILED"; ((FAILED++)) || true
    fi
done

# Compile main
echo -n "  main... "
if emcc $CFLAGS -c "$TOYBOX_SRC/main.c" -o "$TOYBOX_SRC/generated/obj/main.o" 2>/dev/null; then
    echo "ok"; ((SUCCESS++)) || true
else
    echo "FAILED"; ((FAILED++)) || true
fi

# Compile WASM stubs
echo -n "  wasm_stubs... "
if emcc $CFLAGS -c "$TOYBOX_SRC/generated/wasm-stubs/wasm_stubs.c" -o "$TOYBOX_SRC/generated/obj/wasm_stubs.o" 2>/dev/null; then
    echo "ok"; ((SUCCESS++)) || true
else
    echo "FAILED"; ((FAILED++)) || true
fi

# Compile only enabled commands
echo "Commands:"
ENABLED=$(grep "^CONFIG_.*=y" "$TOYBOX_SRC/.config" | sed 's/CONFIG_\(.*\)=y/\1/' | tr '\n' '|' | sed 's/|$//')
for src in $(grep -l -E "^USE_($ENABLED)\(" "$TOYBOX_SRC"/toys/*/*.c 2>/dev/null); do
    name=$(basename "$src" .c)
    echo -n "  $name... "
    if emcc $CFLAGS -c "$src" -o "$TOYBOX_SRC/generated/obj/cmd_$name.o" 2>/dev/null; then
        echo "ok"; ((SUCCESS++)) || true
    else
        echo "FAILED"; ((FAILED++)) || true
    fi
done

echo ""
echo "Compiled: $SUCCESS ok, $FAILED failed"

# Link
echo ""
echo "Linking..."
OBJS=$(find "$TOYBOX_SRC/generated/obj" -name "*.o" | xargs)
LDFLAGS="-sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1"
LDFLAGS="$LDFLAGS -sEXPORTED_FUNCTIONS=['_main']"
LDFLAGS="$LDFLAGS -sEXPORTED_RUNTIME_METHODS=['callMain','FS','cwrap','ccall']"
LDFLAGS="$LDFLAGS -sEXIT_RUNTIME=0"
LDFLAGS="$LDFLAGS -sINVOKE_RUN=0"
LDFLAGS="$LDFLAGS -sMODULARIZE=1"
LDFLAGS="$LDFLAGS -sEXPORT_NAME='ToyboxModule'"

if emcc $OBJS $LDFLAGS -o "$BUILD_DIR/toybox.js" 2>&1; then
    echo ""
    echo "SUCCESS! Toybox WASM built:"
    ls -la "$BUILD_DIR"/toybox.*
    echo ""
    echo "Test: node $BUILD_DIR/toybox.js echo Hello"
else
    echo "Linking failed"
    exit 1
fi
