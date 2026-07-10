/*
 * test_ssh_str_bsd.c — BSD-flavour string helpers ssh leans on.
 *
 * WHAT this verifies:
 *   strlcpy / strlcat / strsep / strpbrk / strcspn / strspn — all
 *   pure passthroughs in the auto-generated bridge. The point of the
 *   test is to pin the FreeBSD-shape contract (size_t return for
 *   strlcpy/strlcat, NUL-terminated truncation, etc.) so a future
 *   bridge regression that drops one of them to ENOSYS shows up
 *   immediately.
 *
 * WHY this matters:
 *   ssh's config parser walks ~/.ssh/config with strsep on " \t" plus
 *   strspn/strcspn for whitespace skipping; "~" expansion uses
 *   strlcat. If any of these silently return 0 / NULL the parser
 *   reads zero options and ssh falls back to defaults — symptomatic
 *   of "ssh ignores ~/.ssh/config" reports.
 *
 * Expected: exit 0, stdout contains "ssh-str-bsd ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

__attribute__((import_module("env"), import_name("strlcpy")))
size_t strlcpy(char *dst, const char *src, size_t sz);

__attribute__((import_module("env"), import_name("strlcat")))
size_t strlcat(char *dst, const char *src, size_t sz);

__attribute__((import_module("env"), import_name("strsep")))
char *strsep(char **stringp, const char *delim);

__attribute__((import_module("env"), import_name("strpbrk")))
char *strpbrk(const char *s, const char *accept);

__attribute__((import_module("env"), import_name("strcspn")))
size_t strcspn(const char *s, const char *reject);

__attribute__((import_module("env"), import_name("strspn")))
size_t strspn(const char *s, const char *accept);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }
static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

void _start(void) {
    /* strlcpy: dst always NUL-terminated when sz > 0; return =
     * strlen(src). Truncates if src longer than sz-1. */
    char dst[16];
    size_t n = strlcpy(dst, "hello world", sizeof dst);
    if (n != 11 || !streq(dst, "hello world")) {
        say("strlcpy FAIL: basic\n");
        _exit(1);
    }
    n = strlcpy(dst, "this string is way too long", 8);
    /* sz=8 → writes 7 chars + NUL; returns full src length 27 */
    if (n != 27 || !streq(dst, "this st")) {
        say("strlcpy FAIL: truncation\n");
        _exit(2);
    }

    /* strlcat: returns strlen(initial dst) + strlen(src). */
    char buf[16] = "foo";
    n = strlcat(buf, "/bar", sizeof buf);
    if (n != 7 || !streq(buf, "foo/bar")) {
        say("strlcat FAIL: basic\n");
        _exit(3);
    }
    /* truncating cat: dst="0123456789", sz=12, src="abc" → "0123456789a"
     * + NUL, return 13. */
    char buf2[12] = "0123456789";
    n = strlcat(buf2, "abc", sizeof buf2);
    if (n != 13 || !streq(buf2, "0123456789a")) {
        say("strlcat FAIL: truncation\n");
        _exit(4);
    }

    /* strsep: walks src splitting on any delim char, NUL-terminates
     * each token, advances *stringp. Critical: returns the empty
     * token between two adjacent delims (this is the difference from
     * strtok and what ssh's config parser depends on). */
    char src[] = "Host  alpha\tbeta";
    char *p = src;
    char *t1 = strsep(&p, " \t");   /* "Host" */
    char *t2 = strsep(&p, " \t");   /* ""    (between the two spaces) */
    char *t3 = strsep(&p, " \t");   /* "alpha" */
    char *t4 = strsep(&p, " \t");   /* "beta" */
    char *t5 = strsep(&p, " \t");   /* NULL  */
    if (!t1 || !streq(t1, "Host")) { say("strsep FAIL: t1\n"); _exit(5); }
    if (!t2 || t2[0] != '\0')      { say("strsep FAIL: t2 empty\n"); _exit(6); }
    if (!t3 || !streq(t3, "alpha")){ say("strsep FAIL: t3\n"); _exit(7); }
    if (!t4 || !streq(t4, "beta")) { say("strsep FAIL: t4\n"); _exit(8); }
    if (t5 != 0)                   { say("strsep FAIL: t5 should be NULL\n"); _exit(9); }

    /* strpbrk: first char in s that's also in accept. */
    char *hit = strpbrk("port=22 user=root", "= \t");
    if (!hit || hit[0] != '=') {
        say("strpbrk FAIL\n");
        _exit(10);
    }

    /* strcspn / strspn: ssh uses these as the "skip / find run of
     * whitespace" pair. */
    if (strspn("   abc", " \t") != 3) {
        say("strspn FAIL\n");
        _exit(11);
    }
    if (strcspn("abc def", " \t") != 3) {
        say("strcspn FAIL\n");
        _exit(12);
    }

    say("ssh-str-bsd ok\n");
    _exit(0);
}
