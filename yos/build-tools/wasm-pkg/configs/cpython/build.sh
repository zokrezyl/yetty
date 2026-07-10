#!/usr/bin/env bash
# cpython — Python interpreter, autoconf-driven.
#
# Cross-compile-to-wasm32 strategy:
#   - Like zsh: feed configure --host=wasm32-unknown-unknown so it
#     takes the cross-compile branch and skips run-time feature tests.
#   - CPython needs a BUILD-Python (same major version) at build time
#     to compile its frozen-module bytecode (deep-freeze pass). We pass
#     --with-build-python=python3.12 from the host PATH.
#   - Static link only (no dlopen under wasm). Optional stdlib C
#     extensions that need third-party libs (ssl, sqlite, zlib, ctypes,
#     readline, curses, etc.) are disabled at configure-time and pulled
#     out of Modules/Setup.local.
#   - Pre-seed ac_cv_func_* for every yos-unbridged libc fn so
#     CPython's --allow-undefined link-test doesn't mark them HAVE_*.
#
# Target: `python3 -c "print(1+1)"` runs under yos. Stdlib import
# coverage grows from there.

set -euo pipefail

NAME=cpython
VERSION=3.12.7
URL="https://www.python.org/ftp/python/${VERSION}/Python-${VERSION}.tar.xz"
SHA256="24887b92e2afd4a2ac602419ad4b596372f67ac9b077190f459aba390faf5550"
DEPS=""

: "${ROOT:?}"; : "${PREFIX:?}"; : "${WASM_CC:?}"; : "${WASM_SYSROOT:?}"
: "${WORK:=$ROOT/build-linux/wasm-pkgs/${NAME}-${VERSION}}"

mkdir -p "$WORK" "$PREFIX/bin"

# Nix's buildRecipe pre-places the tarball at $WORK/${NAME}-${VERSION}.tar.gz
# (always `.tar.gz` regardless of actual format); the standalone wasm-pkg.sh
# driver fetches via curl. Match that naming so both paths work.
TARBALL="$WORK/${NAME}-${VERSION}.tar.gz"
[[ -f "$TARBALL" ]] || curl -fsSL "$URL" -o "$TARBALL"
echo "${SHA256}  $TARBALL" | sha256sum -c - > /dev/null

SRC="$WORK/src"
if [[ ! -f "$SRC/.extracted" ]]; then
    rm -rf "$SRC"; mkdir -p "$SRC"
    # Auto-detect compression: upstream tarball is .tar.xz despite our naming.
    tar -xJf "$TARBALL" -C "$SRC" --strip-components=1

    # config.sub patch — same as zsh's. Python 3.12 ships a config.sub
    # from autoconf-2.71 (Jan 2022) which DOES know wasm32-wasi but
    # not wasm32-unknown-unknown. Short-circuit to keep --host happy.
    if [[ -f "$SRC/config.sub" ]]; then
        python3 - "$SRC/config.sub" <<'PY'
import sys, pathlib
p = pathlib.Path(sys.argv[1])
text = p.read_text()
shim = '''# yos: short-circuit wasm32/wasm64 (any subtarget).
case $1 in
    wasm32-*-* | wasm64-*-*) echo "$1"; exit 0 ;;
esac

'''
marker = 'case $# in'
i = text.find(marker)
if i < 0:
    sys.exit('config.sub: did not find expected case-statement marker')
new = text[:i] + shim + text[i:]
p.write_text(new)
print('config.sub: wasm short-circuit installed')
PY
    fi

    # ── configure patch: accept wasm32-unknown-unknown ───────────────
    # CPython 3.12's configure has a hard-coded allowlist of cross
    # targets and aborts with "cross build not supported" on anything
    # else. Our triple (wasm32-unknown-unknown) gets rejected. Inject
    # cases that treat any wasm32-*-* / wasm64-*-* as if it were WASI,
    # which is the closest of the existing cross-targets to what yos
    # implements (POSIX-shaped libc as wasm imports).
    python3 - "$SRC/configure" <<'PY'
