/* impl/pwd.c — getpwuid / getpwnam / getlogin and friends.
 *
 * Hand-written because FreeBSD's struct passwd is pointer-heavy
 * (5 char* fields) and each pointer must point INTO the wasm
 * guest's linear memory, not into glibc's static buffer. The
 * auto-bridge can't translate host pointers to wasm offsets.
 *
 * Approach mirrors POSIX: each call returns a pointer to a per-
 * ctx static buffer that holds the struct + all string contents.
 * The buffer is allocated once via yos_malloc() and reused on
 * every subsequent call — same semantics as libc's static
 * `_pw` storage, with the bonus that yos_malloc puts the buffer
 * at a stable wasm offset that the guest can dereference.
 *
 * The user's ssh bug is exactly this: yos used to have getpwuid
 * stubbed as -ENOSYS, so getpwuid(getuid()) returned NULL and
 * ssh printed "No user exists for uid 1000" before exec.
 *
 * FreeBSD wasm32 struct passwd layout (sizeof = 44):
 *   off  0: char *pw_name      (4 — wasm32 ptr)
 *   off  4: char *pw_passwd    (4)
 *   off  8: uid_t pw_uid       (4)
 *   off 12: gid_t pw_gid       (4)
 *   off 16: time_t pw_change   (4 — wasm32 time_t is 4 bytes)
 *   off 20: char *pw_class     (4)
 *   off 24: char *pw_gecos     (4)
 *   off 28: char *pw_dir       (4)
 *   off 32: char *pw_shell     (4)
 *   off 36: time_t pw_expire   (4)
 *   off 40: int pw_fields      (4)
 *
 * Same idea for struct group.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>          /* tmpfile, fclose, FILE */
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>          /* gai_strerror, hstrerror */
#include <signal.h>         /* strsignal — declared in <string.h>
                             * with _GNU_SOURCE but pulling signal
                             * via posix.h ensures it's seen. */
#include <locale.h>          /* setlocale */
#include <langinfo.h>        /* nl_langinfo */
#include <libgen.h>          /* dirname (POSIX), basename */

#include "yos/types.h"
#include <yos/ytrace/ytrace.h>

/* Forward decls for the auto-generated struct converter we use here.
 * Defined in build/<host>/src/yos/codegen/yos_struct_convert.c via
 * codegen/struct_convert.py. We don't #include the header because
 * impl/*.c don't have the codegen output dir on their -I path; the
 * forward decl + 44-byte size constant is all we need. */
extern void cv_tm_h2w(uint8_t *w, const struct tm *h);
extern void cv_tm_w2h(struct tm *h, const uint8_t *w);
#define CV_TM_GUEST_SZ 44u

extern uint32_t yos_malloc(struct yos_exec_ctx *ctx, uint32_t size);

#define WASM_PASSWD_SZ  44u
#define WASM_GROUP_SZ   16u   /* gr_name(4) + gr_passwd(4) + gr_gid(4) + gr_mem(4) */
#define PWD_BUF_SZ      512u
#define GRP_BUF_SZ      1024u
#define LOGIN_BUF_SZ    64u
#define UGNAME_BUF_SZ   64u   /* user_from_uid / group_from_gid scratch */

/* Per-process static storage anchors. We keep one wasm-side buffer
 * per kind, reused across calls — same lifetime contract as libc's
 * static `_pw` / `_gr` slots. Allocated lazily on first call so
 * yos_malloc has a real ctx to allocate from. */
/* ctx->pwd_anchors.pwd moved to ctx->pwd_anchors.pwd */
/* ctx->pwd_anchors.grp moved to ctx->pwd_anchors.grp */
/* ctx->pwd_anchors.login moved to ctx->pwd_anchors.login */
/* SEPARATE buffers for user_from_uid vs group_from_gid. ls prints both
 * via `printf("%s %s", user_from_uid(...), group_from_gid(...))` —
 * arg evaluation order writes user first, group second, both into the
 * same buffer if shared, then printf reads `user` AT PRINT TIME and
 * gets the group name back from the now-overwritten slot. Two anchors
 * keep the values stable for the duration of one printf call.
 *
 * ALL the *_buf_off anchors above are wasm-memory offsets — they are
 * meaningful ONLY inside the wasm memory of the proc that ensure_buf'd
 * them. After execve frees the old wasm memory and m3_NewRuntime
 * allocates fresh memory, those offsets are stale and writing to them
 * scribbles random bytes into the new program's heap. Two ls(1) calls
 * in a row in the same yos run hit exactly this: first ls allocates
 * the buffer, exits; second ls reuses the stale offset, corrupts
 * itself, traps. yos_pwd_post_execve_reset() (called from proc.c after
 * every m3_FreeRuntime, mirrors yos_env_post_execve_reset) wipes the
 * anchors so each new wasm process re-allocates fresh. */
/* ctx->pwd_anchors.ufu moved to ctx->pwd_anchors.ufu */
/* ctx->pwd_anchors.gfg moved to ctx->pwd_anchors.gfg */
static uint32_t ensure_buf(struct yos_exec_ctx *ctx,
                           uint32_t *anchor, uint32_t want)
{
    if (*anchor) return *anchor;
    uint32_t off = yos_malloc(ctx, want);
    if (!off) return 0;
    *anchor = off;
    return off;
}

/* Copy NUL-terminated string `s` into the wasm slab starting at
 * `slab_base + *cursor`, return the wasm offset of the copy and
 * advance the cursor past the trailing NUL. NULL/empty strings get
 * the empty literal at the slab's first byte. Caller MUST pre-place
 * "" at slab_base[0] and start cursor at 1 for that to be valid. */
static uint32_t pack_string(struct yos_exec_ctx *ctx,
                            uint32_t slab_base, uint32_t slab_cap,
                            uint32_t *cursor, const char *s)
{
    if (!s) s = "";
    size_t n = strlen(s) + 1;
    if (*cursor + n > slab_cap) {
        /* No room — point at the slab's empty-string sentinel. */
        return slab_base;
    }
    memcpy(ctx->memory + slab_base + *cursor, s, n);
    uint32_t off = slab_base + *cursor;
    *cursor += (uint32_t)n;
    return off;
}

/* Write the 44-byte wasm-shape struct passwd at wasm offset `out`
 * from a host `struct passwd`. String fields are appended after the
 * struct in the same buffer (passed via slab_base/cap/cursor). */
