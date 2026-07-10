#!/usr/bin/env bash
# tmux — terminal multiplexer. autoconf cross-build to wasm32 against
# the yos sysroot + our wasm libevent. yos has no ncurses, so the recipe
# compiles a minimal terminfo/curses stub (libtinfo.a + curses headers)
# providing the symbols tmux links: setupterm/tigetstr/tigetnum/
# tigetflag/tparm/del_curterm/cur_term. Same tactic zsh uses for termcap
# — gets tmux to link and start; full terminfo rendering is iterative.
set -euo pipefail

NAME=tmux
VERSION=3.4
URL="https://github.com/tmux/tmux/releases/download/${VERSION}/tmux-${VERSION}.tar.gz"
SHA256="551ab8dea0bf505c0ad6b7bb35ef567cdde0ccb84357df142c254f35a23e19aa"
DEPS="libevent"

: "${ROOT:?}"; : "${PREFIX:?}"; : "${WASM_CC:?}"; : "${WASM_SYSROOT:?}"
: "${DEP_PREFIXES:?}"
: "${WORK:=$ROOT/build-linux/wasm-pkgs/${NAME}-${VERSION}}"

read LIBEVENT_PREFIX <<<"$DEP_PREFIXES"

mkdir -p "$WORK" "$PREFIX/bin"

TARBALL="$WORK/${NAME}-${VERSION}.tar.gz"
[[ -f "$TARBALL" ]] || curl -fsSL "$URL" -o "$TARBALL"
echo "${SHA256}  $TARBALL" | sha256sum -c - > /dev/null

SRC="$WORK/src"
if [[ ! -f "$SRC/.extracted" ]]; then
    rm -rf "$SRC"; mkdir -p "$SRC"
    tar -xzf "$TARBALL" -C "$SRC" --strip-components=1
    while IFS= read -r CS; do
        python3 - "$CS" <<'PY'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); t = p.read_text()
nl = t.find('\n') + 1
shim = 'case ${1:-} in\n    wasm32-*|wasm64-*) echo "$1"; exit 0 ;;\nesac\n'
p.write_text(t[:nl] + shim + t[nl:]); print("config.sub patched:", sys.argv[1])
PY
    done < <(find "$SRC" -name config.sub)

    # The client-side cmd parse (to detect CMD_STARTSERVER) may not flag
    # the server-start under wasm, so the server never launches and the
    # client just prints "error connecting" on a missing socket. Force the
    # client to always be willing to spawn a server: OR CLIENT_STARTSERVER
    # into client_flags unconditionally at the assignment site.
    if [[ -f "$SRC/client.c" ]]; then
        sed -i 's/^\tclient_flags = flags;/\tclient_flags = flags | CLIENT_STARTSERVER;/' "$SRC/client.c"
        grep -q 'client_flags = flags | CLIENT_STARTSERVER;' "$SRC/client.c" \
            && echo "client.c: forced CLIENT_STARTSERVER" \
            || { echo "ERROR: client.c STARTSERVER patch did not match"; exit 1; }
    fi

    # tmux's configure picks a per-platform forkpty compat unit; for our
    # host_os=unknown it asks for compat/forkpty-unknown.c, which doesn't
    # ship. Provide a stub (yos has no PTYs yet — forkpty returns ENOSYS;
    # tmux links and the server starts, pane creation degrades).
    # Real forkpty built on yos's bridged PTY primitives (posix_openpt /
    # grantpt / unlockpt / ptsname / open(slave) — all real under yos's
    # impl/io/pty.c). This is what lets tmux actually create panes.
    cat > "$SRC/compat/forkpty-unknown.c" <<'FPTY_EOF'
#include <sys/types.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

