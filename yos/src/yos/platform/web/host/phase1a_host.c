/* Phase 1a host (epic #33, issue #35).
 *
 * The Phase 0 host proved wasm3 runs inside a browser wasm module. This one
 * adds the next layer: it serves a guest's env.* libc imports through C bridge
 * wrappers in the SAME shape the desktop yos bridge uses — pop the wasm-ABI
 * args off the wasm3 stack, translate the guest pointer with
 * `host = ctx->memory + guest_offset`, then call the host implementation.
 * JavaScript never touches these calls.
 *
 * Three imports are wired: write, getpid, exit. This is a deliberate minimal
 * subset; Phase 1b (#36) replaces these hand-written wrappers with the full
 * generated yos_bridge.c plus the impl and vfs source trees.
 *
 * Bridge shape mirrored from build-<host>/src/yos/codegen/yos_bridge.c:
 *   - the raw-function ABI is (IM3Runtime, IM3ImportContext, uint64_t *_sp,
 *     void *_mem); the per-call ctx comes from m3_GetUserData(runtime);
 *   - each wasm stack slot is 64-bit, read low-32 via *(uint32_t *)(_sp++);
 *   - a value-returning import has its return slot FIRST, then args; a void
 *     import (exit) has no return slot.
 */

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "wasm3.h"

#include "bridge_guest_wasm.h"

/* The Phase 1a exec context. This is the minimal stand-in for the desktop
 * struct yos_exec_ctx: the only field the bridge fundamentally needs is the
 * guest linear-memory base (memory) + its size for bounds checks. pid is
 * host-owned state the guest reads through getpid(); exit_* capture the
 * guest's exit request. */
struct phase1a_ctx {
	uint8_t *memory;
	uint32_t memory_size;
	int32_t  pid;
	int      exit_requested;
	int32_t  exit_code;
};

/* Bounds-checked guest offset -> host pointer. Returns NULL on overrun. This
 * is the one operation the whole bridge model rests on. */
static void *guest_ptr(struct phase1a_ctx *ctx, uint32_t offset, uint32_t len)
{
	if ((uint64_t)offset + (uint64_t)len > (uint64_t)ctx->memory_size)
		return NULL;
	return ctx->memory + offset;
}

/* env.write : i(iii) — ssize_t write(int fd, const void *buf, size_t count) */
static const void *m3w_write(IM3Runtime runtime, IM3ImportContext import_ctx,
                             uint64_t *stack, void *mem)
{
	(void)import_ctx;
	(void)mem;
	struct phase1a_ctx *ctx = (struct phase1a_ctx *)m3_GetUserData(runtime);

	uint32_t *raw_return = (uint32_t *)(stack++);
	uint32_t fd     = *(uint32_t *)(stack++);
	uint32_t buf    = *(uint32_t *)(stack++);
	uint32_t count  = *(uint32_t *)(stack++);

	void *host_buf = guest_ptr(ctx, buf, count);
	if (host_buf == NULL)
		return m3Err_trapOutOfBoundsMemoryAccess;

	/* Host libc write. Under Emscripten fd 1/2 land on the JS print hooks —
	 * that hook is output capture, not a libc implementation. */
	ssize_t written = write((int)fd, host_buf, (size_t)count);
	*raw_return = (uint32_t)(int32_t)written;
	return m3Err_none;
}

/* env.getpid : i() — int getpid(void) */
static const void *m3w_getpid(IM3Runtime runtime, IM3ImportContext import_ctx,
                              uint64_t *stack, void *mem)
{
	(void)import_ctx;
	(void)mem;
	struct phase1a_ctx *ctx = (struct phase1a_ctx *)m3_GetUserData(runtime);

	uint32_t *raw_return = (uint32_t *)(stack++);
	/* Host-owned pid, exactly like desktop yos_getpid returns ctx->proc->tgid
	 * rather than the underlying host pid. */
	*raw_return = (uint32_t)ctx->pid;
	return m3Err_none;
}

