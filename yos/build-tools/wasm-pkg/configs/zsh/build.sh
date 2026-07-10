#!/usr/bin/env bash
# zsh — Z Shell, autoconf-driven.
#
# Cross-compile-to-wasm32 strategy:
#   - autoconf doesn't really know about wasm32; we feed it
#     --host=wasm32-unknown-unknown so it picks the cross-compile path
#     and skips run-time feature tests.
#   - Many feature checks try to run test binaries (forbidden under
#     cross). For those we pre-seed config.cache with safe answers
#     (zsh_cv_*) — see the heredoc below.
#   - Modules are static-linked (--disable-dynamic) because dlopen()
#     under wasm/yos isn't a thing.
#   - Line editing (zle/terminfo/ncurses) is disabled — we have no
#     ncurses for wasm32 and zle wants live tty ioctls we mostly
#     stub. The shell still parses, runs commands, etc.
#
# What yos doesn't bridge that zsh wants:
#   - getrusage, setrlimit, ualarm, etc. — disable via configure flags
#     where possible; let the rest fail gracefully at runtime.

set -euo pipefail

NAME=zsh
VERSION=5.9
URL="https://www.zsh.org/pub/zsh-${VERSION}.tar.xz"
SHA256="9b8d1ecedd5b5e81fbf1918e876752a7dd948e05c1a0dba10ab863842d45acd5"
DEPS=""

: "${ROOT:?}"; : "${PREFIX:?}"; : "${WASM_CC:?}"; : "${WASM_SYSROOT:?}"
: "${WORK:=$ROOT/build-linux/wasm-pkgs/${NAME}-${VERSION}}"

mkdir -p "$WORK" "$PREFIX/bin"

TARBALL="$WORK/${NAME}-${VERSION}.tar.gz"
[[ -f "$TARBALL" ]] || curl -fsSL "$URL" -o "$TARBALL"
echo "${SHA256}  $TARBALL" | sha256sum -c - > /dev/null

SRC="$WORK/src"
if [[ ! -f "$SRC/.extracted" ]]; then
    rm -rf "$SRC"; mkdir -p "$SRC"
    # Tarball is .xz despite our naming; tar -xJf handles it.
    tar -xJf "$TARBALL" -C "$SRC" --strip-components=1

    # config.sub patch — zsh's bundled config.sub (timestamp 2020-05)
    # predates wasm32 support. Inject a short-circuit at the very
    # top of the validation case so wasm32-* / wasm64-* triples are
    # echoed back unchanged before any "decode/validate" logic runs.
    if [[ -f "$SRC/config.sub" ]]; then
        python3 - "$SRC/config.sub" <<'PY'
import sys, pathlib
p = pathlib.Path(sys.argv[1])
text = p.read_text()
shim = '''# yos: short-circuit wasm32/wasm64 — they're valid, just unknown to this
# vintage of config.sub. Echo the triple back verbatim so configure's
# host-system detection treats them as cross targets.
case $1 in
    wasm32-*-* | wasm64-*-*) echo "$1"; exit 0 ;;
esac

'''
# Insert immediately after the shebang+header — find the first
# `case $# in` and put our shim above it.
marker = 'case $# in'
i = text.find(marker)
if i < 0:
    sys.exit('config.sub: did not find expected case-statement marker')
new = text[:i] + shim + text[i:]
p.write_text(new)
print('config.sub: wasm short-circuit installed')
PY
    fi

    # ── wasm32 strict-call_indirect patch ──────────────────────────────
    # zsh's hook-dispatch (Src/module.c::runhookdef) does call_indirect
    # with the canonical Hookfn signature `int(Hookdef, void *)`. Two of
    # the functions registered as hooks in Src/Zle/complete.c — namely
    # `invalidate_list` and `accept_last` — are actually declared
    # `int(void)`. Native targets accept the (Hookfn) cast (the calling
    # convention tolerates extra args sitting in registers/stack), but
    # wasm3 type-checks every call_indirect against the table entry's
    # *real* function type, and traps with "indirect call type mismatch"
    # the moment one of these hooks fires (e.g. the first key press in
    # zle).
    #
    # Fix: at boot/cleanup time, register thin trampolines that match
    # Hookfn's shape and forward to the bare functions. Cheaper than
    # ripping out zle, surgical enough to keep zsh's behaviour identical.
    if [[ -f "$SRC/Src/Zle/complete.c" ]]; then
        python3 - "$SRC/Src/Zle/complete.c" <<'PY'
import sys, pathlib
p = pathlib.Path(sys.argv[1])
text = p.read_text()