static void write_wasm_passwd(struct yos_exec_ctx *ctx,
                              uint32_t out,
                              uint32_t slab_base, uint32_t slab_cap,
                              uint32_t *cursor,
                              const struct passwd *pw)
{
    uint8_t *w = ctx->memory + out;
    /* All zero first so missing fields read clean. */
    memset(w, 0, WASM_PASSWD_SZ);
    *(uint32_t *)(w +  0) = pack_string(ctx, slab_base, slab_cap, cursor, pw->pw_name);
    *(uint32_t *)(w +  4) = pack_string(ctx, slab_base, slab_cap, cursor, pw->pw_passwd);
    *(uint32_t *)(w +  8) = (uint32_t)pw->pw_uid;
    *(uint32_t *)(w + 12) = (uint32_t)pw->pw_gid;
    /* pw_change @ off 16 — host glibc has no pw_change; leave 0. */
    *(uint32_t *)(w + 20) = slab_base;  /* pw_class — empty */
    *(uint32_t *)(w + 24) = pack_string(ctx, slab_base, slab_cap, cursor, pw->pw_gecos);
    *(uint32_t *)(w + 28) = pack_string(ctx, slab_base, slab_cap, cursor, pw->pw_dir);
    *(uint32_t *)(w + 32) = pack_string(ctx, slab_base, slab_cap, cursor, pw->pw_shell);
    /* pw_expire @ off 36 — leave 0. */
    /* pw_fields @ off 40: bits for which fields were filled. We
     * always set NAME / UID / GID / GECOS / DIR / SHELL. */
    *(uint32_t *)(w + 40) = (1u<<0) | (1u<<1) | (1u<<2) | (1u<<3) |
                            (1u<<6) | (1u<<7) | (1u<<8);
}

/* Wasm-shape struct passwd: layout = 44 bytes; field 0 = pw_name. */
uint32_t yos_getpwuid(struct yos_exec_ctx *ctx, uint32_t uid)
{
    struct passwd *pw = getpwuid((uid_t)uid);
    if (!pw) {
        ydebug("getpwuid(%u) -> NULL\n", uid);
        return 0;
    }
    uint32_t buf = ensure_buf(ctx, &ctx->pwd_anchors.pwd, PWD_BUF_SZ);
    if (!buf) { errno = ENOMEM; return 0; }
    /* Reserve the first byte for the empty-string sentinel that
     * pack_string returns when out of room. */
    ctx->memory[buf] = '\0';
    uint32_t cursor = WASM_PASSWD_SZ;
    if (cursor < 1) cursor = 1;
    write_wasm_passwd(ctx, buf, buf, PWD_BUF_SZ, &cursor, pw);
    ydebug("getpwuid(%u) -> name=%s uid=%u gid=%u dir=%s shell=%s\n",
           uid, pw->pw_name ? pw->pw_name : "(null)",
           pw->pw_uid, pw->pw_gid,
           pw->pw_dir  ? pw->pw_dir  : "(null)",
           pw->pw_shell? pw->pw_shell: "(null)");
    return buf;
}

uint32_t yos_getpwnam(struct yos_exec_ctx *ctx, uint32_t name_off)
{
    if (!name_off || name_off >= ctx->memory_size) return 0;
    const char *name = (const char *)(ctx->memory + name_off);
    struct passwd *pw = getpwnam(name);
    if (!pw) { ydebug("getpwnam(%s) -> NULL\n", name); return 0; }
    uint32_t buf = ensure_buf(ctx, &ctx->pwd_anchors.pwd, PWD_BUF_SZ);
    if (!buf) return 0;
    ctx->memory[buf] = '\0';
    uint32_t cursor = WASM_PASSWD_SZ;
    write_wasm_passwd(ctx, buf, buf, PWD_BUF_SZ, &cursor, pw);
    ydebug("getpwnam(%s) -> uid=%u\n", name, pw->pw_uid);
    return buf;
}

/* getpwuid_r / getpwnam_r — caller-supplied result buffer + scratch.
 *
 *   int getpwuid_r(uid_t uid, struct passwd *pwd, char *buf,
 *                  size_t buflen, struct passwd **result);
 *
 * Wasm: pwd_off, buf_off, buflen, result_off (uint32 each). On
 * success, *result = pwd. On not-found, *result = NULL and rc = 0.
 */
int32_t yos_getpwuid_r(struct yos_exec_ctx *ctx, uint32_t uid,
                       uint32_t pwd_off, uint32_t buf_off,
                       uint32_t buflen, uint32_t result_off)
{
    if (!pwd_off || !result_off ||
        pwd_off >= ctx->memory_size || result_off >= ctx->memory_size)
        return EFAULT;
    struct passwd *pw = getpwuid((uid_t)uid);
    *(uint32_t *)(ctx->memory + result_off) = 0;
    if (!pw) return 0;
    if (buflen < 16 || !buf_off) {
        return ERANGE;
    }
    uint32_t cursor = 0;
    /* tiny sentinel so pack_string fallback is valid */
    ctx->memory[buf_off] = '\0';
    cursor = 1;
    write_wasm_passwd(ctx, pwd_off, buf_off, buflen, &cursor, pw);
    *(uint32_t *)(ctx->memory + result_off) = pwd_off;
    return 0;
}

int32_t yos_getpwnam_r(struct yos_exec_ctx *ctx, uint32_t name_off,
                       uint32_t pwd_off, uint32_t buf_off,
                       uint32_t buflen, uint32_t result_off)
{
    if (!name_off || !pwd_off || !result_off) return EFAULT;
    const char *name = (const char *)(ctx->memory + name_off);
    struct passwd *pw = getpwnam(name);
    *(uint32_t *)(ctx->memory + result_off) = 0;
    if (!pw) return 0;
    if (buflen < 16 || !buf_off) return ERANGE;
    uint32_t cursor = 0;
    ctx->memory[buf_off] = '\0';
    cursor = 1;
    write_wasm_passwd(ctx, pwd_off, buf_off, buflen, &cursor, pw);
    *(uint32_t *)(ctx->memory + result_off) = pwd_off;
    return 0;
}

/* getpwent / setpwent / endpwent — sequential walk over /etc/passwd.
 * Most programs we care about (ssh, zsh) only call getpwuid/getpwnam,
 * but provide functional implementations so e.g. `passwd -l` style
 * tools that walk the database don't return NULL straight away. */
uint32_t yos_getpwent(struct yos_exec_ctx *ctx)
{
    struct passwd *pw = getpwent();
    if (!pw) return 0;
    uint32_t buf = ensure_buf(ctx, &ctx->pwd_anchors.pwd, PWD_BUF_SZ);
    if (!buf) return 0;
    ctx->memory[buf] = '\0';
    uint32_t cursor = WASM_PASSWD_SZ;
    write_wasm_passwd(ctx, buf, buf, PWD_BUF_SZ, &cursor, pw);
    return buf;
}

void yos_setpwent(struct yos_exec_ctx *ctx) { (void)ctx; setpwent(); }
void yos_endpwent(struct yos_exec_ctx *ctx) { (void)ctx; endpwent(); }

/* getlogin / getlogin_r — return the login name. Falls back to
 * `getpwuid(getuid())->pw_name` when getlogin returns NULL (the
 * usual "no controlling tty has a known login" case in CI / sandbox). */
uint32_t yos_getlogin(struct yos_exec_ctx *ctx)
{
    const char *l = getlogin();
    if (!l) {
        struct passwd *pw = getpwuid(getuid());
        if (pw && pw->pw_name) l = pw->pw_name;
        else l = "yos";
    }
    uint32_t buf = ensure_buf(ctx, &ctx->pwd_anchors.login, LOGIN_BUF_SZ);
    if (!buf) return 0;
    size_t n = strlen(l);
    if (n >= LOGIN_BUF_SZ) n = LOGIN_BUF_SZ - 1;
    memcpy(ctx->memory + buf, l, n);
    ctx->memory[buf + n] = '\0';
    return buf;
}