pid_t forkpty(int *amaster, char *name, struct termios *tio, struct winsize *ws)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) return -1;
    if (grantpt(master) < 0 || unlockpt(master) < 0) { close(master); return -1; }
    char *sname = ptsname(master);
    if (sname == NULL) { close(master); return -1; }
    int slave = open(sname, O_RDWR | O_NOCTTY);
    if (slave < 0) { close(master); return -1; }
    if (tio != NULL) tcsetattr(slave, TCSANOW, tio);
    if (ws != NULL) ioctl(slave, TIOCSWINSZ, ws);

    pid_t pid = fork();
    if (pid < 0) { close(master); close(slave); return -1; }
    if (pid == 0) {                 /* child becomes the pty slave session */
        close(master);
        setsid();
        ioctl(slave, TIOCSCTTY, (char *)0);
        dup2(slave, 0); dup2(slave, 1); dup2(slave, 2);
        if (slave > 2) close(slave);
        return 0;
    }
    close(slave);                   /* parent keeps the master */
    if (amaster != NULL) *amaster = master;
    if (name != NULL) strcpy(name, sname);
    return pid;
}
/* fdforkpty is provided by tmux's own compat/fdforkpty.c (it calls the
 * forkpty above) — do NOT define it here or wasm-ld sees a duplicate. */
FPTY_EOF
    touch "$SRC/.extracted"
fi

# ── minimal curses/terminfo stub (lib + headers) ────────────────────
STUB="$WORK/curses-stub"
rm -rf "$STUB"; mkdir -p "$STUB/include"
cat > "$STUB/tinfo.c" <<'STUB_EOF'
/* Minimal but REAL terminfo provider for tmux under yos. yos has no
 * ncurses and no compiled terminfo database, so instead of reading
 * $TERMINFO files we answer tigetstr/tigetnum/tigetflag from a built-in
 * capability table describing a 256-colour, xterm/screen-compatible
 * terminal. tmux's client queries these caps and ships them to the
 * server; the server needs at minimum `clear` and `cup` or it aborts
 * with "terminal does not support clear". The strings are deliberately
 * conditional-free (no %?..%t..%e..%;) so the small tparm() below — which
 * understands %%, %i, %pN, %d, %c, %{n}, %+ — can expand them all.
 * tputs/tgoto come from the sysroot's libyos_stubs (tputs really does
 * emit the bytes), so we omit them here. */
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct yos_term { int magic; } TERMINAL;
static TERMINAL yos_term_obj = { 1 };
TERMINAL *cur_term = &yos_term_obj;

int setupterm(const char *term, int fd, int *errret) {
    (void)term; (void)fd; if (errret) *errret = 1; return 0; /* OK */
}
TERMINAL *set_curterm(TERMINAL *t) { TERMINAL *o = cur_term; if (t) cur_term = t; return o; }
int del_curterm(TERMINAL *t) { (void)t; return 0; }
int restartterm(const char *term, int fd, int *errret) { (void)term; (void)fd; if (errret) *errret = 1; return 0; }

struct yos_cap { const char *name; const char *val; };

/* String capabilities. ESC is \033. Parameterised caps use the simple
 * terminfo ops only. 256-colour set via the always-valid 38;5/48;5 form
 * (works for the low 16 colours too, so no conditional is needed). */
