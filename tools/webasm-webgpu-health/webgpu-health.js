/*
 * webgpu-health.js — standalone WebGPU health probe for the yetty web pages.
 *
 * Runs at page-load time and walks the exact GPU bring-up chain the yetty
 * wasm module will need, stopping at the first broken link with a
 * user-actionable message:
 *
 *   api      WebGPU API present (secure context, navigator.gpu, wasm)
 *   adapter  GPU adapter obtainable (core first, compatibility fallback)
 *   hardware the adapter is a real GPU, not a silent CPU fallback
 *            (SwiftShader/llvmpipe/WARP) — Chrome hands out SwiftShader
 *            instead of a null adapter when hardware acceleration is off
 *   limits   adapter limits meet the floors yetty needs
 *   device   device creation with the limits yetty requests
 *   shader   WGSL probe shader compiles — uses yetty's binding pattern
 *            (storage buffer read in the vertex AND fragment stage, a
 *            uniform block, a sampled atlas texture)
 *   pipeline render pipeline builds from that shader
 *   execute  off-screen draw + buffer readback, pixel-verified — proves a
 *            shader actually RAN on this GPU instead of merely validating
 *   canvas   'webgpu' canvas presentation: configure + getCurrentTexture
 *            + clear + present (last on purpose — see STEP_DEFINITIONS)
 *
 * The probe device is destroyed when the run finishes; the page's real
 * terminal instances create their own devices later.
 *
 * The limit floors and requested limits mirror the native side:
 * src/yetty/ywebgpu/limits.c (requested device limits) and the
 * per-instance gate in build-tools/web/terminal.html (hard floors).
 *
 * API (all under window.yettyWebgpuHealth):
 *   run(options)                 → Promise<report>, never rejects.
 *                                  options: { timeoutMs, log,
 *                                             allowSoftwareRenderer }
 *   installBanner(report, opts)  → inject a failure/warning banner into the
 *                                  current page (opts: { detailsUrl })
 *   createReportElement(report)  → detailed DOM node (steps, GPU, limits)
 *   summarize(report)            → one-line human-readable verdict
 *
 * Every run() also sets window.yettyWebgpuHealthReport and dispatches a
 * 'yetty-webgpu-health' CustomEvent on window with the report as detail,
 * and mirrors the verdict to the console — so both HTML-level and
 * JavaScript-level consumers see the result without extra wiring.
 */