trampolines = '''
/* yos/wasm32: thin Hookfn-shape wrappers around the bare-int(void) hook
 * functions. Without these, zle's first runhookdef call traps wasm3's
 * call_indirect type checker. See build-tools/wasm-pkg/configs/zsh/build.sh
 * for the rationale. */
static int yos_invalidate_list_hook(UNUSED(Hookdef d), UNUSED(void *x))
{ return invalidate_list(); }
static int yos_accept_last_hook(UNUSED(Hookdef d), UNUSED(void *x))
{ return accept_last(); }

'''

# boot_ is preceded in the source by:
#     /**/
#     int
#     boot_(Module m)
#     {
# The /**/ + int + name lines belong together — they're the
# prototype-generator marker that makepro.awk turns into the entry in
# complete.pro. We must insert our trampolines BEFORE that block, not
# between the `int` and `boot_` lines (which would create
# `int <trampolines> int boot_` — a syntax error).
marker_boot = '/**/\nint\nboot_(Module m)\n{\n'
i = text.find(marker_boot)
if i < 0:
    sys.exit('complete.c: did not find boot_ /**/+int marker')
text = text[:i] + trampolines + text[i:]

text = text.replace(
    'addhookfunc("invalidate_list", (Hookfn) invalidate_list);',
    'addhookfunc("invalidate_list", yos_invalidate_list_hook);')
text = text.replace(
    'deletehookfunc("invalidate_list", (Hookfn) invalidate_list);',
    'deletehookfunc("invalidate_list", yos_invalidate_list_hook);')
text = text.replace(
    'addhookfunc("accept_completion", (Hookfn) accept_last);',
    'addhookfunc("accept_completion", yos_accept_last_hook);')
text = text.replace(
    'deletehookfunc("accept_completion", (Hookfn) accept_last);',
    'deletehookfunc("accept_completion", yos_accept_last_hook);')

p.write_text(text)
print('complete.c: Hookfn trampolines installed (invalidate_list, accept_last)')
PY
    fi

    touch "$SRC/.extracted"
fi

BLD="$WORK/cmake-build"
rm -rf "$BLD"; mkdir -p "$BLD"
cd "$BLD"

# ── config.cache pre-seed ───────────────────────────────────────────
# Cross-compile with autoconf has two compounded problems:
#   1. Run-time tests can't run (no wasm runner during configure).
#   2. AC_CHECK_FUNCS LINKS a test program; with --allow-undefined in
#      our LDFLAGS, every unresolved symbol "exists". Configure ends
#      up answering "yes" to functions yos has no idea about
#      (faccessx → AIX, sigsetjmp → glibc-ism, etc.) and the build
#      then fails on the actual compile of source guarded by
#      HAVE_FACCESSX et al.
# Seed both kinds of answers below. Anything not pre-seeded is left
# to configure's own probing.
CACHE="$BLD/config.cache"
cat > "$CACHE" <<'CACHE_EOF'
# zsh-specific cache entries that need answers when cross-compiling.
zsh_cv_sys_getpwnam_faked=no
zsh_cv_sys_getpwuid_faked=no
zsh_cv_sys_path_max=4096
zsh_cv_sys_pathconf=yes
zsh_cv_sys_dynamic_clash_ok=no
zsh_cv_sys_dynamic_rtld_global=no
zsh_cv_sys_dynamic_strip_lib=no
zsh_cv_sys_dynamic_strip_exe=no
# Numeric overflow trap behaviour — assume signed-overflow does NOT
# trap (true on every yos-supported host).
zsh_cv_c_signed_overflow_traps=no
# `printf %a` for IEEE doubles — yos's printf supports it.
zsh_cv_printf_has_a=yes
# cygwin-only quirks — no.
zsh_cv_path_kill_supported=yes
# Variadic prototypes — every clang we ship has them.
zsh_cv_c_prototypes=yes
# malloc-of-zero — POSIX-mandated, our libc returns a valid ptr.
ac_cv_func_malloc_0_nonnull=yes
ac_cv_func_realloc_0_nonnull=yes
# /dev/fd existence — we don't have it under yos's vfs.
zsh_cv_sys_fifo=yes
zsh_cv_sys_dev_fd=no
# tcsetpgrp returns 0 on success — true on yos via the host bridge.
zsh_cv_func_tcsetpgrp=yes