int32_t yos_getlogin_r(struct yos_exec_ctx *ctx, uint32_t buf_off,
                       uint32_t buflen)
{
    if (!buf_off) return EFAULT;
    const char *l = getlogin();
    if (!l) {
        struct passwd *pw = getpwuid(getuid());
        if (pw && pw->pw_name) l = pw->pw_name;
        else l = "yos";
    }
    size_t n = strlen(l);
    if (n + 1 > buflen) return ERANGE;
    memcpy(ctx->memory + buf_off, l, n + 1);
    return 0;
}

/* ── struct group ─────────────────────────────────────────────────
 *
 * FreeBSD wasm32 struct group layout (sizeof = 16):
 *   off  0: char  *gr_name      (4)
 *   off  4: char  *gr_passwd    (4)
 *   off  8: gid_t  gr_gid       (4)
 *   off 12: char **gr_mem       (4 — pointer to array of char*)
 *
 * gr_mem is a NULL-terminated array of member-name pointers. Each
 * char* in the array is itself a wasm offset. We pack the entire
 * structure + name strings + member-pointer array + member name
 * strings into one shared buffer. */

static void write_wasm_group(struct yos_exec_ctx *ctx,
                             uint32_t out,
                             uint32_t slab_base, uint32_t slab_cap,
                             uint32_t *cursor,
                             const struct group *gr)
{
    uint8_t *w = ctx->memory + out;
    memset(w, 0, WASM_GROUP_SZ);
    *(uint32_t *)(w + 0) = pack_string(ctx, slab_base, slab_cap, cursor, gr->gr_name);
    *(uint32_t *)(w + 4) = pack_string(ctx, slab_base, slab_cap, cursor, gr->gr_passwd);
    *(uint32_t *)(w + 8) = (uint32_t)gr->gr_gid;

    /* Build the gr_mem array. Count members first. */
    size_t nmem = 0;
    if (gr->gr_mem) while (gr->gr_mem[nmem]) nmem++;
    /* Align cursor to 4 bytes for the pointer array. */
    if (*cursor & 3) *cursor = (*cursor + 3) & ~3u;
    uint32_t arr_off = slab_base + *cursor;
    if (*cursor + (nmem + 1) * 4 > slab_cap) {
        /* Out of room for the array — write NULL. */
        return;
    }
    *cursor += (uint32_t)((nmem + 1) * 4);
    /* Copy each member name and write its offset into the array. */
    for (size_t i = 0; i < nmem; i++) {
        uint32_t off = pack_string(ctx, slab_base, slab_cap, cursor,
                                   gr->gr_mem[i]);
        *(uint32_t *)(ctx->memory + arr_off + i * 4) = off;
    }
    *(uint32_t *)(ctx->memory + arr_off + nmem * 4) = 0;  /* NULL term */
    *(uint32_t *)(w + 12) = arr_off;
}

uint32_t yos_getgrgid(struct yos_exec_ctx *ctx, uint32_t gid)
{
    struct group *gr = getgrgid((gid_t)gid);
    if (!gr) { ydebug("getgrgid(%u) -> NULL\n", gid); return 0; }
    uint32_t buf = ensure_buf(ctx, &ctx->pwd_anchors.grp, GRP_BUF_SZ);
    if (!buf) return 0;
    ctx->memory[buf] = '\0';
    uint32_t cursor = WASM_GROUP_SZ;
    write_wasm_group(ctx, buf, buf, GRP_BUF_SZ, &cursor, gr);
    ydebug("getgrgid(%u) -> name=%s\n", gid,
           gr->gr_name ? gr->gr_name : "(null)");
    return buf;
}

uint32_t yos_getgrnam(struct yos_exec_ctx *ctx, uint32_t name_off)
{
    if (!name_off || name_off >= ctx->memory_size) return 0;
    const char *name = (const char *)(ctx->memory + name_off);
    struct group *gr = getgrnam(name);
    if (!gr) return 0;
    uint32_t buf = ensure_buf(ctx, &ctx->pwd_anchors.grp, GRP_BUF_SZ);
    if (!buf) return 0;
    ctx->memory[buf] = '\0';
    uint32_t cursor = WASM_GROUP_SZ;
    write_wasm_group(ctx, buf, buf, GRP_BUF_SZ, &cursor, gr);
    return buf;
}

int32_t yos_getgrgid_r(struct yos_exec_ctx *ctx, uint32_t gid,
                       uint32_t grp_off, uint32_t buf_off,
                       uint32_t buflen, uint32_t result_off)
{
    if (!grp_off || !result_off) return EFAULT;
    struct group *gr = getgrgid((gid_t)gid);
    *(uint32_t *)(ctx->memory + result_off) = 0;
    if (!gr) return 0;
    if (buflen < 32 || !buf_off) return ERANGE;
    uint32_t cursor = 0;
    ctx->memory[buf_off] = '\0';
    cursor = 1;
    write_wasm_group(ctx, grp_off, buf_off, buflen, &cursor, gr);
    *(uint32_t *)(ctx->memory + result_off) = grp_off;
    return 0;
}

int32_t yos_getgrnam_r(struct yos_exec_ctx *ctx, uint32_t name_off,
                       uint32_t grp_off, uint32_t buf_off,
                       uint32_t buflen, uint32_t result_off)
{
    if (!name_off || !grp_off || !result_off) return EFAULT;
    const char *name = (const char *)(ctx->memory + name_off);
    struct group *gr = getgrnam(name);
    *(uint32_t *)(ctx->memory + result_off) = 0;
    if (!gr) return 0;
    if (buflen < 32 || !buf_off) return ERANGE;
    uint32_t cursor = 0;
    ctx->memory[buf_off] = '\0';
    cursor = 1;
    write_wasm_group(ctx, grp_off, buf_off, buflen, &cursor, gr);
    *(uint32_t *)(ctx->memory + result_off) = grp_off;
    return 0;
}

uint32_t yos_getgrent(struct yos_exec_ctx *ctx)
{
    struct group *gr = getgrent();
    if (!gr) return 0;
    uint32_t buf = ensure_buf(ctx, &ctx->pwd_anchors.grp, GRP_BUF_SZ);
    if (!buf) return 0;
    ctx->memory[buf] = '\0';
    uint32_t cursor = WASM_GROUP_SZ;
    write_wasm_group(ctx, buf, buf, GRP_BUF_SZ, &cursor, gr);
    return buf;
}

void yos_setgrent(struct yos_exec_ctx *ctx) { (void)ctx; setgrent(); }
void yos_endgrent(struct yos_exec_ctx *ctx) { (void)ctx; endgrent(); }

/* ── time-string family (gmtime / localtime / ctime / asctime) ───
 *
 * All four return `char *` or `struct tm *` to a static buffer in
 * libc. Same wasm-side problem as getpwuid: the host pointer can't
 * be returned directly. Allocate per-ctx static slots, copy into
 * them, return the wasm offset.
 *
 * Auto-bridge stubs them as -ENOSYS today (see hooks.yaml stub:);
 * here we route them via custom_proc instead. */

