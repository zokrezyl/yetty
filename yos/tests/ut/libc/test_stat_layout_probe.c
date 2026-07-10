/*
 * test_stat_layout_probe.c — probe the actual offset the bridge writes
 *                            st_mode to, and what offset the FreeBSD
 *                            sysroot's <sys/stat.h> reads it from.
 *
 * WHAT this verifies:
 *   This test does TWO things in one shot:
 *   1. Includes <sys/stat.h> (the FreeBSD sysroot copy nvim was built
 *      against) and prints offsetof(struct stat, st_mode) — this is
 *      where libuv's uv_guess_handle reads it from.
 *   2. Calls fstat on a socketpair end into a 256-byte zeroed buffer
 *      and scans for the byte pattern that matches S_IFSOCK in any of
 *      the bytes — proving where the BRIDGE writes st_mode to.
 *
 *   If the two offsets differ, fstat is broken: the bridge writes to
 *   one offset, the wasm guest's struct stat reads from another. Then
 *   uv_guess_handle on a SOCK fd reads garbage (likely 0), falls
 *   through to UV_UNKNOWN_HANDLE, and stream_init may assert OR
 *   stream is mis-classified. This is the leading hypothesis for why
 *   the IPC stdin (`wfd=17 hfd=35` in tmp/nvim-runtime-issues.md)
 *   never appears in any kqueue change.
 *
 * Expected: exit 0, stdout contains "stat layout ok".
 *           exit 2 with "header offset N bridge offset M" if mismatch.
 */

#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* socketpair / write / fstat / close come from FreeBSD libc through
 * the wasm-import contract automatically (no manual __attribute__). */

static void put_u32(char *buf, unsigned x) {
    int n = 0; char d[16];
    if (!x) { d[n++] = '0'; }
    while (x) { d[n++] = '0' + (x % 10); x /= 10; }
    int p = 0; while (buf[p]) p++;
    for (int i = n - 1; i >= 0; i--) buf[p++] = d[i];
    buf[p] = 0;
}

void _start(void) {
    /* (1) Header offset — what the wasm guest's struct stat layout
     *     puts st_mode at. This is what nvim/libuv reads. */
    size_t header_off = offsetof(struct stat, st_mode);

    /* (2) Bridge offset — find which byte the bridge actually wrote
     *     S_IFSOCK to. We use a 256-byte oversized buffer covering
     *     any plausible struct stat size; the bridge writes a 16-bit
     *     mode containing S_IFSOCK low-bits at SOME offset; we scan
     *     for it. */
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        write(2, "socketpair failed\n", 18);
        _exit(1);
    }

    unsigned char raw[256];
    memset(raw, 0xee, sizeof raw);   /* poison so unwritten bytes look obvious */
    if (fstat(sv[0], (struct stat *)raw) != 0) {
        write(2, "fstat failed\n", 13);
        _exit(1);
    }

    /* Scan from offset 16 onward (st_dev + st_ino occupy 0..15 and
     * may legitimately contain bytes that happen to match the
     * S_IFSOCK pattern, which would give a false positive). */
    int bridge_off = -1;
    for (size_t i = 16; i + 2 <= sizeof raw; i++) {
        uint16_t v = (uint16_t)(raw[i] | (raw[i+1] << 8));
        if ((v & 0xf000) == 0xc000) {  /* S_IFSOCK = 0140000 = 0xc000 */
            bridge_off = (int)i;
            break;
        }
    }

    char msg[160] = "header offset ";
    put_u32(msg, (unsigned)header_off);
    int p = 0; while (msg[p]) p++;
    msg[p++] = ' '; msg[p++] = 'b'; msg[p++] = 'r'; msg[p++] = 'i';
    msg[p++] = 'd'; msg[p++] = 'g'; msg[p++] = 'e'; msg[p++] = ' ';
    msg[p++] = 'o'; msg[p++] = 'f'; msg[p++] = 'f'; msg[p++] = 's';
    msg[p++] = 'e'; msg[p++] = 't'; msg[p++] = ' '; msg[p] = 0;
    if (bridge_off < 0) {
        const char *s = "NOT FOUND\n";
        for (; *s; s++) msg[p++] = *s;
    } else {
        put_u32(msg, (unsigned)bridge_off);
        p = 0; while (msg[p]) p++;
        msg[p++] = '\n';
    }
    msg[p] = 0;
    write(1, msg, p);

    if ((int)header_off != bridge_off) {
        write(1, "stat layout MISMATCH\n", 21);
        _exit(2);
    }

    /* And the actual S_ISSOCK as the wasm guest sees it. */
    struct stat *st = (struct stat *)raw;
    if (!S_ISSOCK(st->st_mode)) {
        write(1, "S_ISSOCK false from guest perspective\n", 38);
        _exit(3);
    }
    write(1, "stat layout ok\n", 15);
    close(sv[0]);
    close(sv[1]);
    _exit(0);
}
