// yetty-assets-preload.js — emscripten --pre-js shim that pulls runtime
// assets (shaders, fonts, MSDF CDBs, configs) from the server into MEMFS
// before main() runs.
//
// Replaces the incbin embed pipeline on webasm. Without this, the only
// way to ship assets was to bake every byte into yetty.wasm via a
// 240 MB yetty_data_data.c — slow link, ~20 GB clang RAM. The new flow:
//
//   1. cmake stages files under build/yetty-assets/ (.br for compressible
//      assets) and writes manifest.json describing where each one lives.
//   2. This shim defers main() with Module.noInitialRun, fetches the
//      manifest + each entry, decompresses brotli'd payloads via the
//      brotli decoder LINKED INTO yetty.wasm
//      (src/yetty/yplatform/webasm/brotli-glue.c, exported as
//      _yetty_brotli_decode), then FS.writeFile()s into MEMFS at the
//      same paths the C extract path used to produce.
//   3. Once everything is in MEMFS, calls Module.callMain() — main()
//      starts and finds all assets ready.
//
// Why not Module.preRun? preRun runs BEFORE __wasm_call_ctors /
// initRuntime, so _malloc and _yetty_brotli_decode aren't usable yet
// (emscripten asserts "native function `malloc` called before runtime
// initialization"). Why not DecompressionStream('br')? Only landed in
// Chrome 124 / FF 127 / Safari 18 — not yet ubiquitous, and it throws
// hard when missing. Doing the brotli pass in our own linked decoder
// works on every browser that runs wasm.
//
// Manifest schema:
//   { "version": "<stamp>",
//     "entries": [ { "url": "data/...", "dest": "/data/...", "brotli": bool }, ... ] }

// Block emscripten's automatic main() invocation. We trigger it manually
// from onRuntimeInitialized once assets are in MEMFS. callMain must be
// in EXPORTED_RUNTIME_METHODS — see build-tools/cmake/targets/webasm.cmake.
Module.noInitialRun = true;