/* ctx->pwd_anchors.tm moved to ctx->pwd_anchors.tm */
/* ctx->pwd_anchors.timestr moved to ctx->pwd_anchors.timestr */
#define TIMESTR_BUF_SZ  64u

uint32_t yos_gmtime(struct yos_exec_ctx *ctx, uint32_t time_off)
{
    if (!time_off || time_off + 4 > ctx->memory_size) {
        errno = EFAULT; return 0;
    }
    time_t t = (time_t)*(int32_t *)(ctx->memory + time_off);
    struct tm tm;
    if (!gmtime_r(&t, &tm)) return 0;
    uint32_t buf = ensure_buf(ctx, &ctx->pwd_anchors.tm, CV_TM_GUEST_SZ);
    if (!buf) return 0;
    cv_tm_h2w(ctx->memory + buf, &tm);
    return buf;
}

uint32_t yos_localtime(struct yos_exec_ctx *ctx, uint32_t time_off)
{
    if (!time_off || time_off + 4 > ctx->memory_size) {
        errno = EFAULT; return 0;
    }
    time_t t = (time_t)*(int32_t *)(ctx->memory + time_off);
    struct tm tm;
    if (!localtime_r(&t, &tm)) return 0;
    uint32_t buf = ensure_buf(ctx, &ctx->pwd_anchors.tm, CV_TM_GUEST_SZ);
    if (!buf) return 0;
    cv_tm_h2w(ctx->memory + buf, &tm);
    return buf;
}

uint32_t yos_ctime(struct yos_exec_ctx *ctx, uint32_t time_off)
{
    if (!time_off || time_off + 4 > ctx->memory_size) {
        errno = EFAULT; return 0;
    }
    time_t t = (time_t)*(int32_t *)(ctx->memory + time_off);
    char hostbuf[64];
    if (!ctime_r(&t, hostbuf)) return 0;
    uint32_t buf = ensure_buf(ctx, &ctx->pwd_anchors.timestr, TIMESTR_BUF_SZ);
    if (!buf) return 0;
    size_t n = strlen(hostbuf);
    if (n >= TIMESTR_BUF_SZ) n = TIMESTR_BUF_SZ - 1;
    memcpy(ctx->memory + buf, hostbuf, n);
    ctx->memory[buf + n] = '\0';
    return buf;
}

int32_t yos_ctime_r(struct yos_exec_ctx *ctx, uint32_t time_off,
                    uint32_t buf_off)
{
    if (!time_off || !buf_off ||
        time_off + 4 > ctx->memory_size ||
        buf_off + 26 > ctx->memory_size) {
        errno = EFAULT; return 0;
    }
    time_t t = (time_t)*(int32_t *)(ctx->memory + time_off);
    char hostbuf[64];
    if (!ctime_r(&t, hostbuf)) return 0;
    size_t n = strlen(hostbuf);
    if (n >= 26) n = 25;
    memcpy(ctx->memory + buf_off, hostbuf, n);
    ctx->memory[buf_off + n] = '\0';
    /* ctime_r returns the buf pointer (= buf_off as wasm offset). */
    return (int32_t)buf_off;
}

uint32_t yos_asctime(struct yos_exec_ctx *ctx, uint32_t tm_off)
{
    if (!tm_off || tm_off + CV_TM_GUEST_SZ > ctx->memory_size) {
        errno = EFAULT; return 0;
    }
    /* Decode wasm-side struct tm into a host struct tm. */
    const uint8_t *w = ctx->memory + tm_off;
    struct tm tm = {0};
    tm.tm_sec   = *(int32_t *)(w +  0);
    tm.tm_min   = *(int32_t *)(w +  4);
    tm.tm_hour  = *(int32_t *)(w +  8);
    tm.tm_mday  = *(int32_t *)(w + 12);
    tm.tm_mon   = *(int32_t *)(w + 16);
    tm.tm_year  = *(int32_t *)(w + 20);
    tm.tm_wday  = *(int32_t *)(w + 24);
    tm.tm_yday  = *(int32_t *)(w + 28);
    tm.tm_isdst = *(int32_t *)(w + 32);
    char hostbuf[64];
    if (!asctime_r(&tm, hostbuf)) return 0;
    uint32_t buf = ensure_buf(ctx, &ctx->pwd_anchors.timestr, TIMESTR_BUF_SZ);
    if (!buf) return 0;
    size_t n = strlen(hostbuf);
    if (n >= TIMESTR_BUF_SZ) n = TIMESTR_BUF_SZ - 1;
    memcpy(ctx->memory + buf, hostbuf, n);
    ctx->memory[buf + n] = '\0';
    return buf;
}

int32_t yos_asctime_r(struct yos_exec_ctx *ctx, uint32_t tm_off,
                      uint32_t buf_off)
{
    if (!tm_off || !buf_off) { errno = EFAULT; return 0; }
    const uint8_t *w = ctx->memory + tm_off;
    struct tm tm = {0};
    tm.tm_sec   = *(int32_t *)(w +  0);
    tm.tm_min   = *(int32_t *)(w +  4);
    tm.tm_hour  = *(int32_t *)(w +  8);
    tm.tm_mday  = *(int32_t *)(w + 12);
    tm.tm_mon   = *(int32_t *)(w + 16);
    tm.tm_year  = *(int32_t *)(w + 20);
    tm.tm_wday  = *(int32_t *)(w + 24);
    tm.tm_yday  = *(int32_t *)(w + 28);
    tm.tm_isdst = *(int32_t *)(w + 32);
    char hostbuf[64];
    if (!asctime_r(&tm, hostbuf)) return 0;
    size_t n = strlen(hostbuf);
    if (n >= 26) n = 25;
    memcpy(ctx->memory + buf_off, hostbuf, n);
    ctx->memory[buf_off + n] = '\0';
    return (int32_t)buf_off;
}

/* ── string-returning helpers (static-buffer pattern) ────────────
 *
 *   strerror, gai_strerror, hstrerror, strsignal — all return
 *   a libc-internal `const char *`. Same wasm-side problem as
 *   getlogin: the host pointer can't cross the boundary; we copy
 *   to a per-ctx wasm buffer and return its offset.
 *
 *   The buffers are reused across calls, matching libc's static-
 *   storage semantics. Each function gets its own slot so calls
 *   don't trample each other.
 */

/* ctx->pwd_anchors.errstr moved to ctx->pwd_anchors.errstr */
/* ctx->pwd_anchors.gaistr moved to ctx->pwd_anchors.gaistr */
/* ctx->pwd_anchors.hstr moved to ctx->pwd_anchors.hstr */
/* ctx->pwd_anchors.signam moved to ctx->pwd_anchors.signam */
#define ERRSTR_BUF_SZ  128u

static uint32_t copy_const_to_wasm(struct yos_exec_ctx *ctx,
                                   uint32_t *anchor, const char *s)
{
    if (!s) s = "";
    uint32_t buf = ensure_buf(ctx, anchor, ERRSTR_BUF_SZ);
    if (!buf) return 0;
    size_t n = strlen(s);
    if (n >= ERRSTR_BUF_SZ) n = ERRSTR_BUF_SZ - 1;
    memcpy(ctx->memory + buf, s, n);
    ctx->memory[buf + n] = '\0';
    return buf;
}