static const struct yos_cap yos_str_caps[] = {
    { "clear", "\033[H\033[2J" },
    { "cup",   "\033[%i%p1%d;%p2%dH" },
    { "home",  "\033[H" },
    { "cuu1",  "\033[A" }, { "cud1", "\n" }, { "cuf1", "\033[C" }, { "cub1", "\b" },
    { "cuu",   "\033[%p1%dA" }, { "cud", "\033[%p1%dB" },
    { "cuf",   "\033[%p1%dC" }, { "cub", "\033[%p1%dD" },
    { "hpa",   "\033[%i%p1%dG" }, { "vpa", "\033[%i%p1%dd" },
    { "el",    "\033[K" }, { "el1", "\033[1K" }, { "ed", "\033[J" },
    { "ech",   "\033[%p1%dX" },
    { "ich",   "\033[%p1%d@" }, { "ich1", "\033[@" },
    { "dch",   "\033[%p1%dP" }, { "dch1", "\033[P" },
    { "il",    "\033[%p1%dL" }, { "il1", "\033[L" },
    { "dl",    "\033[%p1%dM" }, { "dl1", "\033[M" },
    { "ri",    "\033M" }, { "ind", "\n" },
    { "indn",  "\033[%p1%dS" }, { "rin", "\033[%p1%dT" },
    { "csr",   "\033[%i%p1%d;%p2%dr" },
    { "bold",  "\033[1m" }, { "dim", "\033[2m" },
    { "smul",  "\033[4m" }, { "rmul", "\033[24m" },
    { "blink", "\033[5m" }, { "rev", "\033[7m" }, { "invis", "\033[8m" },
    { "smso",  "\033[7m" }, { "rmso", "\033[27m" },
    { "sgr0",  "\033[m" }, { "op", "\033[39;49m" },
    { "setaf", "\033[38;5;%p1%dm" }, { "setab", "\033[48;5;%p1%dm" },
    { "smcup", "\033[?1049h" }, { "rmcup", "\033[?1049l" },
    { "civis", "\033[?25l" }, { "cnorm", "\033[?25h" }, { "cvvis", "\033[?25h" },
    { "smkx",  "\033[?1h\033=" }, { "rmkx", "\033[?1l\033>" },
    { "bel",   "\007" }, { "cr", "\r" },
    { "smacs", "\033(0" }, { "rmacs", "\033(B" }, { "enacs", "\033(B\033)0" },
    { "acsc",  "``aaffggiijjkkllmmnnooppqqrrssttuuvvwwxxyyzz{{||}}~~" },
    /* key input (application-keypad form, matching smkx) */
    { "kcuu1", "\033OA" }, { "kcud1", "\033OB" },
    { "kcuf1", "\033OC" }, { "kcub1", "\033OD" },
    { "khome", "\033[1~" }, { "kend", "\033[4~" },
    { "kpp",   "\033[5~" }, { "knp", "\033[6~" },
    { "kich1", "\033[2~" }, { "kdch1", "\033[3~" },
    { "kbs",   "\177" }, { "kcbt", "\033[Z" }, { "kmous", "\033[M" },
    { "kf1", "\033OP" }, { "kf2", "\033OQ" }, { "kf3", "\033OR" }, { "kf4", "\033OS" },
    { "kf5", "\033[15~" }, { "kf6", "\033[17~" }, { "kf7", "\033[18~" },
    { "kf8", "\033[19~" }, { "kf9", "\033[20~" }, { "kf10", "\033[21~" },
    { "kf11", "\033[23~" }, { "kf12", "\033[24~" },
    { NULL, NULL }
};
static const struct { const char *name; int val; } yos_num_caps[] = {
    { "colors", 256 }, { "pairs", 32767 }, { "U8", 1 }, { "it", 8 },
    { NULL, 0 }
};
/* Flags reported present-and-true. Everything else is "absent". */
static const char *yos_flag_caps[] = { "am", "XT", "bce", "msgr", "mir", NULL };

char *tigetstr(const char *id) {
    for (const struct yos_cap *c = yos_str_caps; c->name; c++)
        if (strcmp(c->name, id) == 0) return (char *)c->val;
    return (char *)-1;   /* absent (canonical terminfo "cancelled/absent") */
}
int tigetnum(const char *id) {
    for (int i = 0; yos_num_caps[i].name; i++)
        if (strcmp(yos_num_caps[i].name, id) == 0) return yos_num_caps[i].val;
    return -1;           /* absent */
}
int tigetflag(const char *id) {
    for (int i = 0; yos_flag_caps[i]; i++)
        if (strcmp(yos_flag_caps[i], id) == 0) return 1;
    return -1;           /* absent */
}

/* tparm — expand a (conditional-free) terminfo parameter string. Handles
 * %%  %i  %pN  %d  %c  %{n}  %+  which is everything our cap table uses. */