# ── AC_CHECK_FUNCS overrides — explicit "no" for fns yos doesn't
#    bridge. Without these, --allow-undefined makes link tests
#    succeed and the build picks #ifdef HAVE_X paths it can't compile.
# AIX-only:
ac_cv_func_faccessx=no
# Linux/glibc-ism:
ac_cv_func_setresuid=no
ac_cv_func_setresgid=no
ac_cv_func_getresuid=no
ac_cv_func_getresgid=no
ac_cv_func_initgroups=no
ac_cv_func_setpwfile=no
# Wide-char locale machinery — disabled with --disable-multibyte but
# also seed to avoid subtle pulls.
ac_cv_func_iswprint=no
ac_cv_func_wctomb=no
ac_cv_func_mbrtowc=no
# Capability/credential APIs we don't have:
ac_cv_func_cap_get_proc=no
ac_cv_func_pthread_setname_np=no
# Misc fns that occasionally surface:
ac_cv_func_clock_gettime=yes
ac_cv_func_nanosleep=yes
ac_cv_func_mmap=no
# OpenBSD-only:
ac_cv_func_srand_deterministic=no
ac_cv_func_canonicalize_file_name=no
ac_cv_func_getxattr=no
ac_cv_func_cygwin_conv_path=no
# Linux-only:
ac_cv_func_setutxent=no
ac_cv_func_getutxent=no
ac_cv_func_endutxent=no
ac_cv_func_getutent=no
ac_cv_func_gdbm_open=no
# zsh's getenv-related — let it use plain getenv
ac_cv_func_getenv_r=no
# Linux-only prctl()
ac_cv_func_prctl=no
ac_cv_header_sys_prctl_h=no
# Linux-only filesystem APIs
ac_cv_func_statvfs=no
ac_cv_func_fstatvfs=no
# Optional ipv6 socket interfaces
ac_cv_func_getifaddrs=no
# pty allocation (we have openpty in libutil but it's stubbed)
ac_cv_func_openpty=no
ac_cv_func_grantpt=no
# random-source preferences
ac_cv_func_getrandom=no
ac_cv_func_arc4random=yes
# malloc internals
ac_cv_func_malloc_size=no
ac_cv_func_malloc_usable_size=yes
# Hostname resolution
ac_cv_func_gethostbyname2=no
ac_cv_func_inet_aton=yes
# misc
ac_cv_func_sigsetjmp=no
ac_cv_func_realpath=yes

# AC_SEARCH_LIBS fallbacks. zsh's configure pattern: if AC_CHECK_FUNCS
# returns "no", it then runs AC_SEARCH_LIBS for "library containing X".
# With --allow-undefined that always says "found in library: none
# required" and the fn ends up defined as HAVE_X anyway. Block them.
ac_cv_search_prctl=no
ac_cv_search_setresuid=no
ac_cv_search_setresgid=no
ac_cv_search_getresuid=no
ac_cv_search_getresgid=no
ac_cv_search_initgroups=no
ac_cv_search_faccessx=no
ac_cv_search_setpwfile=no
ac_cv_search_canonicalize_file_name=no
ac_cv_search_getxattr=no
ac_cv_search_cygwin_conv_path=no
ac_cv_search_setutxent=no
ac_cv_search_getutxent=no
ac_cv_search_endutxent=no
ac_cv_search_getutent=no
ac_cv_search_gdbm_open=no
ac_cv_search_srand_deterministic=no
ac_cv_search_statvfs=no
ac_cv_search_fstatvfs=no
ac_cv_search_getifaddrs=no
ac_cv_search_openpty=no
ac_cv_search_grantpt=no
ac_cv_search_getrandom=no
ac_cv_search_iswprint=no
ac_cv_search_wctomb=no
ac_cv_search_mbrtowc=no
ac_cv_search_cap_get_proc=no
ac_cv_search_pthread_setname_np=no
ac_cv_search_gethostbyname2=no
ac_cv_search_sigsetjmp=no
ac_cv_search_malloc_size=no

# termcap / curses — yos's sysroot provides a stub <termcap.h> +
# libyos_stubs has no-op tgetent/tgoto/tputs implementations. Tell
# zsh: the functions exist (so tgoto/tputs prototypes are valid),
# the header exists (so prompt.c can include termcap.h), and no
# extra library is needed. Runtime: the stubs make zsh's "no
# terminal" fallback path do the right thing (raw text output).
ac_cv_func_tgetent=yes
ac_cv_func_tputs=yes
ac_cv_func_tgoto=yes
ac_cv_func_tgetstr=yes
ac_cv_func_tgetnum=yes
ac_cv_func_tgetflag=yes
ac_cv_search_tgetent="none required"
ac_cv_search_tputs="none required"
ac_cv_search_tgoto="none required"
ac_cv_search_tgetstr="none required"
ac_cv_search_tgetnum="none required"
ac_cv_search_tgetflag="none required"
ac_cv_header_termcap_h=yes
# We don't ship terminfo / ncurses, so setupterm/del_curterm are off.
ac_cv_func_setupterm=no
ac_cv_func_del_curterm=no
ac_cv_search_setupterm=no
ac_cv_search_del_curterm=no
# Same idea for ncurses-specific extensions:
ac_cv_func_use_default_colors=no
ac_cv_search_use_default_colors=no
CACHE_EOF