uint32_t yos_strerror(struct yos_exec_ctx *ctx, int32_t errnum)
{
    return copy_const_to_wasm(ctx, &ctx->pwd_anchors.errstr, strerror(errnum));
}

uint32_t yos_gai_strerror(struct yos_exec_ctx *ctx, int32_t errnum)
{
    return copy_const_to_wasm(ctx, &ctx->pwd_anchors.gaistr, gai_strerror(errnum));
}

uint32_t yos_hstrerror(struct yos_exec_ctx *ctx, int32_t errnum)
{
    return copy_const_to_wasm(ctx, &ctx->pwd_anchors.hstr, hstrerror(errnum));
}

uint32_t yos_strsignal(struct yos_exec_ctx *ctx, int32_t signum)
{
    return copy_const_to_wasm(ctx, &ctx->pwd_anchors.signam, strsignal(signum));
}

/* ── more static-buffer string returners ────────────────────────
 *
 * Same pattern as strerror / gai_strerror: host returns a pointer to
 * libc-internal memory; we copy into a per-ctx wasm slot and return
 * the wasm offset. Each fn gets its own slot so calls don't trample
 * each other within a single statement.
 */

/* ctx->pwd_anchors.ttyname moved to ctx->pwd_anchors.ttyname */
/* ctx->pwd_anchors.ctermid moved to ctx->pwd_anchors.ctermid */
/* ctx->pwd_anchors.dirname moved to ctx->pwd_anchors.dirname */
/* ctx->pwd_anchors.l64a moved to ctx->pwd_anchors.l64a */
/* ctx->pwd_anchors.nl_langinfo moved to ctx->pwd_anchors.nl_langinfo */
/* ctx->pwd_anchors.setlocale moved to ctx->pwd_anchors.setlocale */
/* ctx->pwd_anchors.getwd moved to ctx->pwd_anchors.getwd */
/* ctx->pwd_anchors.tempnam moved to ctx->pwd_anchors.tempnam */
/* ctx->pwd_anchors.getusershell moved to ctx->pwd_anchors.getusershell */
#define TTYNAME_BUF_SZ   256u
#define CTERMID_BUF_SZ   64u
#define DIRNAME_BUF_SZ   1024u
#define L64A_BUF_SZ      8u
#define NL_LANGINFO_SZ   128u
#define SETLOCALE_SZ     64u
#define GETWD_BUF_SZ     1024u
#define TEMPNAM_BUF_SZ   512u

extern int yos_fd_get(struct yos_exec_ctx *ctx, int wfd);

uint32_t yos_ttyname(struct yos_exec_ctx *ctx, int32_t wfd)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return 0;
    return copy_const_to_wasm(ctx, &ctx->pwd_anchors.ttyname, ttyname(hfd));
}

int32_t yos_ttyname_r(struct yos_exec_ctx *ctx, int32_t wfd,
                      uint32_t buf_off, uint32_t buflen)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return EBADF;
    if (!buf_off || buflen == 0 ||
        (uint64_t)buf_off + (uint64_t)buflen > (uint64_t)ctx->memory_size)
        return EFAULT;
    return ttyname_r(hfd, (char *)(ctx->memory + buf_off), buflen);
}

uint32_t yos_ctermid(struct yos_exec_ctx *ctx, uint32_t s_off)
{
    /* If s_off is non-NULL, the result is written there (caller-
     * supplied buffer of L_ctermid bytes). Otherwise we use our slot. */
    char hostbuf[64];
    char *p = ctermid(hostbuf);
    if (!p) return 0;
    if (s_off) {
        if ((uint64_t)s_off + (uint64_t)strlen(p) + 1ULL > (uint64_t)ctx->memory_size) return 0;
        strcpy((char *)(ctx->memory + s_off), p);
        return s_off;
    }
    return copy_const_to_wasm(ctx, &ctx->pwd_anchors.ctermid, p);
}

/* dirname / basename — POSIX permits modifying the input buffer
 * in-place. Our auto-bridge handles `basename` via the
 * RET_OFFSET_INTO_FIRST classifier (returns a wasm offset INTO
 * the input). dirname is harder because it may rewrite the
 * input AND return a pointer to either the input or a static
 * buffer; allocate our own copy and dirname() into it. */
uint32_t yos_dirname(struct yos_exec_ctx *ctx, uint32_t path_off)
{
    if (!path_off || path_off >= ctx->memory_size) return 0;
    const char *src = (const char *)(ctx->memory + path_off);
    uint32_t buf = ensure_buf(ctx, &ctx->pwd_anchors.dirname, DIRNAME_BUF_SZ);
    if (!buf) return 0;
    size_t n = strnlen(src, DIRNAME_BUF_SZ - 1);
    memcpy(ctx->memory + buf, src, n);
    ctx->memory[buf + n] = 0;
    char *r = dirname((char *)(ctx->memory + buf));
    if (!r) return 0;
    /* dirname may return a pointer to a static "/" or "." instead
     * of into our buffer — copy whatever it returned into our slot
     * unconditionally so the wasm offset is always valid. */
    if (r != (char *)(ctx->memory + buf)) {
        size_t rn = strnlen(r, DIRNAME_BUF_SZ - 1);
        memcpy(ctx->memory + buf, r, rn);
        ctx->memory[buf + rn] = 0;
    }
    return buf;
}

/* getwd — old-style getcwd that uses caller's buffer (PATH_MAX
 * bytes). Linux: deprecated in favor of getcwd. Map to host
 * getcwd into the caller's buffer. */
uint32_t yos_getwd(struct yos_exec_ctx *ctx, uint32_t path_off)
{
    if (!path_off) return 0;
    char *p = (char *)(ctx->memory + path_off);
    if (path_off + 1024 > ctx->memory_size) return 0;
    if (!getcwd(p, 1024)) return 0;
    return path_off;
}

/* tempnam — generate a unique temp-file name. Returns a malloc'd
 * string; we copy it into our wasm slot. */
uint32_t yos_tempnam(struct yos_exec_ctx *ctx, uint32_t dir_off, uint32_t prefix_off)
{
    const char *dir    = dir_off    ? (const char *)(ctx->memory + dir_off)    : NULL;
    const char *prefix = prefix_off ? (const char *)(ctx->memory + prefix_off) : NULL;
    char *r = tempnam(dir, prefix);
    if (!r) return 0;
    uint32_t off = copy_const_to_wasm(ctx, &ctx->pwd_anchors.tempnam, r);
    free(r);
    return off;
}

/* mktemp — modify input template in place, return same buffer
 * (or empty string on failure). Linux deprecates in favor of
 * mkstemp; tools like sh's tempfile use it. */
uint32_t yos_mktemp(struct yos_exec_ctx *ctx, uint32_t template_off)
{
    if (!template_off || template_off >= ctx->memory_size) return 0;
    char *t = (char *)(ctx->memory + template_off);
    char *r = mktemp(t);
    if (!r) return 0;
    return template_off;  /* mktemp returned the input, in-place */
}

