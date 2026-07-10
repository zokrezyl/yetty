/* yos-host general runner (epic #33, issue #37).
 *
 * A browser-host entry point that runs an ARBITRARY guest tool wasm — the same
 * artifact desktop runs — through the real generated yos bridge + impl/vfs.
 * Where phase1b used a fixed custom guest, this reads the guest wasm from
 * /guest.wasm in the Emscripten MEMFS (the JS parity harness writes it there,
 * along with any input files, before main runs) and passes it the program's
 * argv as the guest's argv.
 *
 * It ports the small host-glue import set the desktop keeps in main.c —
 * argv/env bootstrap (__yos_argc / __yos_argv_setup / __yos_envc /
 * __yos_envp_setup), the errno accessor (__error), abort/assert/stack-check —
 * and, crucially, a browser-safe exit: desktop exit() tears down the process,
 * which would kill the whole host module, so the runner links its own
 * exit/_exit/_Exit that trap out of the guest call and hand the code back.
 *
 * The override works because wasm3's LinkRawFunction skips an already-linked
 * import ("specific links before wildcard"): the runner links its glue FIRST,
 * then yos_brg_link_imports links everything else, so the runner's exit wins.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yos/types.h"

#include "wasm3.h"

extern int  yos_brg_link_imports(IM3Module module);
extern void yos_fd_table_init(struct yos_exec_ctx *ctx);

/* Per-module link functions the desktop main.c calls in addition to
 * yos_brg_link_imports — they register imports the generated bridge does not
 * (getprogname/err/warn, strtol family, qsort/bsearch callbacks, sysctl,
 * signals, 128-bit int/float, libc globals). */
extern void yos_freebsd_userland_link(IM3Module mod);
extern void yos_strto_link(IM3Module mod);
extern void yos_callback_link(IM3Module mod);
extern void yos_sysctl_link(IM3Module mod);
extern void yos_signal_link(IM3Module mod);
extern void yos_f128_link(IM3Module mod);
extern void yos_i128_link(IM3Module mod);
extern void yos_libc_globals_link(IM3Module mod);

/* Variadic printf/scanf family — the guest packs its varargs into a typed
 * array and passes its offset; these dispatch into yos's Tier-2 formatters. */
extern int32_t yos_printf(struct yos_exec_ctx *ctx, uint32_t fmt, uint32_t va);
extern int32_t yos_fprintf(struct yos_exec_ctx *ctx, uint32_t fp, uint32_t fmt, uint32_t va);
extern int32_t yos_sprintf(struct yos_exec_ctx *ctx, uint32_t dst, uint32_t fmt, uint32_t va);
extern int32_t yos_snprintf(struct yos_exec_ctx *ctx, uint32_t dst, uint32_t n, uint32_t fmt, uint32_t va);
extern int32_t yos_vprintf(struct yos_exec_ctx *ctx, uint32_t fmt, uint32_t va);
extern int32_t yos_vfprintf(struct yos_exec_ctx *ctx, uint32_t fp, uint32_t fmt, uint32_t va);
extern int32_t yos_scanf(struct yos_exec_ctx *ctx, uint32_t fmt, uint32_t va);
extern int32_t yos_sscanf(struct yos_exec_ctx *ctx, uint32_t src, uint32_t fmt, uint32_t va);

/* The runner's exit code, captured by the exit trap below. */
struct host_run_state {
	int exited;
	int exit_code;
};

/* ── host-glue raw wrappers (ported minimal from main.c) ─────────────── */

static const void *hostrun_argc(IM3Runtime runtime, IM3ImportContext ic,
                                uint64_t *sp, void *mem)
{
	(void)ic; (void)mem;
	struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
	uint32_t *ret = (uint32_t *)(sp++);
	*ret = (uint32_t)ctx->argc;
	return m3Err_none;
}

