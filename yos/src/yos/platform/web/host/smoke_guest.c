/* Phase 0 smoke guest — the most trivial possible yos guest.
 *
 * It has ZERO imports: no env.write, no env.anything. That is the point.
 * The Phase 0 proof is only "the wasm3 interpreter, itself compiled to a
 * browser-targeted wasm module, can parse + instantiate + execute a guest
 * wasm and hand back a result." Nothing about the FreeBSD libc surface is
 * exercised yet (that begins in Phase 1a), so the guest must not call
 * anything the host would have to resolve.
 *
 * Built with:
 *   clang -target wasm32-unknown-unknown -nostdlib \
 *         -Wl,--no-entry -Wl,--export=compute
 *
 * Note there is NO -Wl,--import-memory here (unlike the shared-memory
 * proof slice in ../build.sh): a stand-alone guest must DEFINE its own
 * linear memory so wasm3 can instantiate the module from its own memory
 * section.
 */

/* Compute 42 at run time (6 * 7 via a loop) rather than returning a bare
 * constant, so the result genuinely comes out of executed guest opcodes
 * inside the interpreter — not something the toolchain could fold away or
 * the host could fake without actually running the module. */
__attribute__((export_name("compute"))) int compute(void)
{
	int total = 0;
	for (int i = 0; i < 6; i++)
		total += 7;
	return total;
}