/* l64a — convert a 32-bit int to a 6-char base-64 string in a
 * libc-internal buffer. Used by /etc/passwd parsers historically. */
uint32_t yos_l64a(struct yos_exec_ctx *ctx, int32_t value)
{
    return copy_const_to_wasm(ctx, &ctx->pwd_anchors.l64a, l64a((long)value));
}

/* nl_langinfo — return locale-specific descriptive string for the
 * given item (e.g. CODESET, D_T_FMT, AM_STR). Returns "" or
 * placeholder when the item isn't supported.
 *
 * FreeBSD nl_item is a small contiguous enum (0..69). glibc uses
 * (LC_CTYPE<<16 | offset) — completely different values. The wasm
 * guest passes its FreeBSD enum value; we must translate to the
 * host's enum before calling host nl_langinfo. The table uses
 * symbolic glibc names so the same source compiles on Linux,
 * macOS (libSystem) and FreeBSD (where translation is identity). */
static int yos_freebsd_nl_item_to_host(int32_t fbsd_item)
{
    /* All host-side names live in <langinfo.h>; missing ones map
     * to -1 and we let the host return "" rather than a wrong key. */
    static const int xlat[] = {
        [0]  = CODESET,
        [1]  = D_T_FMT,   [2]  = D_FMT,    [3]  = T_FMT,    [4]  = T_FMT_AMPM,
        [5]  = AM_STR,    [6]  = PM_STR,
        [7]  = DAY_1,     [8]  = DAY_2,    [9]  = DAY_3,    [10] = DAY_4,
        [11] = DAY_5,     [12] = DAY_6,    [13] = DAY_7,
        [14] = ABDAY_1,   [15] = ABDAY_2,  [16] = ABDAY_3,  [17] = ABDAY_4,
        [18] = ABDAY_5,   [19] = ABDAY_6,  [20] = ABDAY_7,
        [21] = MON_1,     [22] = MON_2,    [23] = MON_3,    [24] = MON_4,
        [25] = MON_5,     [26] = MON_6,    [27] = MON_7,    [28] = MON_8,
        [29] = MON_9,     [30] = MON_10,   [31] = MON_11,   [32] = MON_12,
        [33] = ABMON_1,   [34] = ABMON_2,  [35] = ABMON_3,  [36] = ABMON_4,
        [37] = ABMON_5,   [38] = ABMON_6,  [39] = ABMON_7,  [40] = ABMON_8,
        [41] = ABMON_9,   [42] = ABMON_10, [43] = ABMON_11, [44] = ABMON_12,
        [45] = ERA,       [46] = ERA_D_FMT,[47] = ERA_D_T_FMT,[48] = ERA_T_FMT,
        [49] = ALT_DIGITS,[50] = RADIXCHAR,[51] = THOUSEP,
        [52] = YESEXPR,   [53] = NOEXPR,
        /* glibc lacks YESSTR (54), NOSTR (55), D_MD_ORDER (57), ALTMON_* (58..69). */
        [54] = -1,        [55] = -1,
        [56] = CRNCYSTR,
        [57] = -1,
        [58] = -1, [59] = -1, [60] = -1, [61] = -1, [62] = -1, [63] = -1,
        [64] = -1, [65] = -1, [66] = -1, [67] = -1, [68] = -1, [69] = -1,
    };
    if (fbsd_item < 0 || fbsd_item >= (int32_t)(sizeof(xlat)/sizeof(xlat[0])))
        return -1;
    return xlat[fbsd_item];
}

uint32_t yos_nl_langinfo(struct yos_exec_ctx *ctx, int32_t item)
{
    int host_item = yos_freebsd_nl_item_to_host(item);
    if (host_item < 0) return copy_const_to_wasm(ctx, &ctx->pwd_anchors.nl_langinfo, "");
    return copy_const_to_wasm(ctx, &ctx->pwd_anchors.nl_langinfo, nl_langinfo(host_item));
}

/* setlocale(category, locale) — query/set the locale.
 *
 * Per-ctx isolation: the previous impl called host setlocale() which
 * is process-wide → child's setlocale("POSIX") overwrote the parent's
 * locale state. The CLAUDE.md / types.h note flags this as a known
 * leak with a long-term fix via uselocale(); that path needs
 * <xlocale.h> on darwin and full coverage of every locale-sensitive
 * bridge (strftime, printf %.d in some locales, isalpha, …) to avoid
 * divergence between query-mode and actual behaviour.
 *
 * Pragmatic fix for now: route query+set through ctx->locale_name
 * without touching host state. Other guests/forks aren't disturbed,
 * and the FreeBSD-libc contract for setlocale() — "the returned
 * string identifies the locale that's now active" — is honoured for
 * query mode. Side effect: locale-sensitive functions still use the
 * host's default locale, which is "C" in nix-built yos. That matches
 * the previous behaviour for every guest that didn't explicitly call
 * setlocale — and the guests that DID set locale generally only do
 * so to confirm "C" is in effect (ssh, nvim). True per-ctx locale-
 * sensitive behaviour is a follow-up that requires per-bridge
 * uselocale() in every locale-aware impl/*.c entry. */
uint32_t yos_setlocale(struct yos_exec_ctx *ctx, int32_t category, uint32_t locale_off)
{
    (void)category;
    const char *locale = locale_off ? (const char *)(ctx->memory + locale_off) : NULL;

    if (locale == NULL) {
        /* Query mode: return the per-ctx name if set, else default "C". */
        const char *cur = ctx->locale_name[0] ? ctx->locale_name : "C";
        return copy_const_to_wasm(ctx, &ctx->pwd_anchors.setlocale, cur);
    }

    /* Set mode: record on the ctx; do NOT call host setlocale (which
     * would mutate every other guest in this process). */
    size_t n = strlen(locale);
    if (n >= sizeof(ctx->locale_name)) n = sizeof(ctx->locale_name) - 1;
    memcpy(ctx->locale_name, locale, n);
    ctx->locale_name[n] = '\0';

    return copy_const_to_wasm(ctx, &ctx->pwd_anchors.setlocale, ctx->locale_name);
}

/* getusershell — return next entry from /etc/shells. NULL when
 * exhausted. */
uint32_t yos_getusershell(struct yos_exec_ctx *ctx)
{
    char *r = getusershell();
    if (!r) return 0;
    return copy_const_to_wasm(ctx, &ctx->pwd_anchors.getusershell, r);
}
void yos_setusershell(struct yos_exec_ctx *ctx) { (void)ctx; setusershell(); }
void yos_endusershell(struct yos_exec_ctx *ctx) { (void)ctx; endusershell(); }

/* strftime — format struct tm into a buffer using a strftime format
 * string. Wasm guest passes wasm offsets for buf, format, and tm.
 * struct tm is 44 bytes on FreeBSD-i386 wasm32, ~56 bytes on
 * x86_64 host glibc — convert wasm-shape tm to host shape, call host
 * strftime into a host buffer, copy back to the wasm buffer.
 *
 * Auto-bridge stubs because struct tm is on the struct_convert table
 * but not in a way that handles the in-pointer for this signature. */