/* env.exit : v(i) — void exit(int code) */
static const void *m3w_exit(IM3Runtime runtime, IM3ImportContext import_ctx,
                            uint64_t *stack, void *mem)
{
	(void)import_ctx;
	(void)mem;
	struct phase1a_ctx *ctx = (struct phase1a_ctx *)m3_GetUserData(runtime);

	uint32_t code = *(uint32_t *)(stack++);
	ctx->exit_requested = 1;
	ctx->exit_code = (int32_t)code;
	/* Trap out of m3_CallV cleanly. The desktop host bottoms out in host
	 * exit()/pthread_exit(); in the browser host that would kill the whole
	 * module, so a non-forking guest signals exit via a trap the host
	 * recognises — a legitimate browser-platform-backend difference. */
	return m3Err_trapExit;
}

static int link_phase1a_imports(IM3Module module)
{
	struct {
		const char *name;
		const char *signature;
		M3RawCall   fn;
	} imports[] = {
		{ "write",  "i(iii)", &m3w_write },
		{ "getpid", "i()",    &m3w_getpid },
		{ "exit",   "v(i)",   &m3w_exit },
	};
	for (unsigned i = 0; i < sizeof(imports) / sizeof(imports[0]); i++) {
		M3Result r = m3_LinkRawFunction(module, "env", imports[i].name,
		                                imports[i].signature, imports[i].fn);
		/* functionLookupFailed just means this guest doesn't import that
		 * name — not an error. Anything else is a real link failure. */
		if (r != m3Err_none && r != m3Err_functionLookupFailed) {
			fprintf(stderr, "phase1a: link env.%s failed: %s\n",
			        imports[i].name, r);
			return -1;
		}
	}
	return 0;
}

#define YOS_PHASE1A_CHECK(call)                                                 \
	do {                                                                    \
		M3Result phase1a_result = (call);                               \
		if (phase1a_result != m3Err_none) {                             \
			fprintf(stderr, "phase1a: %s failed: %s\n", #call,      \
			        phase1a_result);                                \
			return 1;                                               \
		}                                                               \
	} while (0)

int main(void)
{
	IM3Environment environment = m3_NewEnvironment();
	if (environment == NULL) {
		fprintf(stderr, "phase1a: m3_NewEnvironment failed\n");
		return 1;
	}

	/* The ctx pointer is handed to the runtime as userdata, so every bridge
	 * wrapper recovers it via m3_GetUserData. memory/memory_size are filled
	 * in after the module loads. */
	struct phase1a_ctx ctx = { 0 };
	ctx.pid = 4242;

	IM3Runtime runtime = m3_NewRuntime(environment, 64 * 1024, &ctx);
	if (runtime == NULL) {
		fprintf(stderr, "phase1a: m3_NewRuntime failed\n");
		m3_FreeEnvironment(environment);
		return 1;
	}

	IM3Module module = NULL;
	YOS_PHASE1A_CHECK(m3_ParseModule(environment, &module, bridge_guest_wasm,
	                                 bridge_guest_wasm_len));
	YOS_PHASE1A_CHECK(m3_LoadModule(runtime, module));

	if (link_phase1a_imports(module) != 0)
		return 1;

	uint32_t memory_size = 0;
	ctx.memory = m3_GetMemory(runtime, &memory_size, 0);
	ctx.memory_size = memory_size;
	if (ctx.memory == NULL) {
		fprintf(stderr, "phase1a: guest has no linear memory\n");
		return 1;
	}

	IM3Function start = NULL;
	YOS_PHASE1A_CHECK(m3_FindFunction(&start, runtime, "_start"));

	M3Result run = m3_CallV(start);
	/* A clean exit() traps out with m3Err_trapExit — expected, not a failure,
	 * as long as the guest actually asked to exit. */
	if (run != m3Err_none && !(run == m3Err_trapExit && ctx.exit_requested)) {
		fprintf(stderr, "phase1a: guest run failed: %s\n", run);
		return 1;
	}

	printf("phase1a: guest exited via env.exit, code=%d\n", ctx.exit_code);
	fflush(stdout);

	m3_FreeRuntime(runtime);
	m3_FreeEnvironment(environment);

	/* Non-zero unless the guest exited with the exact code it passed to the
	 * C exit wrapper — proves exit went through the bridge, not a no-op. */
	return (ctx.exit_requested && ctx.exit_code == 7) ? 0 : 3;
}
