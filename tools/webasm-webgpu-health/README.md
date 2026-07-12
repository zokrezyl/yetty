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
| `limits`   | adapter limits meet yetty's floors (storage buffers per stage ≥ 8, 2D textures ≥ 4096, storage binding ≥ 128 MiB, buffers ≥ 128 MiB, storage buffers usable in the **vertex** stage) |
| `device`   | `requestDevice()` with the limits yetty requests, clamped to the adapter — mirrors `src/yetty/ywebgpu/limits.c` |
| `canvas`   | `canvas.getContext('webgpu')`, `configure()` with the preferred format, `getCurrentTexture()` + a clear pass |
| `shader`   | WGSL compile of a probe shader using yetty's flattened-binder binding pattern: one storage buffer read from **both** the vertex and fragment stages, a uniform block, a sampled atlas texture; checked via `getCompilationInfo()` + error scope |
| `pipeline` | render pipeline builds from that shader (`createRenderPipelineAsync` where available). Compatibility-mode adapters with `maxStorageBuffersInVertexStage == 0` fail here — exactly the class of device yetty cannot render on |
| `execute`  | off-screen draw into an `rgba8unorm` target, `copyTextureToBuffer` + `mapAsync` readback, pixels compared against the exact expected value. The target is cleared to a *different* color first, so only a shader that really executed can pass |

Warnings (non-fatal, reported but `canRun` stays true): compatibility
adapter in use, software rasterizer (SwiftShader/llvmpipe/WARP —
detected via `adapter.info` and `isFallbackAdapter`).

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
const report = await yettyWebgpuHealth.run({ timeoutMs: 15000 });
// report.canRun          — hard verdict: can yetty run here at all
// report.ok              — canRun with zero warnings
// report.failedStep      — name of the first failed step, or null
// report.reason          — human-readable failure reason
// report.advice          — what the user should try (flags page, driver, …)
// report.steps[]         — { name, title, status: pass|fail|skip, detail, durationMs }
// report.gpu             — adapter info (vendor/architecture/device/description)
// report.adapterLimits   — numeric snapshot of the adapter limits
// report.warnings[]      — non-fatal degradations (compat mode, software rasterizer)

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