static char yos_tparm_buf[256];
static char *yos_tparm_apply(const char *s, long *p) {
    char *out = yos_tparm_buf;
    size_t cap = sizeof(yos_tparm_buf), n = 0;
    long stack[32]; int sp = 0;
    if (!s || s == (char *)-1) { out[0] = 0; return out; }
    while (*s && n + 1 < cap) {
        if (*s != '%') { out[n++] = *s++; continue; }
        s++;
        switch (*s) {
        case '%': out[n++] = '%'; s++; break;
        case 'i': p[0]++; p[1]++; s++; break;
        case 'p': { int idx = s[1] - '1'; if (idx >= 0 && idx < 9 && sp < 32) stack[sp++] = p[idx]; s += 2; break; }
        case 'd': { long v = sp ? stack[--sp] : 0; int w = snprintf(out + n, cap - n, "%ld", v); if (w > 0) n += (size_t)w; s++; break; }
        case 'c': { long v = sp ? stack[--sp] : 0; out[n++] = (char)v; s++; break; }
        case '{': { long v = 0; s++; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); if (*s == '}') s++; if (sp < 32) stack[sp++] = v; break; }
        case '+': { long b = sp ? stack[--sp] : 0, a = sp ? stack[--sp] : 0; if (sp < 32) stack[sp++] = a + b; s++; break; }
        default: s++; break;   /* skip unsupported op rather than emit garbage */
        }
    }
    out[n] = 0;
    return out;
}
char *tparm(const char *s, ...) {
    long p[9] = {0};
    va_list ap; va_start(ap, s);
    for (int i = 0; i < 9; i++) p[i] = va_arg(ap, int);
    va_end(ap);
    return yos_tparm_apply(s, p);
}
char *tiparm(const char *s, ...) {
    long p[9] = {0};
    va_list ap; va_start(ap, s);
    for (int i = 0; i < 9; i++) p[i] = va_arg(ap, int);
    va_end(ap);
    return yos_tparm_apply(s, p);
}
char *tparm_s(const char *s, ...) {
    long p[9] = {0};
    va_list ap; va_start(ap, s);
    for (int i = 0; i < 9; i++) p[i] = va_arg(ap, int);
    va_end(ap);
    return yos_tparm_apply(s, p);
}
char *tiparm_s(int a, int b, const char *s, ...) {
    (void)a; (void)b;
    long p[9] = {0};
    va_list ap; va_start(ap, s);
    for (int i = 0; i < 9; i++) p[i] = va_arg(ap, int);
    va_end(ap);
    return yos_tparm_apply(s, p);
}
STUB_EOF

cat > "$STUB/include/term.h" <<'STUB_EOF'
#ifndef YOS_TERM_H
#define YOS_TERM_H
#ifndef OK
#define OK 0
#endif
#ifndef ERR
#define ERR (-1)
#endif
typedef struct yos_term { int magic; } TERMINAL;
extern TERMINAL *cur_term;
int setupterm(const char *, int, int *);
TERMINAL *set_curterm(TERMINAL *);
int del_curterm(TERMINAL *);
int restartterm(const char *, int, int *);
int tigetflag(const char *);
int tigetnum(const char *);
char *tigetstr(const char *);
char *tparm(const char *, ...);
char *tiparm(const char *, ...);
char *tparm_s(const char *, ...);
char *tiparm_s(int, int, const char *, ...);
int tputs(const char *, int, int (*)(int));
char *tgoto(const char *, int, int);
#endif
STUB_EOF
# tmux includes <ncurses.h>/<curses.h> for COLS/LINES etc.
cat > "$STUB/include/ncurses.h" <<'STUB_EOF'
#ifndef YOS_NCURSES_H
#define YOS_NCURSES_H
#include <term.h>
#define KEY_CODE_YES 0400
extern int COLS, LINES;
#endif
STUB_EOF
cp "$STUB/include/ncurses.h" "$STUB/include/curses.h"
# Force-declare the PTY compat functions (tmux's compat.h gating doesn't
# expose them for host_os=unknown). -include'd into every TU.
cat > "$STUB/include/yos_pty.h" <<'STUB_EOF'
#ifndef YOS_PTY_H
#define YOS_PTY_H
#include <sys/types.h>
struct termios; struct winsize;
pid_t forkpty(int *, char *, struct termios *, struct winsize *);
pid_t fdforkpty(int, int *, char *, struct termios *, struct winsize *);
#endif
STUB_EOF
cat >> "$STUB/tinfo.c" <<'STUB_EOF'
int COLS = 80, LINES = 24;
STUB_EOF

"$WASM_CC" -target wasm32-unknown-unknown -nostdlib -O2 $WASM_CFLAGS \
    -I"$STUB/include" -c "$STUB/tinfo.c" -o "$STUB/tinfo.o"
"${YOS_LLVM_AR:-llvm-ar}" rcs "$STUB/libtinfo.a" "$STUB/tinfo.o"

