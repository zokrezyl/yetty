# webasm-webgpu-health — page-load WebGPU health probe

A standalone, dependency-free probe that decides — the moment the yetty
web page loads — whether this browser/GPU can run yetty at all, and if
not, says exactly which link of the chain is broken and what the user
can do about it.

The per-instance gate in `build-tools/web/terminal.html` only fires when
a terminal iframe starts, and only tests adapter/device creation. This
tool runs earlier (on `index.html` load) and goes further: it drives the
whole bring-up chain to an actually-executed shader with pixel-verified
output.

## Probe steps (in order; the first failure stops the run)

| step       | what it proves                                                        |
|------------|-----------------------------------------------------------------------|
| `api`      | secure context, `WebAssembly`, `navigator.gpu`, preferred canvas format |
| `adapter`  | `requestAdapter()` — core first, `featureLevel: 'compatibility'` fallback (same order as terminal.html) |
| `hardware` | the adapter is a **real GPU**, not a silent CPU fallback. When hardware acceleration is off or Vulkan is disabled/blocklisted (e.g. the chrome://flags Vulkan entries reset to Default on Linux), Chrome does **not** return a null adapter — it silently hands out SwiftShader, without even setting `isFallbackAdapter`. Every functional step passes on it (a CPU rasterizer executes shaders correctly, just orders of magnitude too slowly), so a software signature (swiftshader / llvmpipe / lavapipe / softpipe / WARP in the adapter identity) is a **hard failure**. Override with `?allow-software=1` (page) / `allowSoftwareRenderer` (API) to downgrade it to a warning |
| `limits`   | adapter limits meet yetty's floors (storage buffers per stage ≥ 8, 2D textures ≥ 4096, storage binding ≥ 128 MiB, buffers ≥ 128 MiB, storage buffers usable in the **vertex** stage) |
| `device`   | `requestDevice()` with the limits yetty requests, clamped to the adapter — mirrors `src/yetty/ywebgpu/limits.c` |
| `shader`   | WGSL compile of a probe shader using yetty's flattened-binder binding pattern: one storage buffer read from **both** the vertex and fragment stages, a uniform block, a sampled atlas texture; checked via `getCompilationInfo()` + error scope |
| `pipeline` | render pipeline builds from that shader (`createRenderPipelineAsync` where available). Compatibility-mode adapters with `maxStorageBuffersInVertexStage == 0` fail here — exactly the class of device yetty cannot render on |
| `execute`  | off-screen draw into an `rgba8unorm` target, `copyTextureToBuffer` + `mapAsync` readback, pixels compared against the exact expected value. The target is cleared to a *different* color first, so only a shader that really executed can pass. Also runs a **timed heavy pass** (1024² target, 256-iteration transcendental loop per pixel, `report.heavyDrawMs`): hardware GPUs finish in single-digit milliseconds, SwiftShader measured ~118 ms on a 32-core desktop — above 100 ms raises a slow-GPU warning, catching software adapters whose identity strings pass the `hardware` signature check |
| `canvas`   | `canvas.getContext('webgpu')`, `configure()` with the preferred format, then the probe color is **drawn to the surface and the PRESENTED image is read back** (2D-canvas `drawImage` + `getImageData`) and pixel-verified. This is the only JS-visible evidence of what compositing actually produced — it catches the case where WebGPU renders perfectly off-screen but the presented image never reaches the compositor. That is exactly what happens when Chrome's driver-workaround list disables the Vulkan↔GL compositing interop for a GPU (fix: `chrome://flags/#force-enable-webgpu-interop`): no JS error, no device loss, the canvas just presents transparent black. Reproduced and verified under Xvfb (Vulkan WebGPU + GL compositor, no interop possible → step fails with "canvas PRESENTS [0,0,0,0]"; all-SwiftShader stack → step passes). Runs **last** on purpose: a failed present can also surface as an asynchronous device loss (headless environments kill the device here), which would take down the earlier steps' reporting. The probe canvas is pinned in the DOM off-screen — a configured but detached canvas is garbage-collectable at the first `await`, and Chrome tears the whole wire connection down with it ("Instance dropped") |

Warnings (non-fatal, reported but `canRun` stays true): compatibility
adapter in use, timed draw above the slow-GPU threshold. A software
rasterizer is NOT a warning — it fails the `hardware` step unless
explicitly allowed.

`report.advice` is **platform-aware and ordered** (detected from the
user agent). Linux/Chromium gets the Vulkan flag ladder in the order
that matches field results — 1) `chrome://flags/#enable-vulkan`
(hardware WebGPU on Linux runs on Vulkan; alone the most common fix),
2) `#enable-unsafe-webgpu` (blocklist override),
3) `#force-enable-webgpu-interop` (renders-but-presents-nothing) —
plus the graphics-acceleration setting and `vulkaninfo`/driver hints.
macOS gets Metal-era advice (no Vulkan flags; Safari Feature Flags →
WebGPU, or Chrome `#enable-unsafe-webgpu`); iOS/iPadOS gets the
Settings → Apps → Safari → Advanced → Feature Flags → WebGPU path
(all iOS browsers are WebKit); Windows (D3D12 + driver update),
Android and Firefox (`dom.webgpu.enabled`) have their own texts. The
abort dialog in `terminal.html` uses the same platform detection and
ordering for its copy-paste flag fields.

