/* Phase 1a guest (epic #33, issue #35).
 *
 * Unlike the Phase 0 guest, this one is a real (if tiny) yos guest: every
 * libc call is a wasm import on env.<freebsd-libc-name>. It imports three —
 * write, getpid, exit — and does nothing else. The host serves them through
 * C bridge wrappers (phase1a_host.c) that translate the guest pointer with
 * `host = ctx->memory + guest_offset`, exactly as the desktop yos bridge does.
 * No JavaScript implements any of these calls.
 *
 * The output is pointer-translation-sensitive on purpose:
 *   - the message is built at run time in a guest buffer, so the bytes the
 *     host writes only come out right if the bridge read guest memory at the
 *     correct offset;
 *   - the pid embedded in it comes from getpid(), i.e. from the C host, not a
 *     JS stub;
 *   - exit(7) hands a specific code to the C exit wrapper, which the smoke
 *     test checks.
 *
 * Built with: clang -target wasm32-unknown-unknown -nostdlib
 *             -Wl,--no-entry -Wl,--export=_start  (own memory, no --import-memory)
 */

typedef unsigned long size_t; /* wasm32 / FreeBSD i386 shape: 4 bytes */
typedef long          ssize_t;

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t count);

__attribute__((import_module("env"), import_name("getpid")))
int getpid(void);

__attribute__((import_module("env"), import_name("exit"), __noreturn__))
void exit(int code);

/* A static buffer near the guest's data segment. Its address is a non-trivial
 * guest offset the host bridge must translate to read these bytes. */
static char message[128];

static size_t append(char *dst, const char *src)
{
	size_t n = 0;
	while (src[n]) {
		dst[n] = src[n];
		n++;
	}
	return n;
}

static size_t append_uint(char *dst, unsigned value)
{
	char tmp[16];
	int i = 0;
	if (value == 0) {
		dst[0] = '0';
		return 1;
	}
	while (value) {
		tmp[i++] = (char)('0' + (value % 10));
		value /= 10;
	}
	size_t n = 0;
	while (i > 0)
		dst[n++] = tmp[--i];
	return n;
}

__attribute__((export_name("_start"))) void _start(void)
{
	char *out = message;
	out += append(out, "phase1a: pid=");
	out += append_uint(out, (unsigned)getpid());
	out += append(out, " via C bridge\n");
	write(1, message, (size_t)(out - message));
	exit(7);
}