static const void *hostrun_argv_setup(IM3Runtime runtime, IM3ImportContext ic,
                                      uint64_t *sp, void *mem)
{
	(void)ic; (void)mem;
	struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
	uint32_t argv_ptr = *(uint32_t *)(sp++);
	uint32_t msize = 0;
	ctx->memory = m3_GetMemory(runtime, &msize, 0);
	ctx->memory_size = msize;
	if (argv_ptr == 0 ||
	    (uint64_t)argv_ptr + 4ULL * (uint64_t)((uint32_t)ctx->argc + 1) > (uint64_t)msize)
		return m3Err_trapOutOfBoundsMemoryAccess;
	uint32_t str_ptr = ctx->heap_end;
	if (str_ptr == 0)
		str_ptr = (argv_ptr + 4u * ((uint32_t)ctx->argc + 1) + 15u) & ~15u;
	uint32_t *argv_arr = (uint32_t *)(ctx->memory + argv_ptr);
	for (int i = 0; i < ctx->argc; i++) {
		size_t len = strlen(ctx->argv[i]) + 1;
		if ((uint64_t)str_ptr + (uint64_t)len > (uint64_t)ctx->memory_size)
			return m3Err_trapOutOfBoundsMemoryAccess;
		memcpy(ctx->memory + str_ptr, ctx->argv[i], len);
		argv_arr[i] = str_ptr;
		str_ptr += (uint32_t)len;
	}
	argv_arr[ctx->argc] = 0;
	ctx->heap_end = (str_ptr + 15) & ~15u;
	return m3Err_none;
}

static const void *hostrun_envc(IM3Runtime runtime, IM3ImportContext ic,
                                uint64_t *sp, void *mem)
{
	(void)ic; (void)mem;
	struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
	uint32_t *ret = (uint32_t *)(sp++);
	*ret = (uint32_t)ctx->envc;
	return m3Err_none;
}

static const void *hostrun_envp_setup(IM3Runtime runtime, IM3ImportContext ic,
                                      uint64_t *sp, void *mem)
{
	(void)ic; (void)mem;
	struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
	uint32_t envp_ptr = *(uint32_t *)(sp++);
	uint32_t msize = 0;
	ctx->memory = m3_GetMemory(runtime, &msize, 0);
	ctx->memory_size = msize;
	int n = ctx->envc;
	if (envp_ptr == 0 ||
	    (uint64_t)envp_ptr + 4ULL * (uint64_t)((uint32_t)n + 1) > (uint64_t)msize)
		return m3Err_trapOutOfBoundsMemoryAccess;
	uint32_t str_ptr = ctx->heap_end;
	uint32_t *envp_arr = (uint32_t *)(ctx->memory + envp_ptr);
	for (int i = 0; i < n; i++) {
		size_t len = strlen(ctx->envp[i]) + 1;
		if ((uint64_t)str_ptr + (uint64_t)len > (uint64_t)ctx->memory_size)
			return m3Err_trapOutOfBoundsMemoryAccess;
		memcpy(ctx->memory + str_ptr, ctx->envp[i], len);
		envp_arr[i] = str_ptr;
		str_ptr += (uint32_t)len;
	}
	envp_arr[n] = 0;
	ctx->heap_end = (str_ptr + 15) & ~15u;
	return m3Err_none;
}

/* env.__error: return the wasm offset of the per-ctx errno slot. */
static const void *hostrun_error(IM3Runtime runtime, IM3ImportContext ic,
                                 uint64_t *sp, void *mem)
{
	(void)ic; (void)mem;
	struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
	uint32_t *ret = (uint32_t *)(sp++);
	*ret = (uint32_t)ctx->errno_off;
	return m3Err_none;
}

/* Browser-safe exit: capture the code and trap out; do NOT call host exit().
 * The host_run_state comes from the import's userdata (set at link time), not
 * from ctx — struct yos_exec_ctx has no spare field to stash it in. */