The probe device is destroyed at the end of the run; terminal iframes
create their own devices later. A watchdog (default 15 s) converts a
hung driver call into a failure of whichever step was in flight.

## Files

- `webgpu-health.js` — the probe library. No dependencies, no DOM
  requirements; banner/report rendering is opt-in.
- `index.html` — standalone diagnostics page (step table, GPU/limit
  details, raw JSON, re-run button). Deployed as `webgpu-health.html`.

## JavaScript API

Everything lives under `window.yettyWebgpuHealth`:

```js
const report = await yettyWebgpuHealth.run({
    timeoutMs: 15000,
    allowSoftwareRenderer: false   // true: software GPU → warning, not failure
});
// report.canRun          — hard verdict: can yetty run here at all
// report.ok              — canRun with zero warnings
// report.failedStep      — name of the first failed step, or null
// report.reason          — human-readable failure reason
// report.advice          — what the user should try (flags page, driver, …)
// report.steps[]         — { name, title, status: pass|fail|skip, detail, durationMs }
// report.gpu             — adapter info (vendor/architecture/device/description)
// report.adapterLimits   — numeric snapshot of the adapter limits
// report.softwareRenderer — true when the adapter is a CPU rasterizer
// report.heavyDrawMs     — wall-clock of the timed heavy pass (null if skipped)
// report.warnings[]      — non-fatal degradations (compat mode, slow timed draw)

yettyWebgpuHealth.summarize(report);            // one-line verdict string
yettyWebgpuHealth.createReportElement(report);  // detailed DOM node
yettyWebgpuHealth.installBanner(report, {       // failure/warning banner at
    detailsUrl: 'webgpu-health.html'            // the top of the page
});
```

Every `run()` additionally:

- sets `window.yettyWebgpuHealthReport`,
- dispatches a `yetty-webgpu-health` `CustomEvent` on `window` with the
  report as `detail`,
- mirrors the verdict to the console (`console.error` on failure) — so
  headless captures see it without scraping the DOM.

## Deployment

The webasm cmake glue (`build-tools/yetty/platform/webasm/cmake.cmake`)
copies `webgpu-health.js` and this directory's `index.html` (renamed
`webgpu-health.html`) into the build dir next to `index.html`;
`make verify-webasm` checks they are present, and the Pages workflow
publishes them with the rest of the site.

`build-tools/web/index.html` loads the probe and runs it immediately on
page load: on failure it injects a prominent banner (with a Details
expander and a link to `webgpu-health.html`); on a degraded-but-usable
setup it shows a dismissible warning banner.

## Running standalone

Serve the checkout (WebGPU needs http(s), not `file://`) and open the
page directly:

```sh
python3 -m http.server 8000
# → http://localhost:8000/tools/webasm-webgpu-health/
```

or, from a webasm build dir: `http://localhost:8000/webgpu-health.html`.