import sys, pathlib
p = pathlib.Path(sys.argv[1])
text = p.read_text()
old = '''	*-*-wasi)
	    ac_sys_system=WASI
	    ;;
	*)
		# for now, limit cross builds to known configurations
		MACHDEP="unknown"
		as_fn_error $? "cross build not supported for $host" "$LINENO" 5
	esac
	ac_sys_release='''
new = '''	*-*-wasi)
	    ac_sys_system=WASI
	    ;;
	wasm32-*-* | wasm64-*-*)
	    # yos: any wasm32/wasm64 target — treat as WASI for MACHDEP
	    # purposes (closest POSIX-shaped sibling in CPython's tree).
	    ac_sys_system=WASI
	    ;;
	*)
		# for now, limit cross builds to known configurations
		MACHDEP="unknown"
		as_fn_error $? "cross build not supported for $host" "$LINENO" 5
	esac
	ac_sys_release='''
if old not in text:
    sys.exit('configure: did not find first MACHDEP wasm-patch marker')
text = text.replace(old, new, 1)
# Second occurrence — _host_cpu detection block — already has a
# wasm32-*-* / wasm64-*-* case; the *) error never fires when our
# match wins. Nothing to patch there.
p.write_text(text)
print('configure: wasm32-unknown-unknown cross-build patched in')
PY

    # ── expat fastcall patch ─────────────────────────────────────────
    # The bundled expat (Modules/expat/internal.h) detects __i386__
    # and turns FASTCALL into __attribute__((regparm(3))). We compile
    # all wasm-pkg recipes with -D__i386__=1 (FreeBSD i386 sysroot
    # headers need it), so expat picks the x86-only attribute and
    # clang-wasm32 rejects every expat call site. Sed the regparm /
    # stdcall defines out of internal.h directly so the macros stay
    # no-ops regardless of who includes them later.
    if [ -f "$SRC/Modules/expat/internal.h" ]; then
        python3 - "$SRC/Modules/expat/internal.h" <<'PY'
import re, sys, pathlib
p = pathlib.Path(sys.argv[1])
t = p.read_text()
# Replace any `#define FASTCALL __attribute__((regparm(N)))` and
# similar PTRFASTCALL / PTRCALL stdcall variants with bare no-op
# defines. Use a regex to catch both x86 and Win32 forms.
def neutralise(text, name):
    pat = re.compile(
        r'^[ \t]*#[ \t]*define[ \t]+' + name +
        r'[ \t]+__attribute__\(\([^)]+\)\).*$', re.MULTILINE)
    return pat.sub('#define ' + name + '  /* yos: no-op on wasm32 */', text)
for n in ('FASTCALL', 'PTRFASTCALL', 'PTRCALL'):
    t = neutralise(t, n)
# Same idea for `__cdecl *` / `__stdcall *` forms (Win32 path).
t = re.sub(r'#[ \t]*define[ \t]+PTRCALL[ \t]+__cdecl\s*\*?', '#define PTRCALL', t)
t = re.sub(r'#[ \t]*define[ \t]+PTRFASTCALL[ \t]+__stdcall\s*\*?', '#define PTRFASTCALL', t)
p.write_text(t)
print('expat/internal.h: FASTCALL/PTRFASTCALL/PTRCALL neutralised')
PY
    fi

    # Modules/Setup.local — declare which C extensions we DON'T want
    # built. CPython's setup.py picks up the rest from Modules/Setup
    # and tries to compile every C extension; explicitly disabling
    # the ones with external library deps avoids configure tripping
    # on missing -lssl / -lsqlite3 / -lz / -lncurses.
    cat > "$SRC/Modules/Setup.local" <<'SETUP_EOF'
# yos wasm32: turn off every C extension that requires a third-party
# library we don't ship. *disabled* lines tell setup.py to skip them.
*disabled*
_ssl _hashlib _socket _ctypes _ctypes_test
_sqlite3 _curses _curses_panel _gdbm _dbm
_lzma _bz2 _uuid _decimal _multiprocessing _posixshmem
nis readline zlib pyexpat _elementtree _md5 _sha1 _sha256 _sha512 _sha3
_blake2 _xxhash _statistics _tkinter _scproxy spwd termios
audioop ossaudiodev grp resource syslog
SETUP_EOF

    touch "$SRC/.extracted"