static const void *hostrun_exit(IM3Runtime runtime, IM3ImportContext ic,
                                uint64_t *sp, void *mem)
{
	(void)runtime; (void)mem;
	uint32_t code = *(uint32_t *)(sp++);
	struct host_run_state *state = (struct host_run_state *)ic->userdata;
	if (state) {
		state->exited = 1;
		state->exit_code = (int)code;
	}
	return m3Err_trapExit;
}

static const void *hostrun_abort(IM3Runtime runtime, IM3ImportContext ic,
                                 uint64_t *sp, void *mem)
{
	(void)runtime; (void)ic; (void)sp; (void)mem;
	return m3Err_trapAbort;
}

static const void *hostrun_assert(IM3Runtime runtime, IM3ImportContext ic,
                                  uint64_t *sp, void *mem)
{
	(void)runtime; (void)ic; (void)sp; (void)mem;
	return m3Err_trapAbort;
}

static const void *hostrun_stack_chk_fail(IM3Runtime runtime, IM3ImportContext ic,
                                          uint64_t *sp, void *mem)
{
	(void)runtime; (void)ic; (void)sp; (void)mem;
	return m3Err_trapAbort;
}

/* env.__main_argc_argv(argc, argv_ptr): clang's command-exec model lowers the
 * crt1's call to the user main() into this import. Resolve it to the module's
 * real `main` and call it, propagating an exit() trap up unchanged. */
static const void *hostrun_main_argc_argv(IM3Runtime runtime, IM3ImportContext ic,
                                          uint64_t *sp, void *mem)
{
	(void)ic; (void)mem;
	uint32_t *raw_return = (uint32_t *)(sp++);
	int32_t argc = *(int32_t *)(sp++);
	int32_t argv = *(int32_t *)(sp++);

	IM3Function main_fn = NULL;
	M3Result r = m3_FindFunction(&main_fn, runtime, "main");
	if (r != m3Err_none || main_fn == NULL) {
		*raw_return = (uint32_t)-1;
		return m3Err_none;
	}
	r = m3_CallV(main_fn, argc, argv);
	if (r != m3Err_none)
		return r; /* propagate exit()/abort() trap to the outer m3_CallV */
	uint32_t main_ret = 0;
	m3_GetResultsV(main_fn, &main_ret);
	*raw_return = main_ret;
	return m3Err_none;
}

/* Variadic printf/scanf raw wrappers: pop the wasm-ABI args and call the
 * matching yos formatter. Signatures match main.c's link table exactly. */
#define YOS_PRINTF_WRAP(NAME, CALL)                                             \
	static const void *NAME(IM3Runtime runtime, IM3ImportContext ic,        \
	                        uint64_t *sp, void *mem)                        \
	{                                                                       \
		(void)ic; (void)mem;                                            \
		struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime); \
		uint32_t *raw_return = (uint32_t *)(sp++);                      \
		uint32_t a0 = *(uint32_t *)(sp++);                              \
		uint32_t a1 = *(uint32_t *)(sp++);                              \
		uint32_t a2 = *(uint32_t *)(sp++);                              \
		uint32_t a3 = *(uint32_t *)(sp++);                              \
		(void)a2; (void)a3;                                             \
		*raw_return = (uint32_t)(CALL);                                 \
		return m3Err_none;                                              \
	}
YOS_PRINTF_WRAP(hostrun_printf,   yos_printf(ctx, a0, a1))
YOS_PRINTF_WRAP(hostrun_fprintf,  yos_fprintf(ctx, a0, a1, a2))
YOS_PRINTF_WRAP(hostrun_sprintf,  yos_sprintf(ctx, a0, a1, a2))
YOS_PRINTF_WRAP(hostrun_snprintf, yos_snprintf(ctx, a0, a1, a2, a3))
YOS_PRINTF_WRAP(hostrun_vprintf,  yos_vprintf(ctx, a0, a1))
YOS_PRINTF_WRAP(hostrun_vfprintf, yos_vfprintf(ctx, a0, a1, a2))
YOS_PRINTF_WRAP(hostrun_scanf,    yos_scanf(ctx, a0, a1))
YOS_PRINTF_WRAP(hostrun_sscanf,   yos_sscanf(ctx, a0, a1, a2))

