/*
 * test_getline_basic.c — yos_getline bridge.
 *
 * WHAT this verifies:
 *   - getline(NULL, 0, fp) allocates a buffer, reads up to '\n',
 *     returns the byte count INCLUDING the '\n'.
 *   - Repeated calls reuse the buffer and read successive lines.
 *   - Lines longer than the initial buffer are correctly grown
 *     via realloc (test forces this with a single long line).
 *   - At EOF the second call returns -1 without trashing the buf.
 *
 * WHY this matters:
 *   ssh's read_config_file() uses getline() to read ~/.ssh/config
 *   line by line. Pre-fix yos_getline was a stub returning -ENOSYS,
 *   so ssh read ZERO lines from the config, never applied any
 *   `Host <name>` stanza, and handed un-resolved hostnames straight
 *   to getaddrinfo. Symptom: `ssh nixem` (where nixem is a Host
 *   block in ~/.ssh/config with a HostName line) failed with
 *   "Could not resolve hostname nixem" even though the user's
 *   config clearly mapped it to an IP.
 *
 * Expected: exit 0, stdout contains "getline ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

extern void *fopen(const char *, const char *);
extern int   fclose(void *);
extern unsigned int fwrite(const void *, unsigned int, unsigned int, void *);
extern int   write(int, const void *, size_t);
extern int   unlink(const char *);
extern unsigned int strlen(const char *);
extern ssize_t getline(char **lineptr, size_t *n, void *stream);
extern void _exit(int) __attribute__((noreturn));
extern void *malloc(size_t);
extern void  free(void *);
extern int   strcmp(const char *, const char *);

static void emit(const char *s) { write(1, s, (size_t)strlen(s)); }
static void emit_err(const char *s) { write(2, s, (size_t)strlen(s)); }

void _start(void)
{
    const char *path = "/tmp/yos_test_getline.txt";
    /* Build a fixture: two short lines + one long line (forces buffer
     * growth past the bridge's initial 128-byte capacity). */
    {
        void *fp = fopen(path, "w");
        if (!fp) { emit_err("FAIL: fopen(w)\n"); _exit(1); }
        const char *line1 = "Host nixem\n";
        const char *line2 = "  HostName 192.168.1.10\n";
        if (fwrite(line1, 1, strlen(line1), fp) != strlen(line1) ||
            fwrite(line2, 1, strlen(line2), fp) != strlen(line2)) {
            emit_err("FAIL: fwrite\n"); fclose(fp); _exit(1);
        }
        /* Long line — 300 'x' followed by newline. Forces the
         * initial 128-byte buffer to realloc at least once. */
        char xs[301]; for (int i = 0; i < 300; i++) xs[i] = 'x'; xs[300] = '\n';
        if (fwrite(xs, 1, 301, fp) != 301) {
            emit_err("FAIL: fwrite long\n"); fclose(fp); _exit(1);
        }
        fclose(fp);
    }

    void *fp = fopen(path, "r");
    if (!fp) { emit_err("FAIL: fopen(r)\n"); _exit(1); }

    char *buf = 0;
    size_t cap = 0;

    /* Line 1. */
    ssize_t n = getline(&buf, &cap, fp);
    if (n < 0) {
        emit_err("FAIL: getline #1 returned -1 (was the bridge "
                 "stubbed as ENOSYS?)\n");
        fclose(fp); unlink(path); _exit(1);
    }
    if (n != (ssize_t)strlen("Host nixem\n") ||
        strcmp(buf, "Host nixem\n") != 0) {
        emit_err("FAIL: line 1 content mismatch: '");
        emit_err(buf ? buf : "(null)");
        emit_err("'\n");
        fclose(fp); unlink(path); _exit(1);
    }

    /* Line 2 — same buffer, possibly grown. */
    n = getline(&buf, &cap, fp);
    if (n < 0 || strcmp(buf, "  HostName 192.168.1.10\n") != 0) {
        emit_err("FAIL: line 2 mismatch\n");
        fclose(fp); unlink(path); _exit(1);
    }

    /* Line 3 — 301 chars. Forces realloc beyond the initial 128. */
    n = getline(&buf, &cap, fp);
    if (n != 301) {
        emit_err("FAIL: long line: wrong byte count\n");
        fclose(fp); unlink(path); _exit(1);
    }
    /* Spot-check first and last data char. */
    if (buf[0] != 'x' || buf[299] != 'x' || buf[300] != '\n' ||
        buf[301] != '\0') {
        emit_err("FAIL: long line content/NUL trailing wrong\n");
        fclose(fp); unlink(path); _exit(1);
    }

    /* EOF — must return -1, buf unchanged (or at least: not crash). */
    n = getline(&buf, &cap, fp);
    if (n != -1) {
        emit_err("FAIL: expected -1 at EOF\n");
        fclose(fp); unlink(path); _exit(1);
    }

    fclose(fp);
    unlink(path);
    emit("getline ok\n");
    _exit(0);
}