fi

BLD="$WORK/cmake-build"
rm -rf "$BLD"; mkdir -p "$BLD"
cd "$BLD"

# ── config.cache pre-seed ───────────────────────────────────────────
# Same approach as zsh: tell configure "no" for every libc fn yos
# doesn't bridge (Linux-only / glibc-only), so it doesn't pick HAVE_X
# paths it can't compile. Anything not listed below falls through to
# configure's own probing.
CACHE="$BLD/config.cache"
cat > "$CACHE" <<'CACHE_EOF'
# Things configure CAN'T determine without running a binary —
# pre-seed safe answers for wasm32.
ac_cv_file__dev_ptmx=no
ac_cv_file__dev_ptc=no
ac_cv_buggy_getaddrinfo=no
ac_cv_have_long_long_format=yes
ac_cv_have_size_t_format=yes
ac_cv_working_tzset=yes
ac_cv_little_endian_double=yes
ac_cv_big_endian_double=no
ac_cv_mixed_endian_double=no
ac_cv_x87_double_rounding=no
ac_cv_tanh_preserves_zero_sign=yes
ac_cv_aligned_required=no
ac_cv_computed_gotos=no
ac_cv_func_clock_gettime_runtime=yes
ac_cv_func_lchflags_works=no
ac_cv_func_chflags_works=no
ac_cv_func_floor_works=yes

# ── AC_CHECK_FUNCS — explicit "no" for Linux-only / glibc-only fns
# yos doesn't bridge. With --allow-undefined the link test always
# succeeds, so without these overrides configure thinks every libc fn
# exists, picks HAVE_X paths, and the build then fails on the actual
# compile.

# Linux-specific:
ac_cv_func_clock_settime=no
ac_cv_func_epoll_create=no
ac_cv_func_epoll_create1=no
ac_cv_func_eventfd=no
ac_cv_func_signalfd=no
ac_cv_func_timerfd_create=no
ac_cv_func_inotify_init=no
ac_cv_func_inotify_init1=no
ac_cv_func_memfd_create=no
ac_cv_func_pidfd_open=no
ac_cv_func_setresuid=no
ac_cv_func_setresgid=no
ac_cv_func_getresuid=no
ac_cv_func_getresgid=no
ac_cv_func_prlimit=no
ac_cv_func_sched_getaffinity=no
ac_cv_func_sched_setaffinity=no
ac_cv_func_splice=no
ac_cv_func_vmsplice=no
ac_cv_func_tee=no
ac_cv_func_sendfile=no
ac_cv_func_copy_file_range=no
ac_cv_func_getrandom=no
ac_cv_func_getentropy=yes
ac_cv_func_initgroups=no
ac_cv_func_getgrouplist=no
ac_cv_func_setgroups=no
ac_cv_func_clone=no
ac_cv_func_unshare=no
ac_cv_func_setns=no
ac_cv_func_mremap=no
ac_cv_func_madvise=yes
ac_cv_func_pipe2=no
ac_cv_func_accept4=no
ac_cv_func_dup3=no
ac_cv_func_close_range=no
ac_cv_func_strlcpy=yes
ac_cv_func_strlcat=yes
ac_cv_func_strsignal=yes

# Solaris/HP-UX-only:
ac_cv_func_posix_openpt=no
ac_cv_func_strptime=yes

# macOS-only or BSD-specific that aren't on plain wasm:
ac_cv_func_kqueue=no

