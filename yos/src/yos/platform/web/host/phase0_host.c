/* Phase 0 host — wasm3-in-wasm smoke.
 *
 * This file is compiled to a browser-targeted wasm module with Emscripten
 * (emcc), together with the vendored wasm3 interpreter sources. It proves
 * the single fact Phase 0 exists to prove:
 *
 *   the wasm3 interpreter, running INSIDE a browser wasm host, can parse,
 *   instantiate, and execute a trivial guest wasm and return its result —
 *   with no JavaScript reimplementation of any guest libc/syscall.
 *
 * The guest bytes are embedded as a C array (smoke_guest_wasm.h, generated
 * by build-phase0.sh). Embedding — rather than emcc --embed-file — keeps the
 * path pure C -> wasm3 and avoids pulling in Emscripten's MEMFS + a JS
 * fopen/read filesystem shim, which would count as JS libc behaviour.
 *
 * Guest stdout is not involved at all in Phase 0: the guest has no imports.
 * The single line of output below is the HOST's own printf, which under
 * Emscripten flows through musl write(fd 1) to the JS `print` hook the node
 * harness (phase0_smoke.mjs) installs. That hook is capture, not libc.
 */

#include <stdint.h>
#include <stdio.h>

#include "wasm3.h"

#include "smoke_guest_wasm.h"

/* Every wasm3 entry point returns M3Result, which is a `const char *`:
 * NULL on success, a static message string on failure. Checking every one
 * is what makes the Phase 0 gate real — if wasm3-in-wasm cannot execute the
 * guest for any reason, main() returns non-zero and the smoke test fails. */
#define YOS_PHASE0_CHECK(call)                                                  \
	do {                                                                    \
		M3Result phase0_result = (call);                                \
		if (phase0_result != m3Err_none) {                              \
			fprintf(stderr, "phase0: %s failed: %s\n", #call,       \
			        phase0_result);                                 \
			return 1;                                               \
		}                                                               \
	} while (0)

int main(void)
{
	IM3Environment environment = m3_NewEnvironment();
	if (environment == NULL) {
		fprintf(stderr, "phase0: m3_NewEnvironment failed\n");
		return 1;
	}

	/* 64 KiB interpreter stack is ample for the trivial guest. */
	IM3Runtime runtime = m3_NewRuntime(environment, 64 * 1024, NULL);
	if (runtime == NULL) {
		fprintf(stderr, "phase0: m3_NewRuntime failed\n");
		m3_FreeEnvironment(environment);
		return 1;
	}

	IM3Module module = NULL;
	YOS_PHASE0_CHECK(m3_ParseModule(environment, &module, smoke_guest_wasm,
	                                smoke_guest_wasm_len));

	/* On success the runtime takes ownership of the module — it must NOT
	 * be freed here (wasm3 frees it with the runtime). */
	YOS_PHASE0_CHECK(m3_LoadModule(runtime, module));

	IM3Function compute = NULL;
	YOS_PHASE0_CHECK(m3_FindFunction(&compute, runtime, "compute"));

	YOS_PHASE0_CHECK(m3_CallV(compute));

	int32_t result = 0;
	YOS_PHASE0_CHECK(m3_GetResultsV(compute, &result));

	printf("phase0: wasm3 executed guest, compute()=%d\n", result);
	fflush(stdout);

	m3_FreeRuntime(runtime);
	m3_FreeEnvironment(environment);

	/* Non-zero on a wrong result so the smoke test also catches a wasm3
	 * that "runs" but computes garbage, not only one that fails to run. */
	return (result == 42) ? 0 : 2;
}
