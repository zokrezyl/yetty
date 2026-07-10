/*
 * test_ssh_string_basic.c — ANSI string ops ssh uses everywhere.
 *
 * WHAT this verifies:
 *   strlen, strcmp, strncmp, strcasecmp, strncasecmp, strchr,
 *   strrchr, strcpy. These are all Tier-1 host-libc passthroughs;
 *   the point of this test is to pin the wasm offset translation +
 *   FreeBSD ABI scalar widening (size_t == 4 bytes on wasm32 vs
 *   8 on the host).
 *
 * WHY this matters:
 *   When auto-bridge regen changes scalar conventions (size_t →
 *   long → uint32_t round-trip), one of these is the first to break
 *   without anyone noticing — until ssh prints garbled config keys
 *   or compares the wrong string. Keeping a single representative
 *   case per fn means future regressions show up here, not in some
 *   downstream "ssh ignores ~/.ssh/config" report.
 *
 * Expected: exit 0, stdout contains "ssh-string ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

__attribute__((import_module("env"), import_name("strlen")))
size_t strlen(const char *s);

__attribute__((import_module("env"), import_name("strcmp")))
int strcmp(const char *a, const char *b);

__attribute__((import_module("env"), import_name("strncmp")))
int strncmp(const char *a, const char *b, size_t n);

__attribute__((import_module("env"), import_name("strcasecmp")))
int strcasecmp(const char *a, const char *b);

__attribute__((import_module("env"), import_name("strncasecmp")))
int strncasecmp(const char *a, const char *b, size_t n);

__attribute__((import_module("env"), import_name("strchr")))
char *strchr(const char *s, int c);

__attribute__((import_module("env"), import_name("strrchr")))
char *strrchr(const char *s, int c);

__attribute__((import_module("env"), import_name("strcpy")))
char *strcpy(char *dst, const char *src);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

void _start(void) {
    if (strlen("ssh") != 3)                 { say("FAIL: strlen\n"); _exit(1); }
    if (strlen("") != 0)                    { say("FAIL: strlen empty\n"); _exit(2); }

    if (strcmp("ssh", "ssh") != 0)          { say("FAIL: strcmp equal\n"); _exit(3); }
    if (strcmp("ssh", "scp") <= 0)          { say("FAIL: strcmp ordering\n"); _exit(4); }
    if (strncmp("ssh-rsa", "ssh-ed", 4) != 0) { say("FAIL: strncmp first 4\n"); _exit(5); }
    /* At length 6, "ssh-rs" > "ssh-ed" (r > e at index 4). */
    if (strncmp("ssh-rsa", "ssh-ed", 6) <= 0) { say("FAIL: strncmp at 6\n"); _exit(6); }

    if (strcasecmp("Host", "host") != 0)    { say("FAIL: strcasecmp\n"); _exit(7); }
    if (strcasecmp("Host", "Port") >= 0)    { say("FAIL: strcasecmp ordering\n"); _exit(8); }
    if (strncasecmp("HostName", "hostxxx", 4) != 0) { say("FAIL: strncasecmp\n"); _exit(9); }

    /* strchr / strrchr: first / last occurrence. */
    const char *s = "/usr/local/bin/ssh";
    char *first = strchr(s, '/');
    char *last  = strrchr(s, '/');
    if (!first || first != s)               { say("FAIL: strchr first /\n"); _exit(10); }
    if (!last || (int)(last - s) != 14)     { say("FAIL: strrchr last /\n"); _exit(11); }
    if (strchr(s, 'z') != 0)                { say("FAIL: strchr miss should be NULL\n"); _exit(12); }

    /* strcpy: copy + NUL termination. */
    char dst[16];
    for (int i = 0; i < 16; i++) dst[i] = 'X';
    if (strcpy(dst, "hello") != dst)        { say("FAIL: strcpy ret\n"); _exit(13); }
    if (dst[0] != 'h' || dst[5] != 0 || dst[6] != 'X') {
        say("FAIL: strcpy contents\n");
        _exit(14);
    }

    say("ssh-string ok\n");
    _exit(0);
}
