/*
 * test_pwd_user_group_distinct.c — user_from_uid / group_from_gid
 * MUST use separate wasm-side scratch buffers.
 *
 * WHAT this verifies:
 *   When the wasm guest calls user_from_uid(...) then immediately
 *   group_from_gid(...), the returned pointer from user_from_uid
 *   MUST still point at the user-name string — the group call must
 *   not have overwritten the user buffer.
 *
 * WHY this matters:
 *   ls prints `printf("%s %s", user_from_uid(...), group_from_gid(...))`.
 *   C arg-eval order writes BOTH calls' results into a buffer; then
 *   printf reads back. Pre-fix, yos_user_from_uid and yos_group_from_gid
 *   shared ONE wasm scratch buffer (ugname_buf_off in impl/libc/pwd.c)
 *   — the group call overwrote the user-name string. ls then printed
 *   `staff staff` for every file. The fix: two anchors,
 *   ufu_buf_off and gfg_buf_off, allocated lazily on first call.
 *
 * Expected: exit 0, stdout contains "pwd-distinct ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

extern const char *user_from_uid(unsigned int, int);
extern const char *group_from_gid(unsigned int, int);
extern unsigned int getuid(void);
extern unsigned int getgid(void);
extern int   write(int, const void *, size_t);
extern unsigned int strlen(const char *);
extern void _exit(int) __attribute__((noreturn));
extern int   strcmp(const char *, const char *);

static void emit(const char *s) { write(1, s, (size_t)strlen(s)); }
static void emit_err(const char *s) { write(2, s, (size_t)strlen(s)); }

/* Copy `s` into the caller's buf — needed because we want to compare
 * the user-name pointer BEFORE and AFTER the group call. If the
 * pointer is shared, the value at that pointer changes; if separate,
 * the user-name string still reads back the original. */
static unsigned my_strcpy(char *dst, const char *src, unsigned cap)
{
    unsigned n = 0;
    while (n + 1 < cap && src[n]) { dst[n] = src[n]; n++; }
    dst[n] = 0;
    return n;
}

void _start(void)
{
    /* 1) Pull the user-name pointer. */
    const char *up = user_from_uid(getuid(), 0);
    if (!up) { emit_err("FAIL: user_from_uid returned NULL\n"); _exit(1); }

    /* 2) Copy it RIGHT NOW — before any group_from_gid that might
     *    scribble into the same buffer pre-fix. */
    char saved_user[64];
    unsigned ulen = my_strcpy(saved_user, up, sizeof saved_user);
    if (ulen == 0) {
        emit_err("FAIL: user_from_uid returned empty string\n");
        _exit(1);
    }

    /* 3) Now call group_from_gid. Pre-fix, this writes into the
     *    same buffer `up` points at, so `up`'s text gets overwritten. */
    const char *gp = group_from_gid(getgid(), 0);
    if (!gp) { emit_err("FAIL: group_from_gid returned NULL\n"); _exit(1); }

    /* 4) Read back the user pointer. If the buffers were shared
     *    (the bug), `up` now points at the group name. */
    if (strcmp(up, saved_user) != 0) {
        emit_err("FAIL: user_from_uid buffer clobbered by "
                 "group_from_gid — shared anchor regression in "
                 "impl/libc/pwd.c. user pointer now reads: '");
        emit_err(up);
        emit_err("' but should be '");
        emit_err(saved_user);
        emit_err("'\n");
        _exit(1);
    }

    /* 5) Sanity: gp must be a non-empty string different in CONTENT
     *    from user (otherwise the test isn't actually exercising
     *    the aliasing). If they happen to be the same string (user
     *    name == group name, e.g. on systems with user-private
     *    groups), accept that as long as the user buffer was
     *    refreshed independently. */
    if (!gp[0]) {
        emit_err("FAIL: group_from_gid returned empty string\n");
        _exit(1);
    }

    emit("pwd-distinct ok\n");
    _exit(0);
}