static void link_host_glue(IM3Module module, struct yos_exec_ctx *ctx,
                           struct host_run_state *state)
{
	/* Variadic printf/scanf family (the generated bridge does not link
	 * these; the desktop main.c links them by hand as here). */
	m3_LinkRawFunction(module, "env", "printf", "i(ii)", hostrun_printf);
	m3_LinkRawFunction(module, "env", "fprintf", "i(iii)", hostrun_fprintf);
	m3_LinkRawFunction(module, "env", "sprintf", "i(iii)", hostrun_sprintf);
	m3_LinkRawFunction(module, "env", "snprintf", "i(iiii)", hostrun_snprintf);
	m3_LinkRawFunction(module, "env", "vprintf", "i(ii)", hostrun_vprintf);
	m3_LinkRawFunction(module, "env", "vfprintf", "i(iii)", hostrun_vfprintf);
	m3_LinkRawFunction(module, "env", "scanf", "i(ii)", hostrun_scanf);
	m3_LinkRawFunction(module, "env", "sscanf", "i(iii)", hostrun_sscanf);

	/* Linked BEFORE yos_brg_link_imports so these win (LinkRawFunction skips
	 * an already-linked import). argc/argv/env/error carry ctx; exit carries
	 * the run state so it can record the code. */
	m3_LinkRawFunctionEx(module, "env", "__yos_argc", "i()", hostrun_argc, ctx);
	m3_LinkRawFunctionEx(module, "env", "__yos_argv_setup", "v(i)", hostrun_argv_setup, ctx);
	m3_LinkRawFunctionEx(module, "env", "__yos_envc", "i()", hostrun_envc, ctx);
	m3_LinkRawFunctionEx(module, "env", "__yos_envp_setup", "v(i)", hostrun_envp_setup, ctx);
	m3_LinkRawFunctionEx(module, "env", "__error", "i()", hostrun_error, ctx);
	m3_LinkRawFunctionEx(module, "env", "exit", "v(i)", hostrun_exit, state);
	m3_LinkRawFunctionEx(module, "env", "_exit", "v(i)", hostrun_exit, state);
	m3_LinkRawFunctionEx(module, "env", "_Exit", "v(i)", hostrun_exit, state);
	m3_LinkRawFunction(module, "env", "abort", "v()", hostrun_abort);
	m3_LinkRawFunction(module, "env", "__assert", "v(iiii)", hostrun_assert);
	m3_LinkRawFunction(module, "env", "__stack_chk_fail", "v()", hostrun_stack_chk_fail);
	m3_LinkRawFunction(module, "env", "__main_argc_argv", "i(ii)", hostrun_main_argc_argv);
}

/* Read /guest.wasm from MEMFS (written by the JS harness before main runs). */
static uint8_t *read_guest(const char *path, size_t *out_len)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n <= 0) { fclose(f); return NULL; }
	uint8_t *buf = malloc((size_t)n);
	if (!buf) { fclose(f); return NULL; }
	size_t got = fread(buf, 1, (size_t)n, f);
	fclose(f);
	if (got != (size_t)n) { free(buf); return NULL; }
	*out_len = (size_t)n;
	return buf;
}

