/* yos-host bridge, compiled to wasm32 and sharing the guest's linear
 * memory. This is the same layer as the native C bridges — pointer
 * translation + ABI shaping + dispatch — only here "translate a guest
 * pointer to a host pointer" is the identity, because the bridge and the
 * guest address the SAME linear memory. A guest offset IS a bridge
 * offset.
 *
 * Every env.<libc-name> the guest imports is an export here. Calls from
 * guest to bridge are wasm->wasm at native engine speed; no JS on the
 * data path.
 *
 * The ONLY thing that crosses into JS is the irreducible sandbox edge:
 * wasm cannot, by itself, push bytes at a terminal / file. That is one
 * host import. Everything else stays in wasm. */

typedef unsigned long size_t;
typedef int           ssize_t;

/* The irreducible host effect. The embedder (JS/WASI) reads the bytes
 * straight out of the shared memory it owns and routes them to the
 * terminal sink. A real yos-host backs this with the VFS / fd table. */
__attribute__((import_module("host"), import_name("write_bytes")))
int host_write_bytes(int fd, const void *buf, size_t count);

/* Tier-1-shaped bridge: translate (no-op here), dispatch to the host
 * effect, return the FreeBSD-ABI value. */
__attribute__((export_name("write")))
ssize_t yos_brg_write(int fd, const void *buf, size_t count)
{
	return host_write_bytes(fd, buf, count);
}

/* Pure bridge: needs zero JS. Reads the guest string directly out of
 * the shared memory and computes — entirely in wasm. */
__attribute__((export_name("strlen")))
size_t yos_brg_strlen(const char *string)
{
	const char *cursor = string;
	while (*cursor)
		cursor++;
	return (size_t)(cursor - string);
}
