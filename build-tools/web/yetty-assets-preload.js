// yetty-assets-preload.js — emscripten --pre-js shim that provides
// yetty's runtime assets (shaders, fonts, MSDF CDBs, configs).
//
// Two modes, picked at boot:
//
// yfs mode (docs/yfs.md phase 2 — the deployed layout). The webasm
// build stages the asset set as the `yetty/` subtree of the yfs static
// filesystem (stage-yetty-yfs.py). This shim then:
//   1. fetches yfs/current.json + yfs/<V>/yetty.yfs (metadata only);
//   2. eagerly fetches the BOOT SET — every asset smaller than
//      BOOT_EAGER_MAX (configs, WGSL shaders, small images) — in
//      parallel into MEMFS;
//   3. registers every remaining asset (raw fonts, videos, logos) as a
//      LAZY file: an empty placeholder in MEMFS plus an entry in the
//      lazy map;
//   4. wraps the wasm's __syscall_openat import (Module.instantiateWasm):
//      opening a cold lazy asset suspends the whole runtime via
//      Asyncify.handleSleep, fetches + decodes the body, writes it to
//      MEMFS and resumes with the real fd. The import is declared in
//      -sASYNCIFY_IMPORTS (platform/webasm/cmake.cmake) so the
//      instrumented wasm can unwind through it. C code is unchanged —
//      an fopen() of a cold asset simply takes one network round trip.
//
// legacy mode (no yfs tree served — e.g. the showcase dev server):
// fetch manifest.json and preload every asset before main(), exactly
// the old behaviour.
//
// Brotli: asset bodies stay brotli'd on the wire. yfs listing entries
// carry z="br"; decode prefers DecompressionStream('br') (present in
// every browser that has WebGPU, i.e. every browser that runs yetty)
// and falls back to the brotli decoder linked into yetty.wasm
// (src/yetty/yplatform/webasm/brotli-glue.c).
//
// Cache Storage: decoded bodies are cached keyed by the wire sha256 —
// shared by both modes; warm starts skip download AND decode.
//
// MSDF font atlases are never served: yetty builds them from the raw
// fonts with its GPU generator on the first start and hands each one to
// Module.yettyPersistFile (below); later visits restore them from Cache
// Storage before main().

// Block emscripten's automatic main() invocation. We trigger it manually
// from onRuntimeInitialized once boot assets are in MEMFS. callMain must
// be in EXPORTED_RUNTIME_METHODS — see platform/webasm/cmake.cmake.
Module.noInitialRun = true;