# Lots of these aren't really yos-issues but the wasm build can't
# easily verify them — explicit answer keeps configure consistent.
ac_cv_func_alloca=yes
ac_cv_func_alarm=yes
ac_cv_func_alarm_works=yes
ac_cv_func_chmod=yes
ac_cv_func_chown=yes
ac_cv_func_clock=yes
ac_cv_func_clock_gettime=yes
ac_cv_func_clock_getres=yes
ac_cv_func_dup=yes
ac_cv_func_dup2=yes
ac_cv_func_execv=yes
ac_cv_func_fchdir=yes
ac_cv_func_fchmod=yes
ac_cv_func_fchown=yes
ac_cv_func_fdatasync=yes
ac_cv_func_fork=yes
ac_cv_func_fpathconf=yes
ac_cv_func_fseek64=no
ac_cv_func_fseeko=yes
ac_cv_func_fstatat=yes
ac_cv_func_fstatvfs=no
ac_cv_func_fsync=yes
ac_cv_func_ftell64=no
ac_cv_func_ftello=yes
ac_cv_func_ftime=no
ac_cv_func_ftruncate=yes
ac_cv_func_futimens=yes
ac_cv_func_futimes=yes
ac_cv_func_gai_strerror=yes
ac_cv_func_getcwd=yes
ac_cv_func_getegid=yes
ac_cv_func_geteuid=yes
ac_cv_func_getgid=yes
ac_cv_func_getgrgid=yes
ac_cv_func_getgrnam=yes
ac_cv_func_gethostbyaddr=yes
ac_cv_func_gethostbyname=yes
ac_cv_func_gethostname=yes
ac_cv_func_getitimer=yes
ac_cv_func_getloadavg=no
ac_cv_func_getlogin=yes
ac_cv_func_getnameinfo=yes
ac_cv_func_getpagesize=yes
ac_cv_func_getpeername=yes
ac_cv_func_getpgid=yes
ac_cv_func_getpgrp=yes
ac_cv_func_getpid=yes
ac_cv_func_getppid=yes
ac_cv_func_getpriority=yes
ac_cv_func_getprotobyname=yes
ac_cv_func_getpwent=yes
ac_cv_func_getpwnam_r=yes
ac_cv_func_getpwuid_r=yes
ac_cv_func_getservbyname=yes
ac_cv_func_getservbyport=yes
ac_cv_func_getsid=yes
ac_cv_func_getsockname=yes
ac_cv_func_getspent=no
ac_cv_func_getspnam=no
ac_cv_func_gettimeofday=yes
ac_cv_func_getuid=yes
ac_cv_func_getwd=yes
ac_cv_func_grantpt=no
ac_cv_func_hstrerror=yes
ac_cv_func_if_nameindex=no
ac_cv_func_inet_aton=yes
ac_cv_func_inet_ntoa=yes
ac_cv_func_inet_pton=yes
ac_cv_func_kill=yes
ac_cv_func_killpg=yes
ac_cv_func_lchmod=no
ac_cv_func_lchown=yes
ac_cv_func_link=yes
ac_cv_func_linkat=yes
ac_cv_func_lockf=no
ac_cv_func_lseek=yes
ac_cv_func_lstat=yes
ac_cv_func_makedev=no
ac_cv_func_memmove=yes
ac_cv_func_mkdirat=yes
ac_cv_func_mkfifo=yes
ac_cv_func_mknod=yes
ac_cv_func_mktime=yes
ac_cv_func_mlockall=no
ac_cv_func_mmap=yes
ac_cv_func_nanosleep=yes
ac_cv_func_openat=yes
ac_cv_func_pathconf=yes
ac_cv_func_pause=yes
ac_cv_func_pipe=yes
ac_cv_func_plock=no
ac_cv_func_poll=yes
ac_cv_func_posix_fadvise=yes
ac_cv_func_posix_fallocate=no
ac_cv_func_posix_spawn=no
ac_cv_func_posix_spawnp=no
ac_cv_func_pread=yes
ac_cv_func_preadv=no
ac_cv_func_preadv2=no
ac_cv_func_pselect=no
ac_cv_func_pthread_condattr_setclock=no
ac_cv_func_pthread_init=no
ac_cv_func_pthread_kill=yes
ac_cv_func_pthread_sigmask=yes
ac_cv_func_putenv=yes
ac_cv_func_pwrite=yes
ac_cv_func_pwritev=no
ac_cv_func_pwritev2=no
ac_cv_func_readlink=yes
ac_cv_func_readlinkat=yes
ac_cv_func_readv=yes
ac_cv_func_realpath=yes
ac_cv_func_renameat=yes
ac_cv_func_rtpSpawn=no
ac_cv_func_sched_get_priority_max=yes
ac_cv_func_sched_rr_get_interval=no
ac_cv_func_sched_setparam=no
ac_cv_func_sched_setscheduler=no
ac_cv_func_select=yes
ac_cv_func_sem_clockwait=no
ac_cv_func_sem_getvalue=no
ac_cv_func_sem_open=no
ac_cv_func_sem_timedwait=no
ac_cv_func_sem_unlink=no
ac_cv_func_sendfile=no
ac_cv_func_setegid=no
ac_cv_func_seteuid=no
ac_cv_func_setgid=yes
ac_cv_func_sethostname=no
ac_cv_func_setitimer=yes
ac_cv_func_setlocale=yes
ac_cv_func_setpgid=yes
ac_cv_func_setpgrp=yes
ac_cv_func_setpriority=yes
ac_cv_func_setregid=no
ac_cv_func_setreuid=no
ac_cv_func_setsid=yes
ac_cv_func_setsockopt=yes
ac_cv_func_setuid=yes
ac_cv_func_setvbuf=yes
ac_cv_func_shutdown=yes
ac_cv_func_sigaction=yes
ac_cv_func_sigaltstack=yes
ac_cv_func_siginterrupt=no
ac_cv_func_sigpending=yes
ac_cv_func_sigrelse=no
ac_cv_func_sigtimedwait=yes
ac_cv_func_sigwait=yes
ac_cv_func_sigwaitinfo=yes
ac_cv_func_snprintf=yes
ac_cv_func_socketpair=yes
ac_cv_func_statvfs=no
ac_cv_func_strdup=yes
ac_cv_func_strftime=yes
ac_cv_func_symlink=yes
ac_cv_func_symlinkat=yes
ac_cv_func_sync=no
ac_cv_func_sysconf=yes
ac_cv_func_system=yes
ac_cv_func_tcgetpgrp=yes
ac_cv_func_tcsetpgrp=yes
ac_cv_func_tempnam=yes
ac_cv_func_tmpfile=yes
ac_cv_func_tmpnam=yes
ac_cv_func_tmpnam_r=no
ac_cv_func_truncate=yes
ac_cv_func_ttyname=yes
ac_cv_func_uname=yes
ac_cv_func_unlinkat=yes
ac_cv_func_utimensat=yes
ac_cv_func_utimes=yes
ac_cv_func_vfork=yes
ac_cv_func_wait=yes
ac_cv_func_wait3=no
ac_cv_func_wait4=yes
ac_cv_func_waitid=yes
ac_cv_func_waitpid=yes
ac_cv_func_wcscoll=yes
ac_cv_func_wcsftime=yes
ac_cv_func_wcsxfrm=yes
ac_cv_func_writev=yes
ac_cv_func_dirfd=yes
ac_cv_func_setpriority=yes

