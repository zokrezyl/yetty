/* Phase 1b host (epic #33, issue #36).
 *
 * The Phase 1a host hand-wrote three bridge wrappers to prove the shape. This
 * host uses the REAL generated bridge (build-<host>/src/yos/codegen/yos_bridge.c)
 * plus the yos impl and vfs source trees, compiled to wasm with Emscripten. So
 * the guest's env.* libc calls get FreeBSD-shaped behaviour from the same C
 * code desktop runs — pointer translation, fd_map, errno remap, struct
 * conversion — with Emscripten musl/MEMFS as the storage substrate beneath
 * yos's VFS.
 *
 * This is the minimal bootstrap: a zeroed struct yos_exec_ctx wired with the
 * guest memory base, an fd table (stdio dup'd to fd_map[0..2]), and a bare
 * yos_runtime. It is enough for the pure/io surface (write/read/open/close/
 * getpid). Fork/exec/wait/pthreads and full proc setup are Phase 3 (#38); the
 * generated bridge references those too, but with -sERROR_ON_UNDEFINED_SYMBOLS=0
 * the un-compiled impls become inert import stubs the echo/cat guest never
 * calls.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "yos/types.h"

#include "wasm3.h"

#include "phase1b_guest_wasm.h"

/* Generated in yos_bridge.c: links every env.<libc> import to its m3w_* raw
 * wrapper. Returns the number of hard link failures (0 = all linked). */
extern int  yos_brg_link_imports(IM3Module module);
/* Idempotent fd-table bootstrap: fd_map[0..2] = dup(host 0/1/2), rest -1. */
extern void yos_fd_table_init(struct yos_exec_ctx *ctx);

#define YOS_PHASE1B_CHECK(call)                                                 \
	do {                                                                    \
		M3Result phase1b_result = (call);                               \
		if (phase1b_result != m3Err_none) {                             \
			fprintf(stderr, "phase1b: %s failed: %s\n", #call,      \
			        phase1b_result);                                \
			return 1;                                               \
		}                                                               \
	} while (0)

int main(void)
{
	IM3Environment environment = m3_NewEnvironment();
	if (environment == NULL) {
		fprintf(stderr, "phase1b: m3_NewEnvironment failed\n");
		return 1;
	}

	struct yos_runtime *rt = calloc(1, sizeof(struct yos_runtime));
	struct yos_exec_ctx *ctx = calloc(1, sizeof(struct yos_exec_ctx));
	if (rt == NULL || ctx == NULL) {
		fprintf(stderr, "phase1b: out of memory\n");
		return 1;
	}
	ctx->rt = rt;

	/* ctx is the runtime userdata — every generated wrapper recovers it via
	 * m3_GetUserData(runtime). */
	IM3Runtime runtime = m3_NewRuntime(environment, 1024 * 1024, ctx);
	if (runtime == NULL) {
		fprintf(stderr, "phase1b: m3_NewRuntime failed\n");
		return 1;
	}

	IM3Module module = NULL;
	YOS_PHASE1B_CHECK(m3_ParseModule(environment, &module, phase1b_guest_wasm,
	                                 phase1b_guest_wasm_len));
	YOS_PHASE1B_CHECK(m3_LoadModule(runtime, module));

	int failed = yos_brg_link_imports(module);
	if (failed != 0) {
		fprintf(stderr, "phase1b: %d bridge imports failed to link\n", failed);
		return 1;
	}

	uint32_t memory_size = 0;
	ctx->memory = m3_GetMemory(runtime, &memory_size, 0);
	ctx->memory_size = memory_size;
	ctx->runtime = runtime;
	ctx->module = module;
	if (ctx->memory == NULL) {
		fprintf(stderr, "phase1b: guest has no linear memory\n");
		return 1;
	}

	/* Wire stdio into the per-ctx fd table so guest fd 1/2 reach the host's
	 * stdout/stderr (→ the JS print hooks under Emscripten). */
	yos_fd_table_init(ctx);

	IM3Function start = NULL;
	YOS_PHASE1B_CHECK(m3_FindFunction(&start, runtime, "_start"));

	/* The guest returns from _start rather than calling exit() (see the guest
	 * source): desktop yos_exit bottoms out in host exit(), which would tear
	 * down the whole host module. So a clean return is success. */
	YOS_PHASE1B_CHECK(m3_CallV(start));

	printf("phase1b: host regained control — guest ran through the generated bridge\n");
	fflush(stdout);
	return 0;
}