(function () {
    const ASSET_BASE   = 'yetty-assets/';
    const MANIFEST_URL = ASSET_BASE + 'manifest.json';
    const YFS_ROOT     = 'yfs';
    // The eager boot set: small files under these subtrees (configs,
    // WGSL shaders, small images — what "first prompt painted" reads).
    // Everything else is demand-paged, including ALL of /demo and /src
    // (the trees that used to be the 40 MB yetty.data package) and the
    // big font/media bodies.
    const BOOT_EAGER_PREFIXES = ['config/', 'data/'];
    const BOOT_EAGER_MAX = 256 * 1024;
    const ASSET_CACHE_NAME = 'yetty-assets-final';
    const LEGACY_CACHE_NAMES = ['yetty-assets'];

    // Lazy map: absolute MEMFS path -> { entry, rel, path, loaded }.
    // Consulted by the wrapped __syscall_openat below.
    const lazyAssets = new Map();
    let yfsVersion = null;

    // ---- generated assets (browser-built MSDF atlases) ---------------
    //
    // The MSDF font atlases are not served at all: yetty builds them from
    // the raw fonts with its GPU generator on the first start
    // (ensure_default_font_atlases in src/yetty/yetty/yetty.c) and hands
    // each finished file to Module.yettyPersistFile. MEMFS does not
    // survive a reload, so the file goes into Cache Storage and
    // restoreGeneratedAssets() puts it back into MEMFS before main() on
    // later visits. Entries are keyed by the MEMFS path and stamped with
    // the content hashes of what they were built from — the source font
    // and the generator shader as the served tree has them — so a font or
    // shader update rebuilds, and a redeploy that changes neither costs
    // nothing.
    const GENERATED_KEY_PREFIX = 'generated-asset?path=';
    const GENERATED_INPUTS_HEADER = 'x-yetty-inputs';
    // MEMFS path -> content hash of the served body (both modes fill it).
    const assetHashes = new Map();

    // The inputs a generated file was built from, as one stamp string, or
    // null when they cannot be determined (then nothing is persisted).
    // /data/msdf-fonts/<stem>.cdb comes from /data/fonts/<stem>.* (the
    // music face maps Emmentaler.cdb <- Emmentaler-20.otf, hence the
    // prefix match) plus the generator's compute shader.
    // Collapse '.' and '..' segments (and resolve a relative path against
    // the process cwd) so the cache key is canonical whatever spelling the
    // C side used — it hands us <fonts dir>/../msdf-fonts/<stem>.cdb.
    function canonicalPath(path) {
        const absolute = path.charAt(0) === '/' ? path : FS.cwd() + '/' + path;
        const segments = [];
        for (const segment of absolute.split('/')) {
            if (!segment || segment === '.') continue;
            if (segment === '..') { segments.pop(); continue; }
            segments.push(segment);
        }
        return '/' + segments.join('/');
    }

    function generatedInputs(path) {
        const match = /^\/data\/msdf-fonts\/([^/]+)\.cdb$/.exec(path);
        if (!match) return null;
        const stem = match[1];
        const shader = assetHashes.get('/data/shaders/msdf_gen.wgsl');
        let font = null;
        for (const [assetPath, hash] of assetHashes) {
            if (assetPath.indexOf('/data/fonts/') !== 0) continue;
            const name = assetPath.slice('/data/fonts/'.length);
            if (name === stem + '.ttf' || name === stem + '.otf' ||
                name.indexOf(stem + '-') === 0) {
                font = hash;
                break;
            }
        }
        if (!shader || !font) return null;
        return 'shader=' + shader + ';font=' + font;
    }

    // Called from C (yetty_yplatform_persist_file) with a MEMFS path.
    // Synchronous from the caller's view: the bytes are copied out of
    // MEMFS right away; the cache write completes in the background.
    Module.yettyPersistFile = function (path) {
        path = canonicalPath(path);
        const inputs = generatedInputs(path);
        if (!inputs) {
            console.warn('[yetty] not persisting %s: inputs unknown', path);
            return false;
        }
        let bytes;
        try {
            bytes = FS.readFile(path);
        } catch (e) {
            console.error('[yetty] persist: cannot read', path, e);
            return false;
        }
        const headers = {};
        headers[GENERATED_INPUTS_HEADER] = inputs;
        assetCache().then(function (cache) {
            if (!cache) return;
            return cache.put(GENERATED_KEY_PREFIX + encodeURIComponent(path),
                             new Response(bytes, { headers: headers }));
        }).then(function () {
            console.log('[yetty] persisted %s (%s)', path, fmtBytes(bytes.length));
        }).catch(function (e) {
            console.warn('[yetty] persist failed for', path, e);
        });
        return true;
    };

    // Put every still-valid generated file back into MEMFS; drop the rest.
    async function restoreGeneratedAssets() {
        const cache = await assetCache();
        if (!cache) return;
        let keys;
        try { keys = await cache.keys(); } catch (_) { return; }
        let restored = 0, restoredBytes = 0;
        for (const request of keys) {
            const index = request.url.indexOf(GENERATED_KEY_PREFIX);
            if (index < 0) continue;
            const path = decodeURIComponent(
                request.url.slice(index + GENERATED_KEY_PREFIX.length));
            let response = null;
            try { response = await cache.match(request); } catch (_) {}
            if (!response) continue;
            const expected = generatedInputs(path);
            if (!expected || response.headers.get(GENERATED_INPUTS_HEADER) !== expected) {
                try { await cache.delete(request); } catch (_) {}
                console.log('[yetty] dropped stale generated %s', path);
                continue;
            }
            const bytes = new Uint8Array(await response.arrayBuffer());
            mkdirsForFile(path);
            FS.writeFile(path, bytes);
            restored++;
            restoredBytes += bytes.length;
        }
        if (restored) {
            console.log('[yetty] restored %d generated atlas(es) from cache (%s)',
                        restored, fmtBytes(restoredBytes));
            status('restored ' + restored + ' generated atlas(es) from cache (' +
                   fmtBytes(restoredBytes) + ')', 'ok');
        } else {
            console.log('[yetty] no generated atlases cached yet — yetty builds them on this start');
            status('no generated atlases cached yet — yetty builds them on this start', 'phase');
        }
    }

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

    // Brotli-decode `raw`. DecompressionStream first — it is async (safe
    // while the wasm is Asyncify-suspended) and present in every
    // WebGPU-capable browser; the linked wasm decoder is the fallback
    // for the eager phase on exotic browsers.
    async function decodeBrotli(raw) {
        if (typeof DecompressionStream === 'function') {
            try {
                const stream = new Response(raw).body
                    .pipeThrough(new DecompressionStream('br'));
                return new Uint8Array(await new Response(stream).arrayBuffer());
            } catch (_) { /* fall through to the linked decoder */ }
        }
        return brotliDecode(raw);
    }

    // Cache Storage handle, or null when unavailable (non-secure
    // context, ancient browser, storage error). Every cache failure
    // degrades to a plain network fetch — caching is an optimization,
    // never a requirement.
    async function openAssetCache() {
        if (typeof caches === 'undefined') return null;
        try {
            // Reclaim the space of retired bucket layouts first.
            for (const legacyName of LEGACY_CACHE_NAMES) {
                try { await caches.delete(legacyName); } catch (ignored) {}
            }
            return await caches.open(ASSET_CACHE_NAME);
        } catch (ignored) { return null; }
    }

    let assetCachePromise = null;
    function assetCache() {
        if (!assetCachePromise) assetCachePromise = openAssetCache();
        return assetCachePromise;
    }

    // Fetch + decode one body, through the decoded-bytes cache. `key`
    // must be unique per wire content (sha256-based).
    async function fetchDecoded(url, key, isBrotli) {
        const cache = await assetCache();
        if (cache) {
            try {
                const hit = await cache.match(key);
                if (hit) return { bytes: new Uint8Array(await hit.arrayBuffer()), cached: true };
            } catch (ignored) { /* fall through to network */ }
        }
        const resp = await fetch(url);
        if (!resp.ok) {
            throw new Error(`asset fetch failed: ${url} -> HTTP ${resp.status}`);
        }
        const raw = await resp.arrayBuffer();
        const bytes = isBrotli ? await decodeBrotli(raw) : new Uint8Array(raw);
        if (cache) {
            // Best-effort store of the DECODED bytes; quota pressure
            // just means the next visit downloads + decodes again.
            // (new Response(view) copies, so `bytes` stays usable.)
            try { await cache.put(key, new Response(bytes)); }
            catch (ignored) {}
        }
        return { bytes, cached: false };
    }

    // ---- yfs mode -------------------------------------------------

    function yfsBodyUrl(state) {
        const entry = state.entry;
        if (entry.b) return YFS_ROOT + '/blob/' + entry.h;
        return YFS_ROOT + '/' + yfsVersion + '/yetty/' +
            state.rel.split('/').map(encodeURIComponent).join('/');
    }

    async function yfsMaterialize(state) {
        const { bytes } = await fetchDecoded(
            yfsBodyUrl(state), 'yfs-body?v=' + state.entry.h,
            state.entry.z === 'br');
        mkdirsForFile(state.path);
        FS.writeFile(state.path, bytes);
        state.loaded = true;
        console.log('[yetty] yfs: materialized %s (%s)', state.path,
                    fmtBytes(bytes.length));
    }

    // Probe for the yetty yfs subtree. Returns the manifest dirs or
    // null (→ legacy mode).
    async function yfsProbe() {
        try {
            const current = await (await fetch(YFS_ROOT + '/current.json',
                                               { cache: 'no-cache' })).json();
            yfsVersion = current.version;
            const resp = await fetch(YFS_ROOT + '/' + yfsVersion + '/yetty.yfs');
            if (!resp.ok) return null;
            return (await resp.json()).dirs;
        } catch (_) { return null; }
    }

    async function preloadYfs(dirs) {
        const eager = [];
        let lazyCount = 0, lazyBytes = 0;
        for (const dirPath of Object.keys(dirs)) {
            for (const entry of dirs[dirPath]) {
                if (entry.t !== 'f') continue;
                const rel = dirPath ? dirPath + '/' + entry.n : entry.n;
                const path = '/' + rel;
                const state = { entry, rel, path, loaded: false };
                assetHashes.set(path, entry.h);
                const bootEligible = entry.s < BOOT_EAGER_MAX &&
                    BOOT_EAGER_PREFIXES.some(function (prefix) {
                        return rel.indexOf(prefix) === 0;
                    });
                if (bootEligible) {
                    eager.push(state);
                } else {
                    lazyAssets.set(path, state);
                    lazyCount++;
                    lazyBytes += entry.s;
                    // Placeholder: the file EXISTS (stat/access probes
                    // succeed); the first open pulls the real bytes.
                    mkdirsForFile(path);
                    FS.writeFile(path, new Uint8Array(0));
                }
            }
        }
        status('yfs ' + yfsVersion + ': boot set ' + eager.length +
               ' files, ' + lazyCount + ' lazy (' + fmtBytes(lazyBytes) +
               ' wire deferred)', 'phase');
        let cachedCount = 0;
        await Promise.all(eager.map(async function (state) {
            const { bytes, cached } = await fetchDecoded(
                yfsBodyUrl(state), 'yfs-body?v=' + state.entry.h,
                state.entry.z === 'br');
            mkdirsForFile(state.path);
            FS.writeFile(state.path, bytes);
            state.loaded = true;
            if (cached) cachedCount++;
        }));
        status('boot set installed (' + eager.length + ' files, ' +
               cachedCount + ' from cache)', 'ok');
    }

    // ---- the openat interception (yfs lazy loads) ------------------
    //
    // Wraps the wasm's __syscall_openat import before instantiation.
    // Cold lazy asset → Asyncify.handleSleep: the whole runtime
    // suspends, the body is fetched + decoded + written to MEMFS, the
    // original syscall runs on resume and returns a real fd. Everything
    // else takes the original path untouched. The import is listed in
    // -sASYNCIFY_IMPORTS so the instrumented wasm expects the unwind.
    // Lexical normalization — C callers open assets through relative
    // segments ("/data/fonts/../msdf-fonts/X.cdb") and the lazy map is
    // keyed by clean absolute paths.
    function normalizePath(raw) {
        const stack = [];
        for (const part of raw.split('/')) {
            if (!part || part === '.') continue;
            if (part === '..') stack.pop();
            else stack.push(part);
        }
        return '/' + stack.join('/');
    }
    function lazyStateFor(pathPtr) {
        return lazyAssets.get(normalizePath(Module.UTF8ToString(pathPtr)));
    }

    Module.instantiateWasm = function (imports, receiveInstance) {
        const originalOpenat = imports.env.__syscall_openat;
        imports.env.__syscall_openat = function (dirfd, pathPtr, flags, varargs) {
            // Fast path: nothing suspended, target warm or not ours.
            if (!Asyncify.currData) {
                const state = lazyStateFor(pathPtr);
                if (!state || state.loaded) {
                    return originalOpenat(dirfd, pathPtr, flags, varargs);
                }
            }
            // Cold asset — or the rewind of a previously suspended open
            // (handleSleep then just returns the saved fd without
            // re-running the callback).
            return Asyncify.handleSleep(function (wakeUp) {
                const state = lazyStateFor(pathPtr);
                const finish = function () {
                    wakeUp(originalOpenat(dirfd, pathPtr, flags, varargs));
                };
                if (!state || state.loaded) { finish(); return; }
                yfsMaterialize(state).then(finish, function (err) {
                    console.error('[yetty] yfs: lazy fetch failed:', state.path, err);
                    state.loaded = true; // open the empty placeholder, no loop
                    finish();
                });
            });
        };
        const wasmFile = (typeof Module.locateFile === 'function')
            ? Module.locateFile('yetty.wasm', '') : 'yetty.wasm';
        WebAssembly.instantiateStreaming(fetch(wasmFile), imports)
            .then(function (result) { receiveInstance(result.instance, result.module); })
            .catch(function () {
                // Streaming needs application/wasm — fall back for dev
                // servers with sloppy MIME tables.
                fetch(wasmFile)
                    .then(function (resp) { return resp.arrayBuffer(); })
                    .then(function (bytes) { return WebAssembly.instantiate(bytes, imports); })
                    .then(function (result) { receiveInstance(result.instance, result.module); })
                    .catch(function (err) {
                        console.error('[yetty] wasm instantiation failed:', err);
                        status('wasm instantiation failed: ' + err, 'err');
                    });
            });
        return {}; // async instantiation — emscripten accepts the empty stub
    };

    // ---- legacy mode (no yfs tree served) ---------------------------

    async function legacyLoadOne(entry) {
        assetHashes.set(entry.dest, entry.sha256 || 'unversioned');
        const { bytes, cached } = await fetchDecoded(
            ASSET_BASE + entry.url,
            ASSET_BASE + entry.url + '?v=' + (entry.sha256 || 'unversioned'),
            !!entry.brotli);
        mkdirsForFile(entry.dest);
        FS.writeFile(entry.dest, bytes);
        status('  ' + entry.dest + '  ' + fmtBytes(bytes.length) +
               (cached ? '  (cached, pre-decoded)' : ''), 'dim');
        return cached;
    }

    async function preloadLegacy() {
        status('yfs yetty tree not served — legacy full preload', 'phase');
        const mr = await fetch(MANIFEST_URL, { cache: 'no-cache' });
        if (!mr.ok) {
            throw new Error(`manifest fetch failed: ${MANIFEST_URL} -> HTTP ${mr.status}`);
        }
        const manifest = await mr.json();
        const entries = manifest.entries || [];
        status('manifest: ' + entries.length + ' files', 'phase');
        const cachedFlags = await Promise.all(entries.map(legacyLoadOne));
        status('all assets installed in MEMFS (' + entries.length + ' files, ' +
               cachedFlags.filter(Boolean).length + ' from cache)', 'ok');
    }

    // ---- boot -------------------------------------------------------

    const userInit = Module.onRuntimeInitialized;
    Module.onRuntimeInitialized = function () {
        // Fire the page's existing onRuntimeInitialized (loading-screen
        // hide, etc.) immediately — it doesn't depend on MEMFS state.
        if (typeof userInit === 'function') {
            try { userInit(); } catch (e) { console.error(e); }
        }

        (async function () {
            const dirs = await yfsProbe();
            if (dirs) await preloadYfs(dirs);
            else await preloadLegacy();
            await restoreGeneratedAssets();
        })().then(function () {
            console.log('[yetty] asset preload complete; calling main()');
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