# Headers configure can't probe properly under wasm32:
ac_cv_header_sched_h=yes
ac_cv_header_sys_eventfd_h=no
ac_cv_header_sys_inotify_h=no
ac_cv_header_sys_timerfd_h=no
ac_cv_header_sys_signalfd_h=no
ac_cv_header_sys_epoll_h=no
ac_cv_header_linux_random_h=no
ac_cv_header_linux_vm_sockets_h=no
ac_cv_header_linux_tipc_h=no
ac_cv_header_linux_can_h=no
ac_cv_header_linux_wait_h=no
ac_cv_header_linux_membarrier_h=no
ac_cv_header_linux_memfd_h=no

# C99 libm — Python's math module insists these all exist. They come
# from FreeBSD libm.a (which we don't link), but yos resolves them at
# runtime via env imports (--allow-undefined). Seed yes so configure
# doesn't run a link test that succeeds-for-wrong-reasons.
ac_cv_func_acosh=yes
ac_cv_func_asinh=yes
ac_cv_func_atanh=yes
ac_cv_func_erf=yes
ac_cv_func_erfc=yes
ac_cv_func_expm1=yes
ac_cv_func_log1p=yes
ac_cv_func_log2=yes
ac_cv_func_copysign=yes
ac_cv_func_hypot=yes
ac_cv_func_round=yes
ac_cv_func_rint=yes
ac_cv_func_trunc=yes
ac_cv_func_nextafter=yes
ac_cv_func_lgamma=yes
ac_cv_func_tgamma=yes
ac_cv_func_cbrt=yes
ac_cv_func_remainder=yes
ac_cv_func_fma=yes
ac_cv_func_finite=yes
ac_cv_func_isfinite=yes
ac_cv_func_isinf=yes
ac_cv_func_isnan=yes
ac_cv_func_gamma=no

