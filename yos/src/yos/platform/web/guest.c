/* Guest app, exactly as a real yos guest sees the world: every libc
 * function is a wasm import on env.<freebsd-libc-name>. The browser's
 * own wasm engine runs this module natively — no interpreter.
 *
 * The guest neither knows nor cares that env.write / env.strlen are
 * satisfied by another wasm module (the yos-host bridge) sharing its
 * linear memory. */

typedef unsigned long size_t; /* wasm32: 4 bytes — FreeBSD i386 shape */
typedef int           ssize_t;

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t count);

__attribute__((import_module("env"), import_name("strlen")))
size_t strlen(const char *string);

/* Built at runtime so the compiler can't constant-fold strlen away —
 * the call must reach the bridge. */
static char message[64];

__attribute__((export_name("_start"))) void _start(void)
{
	const char *source = "hello from guest, served by a wasm bridge\n";
	char *out = message;
	while ((*out = *source)) {
		out++;
		source++;
	}
	/* strlen runs entirely inside the bridge wasm, reading these very
	 * bytes out of the shared memory; write hands the same guest
	 * pointer to the bridge untouched. */
	write(1, message, strlen(message));
}