# ── execl/execlp/execle shims ───────────────────────────────────────
# yos bridges the array-form exec calls (execv/execvp/execve) but NOT
# the variadic list-form ones. The guest libc declares execl* without a
# body, so --allow-undefined turns each into an env.execl* import that
# traps at runtime ("unresolved import env.execl") — which is exactly
# what tmux's spawn_pane hits when it launches a pane's shell. Provide
# real guest-side definitions that pack the va-list into an argv array
# and dispatch to the bridged array-form call.
cat > "$STUB/exec_compat.c" <<'EXEC_EOF'
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

/* execl/execlp: args are arg0, arg1, …, (char *)NULL. */
static int yos_exec_list(const char *path, const char *arg0, va_list ap,
                         int use_path)
{
    size_t cap = 8, n = 1;
    char **argv = malloc(cap * sizeof(char *));
    if (argv == NULL) return -1;
    argv[0] = (char *)arg0;
    char *a;
    do {
        a = va_arg(ap, char *);
        if (n + 1 > cap) {
            cap *= 2;
            char **grown = realloc(argv, cap * sizeof(char *));
            if (grown == NULL) { free(argv); return -1; }
            argv = grown;
        }
        argv[n++] = a;          /* the terminating NULL is stored too */
    } while (a != NULL);
    int rc = use_path ? execvp(path, argv) : execv(path, argv);
    free(argv);                 /* only reached if exec failed */
    return rc;
}

int execl(const char *path, const char *arg0, ...)
{
    va_list ap; va_start(ap, arg0);
    int rc = yos_exec_list(path, arg0, ap, 0);
    va_end(ap);
    return rc;
}

int execlp(const char *file, const char *arg0, ...)
{
    va_list ap; va_start(ap, arg0);
    int rc = yos_exec_list(file, arg0, ap, 1);
    va_end(ap);
    return rc;
}

int execle(const char *path, const char *arg0, ...)
{
    va_list ap; va_start(ap, arg0);
    size_t cap = 8, n = 1;
    char **argv = malloc(cap * sizeof(char *));
    if (argv == NULL) { va_end(ap); return -1; }
    argv[0] = (char *)arg0;
    char *a;
    do {
        a = va_arg(ap, char *);
        if (n + 1 > cap) {
            cap *= 2;
            char **grown = realloc(argv, cap * sizeof(char *));
            if (grown == NULL) { free(argv); va_end(ap); return -1; }
            argv = grown;
        }
        argv[n++] = a;
    } while (a != NULL);
    char **envp = va_arg(ap, char **);   /* envp follows the NULL */
    va_end(ap);
    int rc = execve(path, argv, envp);
    free(argv);
    return rc;
}
EXEC_EOF
"$WASM_CC" -target wasm32-unknown-unknown -nostdlib -O2 $WASM_CFLAGS \
    -I"$STUB/include" -c "$STUB/exec_compat.c" -o "$STUB/exec_compat.o"
# tmux's configure may resolve the curses lib to any of these names;
# provide the stub under every alias so the final link finds it.
for alias in libncurses libncursesw libtinfow libcurses; do
    cp "$STUB/libtinfo.a" "$STUB/${alias}.a"
done

# ── configure (in-tree: tmux's compat/ headers are found relative to
# the source dir; an out-of-tree build misses imsg.h etc.) ───────────
BLD="$SRC"; cd "$BLD"
make distclean 2>/dev/null || true

CFLAGS_W="$WASM_CFLAGS -D_GNU_SOURCE -D__FreeBSD__=14 \
    -include arpa/inet.h -include "$STUB/include/yos_pty.h" \
    -I$SRC -I$SRC/compat \
    -I$LIBEVENT_PREFIX/include -I$STUB/include"
# yos_libc_init.o initialises the FreeBSD _DefaultRuneLocale C-locale
# ctype table. Without it _CurrentRuneLocale is NULL and the <ctype.h>
# macros (isalnum/isdigit/…) all return 0 — which makes tmux's arg parser
# reject every flag ("invalid flag -N") and fatal in key_bindings_init.
LDFLAGS_W="-target wasm32-unknown-unknown -nostdlib $WASM_LDFLAGS \
    $WASM_SYSROOT/usr/lib/crt1.o \
    $WASM_SYSROOT/usr/lib/yos_libc_init.o \
    $STUB/exec_compat.o \
    -L$WASM_SYSROOT/usr/lib -L$LIBEVENT_PREFIX/lib -L$STUB"