// Chain on top of any onRuntimeInitialized hook the page already set
// (index.html sets one to hide the loading spinner).
(function () {
    const ASSET_BASE   = 'yetty-assets/';
    const MANIFEST_URL = ASSET_BASE + 'manifest.json';

    // Forward to the page's boot-status overlay (window.yettyStatus is
    // installed by index.html). Falls back to console.log when the
    // overlay isn't available (headless builds, embedded use).
    function status(text, level) {
        try {
            if (window.yettyStatus && window.yettyStatus.append) {
                window.yettyStatus.append(text, level);
                return;
            }
        } catch (_) { /* swallow — never let the status path break boot */ }
        console.log('[yetty]', text);
    }
    function fmtBytes(n) {
        if (n < 1024) return n + 'B';
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + 'K';
        return (n / 1024 / 1024).toFixed(1) + 'M';
    }

    function mkdirsForFile(path) {
        const parts = path.split('/').filter(Boolean);
        let cur = '';
        // Drop the last component (the filename itself).
        for (let i = 0; i < parts.length - 1; i++) {
            cur += '/' + parts[i];
            try { FS.mkdir(cur); }
            catch (e) { /* EEXIST is fine */ }
        }
    }

    // Decompress a brotli buffer using yetty.wasm's linked brotli
    // decoder (BrotliDecoderDecompressStream wrapped in
    // _yetty_brotli_decode). Returns Uint8Array.
    function brotliDecode(buf) {
        const inBytes = new Uint8Array(buf);
        const inPtr = Module._malloc(inBytes.length);
        if (inPtr === 0) {
            throw new Error('brotliDecode: input alloc failed');
        }
        Module.HEAPU8.set(inBytes, inPtr);

        // Two pointer-sized scratch slots for (out_ptr, out_len).
        // wasm32 → both 32-bit, total 8 bytes.
        const scratch = Module._malloc(8);
        if (scratch === 0) {
            Module._free(inPtr);
            throw new Error('brotliDecode: scratch alloc failed');
        }

        let outPtr = 0;
        try {
            const ok = Module._yetty_brotli_decode(
                inPtr, inBytes.length, scratch, scratch + 4);
            if (!ok) {
                throw new Error('brotliDecode: decoder rejected input');
            }
            outPtr = Module.HEAPU32[scratch >> 2];
            const outLen = Module.HEAPU32[(scratch + 4) >> 2];
            // .slice() copies — the C-side malloc'd buffer is freed in
            // the finally block. Without the copy, the returned bytes
            // would alias linear memory that's about to be freed.
            return Module.HEAPU8.slice(outPtr, outPtr + outLen);
        } finally {
            Module._free(inPtr);
            Module._free(scratch);
            if (outPtr !== 0) {
                Module._free(outPtr);
            }
        }
    }

    async function loadOne(entry) {
        const url = ASSET_BASE + entry.url;
        const resp = await fetch(url);
        if (!resp.ok) {
            throw new Error(`asset fetch failed: ${url} -> HTTP ${resp.status}`);
        }
        const raw = await resp.arrayBuffer();
        const bytes = entry.brotli ? brotliDecode(raw) : new Uint8Array(raw);
        mkdirsForFile(entry.dest);
        FS.writeFile(entry.dest, bytes);
        status('  ' + entry.dest +
               '  ' + fmtBytes(raw.byteLength) +
               (entry.brotli ? ' → ' + fmtBytes(bytes.length) : ''),
               'dim');
    }

    async function preloadAll() {
        status('fetching asset manifest…');
        const mr = await fetch(MANIFEST_URL);
        if (!mr.ok) {
            throw new Error(`manifest fetch failed: ${MANIFEST_URL} -> HTTP ${mr.status}`);
        }
        const manifest = await mr.json();
        const entries = manifest.entries || [];
        status('manifest: ' + entries.length + ' files', 'phase');

        // Concurrent fetches — browsers pipeline these and each file is
        // small (KB to a few MB). Sequential would multiply round-trip
        // latency by ~100 entries.
        await Promise.all(entries.map(loadOne));

        status('all assets installed in MEMFS (' + entries.length + ' files)', 'ok');
        console.log('[yetty] preload: %d assets installed in MEMFS', entries.length);
    }

    const userInit = Module.onRuntimeInitialized;
    Module.onRuntimeInitialized = function () {
        // Fire the page's existing onRuntimeInitialized (loading-screen
        // hide, etc.) immediately — it doesn't depend on MEMFS state.
        if (typeof userInit === 'function') {
            try { userInit(); } catch (e) { console.error(e); }
        }

        preloadAll().then(function () {
            console.log('[yetty] preload complete; calling main()');
            status('starting yetty terminal…', 'phase');
            try {
                // Pass Module.arguments explicitly: the generated
                // yetty.js only honors it when 'arguments' is in
                // INCOMING_MODULE_JS_API, which the release build
                // strips — a bare callMain() would run main() with
                // an empty argv and silently drop the session-mode
                // flags index.html set (--temu / --websocket / …).
                Module.callMain(Module.arguments || []);
                // Console stays visible — user wants to see the full
                // boot log without it disappearing under the terminal.
                // Manual dismiss path could be added later; for now
                // the overlay sits on top of the canvas at 78% alpha
                // and never auto-fades.
                status('yetty terminal running', 'ok');
            } catch (e) {
                console.error('[yetty] callMain threw:', e);
                status('callMain threw: ' + (e && e.message || e), 'err');
            }
        }).catch(function (e) {
            console.error('[yetty] preload failed:', e);
            status('preload failed: ' + (e && e.message || e), 'err');
            // Intentionally do NOT call main() — running on a partial
            // MEMFS would produce confusing crashes far from the root
            // cause. Failing loud is the right call here.
        });
    };
})();
