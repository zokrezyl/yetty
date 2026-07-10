// Browser host selector (epic #33, issue #39).
//
// Chooses which browser runtime backs a guest run:
//   - "js"  — the LEGACY hand-written JavaScript libc host (../yos_proc.mjs).
//             This is the DEFAULT and stays the default until the parity gates
//             in docs/browser.md §5 pass (see `make test-browser-host-parity`).
//   - "c"   — the converged yos C runtime compiled to wasm (yos-host.wasm),
//             the target of the convergence. Explicit opt-in only, for now.
//
// The default is deliberately the legacy runner: switching the browser default
// to the C host is TEST-GATED, not intent-gated. When parity passes for the
// covered matrix, flip DEFAULT_BACKEND to "c" here (one line) and retire the
// JS libc path — not before.
//
// Selection precedence (first that resolves wins):
//   1. explicit `backend` argument to selectHostBackend()
//   2. URL query param  ?host=c|js         (browser)
//   3. env var          YOS_BROWSER_HOST=c|js   (node)
//   4. DEFAULT_BACKEND

export const DEFAULT_BACKEND = "js"; // legacy until parity gates pass (#39)

export function resolveBackendName(explicit) {
	if (explicit === "c" || explicit === "js") return explicit;
	// Browser: ?host=c
	try {
		if (typeof location !== "undefined" && location.search) {
			const q = new URLSearchParams(location.search).get("host");
			if (q === "c" || q === "js") return q;
		}
	} catch { /* not a browser */ }
	// Node: YOS_BROWSER_HOST=c
	try {
		const e = typeof process !== "undefined" && process.env && process.env.YOS_BROWSER_HOST;
		if (e === "c" || e === "js") return e;
	} catch { /* no process.env */ }
	return DEFAULT_BACKEND;
}

// Returns { backend, isLegacy, note }. Loading the actual module is left to the
// caller so this stays a pure, testable decision function with no side effects
// (the two backends have different entry shapes: yos_proc.mjs exports runYos,
// yos-host.mjs is an Emscripten factory).
export function selectHostBackend(explicit) {
	const backend = resolveBackendName(explicit);
	return {
		backend,
		isLegacy: backend === "js",
		note: backend === "js"
			? "legacy JS libc host (frozen; default until parity gates pass)"
			: "converged yos C runtime (yos-host.wasm)",
	};
}