# Misc CPython quirks:
ac_cv_pthread=yes
ac_cv_pthread_is_default=yes
ac_cv_kpthread=no
ac_cv_kthread=no
CACHE_EOF

# ── configure ──────────────────────────────────────────────────────
CFLAGS_W="$WASM_CFLAGS -D_GNU_SOURCE -D__FreeBSD__=14"
LDFLAGS_W="-target wasm32-unknown-unknown -nostdlib $WASM_LDFLAGS \
    $WASM_SYSROOT/usr/lib/crt1.o \
    -L$WASM_SYSROOT/usr/lib"

# CPython needs a HOST python of the same minor version to bootstrap.
# Look for python3.12 first, then fall back to whatever python3 we have.
BUILD_PY="$(command -v python3.12 || command -v python3)"
[[ -x "$BUILD_PY" ]] || { echo "[$NAME] no host python3.12/python3 in PATH"; exit 1; }
echo "[$NAME] build-python: $BUILD_PY"

CC="$WASM_CC" \
CFLAGS="$CFLAGS_W" \
CPPFLAGS="$CFLAGS_W" \
LDFLAGS="$LDFLAGS_W" \
LIBS="-lc -lyos_stubs" \
CPP="$WASM_CC -E $CFLAGS_W" \
"$SRC/configure" \
    --host=wasm32-unknown-unknown \
    --build="$($SRC/config.guess)" \
    --prefix="$PREFIX" \
    --cache-file="$CACHE" \
    --with-build-python="$BUILD_PY" \
    --disable-shared \
    --disable-ipv6 \
    --disable-test-modules \
    --without-pymalloc \
    --without-doc-strings \
    --without-readline \
    --without-decimal-contextvar \
    --without-pydebug \
    --without-static-libpython \
    --without-ensurepip \
    --with-system-ffi=no \
    --enable-big-digits=30 \
    ac_cv_file__dev_ptmx=no \
    ac_cv_file__dev_ptc=no \
    || { echo "[$NAME] configure failed; check $BLD/config.log"; tail -100 "$BLD/config.log"; exit 1; }

# ── build ──────────────────────────────────────────────────────────
# CPython's make tree builds python (the interpreter) + libpython
# static archive + a pile of frozen stdlib bytecode modules. -j is
# important: deep-freeze pass runs many invocations of the build-python.
# With ac_sys_system=WASI, CPython's EXEEXT becomes .wasm and the
# binary target is python.wasm (not python). Build that directly
# rather than the default `all` target — `all` also runs setup.py to
# build C extensions as shared libs, which doesn't work under wasm32
# without dlopen.
make -j"$(nproc 2>/dev/null || echo 4)" python.wasm || {
    echo "[$NAME] make failed"
    exit 1
}

# ── install ────────────────────────────────────────────────────────
# Just install the wasm binary; the deep-frozen modules are linked
# into the binary already so no separate stdlib install needed for
# the minimal `python3 -c '...'` smoke target.
mkdir -p "$PREFIX/bin"
wasm-opt --asyncify -O2 "$BLD/python.wasm" -o "$PREFIX/bin/python3.wasm"

cat > "$PREFIX/manifest.txt" <<EOF
name=cpython
version=${VERSION}
prefix=${PREFIX}
src=${SRC}
EOF

echo "[$NAME] installed → $PREFIX/bin/python3.wasm"
