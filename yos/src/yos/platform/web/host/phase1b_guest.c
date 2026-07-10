/* Phase 1b guest (epic #33, issue #36).
 *
 * A tiny guest that exercises the REAL generated yos bridge + impl in the
 * browser host: it does echo (write) and cat (open/read/write/close) plus
 * getpid, all as env.* imports served by yos's C code — the same
 * generated bridge desktop uses — with Emscripten musl/MEMFS as the storage
 * substrate beneath yos's VFS/io impl.
 *
 * It deliberately does NOT call exit(): desktop yos_exit() bottoms out in host
 * exit(), which under Emscripten would tear down the whole host module. A
 * non-forking guest simply returns from _start and the host regains control.
 * Clean process-exit in the browser host is a platform-backend item for a
 * later phase.
 *
 * Import arities match the generated bridge link signatures exactly
 * (open is i(iii): path, flags, mode).
 */

typedef unsigned long size_t; /* wasm32 / FreeBSD i386 shape */
typedef long          ssize_t;

#define O_RDONLY 0 /* FreeBSD + host value */

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t count);
__attribute__((import_module("env"), import_name("read")))
ssize_t read(int fd, void *buf, size_t count);
__attribute__((import_module("env"), import_name("open")))
int open(const char *path, int flags, int mode);
__attribute__((import_module("env"), import_name("close")))
int close(int fd);
__attribute__((import_module("env"), import_name("getpid")))
int getpid(void);

static char line[128];
static char filebuf[512];

static size_t append(char *dst, const char *src)
{
	size_t n = 0;
	while (src[n]) { dst[n] = src[n]; n++; }
	return n;
}

static size_t append_uint(char *dst, unsigned value)
{
	char tmp[16];
	int i = 0;
	if (value == 0) { dst[0] = '0'; return 1; }
	while (value) { tmp[i++] = (char)('0' + (value % 10)); value /= 10; }
	size_t n = 0;
	while (i > 0) dst[n++] = tmp[--i];
	return n;
}

__attribute__((export_name("_start"))) void _start(void)
{
	/* echo: write a line built in guest memory (getpid via the C bridge). */
	char *out = line;
	out += append(out, "phase1b: pid=");
	out += append_uint(out, (unsigned)getpid());
	out += append(out, " echo via generated bridge\n");
	write(1, line, (size_t)(out - line));

	/* cat: open a file (host MEMFS substrate under yos VFS), read, write. */
	int fd = open("/phase1b.txt", O_RDONLY, 0);
	if (fd >= 0) {
		ssize_t n = read(fd, filebuf, sizeof(filebuf));
		if (n > 0)
			write(1, filebuf, (size_t)n);
		close(fd);
	} else {
		static const char miss[] = "phase1b: open(/phase1b.txt) failed\n";
		write(1, miss, sizeof(miss) - 1);
	}
}