size_t yos_strftime(struct yos_exec_ctx *ctx, uint32_t buf_off, uint32_t maxsize,
                    uint32_t fmt_off, uint32_t tm_off)
{
    /* 64-bit end calcs so guest-controlled lengths can't wrap past the
     * memory_size bound. */
    if (!buf_off ||
        (uint64_t)buf_off + (uint64_t)maxsize > (uint64_t)ctx->memory_size) return 0;
    if (!fmt_off || fmt_off >= ctx->memory_size) return 0;
    if (!tm_off  ||
        (uint64_t)tm_off + (uint64_t)CV_TM_GUEST_SZ > (uint64_t)ctx->memory_size) return 0;

    struct tm host_tm;
    memset(&host_tm, 0, sizeof host_tm);
    cv_tm_w2h(&host_tm, ctx->memory + tm_off);

    /* Format directly into wasm memory — strftime is purely byte-output,
     * doesn't care if the destination is in the heap or shared memory. */
    return strftime((char *)(ctx->memory + buf_off), maxsize,
                    (const char *)(ctx->memory + fmt_off), &host_tm);
}

/* tmpnam — generate a unique temp-file name. Like tempnam but no
 * dir/prefix args. Wasm side may pass NULL → use static buffer; or
 * a buffer pointer (FreeBSD L_tmpnam = 1024 bytes). */
/* ctx->pwd_anchors.tmpnam moved to ctx->pwd_anchors.tmpnam */
uint32_t yos_tmpnam(struct yos_exec_ctx *ctx, uint32_t s_off)
{
    char host[1024];
    char *r = tmpnam(host);  /* deprecated but spec-required */
    if (!r) return 0;
    if (s_off) {
        size_t n = strlen(r);
        if ((uint64_t)s_off + (uint64_t)n + 1ULL > (uint64_t)ctx->memory_size)
            return 0;
        memcpy(ctx->memory + s_off, r, n + 1);
        return s_off;
    }
    return copy_const_to_wasm(ctx, &ctx->pwd_anchors.tmpnam, r);
}

/* ── strtok / strtok_r / strsep (pointer-into-input return) ─────
 *
 * These return a pointer to a substring of the INPUT buffer (which
 * lives in wasm memory). The bridge can compute the wasm offset by:
 *   wasm_offset = input_wasm_offset + (host_result - host_input).
 *
 * The wasm input pointer is `s_off`; we resolve it to a host
 * pointer, call host strtok which writes a NUL into the buffer and
 * returns a pointer to the token start (or NULL).
 */

uint32_t yos_strtok(struct yos_exec_ctx *ctx, uint32_t s_off, uint32_t delim_off)
{
    /* strtok keeps state across calls in a libc-internal slot.
     * First call: s_off != 0; subsequent calls: s_off == 0 (NULL).
     * We honour that exactly. */
    char *s_h = s_off ? (char *)(ctx->memory + s_off) : NULL;
    const char *delim_h = delim_off ? (const char *)(ctx->memory + delim_off) : "";
    char *r = strtok(s_h, delim_h);
    if (!r) return 0;
    return (uint32_t)(r - (char *)ctx->memory);
}

uint32_t yos_strtok_r(struct yos_exec_ctx *ctx, uint32_t s_off,
                      uint32_t delim_off, uint32_t saveptr_off)
{
    if (!saveptr_off || saveptr_off + 4 > ctx->memory_size)
        return 0;
    /* The wasm-side `*saveptr` is a wasm offset; convert it to a
     * host pointer for strtok_r, which we then convert back. */
    uint32_t *wsave = (uint32_t *)(ctx->memory + saveptr_off);
    char *host_save = *wsave ? (char *)(ctx->memory + *wsave) : NULL;
    char *s_h = s_off ? (char *)(ctx->memory + s_off) : NULL;
    const char *delim_h = delim_off ? (const char *)(ctx->memory + delim_off) : "";
    char *r = strtok_r(s_h, delim_h, &host_save);
    /* Persist updated saveptr back to wasm. */
    *wsave = host_save ? (uint32_t)(host_save - (char *)ctx->memory) : 0;
    if (!r) return 0;
    return (uint32_t)(r - (char *)ctx->memory);
}

uint32_t yos_strsep(struct yos_exec_ctx *ctx, uint32_t stringp_off,
                    uint32_t delim_off)
{
    if (!stringp_off || stringp_off + 4 > ctx->memory_size) return 0;
    uint32_t *wstringp = (uint32_t *)(ctx->memory + stringp_off);
    char *host_str = *wstringp ? (char *)(ctx->memory + *wstringp) : NULL;
    const char *delim_h = delim_off ? (const char *)(ctx->memory + delim_off) : "";
    char *r = strsep(&host_str, delim_h);
    *wstringp = host_str ? (uint32_t)(host_str - (char *)ctx->memory) : 0;
    if (!r) return 0;
    return (uint32_t)(r - (char *)ctx->memory);
}

/* strptime — returns char* one past the last parsed character (or
 * NULL on failure). Same pointer-into-input convention as strtok. */
uint32_t yos_strptime(struct yos_exec_ctx *ctx, uint32_t buf_off,
                      uint32_t fmt_off, uint32_t tm_off)
{
    if (!buf_off || !fmt_off) return 0;
    const char *buf = (const char *)(ctx->memory + buf_off);
    const char *fmt = (const char *)(ctx->memory + fmt_off);
    struct tm tm = {0};
    char *r = strptime(buf, fmt, &tm);
    if (!r) return 0;
    if (tm_off) {
        cv_tm_h2w(ctx->memory + tm_off, &tm);
    }
    return buf_off + (uint32_t)(r - buf);
}

/* posix_memalign lives in impl/alloc.c — see yos_posix_memalign there. */

/* tmpfile() — open an unlinked temp file, register its host FILE*
 * in impl/file.c's handle table. The auto-bridge stubs this
 * because the return type FILE* is complex; the handle table makes
 * it trivial. */
extern uint32_t yos_alloc_file_handle(struct yos_exec_ctx *ctx, FILE *f);

uint32_t yos_tmpfile(struct yos_exec_ctx *ctx)
{
    (void)ctx;
    FILE *f = tmpfile();
    if (!f) return 0;
    uint32_t h = yos_alloc_file_handle(ctx, f);
    if (!h) fclose(f);
    return h;
}

/* ── getprotobyname / getprotobynumber ────────────────────────────────
 *
 * Returns a wasm-side `struct protoent` (12 bytes: p_name@0, p_aliases@4,
 * p_proto@8) populated from a small static table covering the protocols
 * that real wasm callers (curl, ssh, libuv) actually ask about. We
 * skip the libc-side getprotobyname() because its `struct protoent`
 * contains host pointers (p_name, p_aliases[]) that the auto-bridge
 * can't translate to wasm offsets.
 *
 * Per-process static slab (same lifetime contract as libc's `_proto`
 * slot), lazily allocated via yos_malloc. Each lookup overwrites the
 * slab — POSIX explicitly warns that the returned pointer is only
 * valid until the next call.
 */
