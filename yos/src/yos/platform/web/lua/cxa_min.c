/* Minimal C++ exception ABI for liblua.wasm (epic #33).
 *
 * liblua is compiled with -fwasm-exceptions so Lua's pcall/error use native
 * wasm exception handling. clang emits calls to the Itanium ABI helpers
 * (__cxa_allocate_exception / __cxa_throw / __cxa_begin_catch / …) which
 * normally live in libc++abi. We don't link libc++abi (it needs a full C++
 * runtime), so provide the tiny single-threaded subset Lua actually uses:
 * allocate an exception object, throw it via the wasm `throw` instruction, and
 * free it when the catch handler is done. No RTTI, no exception chaining —
 * Lua's LUAI_THROW throws a lua_longjmp* and its LUAI_TRY catches everything,
 * so that is all that is needed.
 *
 * Compiled as C++ (built alongside the Lua sources) with -fwasm-exceptions.
 */

typedef __SIZE_TYPE__ size_t;
extern void *malloc(size_t);
extern void free(void *);

/* Space reserved before the thrown object for the ABI header. libc++abi uses a
 * larger __cxa_exception; Lua never inspects it, so a small aligned pad is
 * enough to keep the returned object pointer distinct and freeable. */
#define CXA_HEADER 64

extern "C" void *__cxa_allocate_exception(size_t size)
{
	char *base = (char *)malloc(CXA_HEADER + size);
	return base ? base + CXA_HEADER : (void *)0;
}

extern "C" void __cxa_free_exception(void *thrown)
{
	if (thrown)
		free((char *)thrown - CXA_HEADER);
}

/* Throw via the wasm exception instruction. Tag 0 is the C++ exception tag the
 * -fwasm-exceptions codegen emits for this module. */
extern "C" __attribute__((noreturn)) void __cxa_throw(void *thrown, void *tinfo,
                                                      void (*dtor)(void *))
{
	(void)tinfo;
	(void)dtor;
	__builtin_wasm_throw(0, thrown);
	__builtin_unreachable();
}

/* Single in-flight exception is sufficient for Lua's non-nested pcall model. */
static void *g_current;

extern "C" void *__cxa_begin_catch(void *exc)
{
	g_current = exc;
	return exc;
}

extern "C" void __cxa_end_catch(void)
{
	if (g_current) {
		__cxa_free_exception(g_current);
		g_current = (void *)0;
	}
}

extern "C" __attribute__((noreturn)) void __cxa_rethrow(void)
{
	__builtin_wasm_throw(0, g_current);
	__builtin_unreachable();
}
