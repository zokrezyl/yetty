// wasm_patch.mjs — load-time wasm binary patcher (epic #32).
//
// The tools built for yos do NOT export their function table, so a host that
// runs them on the NATIVE WebAssembly engine (the JS process engine,
// yos_proc.mjs) cannot reach the `__indirect_function_table` to invoke a guest
// function pointer. That silently breaks every callback-based libc call —
// qsort/bsearch/mergesort/heapsort comparators, signal handlers, pthread_once,
// atexit — because `proc.inst.exports.__indirect_function_table` is undefined
// and the handlers bail (`if (!table) return 0`).
//
// Symptom: `ps` prints "pid: keyword not found" (bsearch over its keyword
// table returns NULL), `qsort` is a no-op, installed signal handlers never
// fire. wasm3 (native yos) is unaffected because it accesses the table
// internally.
//
// This patches the wasm binary at load time to add a table export named
// `__indirect_function_table` for table index 0, so the JS host can call guest
// function pointers. No tool rebuild required. If the module has no table or
// already exports one, it is returned unchanged.

function decodeLEB(bytes, off) {
	let result = 0, shift = 0, byte;
	do { byte = bytes[off++]; result |= (byte & 0x7f) << shift; shift += 7; } while (byte & 0x80);
	return [result >>> 0, off];
}
function encodeLEB(value) {
	const out = [];
	value >>>= 0;
	do { let byte = value & 0x7f; value >>>= 7; if (value !== 0) byte |= 0x80; out.push(byte); } while (value !== 0);
	return out;
}

const TABLE_EXPORT_NAME = "__indirect_function_table";

// Returns true if the module has at least one table (imported or defined),
// so exporting table index 0 is valid.
function moduleHasTable(bytes) {
	let off = 8;
	while (off < bytes.length) {
		const id = bytes[off];
		const [size, after] = decodeLEB(bytes, off + 1);
		const end = after + size;
		if (id === 4) { // table section
			const [count] = decodeLEB(bytes, after);
			if (count > 0) return true;
		} else if (id === 2) { // import section — look for an imported table (kind 0x01)
			let [count, p] = decodeLEB(bytes, after);
			for (let i = 0; i < count; i++) {
				let [mlen, r] = decodeLEB(bytes, p); p = r + mlen;
				let [flen, r2] = decodeLEB(bytes, p); p = r2 + flen;
				const kind = bytes[p++];
				if (kind === 0x00) { const [, r3] = decodeLEB(bytes, p); p = r3; }        // func: typeidx
				else if (kind === 0x01) { return true; }                                  // table
				else if (kind === 0x02) { const fl = bytes[p++]; const [, r3] = decodeLEB(bytes, p); p = r3; if (fl === 1) { const [, r4] = decodeLEB(bytes, p); p = r4; } } // mem
				else if (kind === 0x03) { p += 1; const [, r3] = decodeLEB(bytes, p); p = r3; } // global: valtype + mut
			}
		}
		off = end;
	}
	return false;
}

export function patchWasmTableExport(input) {
	const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
	if (bytes.length < 8 || bytes[0] !== 0x00 || bytes[1] !== 0x61 || bytes[2] !== 0x73 || bytes[3] !== 0x6d)
		return bytes; // not a wasm binary
	if (!moduleHasTable(bytes)) return bytes; // nothing to export

	let off = 8;
	while (off < bytes.length) {
		const id = bytes[off];
		const [size, after] = decodeLEB(bytes, off + 1);
		const contentEnd = after + size;
		if (id === 7) { // export section
			let [count, p] = decodeLEB(bytes, after);
			// Bail if a table is already exported (don't add a duplicate).
			let q = p;
			for (let i = 0; i < count; i++) {
				let [nl, r] = decodeLEB(bytes, q); q = r + nl;
				const kind = bytes[q++];
				const [, r2] = decodeLEB(bytes, q); q = r2;
				if (kind === 0x01) return bytes;
			}
			const nameBytes = new TextEncoder().encode(TABLE_EXPORT_NAME);
			const entry = [...encodeLEB(nameBytes.length), ...nameBytes, 0x01, ...encodeLEB(0)];
			const oldExports = bytes.slice(p, contentEnd);
			const newCount = encodeLEB(count + 1);
			const newContent = new Uint8Array(newCount.length + oldExports.length + entry.length);
			newContent.set(newCount, 0);
			newContent.set(oldExports, newCount.length);
			newContent.set(entry, newCount.length + oldExports.length);
			const newSize = encodeLEB(newContent.length);
			const head = bytes.slice(0, off);
			const tail = bytes.slice(contentEnd);
			const out = new Uint8Array(head.length + 1 + newSize.length + newContent.length + tail.length);
			let w = 0;
			out.set(head, w); w += head.length;
			out[w++] = 7;
			out.set(newSize, w); w += newSize.length;
			out.set(newContent, w); w += newContent.length;
			out.set(tail, w);
			return out;
		}
		off = contentEnd;
	}
	return bytes; // no export section (unusual) — leave unchanged
}

// The tools link their function table with max == initial (lld default), so
// the JS host cannot table.grow() it. That blocks the shared-library pattern
// (lua_bridge.mjs): a companion wasm like liblua parks its element segment at
// a high --table-base and the host must grow the guest's table to cover it
// before instantiating. Lift table 0's max to this. Costs nothing until grown.
const TABLE_MAX_LIFTED = 1 << 20;

export function patchWasmTableMax(input) {
	const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
	if (bytes.length < 8 || bytes[0] !== 0x00 || bytes[1] !== 0x61 || bytes[2] !== 0x73 || bytes[3] !== 0x6d)
		return bytes;
	let off = 8;
	while (off < bytes.length) {
		const id = bytes[off];
		const [size, after] = decodeLEB(bytes, off + 1);
		const contentEnd = after + size;
		if (id === 4) { // table section: count, then per table reftype + limits
			let [count, p] = decodeLEB(bytes, after);
			if (count === 0) return bytes;
			const reftype = bytes[p++];
			const flags = bytes[p++];
			const [min, afterMin] = decodeLEB(bytes, p); p = afterMin;
			let max = min;
			if (flags & 0x01) { const [m, r] = decodeLEB(bytes, p); max = m; p = r; }
			if (max >= TABLE_MAX_LIFTED) return bytes; // already roomy
			const rest = bytes.slice(p, contentEnd); // any further tables, unchanged
			const entry = [reftype, 0x01, ...encodeLEB(min), ...encodeLEB(TABLE_MAX_LIFTED)];
			const newContent = new Uint8Array([...encodeLEB(count), ...entry, ...rest]);
			const newSize = encodeLEB(newContent.length);
			const head = bytes.slice(0, off);
			const tail = bytes.slice(contentEnd);
			const out = new Uint8Array(head.length + 1 + newSize.length + newContent.length + tail.length);
			let w = 0;
			out.set(head, w); w += head.length;
			out[w++] = 4;
			out.set(newSize, w); w += newSize.length;
			out.set(newContent, w); w += newContent.length;
			out.set(tail, w);
			return out;
		}
		off = contentEnd;
	}
	return bytes; // no table section (table imported or absent) — leave unchanged
}

// Drop-in for `WebAssembly.compile(bytes)` that patches first, with a fallback
// to the unpatched bytes if the patched module fails to compile.
export async function compileGuest(input) {
	const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
	try {
		return await WebAssembly.compile(patchWasmTableMax(patchWasmTableExport(bytes)));
	} catch {
		return WebAssembly.compile(bytes);
	}
}