#define WASM_PROTOENT_SZ 12u
#define PROTO_BUF_SZ     128u
/* ctx->pwd_anchors.proto moved to ctx->pwd_anchors.proto */
struct yos_proto_entry { const char *name; int number; };
static const struct yos_proto_entry yos_proto_table[] = {
    { "ip",   0 },
    { "icmp", 1 },
    { "tcp",  6 },
    { "udp",  17 },
};

static uint32_t emit_protoent(struct yos_exec_ctx *ctx,
                              const struct yos_proto_entry *e)
{
    uint32_t buf = ensure_buf(ctx, &ctx->pwd_anchors.proto, PROTO_BUF_SZ);
    if (!buf) return 0;
    /* Layout in the slab:
     *    [0 .. 11]   struct protoent (p_name, p_aliases, p_proto)
     *    [12 .. 15]  p_aliases slot: one NULL terminator
     *    [16 ..  ]   p_name string                                 */
    uint8_t *w = ctx->memory + buf;
    memset(w, 0, WASM_PROTOENT_SZ + 4);
    uint32_t aliases_off = buf + WASM_PROTOENT_SZ;
    *(uint32_t *)(ctx->memory + aliases_off) = 0;   /* NULL terminator */
    uint32_t cursor = WASM_PROTOENT_SZ + 4;
    uint32_t name_off = pack_string(ctx, buf, PROTO_BUF_SZ, &cursor, e->name);
    *(uint32_t *)(w + 0) = name_off;
    *(uint32_t *)(w + 4) = aliases_off;
    *(uint32_t *)(w + 8) = (uint32_t)e->number;
    return buf;
}

uint32_t yos_getprotobyname(struct yos_exec_ctx *ctx, uint32_t name_off)
{
    if (!name_off || name_off >= ctx->memory_size) return 0;
    const char *name = (const char *)(ctx->memory + name_off);
    for (size_t i = 0; i < sizeof yos_proto_table / sizeof yos_proto_table[0]; i++) {
        if (strcmp(name, yos_proto_table[i].name) == 0)
            return emit_protoent(ctx, &yos_proto_table[i]);
    }
    return 0;
}

uint32_t yos_getprotobynumber(struct yos_exec_ctx *ctx, int32_t number)
{
    for (size_t i = 0; i < sizeof yos_proto_table / sizeof yos_proto_table[0]; i++) {
        if (yos_proto_table[i].number == number)
            return emit_protoent(ctx, &yos_proto_table[i]);
    }
    return 0;
}

/* user_from_uid / group_from_gid — FreeBSD libutil helpers ls(1) uses
 * to render the owner/group columns. Glibc has no equivalent, so they
 * land in the codegen ENOSYS stub by default; that returns NULL and
 * ls treats NULL as a fatal OOM ("err(1, \"user_from_uid\")").
 *
 * Both return a pointer to a static buffer holding the name. On lookup
 * failure: with nouser/nogroup == 0 they return the numeric form
 * (printed as decimal); with !=0 they return NULL. We share one wasm
 * scratch buffer per kind (one user, one group) — same lifetime
 * contract as FreeBSD's own static, and matches how ls/find use the
 * call (one-at-a-time per entry, no nested results held). */
static uint32_t emit_ugname(struct yos_exec_ctx *ctx,
                            uint32_t *anchor, const char *s)
{
    uint32_t buf = ensure_buf(ctx, anchor, UGNAME_BUF_SZ);
    if (!buf) return 0;
    size_t n = strlen(s);
    if (n >= UGNAME_BUF_SZ) n = UGNAME_BUF_SZ - 1;
    memcpy(ctx->memory + buf, s, n);
    ctx->memory[buf + n] = '\0';
    return buf;
}

uint32_t yos_user_from_uid(struct yos_exec_ctx *ctx, uint32_t uid, int32_t nouser)
{
    struct passwd *pw = getpwuid((uid_t)uid);
    if (pw && pw->pw_name && pw->pw_name[0])
        return emit_ugname(ctx, &ctx->pwd_anchors.ufu, pw->pw_name);
    if (nouser) return 0;
    char num[16];
    snprintf(num, sizeof num, "%u", uid);
    return emit_ugname(ctx, &ctx->pwd_anchors.ufu, num);
}

uint32_t yos_group_from_gid(struct yos_exec_ctx *ctx, uint32_t gid, int32_t nogroup)
{
    struct group *gr = getgrgid((gid_t)gid);
    if (gr && gr->gr_name && gr->gr_name[0])
        return emit_ugname(ctx, &ctx->pwd_anchors.gfg, gr->gr_name);
    if (nogroup) return 0;
    char num[16];
    snprintf(num, sizeof num, "%u", gid);
    return emit_ugname(ctx, &ctx->pwd_anchors.gfg, num);
}

/* Called from impl/proc/proc.c::fork_thread_func right after the old
 * wasm runtime is m3_FreeRuntime'd during execve. All the *_buf_off
 * anchors above are offsets INTO THE NOW-FREED wasm linear memory;
 * the next yos_malloc on the fresh memory will hand back an unrelated
 * region but ensure_buf would return the stale offset as if it had
 * already allocated. Symptom: second `ls` (or anything pwd-using) in
 * a single yos session traps because user_from_uid writes the result
 * string into garbage / random wasm memory. Mirrors the same fix
 * yos_env_post_execve_reset does for env.c. */
void yos_pwd_post_execve_reset(struct yos_exec_ctx *ctx)
{
    /* Every `*_buf_off` anchor on ctx->pwd_anchors is a wasm offset
     * that survives across the m3_FreeRuntime + m3_NewRuntime pair
     * execve does. The next process's ensure_buf() / equivalent
     * would otherwise see the anchor as already-allocated and hand back
     * a stale offset into freed memory. Zero them so each new wasm
     * program starts with fresh allocations.
     *
     * Anchors now live PER-CTX (types.h::yos_exec_ctx::pwd_anchors)
     * — used to be file-scope statics, which broke when two
     * concurrent guests in the same yos host hit them. */
    ctx->pwd_anchors.pwd          = 0;
    ctx->pwd_anchors.grp          = 0;
    ctx->pwd_anchors.login        = 0;
    ctx->pwd_anchors.ufu          = 0;
    ctx->pwd_anchors.gfg          = 0;
    ctx->pwd_anchors.tm           = 0;
    ctx->pwd_anchors.timestr      = 0;
    ctx->pwd_anchors.errstr       = 0;
    ctx->pwd_anchors.gaistr       = 0;
    ctx->pwd_anchors.hstr         = 0;
    ctx->pwd_anchors.signam       = 0;
    ctx->pwd_anchors.ttyname      = 0;
    ctx->pwd_anchors.ctermid      = 0;
    ctx->pwd_anchors.dirname      = 0;
    ctx->pwd_anchors.l64a         = 0;
    ctx->pwd_anchors.nl_langinfo  = 0;
    ctx->pwd_anchors.setlocale    = 0;
    ctx->pwd_anchors.getwd        = 0;
    ctx->pwd_anchors.tempnam      = 0;
    ctx->pwd_anchors.getusershell = 0;
    ctx->pwd_anchors.tmpnam       = 0;
    ctx->pwd_anchors.proto        = 0;
}