# ── configure ──────────────────────────────────────────────────────
# Cross-compilation triple is wasm32-unknown-unknown. We disable
# everything that needs runtime tty access (zle/multibyte/zftp) and
# everything that needs dlopen (--disable-dynamic).
CFLAGS_W="$WASM_CFLAGS -D_GNU_SOURCE -D__FreeBSD__=14"
# Make's link rule doesn't always include CFLAGS, so the wasm-target
# args need to also be in LDFLAGS — otherwise clang dispatches to
# the host's ld instead of wasm-ld AND auto-adds the host's crt1.
# `-nostdlib` stops clang from injecting its default crt + libclang_rt.
LDFLAGS_W="-target wasm32-unknown-unknown -nostdlib $WASM_LDFLAGS \
    $WASM_SYSROOT/usr/lib/crt1.o \
    -L$WASM_SYSROOT/usr/lib"

CC="$WASM_CC" \
CFLAGS="$CFLAGS_W" \
CPPFLAGS="$CFLAGS_W" \
LDFLAGS="$LDFLAGS_W" \
LIBS="-lc -lyos_stubs" \
CPP="$WASM_CC -E $CFLAGS_W" \
"$SRC/configure" \
    --enable-etcdir="$PREFIX/etc" \
    --enable-fndir="$PREFIX/share/zsh/${VERSION}/functions" \
    --host=wasm32-unknown-unknown \
    --prefix="$PREFIX" \
    --cache-file="$CACHE" \
    --disable-dynamic \
    --disable-zle \
    --disable-multibyte \
    --disable-cap \
    --disable-gdbm \
    --disable-pcre \
    --without-tcsetpgrp \
    --without-term-lib \
    --without-curses-terminfo \
    --enable-static \
    --disable-libc-musl \
    || { echo "[$NAME] configure failed; check $BLD/config.log"; tail -50 "$BLD/config.log"; exit 1; }

# ── build ──────────────────────────────────────────────────────────
make -j"$(nproc 2>/dev/null || echo 4)" || {
    echo "[$NAME] make failed"
    exit 1
}

# ── install ────────────────────────────────────────────────────────
# zsh's install target wants directories owned + execute perms; we
# install just the resulting Src/zsh wasm binary.
#
# Apply binaryen's --asyncify pass: yos's fork() is implemented as a
# memory snapshot + asyncify rewind on the parent and a fresh
# pthread/runtime on the child. Without an asyncify-instrumented
# binary, yos_fork returns -ENOSYS, zsh's `cmd1 | cmd2` and
# `x=$(cmd)` paths can't actually fork their right-hand side, and
# the shell wedges on every pipe / command-substitution. Same pass
# nvim and the freebsd-tools use.
mkdir -p "$PREFIX/bin"
wasm-opt --asyncify -O2 "$BLD/Src/zsh" -o "$PREFIX/bin/zsh.wasm"

# ── autoload Functions/ tree ───────────────────────────────────────
# zsh's autoload-on-demand scripts live in Functions/<Section>/<name>
# in the source tree (Misc/is-at-least, Misc/zed, Newuser/zsh-newuser-
# install, …). Antidote and most plugin managers call `is-at-least
# 5.4.2` to gate on zsh version; without these files on FPATH the
# call fails with "function definition file not found", antidote
# bails, and the user's interactive ~/.zshrc never finishes loading.
# Install the whole tree under $PREFIX/share/zsh/<ver>/functions/ —
# the same layout stock zsh uses on Linux/macOS. fpath is set at
# zsh startup via /etc/zshenv (which we ALSO install below).
SHARE="$PREFIX/share/zsh/${VERSION}"
mkdir -p "$SHARE/functions"
# Flatten Functions/<Section>/* → functions/<name>. (zsh historically
# uses the per-section structure for organisation only; the actual
# fpath lookup is flat.)
for f in "$SRC"/Functions/*/*; do
    [ -f "$f" ] && cp "$f" "$SHARE/functions/$(basename "$f")"
done

# ── /etc/zshenv: bootstrap fpath ───────────────────────────────────
# zsh reads /etc/zshenv before anything else (even with NO_RCS). Set
# the system-wide fpath here so autoload finds our Functions/ even
# when the host's /etc/zshenv (which yos can't override per-guest)
# isn't suitable. Guests source this via the wasm sysroot's /etc.
mkdir -p "$PREFIX/etc"
cat > "$PREFIX/etc/zshenv" <<ZSHENV_EOF
# yos wasm-zsh — system-wide environment.
# Prepend our autoload functions dir so antidote / oh-my-zsh /
# is-at-least and friends resolve even when the host \$fpath has
# entries pointing at incompatible function trees.
fpath=( $SHARE/functions \$fpath )
ZSHENV_EOF

cat > "$PREFIX/manifest.txt" <<EOF
name=zsh
version=${VERSION}
prefix=${PREFIX}
src=${SRC}
EOF

echo "[$NAME] installed → $PREFIX/bin/zsh.wasm"