CACHE="$BLD/config.cache"
cat > "$CACHE" <<'CACHE_EOF'
ac_cv_func_setupterm=yes
ac_cv_search_setupterm="none required"
ac_cv_lib_tinfo_setupterm=yes
ac_cv_func_tparm=yes
ac_cv_header_curses_h=yes
ac_cv_header_ncurses_h=yes
ac_cv_header_term_h=yes
ac_cv_func_forkpty=no
ac_cv_func_openpty=no
ac_cv_func_setproctitle=no
ac_cv_func_getrandom=no
ac_cv_func_arc4random=yes
ac_cv_func_arc4random_buf=no
ac_cv_func_clock_gettime=yes
ac_cv_func_eventfd=no
ac_cv_func_kqueue=no
ac_cv_func_epoll_create=no
ac_cv_func_sysconf=yes
ac_cv_func_cfmakeraw=yes
ac_cv_func_strlcpy=yes
ac_cv_func_strlcat=yes
ac_cv_func_reallocarray=no
ac_cv_func_recallocarray=no
ac_cv_func_getentropy=no
# --allow-undefined makes configure's link tests pass for functions yos
# doesn't have; force "no" so tmux compiles its own compat/ versions.
ac_cv_func_getdtablecount=no
ac_cv_func_getdtablesize=no
ac_cv_func_htonll=no
ac_cv_func_ntohll=no
ac_cv_func_freezero=no
ac_cv_func_explicit_bzero=no
ac_cv_func_prctl=no
ac_cv_func_proc_pidinfo=no
ac_cv_func_getpeerucred=no
ac_cv_func_getpeereid=no
ac_cv_func_closefrom=no
ac_cv_func_fgetln=no
ac_cv_func_memmem=no
ac_cv_func_setproctitle=no
ac_cv_func_b64_ntop=no
ac_cv_func_strtonum=no
ac_cv_func_fdforkpty=no
ac_cv_func_daemon=no
# BSD string-visual-encoding family — yos has none; force tmux to build
# its compat/vis.c + unvis.c (otherwise they become unresolved env
# imports that trap at runtime: "unresolved import env.stravis").
ac_cv_func_vis=no
ac_cv_func_strvis=no
ac_cv_func_strnvis=no
ac_cv_func_strvisx=no
ac_cv_func_stravis=no
ac_cv_func_unvis=no
ac_cv_func_strunvis=no
ac_cv_func_strnunvis=no
# imsg: --allow-undefined makes the AC_SEARCH_LIBS link probe pass, so
# configure thinks libutil's imsg is present and skips tmux's bundled
# compat/imsg.c — leaving env.imsg_* as unresolved imports that trap at
# runtime. Force "not found" so the compat implementation is compiled in.
ac_cv_search_imsg_init=no
ac_cv_func_flock=yes
ac_cv_func_dirfd=yes
ac_cv_func_getline=yes
ac_cv_func_asprintf=yes
CACHE_EOF

CC="$WASM_CC" CFLAGS="$CFLAGS_W" CPPFLAGS="$CFLAGS_W" LDFLAGS="$LDFLAGS_W" \
LIBS="-levent -ltinfo -lc -lyos_stubs" CPP="$WASM_CC -E $CFLAGS_W" \
LIBEVENT_CFLAGS="-I$LIBEVENT_PREFIX/include" LIBEVENT_LIBS="-L$LIBEVENT_PREFIX/lib -levent" \
"$SRC/configure" \
    --host=wasm32-unknown-unknown \
    --prefix="$PREFIX" \
    --cache-file="$CACHE" \
    --disable-utf8proc \
    --enable-static \
    || { echo "[$NAME] configure failed; tail config.log:"; tail -70 "$BLD/config.log"; exit 1; }

make -j"$(nproc 2>/dev/null || echo 4)" tmux \
    || { echo "[$NAME] make failed"; exit 1; }

mkdir -p "$PREFIX/bin"
wasm-opt --asyncify -O2 "$BLD/tmux" -o "$PREFIX/bin/tmux.wasm"

cat > "$PREFIX/manifest.txt" <<EOF
name=tmux
version=${VERSION}
prefix=${PREFIX}
src=${SRC}
EOF
echo "[$NAME] installed → $PREFIX/bin/tmux.wasm"
