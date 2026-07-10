// lua_bridge.mjs — provide the Lua C API to a guest (nvim) in the JS process
// engine by loading liblua.wasm and SHARING the guest's linear memory + table
// (epic #33 — the browser gets the same Lua the desktop gets from host liblua).
//
// Why shared memory/table:
//   - lua_State and every pointer nvim passes to lua_* live in the guest's
//     memory; liblua sees them directly (one memory), so no marshalling.
//   - lua_pushcclosure registers a nvim C function (a guest table index); Lua
//     later call_indirect's it through the SHARED table, so the callback lands
//     back in nvim. (The table-export patch, wasm_patch.mjs, makes the guest's
//     table reachable.)
//   - liblua's static data is parked at LUA_DATA_BASE (256 MiB, set at build);
//     the host reserves that slice so the guest allocator never overwrites it.
//
// liblua is built as C++ with -fwasm-exceptions, so Lua's pcall/error use native
// wasm exceptions — no setjmp bridge.

const LUA_DATA_BASE = 268435456; // 256 MiB — must match build-liblua.sh
const LUA_DATA_RESERVE = 24 * 1024 * 1024; // data + 16 MiB parser stack
// liblua's element segment sits at this offset in the SHARED function table
// (build-liblua.sh --table-base — must match). Parked high so instantiating
// liblua never overwrites the guest's own table entries; the guest table is
// grown to cover it below. Fixed (not "current table end") so function-pointer
// values stay identical across fork — a child re-instantiating liblua then
// agrees with Lua state copied from the parent.
const LUA_TABLE_BASE = 65536;
const LUA_TABLE_RESERVE = 1024; // >= liblua's element count (~170)

let libluaModule = null; // compiled WebAssembly.Module (loaded once)

// Load + compile liblua.wasm. Works in node (fs) and browser (fetch).
export async function loadLiblua() {
	if (libluaModule) return libluaModule;
	let bytes;
	if (typeof fetch === "function" && typeof window !== "undefined") {
		bytes = await (await fetch(new URL("./liblua.wasm", import.meta.url))).arrayBuffer();
	} else {
		const { readFileSync } = await import("node:fs");
		const { fileURLToPath } = await import("node:url");
		bytes = readFileSync(fileURLToPath(new URL("./liblua.wasm", import.meta.url)));
	}
	libluaModule = await WebAssembly.compile(bytes);
	return libluaModule;
}

// The set of lua_*/luaL_* export names liblua provides (for building forwarders).
export function luaExportNames() {
	return WebAssembly.Module.exports(libluaModule)
		.filter((e) => e.kind === "function" && /^lua/.test(e.name))
		.map((e) => e.name);
}

// Install lua_* forwarders on `env`. They lazily instantiate liblua on first
// call (by then the guest is instantiated, so its memory/table exist).
//   getGuest()  → the guest WebAssembly.Instance (with exports.memory + table)
//   luaEnv      → the import object for liblua (libc funcs on the shared memory)
//   growTo(n)   → grow the guest memory to at least n bytes
//   reserve(lo, hi) → tell the host allocator to avoid [lo, hi)
export function installLuaForwarders(env, getGuest, luaEnv, growTo, reserve) {
	if (!libluaModule) return; // liblua not loaded — nothing to forward or reserve
	let luaInst = null;
	// Reserve liblua's data+stack window EAGERLY (not at first lua_* call):
	// the guest's heap may cross LUA_DATA_BASE before Lua is ever touched, and
	// instantiating liblua would then shred live guest data — or, the other
	// way round, a later-growing guest heap would shred live Lua state (nvim's
	// embedded server died with "unreachable" traps exactly that way).
	if (reserve) reserve(LUA_DATA_BASE, LUA_DATA_BASE + LUA_DATA_RESERVE);
	const ensure = () => {
		if (luaInst) return luaInst;
		const guest = getGuest();
		const mem = guest.exports.memory;
		// liblua's static data + stack live around LUA_DATA_BASE in the shared
		// memory; grow it so those addresses exist before liblua runs.
		if (growTo) growTo(LUA_DATA_BASE + LUA_DATA_RESERVE);
		// Grow the shared function table past liblua's --table-base so its
		// element segment has slots to land in (the guest's table max is lifted
		// by wasm_patch.mjs; an unpatched fixed-max table throws here — loudly,
		// which beats the silent table corruption it would otherwise be).
		const table = guest.exports.__indirect_function_table;
		const need = LUA_TABLE_BASE + LUA_TABLE_RESERVE;
		if (table && table.length < need) table.grow(need - table.length);
		// liblua's libc imports come from the guest env (shared memory), plus a
		// stub fallback for any the JS host doesn't implement (mostly FILE* stdio
		// Lua's io/os libs pull in but nvim's init path doesn't exercise).
		const base = { ...luaEnv, memory: mem, __indirect_function_table: guest.exports.__indirect_function_table };
		const imports = { env: new Proxy(base, { get(t, k) { return k in t ? t[k] : () => 0; } }) };
		luaInst = new WebAssembly.Instance(libluaModule, imports);
		if (luaInst.exports.__wasm_call_ctors) luaInst.exports.__wasm_call_ctors();
		return luaInst;
	};
	for (const name of luaExportNames()) {
		env[name] = (...args) => ensure().exports[name](...args);
	}
}