(function () {
    'use strict';

    var HEALTH_VERSION = 1;

    /* Hard floors below which yetty cannot operate. yetty's own device
     * request clamps against the adapter (so the request itself succeeds),
     * but adapters under these floors cannot hold the flattened binder's
     * mega storage buffer / atlas textures. */
    var REQUIRED_LIMIT_FLOORS = {
        maxStorageBuffersPerShaderStage: 8,
        maxTextureDimension2D: 4096,
        maxStorageBufferBindingSize: 128 * 1024 * 1024,
        maxBufferSize: 128 * 1024 * 1024
    };

    /* What yetty actually asks for at device creation, clamped to the
     * adapter maxima — mirrors yetty_ywebgpu_fill_default_limits(). */
    var REQUESTED_LIMIT_CEILINGS = {
        maxTextureDimension2D: 16384,
        maxStorageBufferBindingSize: 512 * 1024 * 1024,
        maxBufferSize: 1024 * 1024 * 1024,
        maxStorageBuffersPerShaderStage: 10
    };

    /* The canvas/presentation probe runs LAST, after the off-screen
     * shader-execution proof: presenting to a canvas is the one step
     * known to hard-crash the GPU process in headless/virtualized
     * browsers (device lost, "Instance dropped"), and running it first
     * would take the device down before the shader steps get a chance
     * to report anything. */
    var STEP_DEFINITIONS = [
        { name: 'api', title: 'WebGPU API available' },
        { name: 'adapter', title: 'GPU adapter' },
        { name: 'hardware', title: 'Hardware GPU (not a software fallback)' },
        { name: 'limits', title: 'Adapter limits meet yetty’s floors' },
        { name: 'device', title: 'GPU device with yetty’s limits' },
        { name: 'shader', title: 'Probe shader compiles' },
        { name: 'pipeline', title: 'Render pipeline builds' },
        { name: 'execute', title: 'Shader runs, output verified' },
        { name: 'canvas', title: 'WebGPU canvas presentation' }
    ];

    /* Mirrors the flattened-binder access pattern of the real renderer:
     * ONE storage buffer read from BOTH shader stages (compatibility-mode
     * adapters with maxStorageBuffersInVertexStage == 0 reject exactly
     * this), one uniform block, one sampled atlas texture. */
    var PROBE_SHADER_WGSL =
        'struct ProbeUniforms {\n' +
        '    color_scale : vec4<f32>,\n' +
        '};\n' +
        '\n' +
        '@group(0) @binding(0) var<storage, read> probe_cells : array<vec4<f32>>;\n' +
        '@group(0) @binding(1) var<uniform> probe_uniforms : ProbeUniforms;\n' +
        '@group(0) @binding(2) var probe_atlas : texture_2d<f32>;\n' +
        '@group(0) @binding(3) var probe_sampler : sampler;\n' +
        '\n' +
        'struct ProbeVertexOutput {\n' +
        '    @builtin(position) position : vec4<f32>,\n' +
        '    @location(0) base_color : vec4<f32>,\n' +
        '    @location(1) atlas_uv : vec2<f32>,\n' +
        '};\n' +
        '\n' +
        '@vertex\n' +
        'fn probe_vertex(@builtin(vertex_index) vertex_index : u32) -> ProbeVertexOutput {\n' +
        '    var corners = array<vec2<f32>, 3>(\n' +
        '        vec2<f32>(-1.0, -3.0),\n' +
        '        vec2<f32>( 3.0,  1.0),\n' +
        '        vec2<f32>(-1.0,  1.0));\n' +
        '    var output : ProbeVertexOutput;\n' +
        '    output.position = vec4<f32>(corners[vertex_index], 0.0, 1.0);\n' +
        '    output.base_color = probe_cells[0];\n' +
        '    output.atlas_uv = corners[vertex_index] * 0.5 + vec2<f32>(0.5, 0.5);\n' +
        '    return output;\n' +
        '}\n' +
        '\n' +
        '@fragment\n' +
        'fn probe_fragment(input : ProbeVertexOutput) -> @location(0) vec4<f32> {\n' +
        '    let sampled = textureSample(probe_atlas, probe_sampler, input.atlas_uv);\n' +
        '    let accent = probe_cells[1];\n' +
        '    return (input.base_color + accent) * probe_uniforms.color_scale * sampled;\n' +
        '}\n';

    /* Values chosen so every channel is an exact 8-bit level:
     * (0.2+0.2, 0.4+0.2, 0.6+0.0, 1.0) * scale 1 * white texel
     *   → bytes [102, 153, 153, 255]. */
    var PROBE_BASE_COLOR = [0.2, 0.4, 0.6, 1.0];
    var PROBE_ACCENT_COLOR = [0.2, 0.2, 0.0, 0.0];
    var EXPECTED_PIXEL_BYTES = [102, 153, 153, 255];
    var PIXEL_TOLERANCE = 3;

    var RENDER_TARGET_SIZE = 16;
    var READBACK_BYTES_PER_ROW = 256; /* copyTextureToBuffer alignment */

    /* Timed heavy pass: ~268M transcendental ops. A hardware GPU absorbs
     * this in a few milliseconds; a CPU rasterizer needs hundreds — the
     * wall-clock separates the two even when the adapter's identity
     * strings don't admit to being software. */
    var HEAVY_TARGET_SIZE = 1024;
    var HEAVY_LOOP_ITERATIONS = 256;
    /* SwiftShader measured ~118 ms on a 32-core desktop; real GPUs land
     * in single-digit milliseconds. Weak mobile GPUs may trip this too —
     * it is a warning, and on such devices an honest one. */
    var SOFTWARE_SUSPECT_DRAW_MS = 100;

    var HEAVY_SHADER_WGSL =
        'struct HeavyVertexOutput {\n' +
        '    @builtin(position) position : vec4<f32>,\n' +
        '};\n' +
        '\n' +
        '@vertex\n' +
        'fn heavy_vertex(@builtin(vertex_index) vertex_index : u32) -> HeavyVertexOutput {\n' +
        '    var corners = array<vec2<f32>, 3>(\n' +
        '        vec2<f32>(-1.0, -3.0),\n' +
        '        vec2<f32>( 3.0,  1.0),\n' +
        '        vec2<f32>(-1.0,  1.0));\n' +
        '    var output : HeavyVertexOutput;\n' +
        '    output.position = vec4<f32>(corners[vertex_index], 0.0, 1.0);\n' +
        '    return output;\n' +
        '}\n' +
        '\n' +
        '@fragment\n' +
        'fn heavy_fragment(input : HeavyVertexOutput) -> @location(0) vec4<f32> {\n' +
        '    /* Depends on the pixel position so the loop cannot be\n' +
        '       constant-folded away. */\n' +
        '    var accumulator = 0.0;\n' +
        '    for (var iteration = 0u; iteration < ' + HEAVY_LOOP_ITERATIONS +
        'u; iteration = iteration + 1u) {\n' +
        '        accumulator = accumulator +\n' +
        '            sin(input.position.x * 0.001 + f32(iteration)) *\n' +
        '            cos(input.position.y * 0.001);\n' +
        '    }\n' +
        '    return vec4<f32>(fract(accumulator), 0.0, 0.0, 1.0);\n' +
        '}\n';

    /* What broke, one sentence per step — the platform-specific "what to
     * do about it, in order" list is appended by adviceForStep() below. */
    var STEP_FAILURE_LEADS = {
        api: 'This browser does not expose (or blocks) the WebGPU API. ' +
             'Also note WebGPU needs a secure context — HTTPS or localhost.',
        adapter: 'The browser exposes WebGPU but handed out no GPU ' +
                 'adapter — the GPU/driver is blocklisted or GPU ' +
                 'acceleration is off. (A WebGPU-hooking browser ' +
                 'extension can also cause this — try a Guest window.)',
        hardware: 'The browser silently fell back to CPU-based software ' +
                  'rendering (SwiftShader) — WebGPU “works” but is far ' +
                  'too slow to run yetty. GPU acceleration is off or the ' +
                  'GPU is blocklisted.',
        limits: 'This GPU adapter reports limits below what yetty needs; ' +
                'the browser is probably not using the machine’s real GPU.',
        device: 'A GPU adapter was found but creating a device from it ' +
                'failed — driver or blocklist trouble.',
        canvas: 'Rendering works off-screen but the image presented to ' +
                'the canvas is broken or blank, so yetty draws and ' +
                'nothing shows up (broken canvas compositing/interop; a ' +
                'headless/virtualized environment or a WebGPU-hooking ' +
                'extension can also cause this).',
        shader: 'The WGSL compiler rejected yetty’s shader pattern — a ' +
                'browser/driver defect. Update the browser and the GPU ' +
                'driver first.',
        pipeline: 'The shader compiled but the render pipeline was ' +
                  'refused. On compatibility-mode adapters this means ' +
                  'storage buffers are unavailable in the vertex stage, ' +
                  'which yetty requires.',
        execute: 'The GPU accepted all setup but the rendered output is ' +
                 'wrong or unreadable — a driver defect. Update the GPU ' +
                 'driver first.'
    };

    function detectHostPlatform() {
        var userAgent = (typeof navigator !== 'undefined' &&
                         navigator.userAgent) || '';
        var maxTouchPoints = (typeof navigator !== 'undefined' &&
                              navigator.maxTouchPoints) || 0;
        /* Modern iPadOS masquerades as desktop Safari ("Macintosh") but
         * is the only Mac-flavoured UA with a touch screen. */
        var ios = /iPhone|iPad|iPod/.test(userAgent) ||
                  (/Macintosh/.test(userAgent) && maxTouchPoints > 1);
        var android = /Android/.test(userAgent);
        var mac = !ios && /Macintosh|Mac OS X/.test(userAgent);
        var windows = /Windows/.test(userAgent);
        var firefox = /Firefox\//.test(userAgent);
        var safari = /Safari\//.test(userAgent) &&
                     !/Chrome|Chromium|CriOS|Edg/.test(userAgent);
        return {
            ios: ios,
            android: android,
            mac: mac,
            windows: windows,
            firefox: firefox,
            safari: safari
        };
    }

    /* Ordered, platform-specific fix list appended to the step lead.
     * On Linux/Chromium the ORDER is derived from field results:
     * chrome://flags/#enable-vulkan alone is the most common fix
     * (hardware WebGPU on Linux Chrome runs on Vulkan); the blocklist
     * override and the compositing-interop flag come after it. */
    function adviceForStep(stepName) {
        var host = detectHostPlatform();
        var lead = STEP_FAILURE_LEADS[stepName] || '';

        if (host.ios) {
            return lead + ' On iOS/iPadOS every browser uses WebKit, so ' +
                   'this is a Safari setting no matter which browser app ' +
                   'you use: Settings → Apps → Safari → Advanced → ' +
                   'Feature Flags → enable “WebGPU”, then restart the ' +
                   'browser. On recent iOS (26+) WebGPU is on by ' +
                   'default — if the flag is missing or it still fails, ' +
                   'update iOS.';
        }
        if (host.mac && host.safari) {
            return lead + ' In Safari on macOS: Safari → Settings → ' +
                   'Advanced → enable “Show features for web ' +
                   'developers”, then menu Develop → Feature Flags → ' +
                   'enable “WebGPU” and reload. WebGPU is on by default ' +
                   'in recent Safari (26+), so updating macOS/Safari is ' +
                   'the cleanest fix. Chrome/Edge 113+ also work on ' +
                   'macOS out of the box.';
        }
        if (host.firefox) {
            return lead + ' In Firefox: open about:config, set ' +
                   'dom.webgpu.enabled to true and restart. Firefox ' +
                   'WebGPU is still maturing outside Windows — ' +
                   'Chrome/Edge are the most reliable path today.';
        }
        if (host.mac) {
            return lead + ' Chrome/Edge on macOS run WebGPU on Metal — ' +
                   'there are no Vulkan flags on macOS. Try, in this ' +
                   'order: ' +
                   '1) update the browser and macOS; ' +
                   '2) chrome://settings/system → “Use graphics ' +
                   'acceleration when available” must be ON, relaunch; ' +
                   '3) chrome://flags/#enable-unsafe-webgpu → Enabled ' +
                   '(overrides the GPU blocklist), relaunch. ' +
                   'Verify in chrome://gpu that WebGPU says Hardware ' +
                   'accelerated.';
        }
        if (host.windows) {
            return lead + ' On Windows WebGPU runs on D3D12. Try, in ' +
                   'this order: ' +
                   '1) update the GPU driver (vendor installer, not ' +
                   'Windows Update); ' +
                   '2) chrome://settings/system → “Use graphics ' +
                   'acceleration when available” must be ON, relaunch; ' +
                   '3) chrome://flags/#enable-unsafe-webgpu → Enabled ' +
                   '(overrides the blocklist), relaunch. ' +
                   'chrome://gpu shows the blocklist verdict.';
        }
        if (host.android) {
            return lead + ' On Android: update Chrome and Android System ' +
                   'WebView (WebGPU needs Android 12+ with a recent ' +
                   'Chrome), then try ' +
                   'chrome://flags/#enable-unsafe-webgpu → Enabled and ' +
                   'relaunch.';
        }
        /* Linux + Chromium — default branch. */
        var interopNote = stepName === 'canvas'
            ? ' (this flag targets exactly this renders-but-presents-' +
              'nothing case)'
            : '';
        return lead + ' On Linux, hardware WebGPU in Chrome runs on ' +
               'Vulkan. Set these to Enabled IN THIS ORDER, relaunching ' +
               'and re-testing after each one: ' +
               '1) chrome://flags/#enable-vulkan — this alone is the ' +
               'most common fix; ' +
               '2) chrome://flags/#enable-unsafe-webgpu — overrides the ' +
               'GPU/driver blocklist; ' +
               '3) chrome://flags/#force-enable-webgpu-interop — forces ' +
               'the WebGPU↔GL compositing interop' + interopNote + '. ' +
               'Keep chrome://settings/system → “Use graphics ' +
               'acceleration when available” ON. Verify in chrome://gpu ' +
               'that Vulkan says Enabled and WebGPU says Hardware ' +
               'accelerated; if `vulkaninfo` fails in a terminal, ' +
               'install the Vulkan driver for your GPU ' +
               '(mesa-vulkan-drivers, or the NVIDIA driver).';
    }

    function nowMs() {
        return (typeof performance !== 'undefined' && performance.now)
            ? performance.now() : Date.now();
    }

    function formatByteSize(value) {
        if (typeof value !== 'number' || !isFinite(value)) return String(value);
        if (value >= 1024 * 1024 * 1024) {
            return (value / (1024 * 1024 * 1024)).toFixed(1) + ' GiB';
        }
        if (value >= 1024 * 1024) {
            return Math.round(value / (1024 * 1024)) + ' MiB';
        }
        if (value >= 1024) return Math.round(value / 1024) + ' KiB';
        return value + ' B';
    }

    function limitsSnapshot(limitsObject) {
        var snapshot = {};
        if (!limitsObject) return snapshot;
        /* GPUSupportedLimits is not enumerable with Object.keys on all
         * browsers — walk the prototype's getters instead. */
        var names = [];
        try {
            var prototype = Object.getPrototypeOf(limitsObject);
            names = Object.getOwnPropertyNames(prototype);
        } catch (ignored) {}
        for (var nameIndex = 0; nameIndex < names.length; nameIndex++) {
            var limitName = names[nameIndex];
            if (limitName === 'constructor') continue;
            try {
                var limitValue = limitsObject[limitName];
                if (typeof limitValue === 'number') {
                    snapshot[limitName] = limitValue;
                }
            } catch (ignored) {}
        }
        return snapshot;
    }

    function describeError(error) {
        if (!error) return 'unknown error';
        if (typeof error === 'string') return error;
        if (error.message) {
            return (error.name ? error.name + ': ' : '') + error.message;
        }
        try { return String(error); } catch (ignored) { return 'unknown error'; }
    }

    function looksLikeSoftwareRenderer(description) {
        if (!description) return false;
        var lowered = description.toLowerCase();
        return lowered.indexOf('swiftshader') !== -1 ||
               lowered.indexOf('llvmpipe') !== -1 ||
               lowered.indexOf('lavapipe') !== -1 ||
               lowered.indexOf('softpipe') !== -1 ||
               lowered.indexOf('software') !== -1 ||
               lowered.indexOf('warp') !== -1;
    }

    /* ---------------------------------------------------------------- */
    /* The probe                                                         */
    /* ---------------------------------------------------------------- */

    function runHealthCheck(options) {
        options = options || {};
        var timeoutMs = options.timeoutMs || 15000;

        var report = {
            version: HEALTH_VERSION,
            ok: false,
            canRun: false,
            compatibilityMode: false,
            softwareRenderer: false,
            failedStep: null,
            reason: null,
            advice: null,
            steps: [],
            gpu: {},
            features: [],
            adapterLimits: {},
            requiredFloors: REQUIRED_LIMIT_FLOORS,
            warnings: [],
            uncapturedErrors: [],
            heavyDrawMs: null,
            userAgent: (typeof navigator !== 'undefined' && navigator.userAgent) || '',
            totalMs: 0
        };

        var stepByName = {};
        for (var stepIndex = 0; stepIndex < STEP_DEFINITIONS.length; stepIndex++) {
            var definition = STEP_DEFINITIONS[stepIndex];
            var stepRecord = {
                name: definition.name,
                title: definition.title,
                status: 'skip',
                detail: '',
                durationMs: 0
            };
            report.steps.push(stepRecord);
            stepByName[definition.name] = stepRecord;
        }

        var startedMs = nowMs();
        var currentStepName = 'api';
        var probeDevice = null;
        var probeDeviceDestroyed = false;
        var probeCanvasElement = null;
        var deviceLostMessage = null;

        function logLine(text, level) {
            if (options.log) {
                try { options.log(text, level); } catch (ignored) {}
                return;
            }
            try {
                if (level === 'error') console.error('[webgpu-health] ' + text);
                else console.debug('[webgpu-health] ' + text);
            } catch (ignored) {}
        }

        function beginStep(stepName) {
            currentStepName = stepName;
            stepByName[stepName].startedMs = nowMs();
        }

        function passStep(stepName, detail) {
            var record = stepByName[stepName];
            record.status = 'pass';
            record.detail = detail || '';
            record.durationMs = Math.round(nowMs() - (record.startedMs || nowMs()));
            delete record.startedMs;
            logLine('pass: ' + record.title +
                    (detail ? ' — ' + detail : ''));
        }

        function failStep(stepName, reason) {
            var record = stepByName[stepName];
            record.status = 'fail';
            record.detail = reason;
            record.durationMs = Math.round(nowMs() - (record.startedMs || nowMs()));
            delete record.startedMs;
            report.failedStep = stepName;
            report.reason = reason;
            report.advice = adviceForStep(stepName);
            logLine('FAIL: ' + record.title + ' — ' + reason, 'error');
        }

        function warnStep(stepName, warningText) {
            var record = stepByName[stepName];
            record.status = 'warn';
            record.detail = warningText;
            record.durationMs = Math.round(nowMs() - (record.startedMs || nowMs()));
            delete record.startedMs;
            warn(warningText);
        }

        function warn(text) {
            report.warnings.push(text);
            logLine('warning: ' + text);
        }

        function finishReport() {
            report.totalMs = Math.round(nowMs() - startedMs);
            report.ok = !report.failedStep && report.warnings.length === 0;
            report.canRun = !report.failedStep;
            if (probeDevice && !probeDeviceDestroyed) {
                probeDeviceDestroyed = true;
                try { probeDevice.destroy(); } catch (ignored) {}
            }
            if (probeCanvasElement && probeCanvasElement.parentNode) {
                try { probeCanvasElement.remove(); } catch (ignored) {}
            }
            try { window.yettyWebgpuHealthReport = report; } catch (ignored) {}
            try {
                window.dispatchEvent(new CustomEvent('yetty-webgpu-health',
                                                     { detail: report }));
            } catch (ignored) {}
            try {
                if (report.canRun) {
                    console.info('[webgpu-health] ' + summarize(report));
                } else {
                    console.error('[webgpu-health] ' + summarize(report));
                }
            } catch (ignored) {}
            return report;
        }

        async function popErrorScopeMessage(device) {
            try {
                var scopeError = await device.popErrorScope();
                return scopeError ? describeError(scopeError) : null;
            } catch (popError) {
                return describeError(popError);
            }
        }

        async function probe() {
            /* ---- step: api ------------------------------------------ */
            beginStep('api');
            if (typeof window !== 'undefined' && window.isSecureContext === false) {
                failStep('api', 'the page is not a secure context ' +
                         '(WebGPU needs HTTPS or localhost)');
                return;
            }
            if (typeof WebAssembly !== 'object') {
                failStep('api', 'WebAssembly is not available in this browser');
                return;
            }
            if (!navigator.gpu) {
                failStep('api', 'navigator.gpu is undefined — this browser ' +
                         'does not expose the WebGPU API');
                return;
            }
            var preferredFormat = 'bgra8unorm';
            try {
                preferredFormat = navigator.gpu.getPreferredCanvasFormat();
            } catch (formatError) {
                failStep('api', 'navigator.gpu.getPreferredCanvasFormat() ' +
                         'threw: ' + describeError(formatError));
                return;
            }
            passStep('api', 'preferred canvas format: ' + preferredFormat);

            /* ---- step: adapter --------------------------------------- */
            beginStep('adapter');
            var adapter = null;
            var adapterError = null;
            try {
                adapter = await navigator.gpu.requestAdapter();
            } catch (requestError) {
                adapterError = requestError;
            }
            if (!adapter) {
                /* Same fallback the terminal page uses: on Android and
                 * older GPUs the core (Vulkan) backend is often
                 * blocklisted while the GL/ES compatibility adapter
                 * still works. */
                try {
                    adapter = await navigator.gpu.requestAdapter(
                        { featureLevel: 'compatibility' });
                    if (adapter) report.compatibilityMode = true;
                } catch (compatibilityError) {
                    adapterError = adapterError || compatibilityError;
                }
            }
            if (!adapter) {
                failStep('adapter', 'requestAdapter() returned null for both ' +
                         'the core and the compatibility adapter' +
                         (adapterError ? ' (' + describeError(adapterError) + ')'
                                       : ''));
                return;
            }
            var adapterInfo = adapter.info || {};
            report.gpu = {
                vendor: adapterInfo.vendor || '',
                architecture: adapterInfo.architecture || '',
                device: adapterInfo.device || '',
                description: adapterInfo.description || '',
                isFallbackAdapter: !!adapter.isFallbackAdapter
            };
            try {
                report.features = Array.from(adapter.features || []);
            } catch (ignored) {}
            report.adapterLimits = limitsSnapshot(adapter.limits);
            if (report.compatibilityMode) {
                warn('core WebGPU adapter unavailable — running on the ' +
                     'compatibility adapter');
            }
            var gpuLabel = report.gpu.description ||
                           [report.gpu.vendor, report.gpu.architecture]
                               .filter(Boolean).join(' ') ||
                           'adapter';
            passStep('adapter', gpuLabel +
                     (report.compatibilityMode ? ' (compatibility mode)' : ''));

            /* ---- step: hardware --------------------------------------- */
            /* When the GPU process has no usable hardware backend (e.g.
             * Vulkan off/blocklisted on Linux), Chrome does NOT return a
             * null adapter — it silently hands out SwiftShader, without
             * even setting isFallbackAdapter. Every functional probe
             * passes on it (a CPU rasterizer executes shaders correctly,
             * just orders of magnitude too slowly), so a software
             * adapter must be a hard failure, not a warning. */
            beginStep('hardware');
            if (adapter.isFallbackAdapter ||
                looksLikeSoftwareRenderer(report.gpu.description + ' ' +
                                          report.gpu.architecture + ' ' +
                                          report.gpu.device + ' ' +
                                          report.gpu.vendor)) {
                report.softwareRenderer = true;
                var softwareLabel = report.gpu.architecture ||
                                    report.gpu.description ||
                                    'fallback adapter';
                if (options.allowSoftwareRenderer) {
                    warnStep('hardware', 'software rasterizer (' +
                             softwareLabel + ') accepted because ' +
                             'allowSoftwareRenderer is set — yetty will be ' +
                             'very slow');
                } else {
                    failStep('hardware', 'the adapter is a CPU software ' +
                             'rasterizer (' + softwareLabel + ') — the ' +
                             'browser has no hardware GPU acceleration, ' +
                             'and yetty would be unusably slow');
                    return;
                }
            } else {
                passStep('hardware', gpuLabel);
            }

            /* ---- step: limits ----------------------------------------- */
            beginStep('limits');
            var floorFailures = [];
            var floorNames = Object.keys(REQUIRED_LIMIT_FLOORS);
            for (var floorIndex = 0; floorIndex < floorNames.length; floorIndex++) {
                var floorName = floorNames[floorIndex];
                var floorValue = REQUIRED_LIMIT_FLOORS[floorName];
                var adapterValue = report.adapterLimits[floorName];
                if (typeof adapterValue === 'number' && adapterValue < floorValue) {
                    floorFailures.push(floorName + '=' + adapterValue +
                                       ' (yetty needs ≥ ' + floorValue + ')');
                }
            }
            /* Compatibility-mode adapters can expose vertex-stage storage
             * counts below the per-stage figure; yetty's binder reads its
             * mega storage buffer from the vertex shader. */
            var vertexStorageLimit = report.adapterLimits.maxStorageBuffersInVertexStage;
            if (typeof vertexStorageLimit === 'number' && vertexStorageLimit < 1) {
                floorFailures.push('maxStorageBuffersInVertexStage=0 ' +
                                   '(yetty needs storage buffers in the ' +
                                   'vertex stage)');
            }
            if (floorFailures.length) {
                failStep('limits', floorFailures.join('; '));
                return;
            }
            passStep('limits',
                     'maxStorageBufferBindingSize=' +
                     formatByteSize(report.adapterLimits.maxStorageBufferBindingSize) +
                     ', maxBufferSize=' +
                     formatByteSize(report.adapterLimits.maxBufferSize) +
                     ', maxTextureDimension2D=' +
                     report.adapterLimits.maxTextureDimension2D);

            /* ---- step: device ----------------------------------------- */
            beginStep('device');
            function clampedLimit(limitName) {
                var ceiling = REQUESTED_LIMIT_CEILINGS[limitName];
                var adapterMaximum = report.adapterLimits[limitName];
                return (typeof adapterMaximum === 'number')
                    ? Math.min(ceiling, adapterMaximum) : ceiling;
            }
            try {
                probeDevice = await adapter.requestDevice({
                    label: 'yetty-webgpu-health-probe',
                    requiredLimits: {
                        maxTextureDimension2D: clampedLimit('maxTextureDimension2D'),
                        maxStorageBufferBindingSize:
                            clampedLimit('maxStorageBufferBindingSize'),
                        maxBufferSize: clampedLimit('maxBufferSize'),
                        maxStorageBuffersPerShaderStage:
                            clampedLimit('maxStorageBuffersPerShaderStage')
                    }
                });
            } catch (deviceError) {
                failStep('device', 'requestDevice() failed: ' +
                         describeError(deviceError));
                return;
            }
            if (!probeDevice) {
                failStep('device', 'requestDevice() returned null');
                return;
            }
            if (probeDevice.addEventListener) {
                probeDevice.addEventListener('uncapturederror', function (event) {
                    var text = describeError(event.error);
                    report.uncapturedErrors.push(text);
                    logLine('uncaptured GPU error: ' + text, 'error');
                });
            }
            if (probeDevice.lost && probeDevice.lost.then) {
                probeDevice.lost.then(function (lostInfo) {
                    if (probeDeviceDestroyed) return;
                    deviceLostMessage =
                        (lostInfo && lostInfo.message) || 'no message';
                    report.uncapturedErrors.push('device lost: ' +
                                                 deviceLostMessage);
                }, function () {});
            }
            passStep('device', 'device created with yetty’s clamped limits');

            /* ---- step: shader ------------------------------------------ */
            beginStep('shader');
            var shaderModule = null;
            try {
                probeDevice.pushErrorScope('validation');
                shaderModule = probeDevice.createShaderModule({
                    label: 'yetty-webgpu-health-probe-shader',
                    code: PROBE_SHADER_WGSL
                });
                var compilationMessages = [];
                if (shaderModule.getCompilationInfo) {
                    var compilationInfo = await shaderModule.getCompilationInfo();
                    var messages = (compilationInfo && compilationInfo.messages) || [];
                    for (var messageIndex = 0; messageIndex < messages.length;
                         messageIndex++) {
                        var message = messages[messageIndex];
                        if (message.type === 'error') {
                            compilationMessages.push('line ' + message.lineNum +
                                                     ': ' + message.message);
                        }
                    }
                }
                var shaderScopeError = await popErrorScopeMessage(probeDevice);
                if (compilationMessages.length || shaderScopeError) {
                    failStep('shader', 'WGSL compilation failed: ' +
                             (compilationMessages.join('; ') ||
                              shaderScopeError));
                    return;
                }
            } catch (shaderError) {
                failStep('shader', 'createShaderModule() threw: ' +
                         describeError(shaderError));
                return;
            }
            passStep('shader', 'storage-in-vertex-stage probe shader compiled');

            /* ---- step: pipeline ---------------------------------------- */
            beginStep('pipeline');
            var renderPipeline = null;
            var pipelineDescriptor = {
                label: 'yetty-webgpu-health-probe-pipeline',
                layout: 'auto',
                vertex: { module: shaderModule, entryPoint: 'probe_vertex' },
                fragment: {
                    module: shaderModule,
                    entryPoint: 'probe_fragment',
                    targets: [{ format: 'rgba8unorm' }]
                },
                primitive: { topology: 'triangle-list' }
            };
            try {
                if (probeDevice.createRenderPipelineAsync) {
                    renderPipeline = await probeDevice
                        .createRenderPipelineAsync(pipelineDescriptor);
                } else {
                    probeDevice.pushErrorScope('validation');
                    renderPipeline = probeDevice
                        .createRenderPipeline(pipelineDescriptor);
                    var pipelineScopeError = await popErrorScopeMessage(probeDevice);
                    if (pipelineScopeError) {
                        failStep('pipeline', 'createRenderPipeline() raised: ' +
                                 pipelineScopeError);
                        return;
                    }
                }
            } catch (pipelineError) {
                failStep('pipeline', 'render pipeline creation failed: ' +
                         describeError(pipelineError));
                return;
            }
            passStep('pipeline', 'render pipeline built');

            /* ---- step: execute ----------------------------------------- */
            beginStep('execute');
            try {
                probeDevice.pushErrorScope('validation');
                probeDevice.pushErrorScope('out-of-memory');

                var storageBuffer = probeDevice.createBuffer({
                    label: 'probe-cells',
                    size: 32,
                    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST
                });
                var storageValues = new Float32Array(8);
                storageValues.set(PROBE_BASE_COLOR, 0);
                storageValues.set(PROBE_ACCENT_COLOR, 4);
                probeDevice.queue.writeBuffer(storageBuffer, 0, storageValues);

                var uniformBuffer = probeDevice.createBuffer({
                    label: 'probe-uniforms',
                    size: 16,
                    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
                });
                probeDevice.queue.writeBuffer(uniformBuffer, 0,
                    new Float32Array([1, 1, 1, 1]));

                var atlasTexture = probeDevice.createTexture({
                    label: 'probe-atlas',
                    size: { width: 4, height: 4 },
                    format: 'rgba8unorm',
                    usage: GPUTextureUsage.TEXTURE_BINDING |
                           GPUTextureUsage.COPY_DST
                });
                var whiteTexels = new Uint8Array(4 * 4 * 4);
                whiteTexels.fill(255);
                probeDevice.queue.writeTexture(
                    { texture: atlasTexture }, whiteTexels,
                    { bytesPerRow: 16 }, { width: 4, height: 4 });

                var atlasSampler = probeDevice.createSampler({
                    magFilter: 'linear',
                    minFilter: 'linear'
                });

                var bindGroup = probeDevice.createBindGroup({
                    label: 'probe-bind-group',
                    layout: renderPipeline.getBindGroupLayout(0),
                    entries: [
                        { binding: 0, resource: { buffer: storageBuffer } },
                        { binding: 1, resource: { buffer: uniformBuffer } },
                        { binding: 2, resource: atlasTexture.createView() },
                        { binding: 3, resource: atlasSampler }
                    ]
                });

                var renderTarget = probeDevice.createTexture({
                    label: 'probe-target',
                    size: { width: RENDER_TARGET_SIZE, height: RENDER_TARGET_SIZE },
                    format: 'rgba8unorm',
                    usage: GPUTextureUsage.RENDER_ATTACHMENT |
                           GPUTextureUsage.COPY_SRC
                });

                var readbackBuffer = probeDevice.createBuffer({
                    label: 'probe-readback',
                    size: READBACK_BYTES_PER_ROW * RENDER_TARGET_SIZE,
                    usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ
                });

                var commandEncoder = probeDevice.createCommandEncoder();
                var renderPass = commandEncoder.beginRenderPass({
                    colorAttachments: [{
                        view: renderTarget.createView(),
                        loadOp: 'clear',
                        storeOp: 'store',
                        /* Deliberately NOT the expected color: only an
                         * actually-executed draw can produce the expected
                         * readback. */
                        clearValue: { r: 1, g: 0, b: 0, a: 1 }
                    }]
                });
                renderPass.setPipeline(renderPipeline);
                renderPass.setBindGroup(0, bindGroup);
                renderPass.draw(3);
                renderPass.end();
                commandEncoder.copyTextureToBuffer(
                    { texture: renderTarget },
                    { buffer: readbackBuffer,
                      bytesPerRow: READBACK_BYTES_PER_ROW },
                    { width: RENDER_TARGET_SIZE, height: RENDER_TARGET_SIZE });
                probeDevice.queue.submit([commandEncoder.finish()]);

                var outOfMemoryError = await popErrorScopeMessage(probeDevice);
                var validationError = await popErrorScopeMessage(probeDevice);
                if (validationError || outOfMemoryError) {
                    failStep('execute', 'the probe draw raised: ' +
                             (validationError || outOfMemoryError));
                    return;
                }

                await readbackBuffer.mapAsync(GPUMapMode.READ);
                var pixels = new Uint8Array(readbackBuffer.getMappedRange());
                var centerOffset = (RENDER_TARGET_SIZE / 2) *
                                   READBACK_BYTES_PER_ROW +
                                   (RENDER_TARGET_SIZE / 2) * 4;
                var checkOffsets = [0, centerOffset];
                for (var checkIndex = 0; checkIndex < checkOffsets.length;
                     checkIndex++) {
                    var pixelOffset = checkOffsets[checkIndex];
                    for (var channel = 0; channel < 4; channel++) {
                        var actual = pixels[pixelOffset + channel];
                        var expected = EXPECTED_PIXEL_BYTES[channel];
                        if (Math.abs(actual - expected) > PIXEL_TOLERANCE) {
                            var actualPixel = Array.prototype.slice.call(
                                pixels, pixelOffset, pixelOffset + 4);
                            readbackBuffer.unmap();
                            failStep('execute', 'rendered pixel is [' +
                                     actualPixel.join(', ') +
                                     '], expected [' +
                                     EXPECTED_PIXEL_BYTES.join(', ') +
                                     '] — the shader did not run correctly');
                            return;
                        }
                    }
                }
                readbackBuffer.unmap();

                /* Timed heavy pass — catches software adapters whose
                 * identity strings pass the signature check above. */
                var heavyShaderModule = probeDevice.createShaderModule({
                    label: 'yetty-webgpu-health-heavy-shader',
                    code: HEAVY_SHADER_WGSL
                });
                var heavyPipelineDescriptor = {
                    label: 'yetty-webgpu-health-heavy-pipeline',
                    layout: 'auto',
                    vertex: { module: heavyShaderModule,
                              entryPoint: 'heavy_vertex' },
                    fragment: {
                        module: heavyShaderModule,
                        entryPoint: 'heavy_fragment',
                        targets: [{ format: 'rgba8unorm' }]
                    },
                    primitive: { topology: 'triangle-list' }
                };
                var heavyPipeline = probeDevice.createRenderPipelineAsync
                    ? await probeDevice.createRenderPipelineAsync(
                          heavyPipelineDescriptor)
                    : probeDevice.createRenderPipeline(
                          heavyPipelineDescriptor);
                var heavyTarget = probeDevice.createTexture({
                    label: 'probe-heavy-target',
                    size: { width: HEAVY_TARGET_SIZE,
                            height: HEAVY_TARGET_SIZE },
                    format: 'rgba8unorm',
                    usage: GPUTextureUsage.RENDER_ATTACHMENT
                });
                var heavyEncoder = probeDevice.createCommandEncoder();
                var heavyPass = heavyEncoder.beginRenderPass({
                    colorAttachments: [{
                        view: heavyTarget.createView(),
                        loadOp: 'clear',
                        storeOp: 'store',
                        clearValue: { r: 0, g: 0, b: 0, a: 1 }
                    }]
                });
                heavyPass.setPipeline(heavyPipeline);
                heavyPass.draw(3);
                heavyPass.end();
                var heavyStartMs = nowMs();
                probeDevice.queue.submit([heavyEncoder.finish()]);
                if (probeDevice.queue.onSubmittedWorkDone) {
                    await probeDevice.queue.onSubmittedWorkDone();
                    report.heavyDrawMs = Math.round(nowMs() - heavyStartMs);
                    if (report.heavyDrawMs > SOFTWARE_SUSPECT_DRAW_MS &&
                        !report.softwareRenderer) {
                        warn('the timed draw took ' + report.heavyDrawMs +
                             ' ms (hardware GPUs need a few ms) — this ' +
                             'GPU behaves like a software rasterizer or ' +
                             'is severely throttled; yetty may be ' +
                             'unusably slow');
                    }
                }
            } catch (executeError) {
                failStep('execute', 'probe draw/readback threw: ' +
                         describeError(executeError));
                return;
            }
            if (report.uncapturedErrors.length) {
                failStep('execute', 'uncaptured GPU error(s) during the ' +
                         'probe: ' + report.uncapturedErrors.join('; '));
                return;
            }
            passStep('execute', 'draw executed, readback pixel-verified' +
                     (report.heavyDrawMs !== null
                         ? '; timed draw ' + report.heavyDrawMs + ' ms'
                         : ''));

            /* ---- step: canvas ----------------------------------------- */
            beginStep('canvas');
            try {
                var probeCanvas = document.createElement('canvas');
                probeCanvas.width = 32;
                probeCanvas.height = 32;
                /* Pin the canvas in the DOM (off-screen) for the whole
                 * step. A configured but DETACHED canvas is collectable
                 * the moment the code awaits, and Chrome tears the whole
                 * wire connection down with it. */
                probeCanvas.style.cssText =
                    'position:absolute; left:-99999px; top:-99999px;' +
                    ' width:1px; height:1px; visibility:hidden;' +
                    ' pointer-events:none;';
                probeCanvasElement = probeCanvas;
                (document.body || document.documentElement)
                    .appendChild(probeCanvas);
                var canvasContext = probeCanvas.getContext('webgpu');
                if (!canvasContext) {
                    failStep('canvas', 'canvas.getContext(\'webgpu\') ' +
                             'returned null');
                    return;
                }
                probeDevice.pushErrorScope('validation');
                canvasContext.configure({
                    device: probeDevice,
                    format: preferredFormat,
                    alphaMode: 'opaque'
                });

                /* Draw the exact probe color to the canvas with a
                 * pipeline targeting the surface format, reusing the
                 * verified shader and resources from the execute step. */
                var canvasPipelineDescriptor = {
                    label: 'yetty-webgpu-health-canvas-pipeline',
                    layout: 'auto',
                    vertex: { module: shaderModule,
                              entryPoint: 'probe_vertex' },
                    fragment: {
                        module: shaderModule,
                        entryPoint: 'probe_fragment',
                        targets: [{ format: preferredFormat }]
                    },
                    primitive: { topology: 'triangle-list' }
                };
                var canvasPipeline = probeDevice.createRenderPipelineAsync
                    ? await probeDevice.createRenderPipelineAsync(
                          canvasPipelineDescriptor)
                    : probeDevice.createRenderPipeline(
                          canvasPipelineDescriptor);
                var canvasBindGroup = probeDevice.createBindGroup({
                    label: 'probe-canvas-bind-group',
                    layout: canvasPipeline.getBindGroupLayout(0),
                    entries: [
                        { binding: 0, resource: { buffer: storageBuffer } },
                        { binding: 1, resource: { buffer: uniformBuffer } },
                        { binding: 2, resource: atlasTexture.createView() },
                        { binding: 3, resource: atlasSampler }
                    ]
                });

                function drawProbeColorToCanvas() {
                    var surfaceTexture = canvasContext.getCurrentTexture();
                    var surfaceEncoder = probeDevice.createCommandEncoder();
                    var surfacePass = surfaceEncoder.beginRenderPass({
                        colorAttachments: [{
                            view: surfaceTexture.createView(),
                            loadOp: 'clear',
                            storeOp: 'store',
                            clearValue: { r: 1, g: 0, b: 0, a: 1 }
                        }]
                    });
                    surfacePass.setPipeline(canvasPipeline);
                    surfacePass.setBindGroup(0, canvasBindGroup);
                    surfacePass.draw(3);
                    surfacePass.end();
                    probeDevice.queue.submit([surfaceEncoder.finish()]);
                }

                function waitForPresentation() {
                    /* Two animation frames so the drawing buffer becomes
                     * the canvas bitmap; timeout fallback for throttled
                     * pages where rAF never fires. */
                    function oneFrame() {
                        return new Promise(function (resolve) {
                            var settled = false;
                            function finish() {
                                if (!settled) { settled = true; resolve(); }
                            }
                            if (window.requestAnimationFrame) {
                                window.requestAnimationFrame(finish);
                            }
                            setTimeout(finish, 150);
                        });
                    }
                    return oneFrame().then(oneFrame);
                }

                function snapshotPresentedPixel() {
                    /* drawImage sources the canvas' PRESENTED bitmap —
                     * the only JS-visible evidence of what compositing
                     * actually produced. Broken WebGPU↔GL interop shows
                     * up here as transparent black. */
                    var snapshotCanvas = document.createElement('canvas');
                    snapshotCanvas.width = probeCanvas.width;
                    snapshotCanvas.height = probeCanvas.height;
                    var context2d = snapshotCanvas.getContext('2d',
                        { willReadFrequently: true });
                    context2d.drawImage(probeCanvas, 0, 0);
                    var center = context2d.getImageData(
                        probeCanvas.width >> 1, probeCanvas.height >> 1,
                        1, 1).data;
                    return [center[0], center[1], center[2], center[3]];
                }

                function presentedPixelMatches(pixel) {
                    for (var channel = 0; channel < 4; channel++) {
                        if (Math.abs(pixel[channel] -
                                     EXPECTED_PIXEL_BYTES[channel]) > 4) {
                            return false;
                        }
                    }
                    return true;
                }

                drawProbeColorToCanvas();
                var canvasScopeError = await popErrorScopeMessage(probeDevice);
                /* A failed present surfaces as an ASYNC device loss, not
                 * as a validation error — give the submitted work (or a
                 * short grace period) time to settle before judging. */
                await Promise.race([
                    probeDevice.queue.onSubmittedWorkDone()
                        .catch(function () {}),
                    new Promise(function (resolve) {
                        setTimeout(resolve, 500);
                    })
                ]);
                if (deviceLostMessage) {
                    failStep('canvas', 'presenting to the canvas lost the ' +
                             'GPU device: ' + deviceLostMessage);
                    return;
                }
                if (canvasScopeError) {
                    failStep('canvas', 'drawing to the webgpu canvas ' +
                             'raised: ' + canvasScopeError);
                    return;
                }

                await waitForPresentation();
                var presentedPixel = snapshotPresentedPixel();
                if (!presentedPixelMatches(presentedPixel)) {
                    /* One retry: the first snapshot can race the very
                     * first presentation of a fresh canvas. */
                    drawProbeColorToCanvas();
                    await waitForPresentation();
                    presentedPixel = snapshotPresentedPixel();
                }
                if (!presentedPixelMatches(presentedPixel)) {
                    failStep('canvas', 'the canvas PRESENTS [' +
                             presentedPixel.join(', ') + '] where [' +
                             EXPECTED_PIXEL_BYTES.join(', ') + '] was ' +
                             'drawn — rendering succeeds off-screen but ' +
                             'the presented image never reaches the ' +
                             'compositor (broken WebGPU canvas interop)');
                    return;
                }
                canvasContext.unconfigure();
            } catch (canvasError) {
                failStep('canvas', deviceLostMessage
                    ? 'presenting to the canvas lost the GPU device: ' +
                      deviceLostMessage
                    : 'webgpu canvas setup threw: ' +
                      describeError(canvasError));
                return;
            }
            passStep('canvas', preferredFormat + ' surface presented; ' +
                     'presented image pixel-verified via 2D snapshot');
        }

        var timeoutHandle = null;
        var timeoutPromise = new Promise(function (resolve) {
            timeoutHandle = setTimeout(function () {
                resolve('timeout');
            }, timeoutMs);
        });

        return Promise.race([
            probe().then(function () { return 'done'; },
                         function (probeError) { return probeError; }),
            timeoutPromise
        ]).then(function (outcome) {
            clearTimeout(timeoutHandle);
            if (outcome === 'timeout' && !report.failedStep) {
                failStep(currentStepName, 'timed out after ' + timeoutMs +
                         ' ms — the browser/driver hung during this step');
            } else if (outcome !== 'done' && outcome !== 'timeout' &&
                       !report.failedStep) {
                failStep(currentStepName, 'unexpected error: ' +
                         describeError(outcome));
            }
            return finishReport();
        });
    }

    function summarize(report) {
        if (report.canRun) {
            var qualifiers = [];
            if (report.compatibilityMode) qualifiers.push('compatibility mode');
            if (report.softwareRenderer) qualifiers.push('software renderer');
            return 'WebGPU is healthy — yetty can run' +
                   (qualifiers.length ? ' (' + qualifiers.join(', ') + ')' : '') +
                   ' [' + report.totalMs + ' ms]';
        }
        return 'yetty cannot run in this browser — failed at step “' +
               report.failedStep + '”: ' + report.reason;
    }

    /* ---------------------------------------------------------------- */
    /* DOM output                                                        */
    /* ---------------------------------------------------------------- */

    var STYLE_ELEMENT_ID = 'yetty-webgpu-health-style';

    function ensureStyles(targetDocument) {
        if (targetDocument.getElementById(STYLE_ELEMENT_ID)) return;
        var style = targetDocument.createElement('style');
        style.id = STYLE_ELEMENT_ID;
        style.textContent =
            '.ywh-banner{flex:0 0 auto;background:#141A1F;' +
            'border-bottom:1px solid #364A47;color:#E0E5E4;' +
            'font-family:"JetBrains Mono","Fira Code","Consolas",monospace;' +
            'font-size:13px;padding:10px 14px;line-height:1.5;}' +
            '.ywh-banner.ywh-fail{border-left:4px solid #f55;}' +
            '.ywh-banner.ywh-warn{border-left:4px solid #6BA892;}' +
            '.ywh-banner-head{display:flex;align-items:center;gap:10px;}' +
            '.ywh-banner-title{font-weight:bold;}' +
            '.ywh-fail .ywh-banner-title{color:#f55;}' +
            '.ywh-warn .ywh-banner-title{color:#74C5A5;}' +
            '.ywh-banner-reason{color:#9FA7A8;flex:1 1 auto;min-width:0;}' +
            '.ywh-banner button{background:#1E262C;border:1px solid #364A47;' +
            'border-radius:4px;color:#9FA7A8;font-family:inherit;' +
            'font-size:12px;padding:3px 10px;cursor:pointer;flex:0 0 auto;}' +
            '.ywh-banner button:hover{color:#E0E5E4;border-color:#6BA892;}' +
            '.ywh-banner a{color:#6BA892;}' +
            '.ywh-banner a:hover{color:#74C5A5;}' +
            '.ywh-banner-advice{color:#9FA7A8;margin-top:6px;}' +
            '.ywh-banner-details{display:none;margin-top:10px;}' +
            '.ywh-banner-details.ywh-visible{display:block;}' +
            '.ywh-report{color:#E0E5E4;font-size:12px;line-height:1.6;}' +
            '.ywh-report table{border-collapse:collapse;margin:6px 0;}' +
            '.ywh-report td{padding:2px 12px 2px 0;vertical-align:top;}' +
            '.ywh-step-pass{color:#6c6;}' +
            '.ywh-step-fail{color:#f55;font-weight:bold;}' +
            '.ywh-step-warn{color:#74C5A5;}' +
            '.ywh-step-skip{color:#556162;}' +
            '.ywh-report-detail{color:#9FA7A8;}' +
            '.ywh-report-section{color:#74C5A5;margin-top:10px;' +
            'font-weight:bold;}' +
            '.ywh-report code{background:#1E262C;border:1px solid #364A47;' +
            'border-radius:4px;padding:0 5px;color:#E0E5E4;' +
            'word-break:break-all;}';
        (targetDocument.head || targetDocument.documentElement)
            .appendChild(style);
    }

    var STEP_STATUS_GLYPHS = {
        pass: '✓',
        fail: '✗',
        warn: '⚠',
        skip: '–'
    };

    function createReportElement(report, targetDocument) {
        targetDocument = targetDocument || document;
        ensureStyles(targetDocument);
        var root = targetDocument.createElement('div');
        root.className = 'ywh-report';

        function addSection(labelText) {
            var section = targetDocument.createElement('div');
            section.className = 'ywh-report-section';
            section.textContent = labelText;
            root.appendChild(section);
        }
        function addRow(table, labelText, valueText, valueIsCode) {
            var row = targetDocument.createElement('tr');
            var labelCell = targetDocument.createElement('td');
            labelCell.className = 'ywh-report-detail';
            labelCell.textContent = labelText;
            var valueCell = targetDocument.createElement('td');
            if (valueIsCode) {
                var codeElement = targetDocument.createElement('code');
                codeElement.textContent = valueText;
                valueCell.appendChild(codeElement);
            } else {
                valueCell.textContent = valueText;
            }
            row.appendChild(labelCell);
            row.appendChild(valueCell);
            table.appendChild(row);
        }

        addSection('Probe steps');
        var stepsTable = targetDocument.createElement('table');
        for (var stepIndex = 0; stepIndex < report.steps.length; stepIndex++) {
            var step = report.steps[stepIndex];
            var row = targetDocument.createElement('tr');
            var statusCell = targetDocument.createElement('td');
            statusCell.className = 'ywh-step-' + step.status;
            statusCell.textContent =
                (STEP_STATUS_GLYPHS[step.status] || '?') + ' ' + step.status;
            var titleCell = targetDocument.createElement('td');
            titleCell.textContent = step.title;
            var detailCell = targetDocument.createElement('td');
            detailCell.className = 'ywh-report-detail';
            detailCell.textContent = step.detail || '';
            row.appendChild(statusCell);
            row.appendChild(titleCell);
            row.appendChild(detailCell);
            stepsTable.appendChild(row);
        }
        root.appendChild(stepsTable);

        addSection('GPU');
        var gpuTable = targetDocument.createElement('table');
        if (report.gpu.description || report.gpu.vendor) {
            addRow(gpuTable, 'adapter',
                   report.gpu.description || report.gpu.vendor, true);
        }
        if (report.gpu.vendor) addRow(gpuTable, 'vendor', report.gpu.vendor);
        if (report.gpu.architecture) {
            addRow(gpuTable, 'architecture', report.gpu.architecture);
        }
        addRow(gpuTable, 'mode',
               report.compatibilityMode ? 'compatibility' : 'core');
        if (report.softwareRenderer) {
            addRow(gpuTable, 'rasterizer', 'software (CPU) — not usable');
        }
        if (report.heavyDrawMs !== null) {
            addRow(gpuTable, 'timed draw',
                   report.heavyDrawMs + ' ms (hardware GPUs: a few ms)');
        }
        var limitNames = Object.keys(REQUIRED_LIMIT_FLOORS);
        for (var limitIndex = 0; limitIndex < limitNames.length; limitIndex++) {
            var limitName = limitNames[limitIndex];
            var adapterValue = report.adapterLimits[limitName];
            if (typeof adapterValue !== 'number') continue;
            var isSize = limitName.indexOf('Size') !== -1;
            addRow(gpuTable, limitName,
                   (isSize ? formatByteSize(adapterValue) : String(adapterValue)) +
                   '  (yetty needs ≥ ' +
                   (isSize ? formatByteSize(REQUIRED_LIMIT_FLOORS[limitName])
                           : String(REQUIRED_LIMIT_FLOORS[limitName])) + ')');
        }
        addRow(gpuTable, 'browser', report.userAgent, true);
        root.appendChild(gpuTable);

        if (report.warnings.length) {
            addSection('Warnings');
            for (var warningIndex = 0; warningIndex < report.warnings.length;
                 warningIndex++) {
                var warningLine = targetDocument.createElement('div');
                warningLine.className = 'ywh-step-warn';
                warningLine.textContent =
                    '⚠ ' + report.warnings[warningIndex];
                root.appendChild(warningLine);
            }
        }
        if (report.uncapturedErrors.length) {
            addSection('Uncaptured GPU errors');
            for (var errorIndex = 0; errorIndex < report.uncapturedErrors.length;
                 errorIndex++) {
                var errorLine = targetDocument.createElement('div');
                errorLine.className = 'ywh-step-fail';
                errorLine.textContent = report.uncapturedErrors[errorIndex];
                root.appendChild(errorLine);
            }
        }
        return root;
    }

    function installBanner(report, options) {
        options = options || {};
        if (report.canRun && !report.warnings.length) return null;
        var targetDocument = options.document || document;
        ensureStyles(targetDocument);

        var banner = targetDocument.createElement('div');
        banner.className = 'ywh-banner ' +
                           (report.canRun ? 'ywh-warn' : 'ywh-fail');

        var head = targetDocument.createElement('div');
        head.className = 'ywh-banner-head';

        var title = targetDocument.createElement('span');
        title.className = 'ywh-banner-title';
        title.textContent = report.canRun
            ? '⚠ WebGPU is degraded'
            : '✗ yetty cannot run in this browser';
        head.appendChild(title);

        var reason = targetDocument.createElement('span');
        reason.className = 'ywh-banner-reason';
        reason.textContent = report.canRun
            ? report.warnings.join('; ')
            : (report.reason || 'WebGPU health check failed');
        head.appendChild(reason);

        var detailsButton = targetDocument.createElement('button');
        detailsButton.type = 'button';
        detailsButton.textContent = 'Details';
        head.appendChild(detailsButton);

        if (options.detailsUrl) {
            var reportLink = targetDocument.createElement('a');
            reportLink.href = options.detailsUrl;
            reportLink.textContent = 'full report →';
            head.appendChild(reportLink);
        }

        var dismissButton = targetDocument.createElement('button');
        dismissButton.type = 'button';
        dismissButton.textContent = '×';
        dismissButton.title = 'Dismiss';
        head.appendChild(dismissButton);

        banner.appendChild(head);

        if (!report.canRun && report.advice) {
            var advice = targetDocument.createElement('div');
            advice.className = 'ywh-banner-advice';
            advice.textContent = report.advice;
            banner.appendChild(advice);
        }

        var details = targetDocument.createElement('div');
        details.className = 'ywh-banner-details';
        details.appendChild(createReportElement(report, targetDocument));
        banner.appendChild(details);

        detailsButton.addEventListener('click', function () {
            details.classList.toggle('ywh-visible');
        });
        dismissButton.addEventListener('click', function () {
            banner.remove();
        });

        var container = options.container || targetDocument.body;
        container.insertBefore(banner, container.firstChild);
        return banner;
    }

    window.yettyWebgpuHealth = {
        version: HEALTH_VERSION,
        run: runHealthCheck,
        summarize: summarize,
        createReportElement: createReportElement,
        installBanner: installBanner
    };
})();