int main(int argc, char **argv)
{
	/* The guest wasm lives at /guest.wasm; the guest's argv is this program's
	 * argv[1..] (argv[0] here is the emscripten program name). So guest
	 * argv[0] is the tool name the harness passed first. */
	size_t guest_len = 0;
	uint8_t *guest = read_guest("/guest.wasm", &guest_len);
	if (!guest) {
		fprintf(stderr, "yos-host: cannot read /guest.wasm from MEMFS\n");
		return 70;
	}

	IM3Environment environment = m3_NewEnvironment();
	if (!environment) { fprintf(stderr, "yos-host: NewEnvironment failed\n"); return 70; }

	struct yos_runtime *rt = calloc(1, sizeof(struct yos_runtime));
	struct yos_exec_ctx *ctx = calloc(1, sizeof(struct yos_exec_ctx));
	struct host_run_state state = { 0 };
	if (!rt || !ctx) { fprintf(stderr, "yos-host: oom\n"); return 70; }
	ctx->rt = rt;

	/* Guest argv = host argv[1..]. */
	ctx->argc = argc - 1;
	ctx->argv = (char **)(argv + 1);
	ctx->envc = 0;
	ctx->envp = NULL;

	IM3Runtime runtime = m3_NewRuntime(environment, 4 * 1024 * 1024, ctx);
	if (!runtime) { fprintf(stderr, "yos-host: NewRuntime failed\n"); return 70; }

	IM3Module module = NULL;
	M3Result r = m3_ParseModule(environment, &module, guest, (uint32_t)guest_len);
	if (r) { fprintf(stderr, "yos-host: parse: %s\n", r); return 70; }
	r = m3_LoadModule(runtime, module);
	if (r) { fprintf(stderr, "yos-host: load: %s\n", r); return 70; }

	/* Glue first (so exit/etc. win), then the full generated bridge, then the
	 * per-module link functions the desktop links alongside it. */
	link_host_glue(module, ctx, &state);
	yos_brg_link_imports(module);
	yos_freebsd_userland_link(module);
	yos_strto_link(module);
	yos_callback_link(module);
	yos_sysctl_link(module);
	yos_signal_link(module);
	yos_f128_link(module);
	yos_i128_link(module);
	yos_libc_globals_link(module);

	uint32_t memory_size = 0;
	ctx->memory = m3_GetMemory(runtime, &memory_size, 0);
	ctx->memory_size = memory_size;
	ctx->runtime = runtime;
	ctx->module = module;

	/* heap_end from the guest's __heap_base global (fallback 0x50000); errno
	 * slot at the conventional low offset. */
	ctx->heap_end = 0x50000;
	IM3Global heap_base = m3_FindGlobal(module, "__heap_base");
	if (heap_base) {
		M3TaggedValue v;
		if (m3_GetGlobal(heap_base, &v) == m3Err_none && v.type == c_m3Type_i32) {
			uint32_t hb = (uint32_t)v.value.i32;
			if (hb > ctx->heap_end)
				ctx->heap_end = hb;
		}
	}
	ctx->errno_off = 0x108;

	yos_fd_table_init(ctx);

	/* Invoke the crt1 entry `_start` (no args): it calls __yos_argc /
	 * __yos_argv_setup to build argv in wasm memory, then env.__main_argc_argv
	 * (our wrapper above), which runs the module's main. Fall back to main. */
	IM3Function entry = NULL;
	r = m3_FindFunction(&entry, runtime, "_start");
	if (r || !entry)
		r = m3_FindFunction(&entry, runtime, "main");
	if (r || !entry) {
		fprintf(stderr, "yos-host: guest has no entry point\n");
		return 70;
	}

	M3Result run = m3_CallV(entry);
	if (run != m3Err_none && !(run == m3Err_trapExit && state.exited)) {
		fprintf(stderr, "yos-host: guest trap: %s\n", run);
		return 70;
	}

	/* The guest's stdout/stderr FILE* map to the host's stdout/stderr FILE*
	 * (see impl/io/file.c). Tools that write via printf/puts buffer there;
	 * our exit trap bypasses yos_exit's own fflush(NULL), so flush every
	 * stream now or buffered output is lost. */
	fflush(NULL);
	return state.exited ? state.exit_code : 0;
}
