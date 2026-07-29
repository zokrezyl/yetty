# ywebgpu — WebGPU request / limits / error / description glue

ywebgpu is a small utility library over the raw WebGPU C API: adapter and
device request callbacks, a defaults-with-config-overrides `WGPULimits`
builder, error handling (uncaptured-error fail-fast, device-lost, captured
validation scopes), and a human-readable adapter/surface description. It is
what `yframework` uses to bring the GPU up, and what `yrender` uses to
validate GPU work. Links against `webgpu` and `yetty_yplatform_core` (the
fatal-report page dialog on webasm).

## Pieces

- **`request.c`** — generic callbacks for
  `WGPURequestAdapterCallbackInfo` / `WGPURequestDeviceCallbackInfo`.
  `userdata1` is the output handle, `userdata2` a
  `struct yetty_ywebgpu_request_state` that captures the driver/browser
  failure message, so "adapter blocklisted on this GPU" survives to the log
  instead of a generic failure.
- **`limits.c`** — `yetty_ywebgpu_fill_default_limits()` fills a
  `WGPULimits` with `WGPU_LIMIT_*_UNDEFINED` everywhere except the four
  limits yetty actively wants (`maxTextureDimension2D`,
  `maxStorageBufferBindingSize`, `maxBufferSize`,
  `maxStorageBuffersPerShaderStage`), each overridable via the
  `YETTY_YCONFIG_KEY_GPU_*` config keys and clamped to the adapter-supported
  maximum so the device request cannot be rejected for over-asking.
- **`error.c`** — three layers:
  - the **uncaptured-error callback** stores the error in a global state
    struct, logs it, reports it to the hosting page on webasm, and
    `_Exit(2)`s — fail fast rather than cascade;
  - the **device-lost callback** exits (`_Exit(3)`) on any reason except
    `Destroyed` (normal teardown);
  - **captured validation scopes** (`error_scope_push` / `error_scope_pop`)
    for wgpu calls fed by untrusted input (e.g. user WGSL arriving over the
    wire — see [../yshadertoy](../yshadertoy/README.md)): errors inside the
    scope come back as a Result instead of hitting the fail-fast path. On
    the browser backend the pop blocks on the scope future via
    `wgpuInstanceWaitAny` (Asyncify), guarded against running inside a
    stackful coroutine.
- **`utils.c`** — `yetty_ywebgpu_get_webgpu_description()` builds a
  multi-line adapter summary (backend, vendor, device, key limits) plus,
  when a surface is passed, the supported texture formats and present modes.

## Public API sketch

```c
void yetty_ywebgpu_fill_default_limits(WGPUAdapter adapter,
    const struct yetty_yconfig_config *config, WGPULimits *out);

struct yetty_ywebgpu_request_state state = {{0}, 0};
/* set as .callback in the request callback-info, with &adapter as
 * userdata1 and &state as userdata2 */
void yetty_ywebgpu_adapter_request_callback(WGPURequestAdapterStatus status,
    WGPUAdapter adapter, WGPUStringView message, void *userdata1, void *userdata2);

WGPUUncapturedErrorCallbackInfo yetty_ywebgpu_get_error_callback_info(void);
WGPUDeviceLostCallbackInfo yetty_ywebgpu_get_device_lost_callback_info(void);

void yetty_ywebgpu_error_scope_push(WGPUDevice device);
struct yetty_ycore_void_result yetty_ywebgpu_error_scope_pop(
    WGPUInstance instance, WGPUDevice device);

char *desc = yetty_ywebgpu_get_webgpu_description(adapter, surface); /* free() */
```

## Headers

`include/yetty/ywebgpu/{limits,request,utils}.h` — plus one historical
oddity: the error API lives at `include/yetty/webgpu/error.h` (no `y` in
the directory), a path predating the module rename that its consumers still
include.

## File map

| file | role |
|------|------|
| `request.c` | adapter/device request callbacks + failure-message capture |
| `limits.c` | default limits, config overrides, adapter clamping |
| `error.c` | uncaptured-error state + fail-fast, device-lost, validation scopes |
| `utils.c` | adapter / surface-capability description string |

## Consumers

- `../yframework/yframework.c` — the whole bring-up: request callbacks,
  limits, error + device-lost callback infos, adapter description logging.
- `../yrender/` (`pipeline.c`, `gpu-resource-binder.c`,
  `render-target-texture.c`) — validation scopes around shader compiles and
  resource creation.
- `../yvterm/grid-shader-glyph-layer.c`, `../yvnc/vnc-server.c` — scopes around
  runtime-assembled shaders.
- `tools/msdf/gen-msdf-gpu`, `tools/msdf/render-atlas` — headless GPU
  bring-up in CLI tools.

## See also

- [../../../docs/webgpu.md](../../../docs/webgpu.md),
  [../../../docs/webgpu-architecture.md](../../../docs/webgpu-architecture.md).
- [../yrender/README.md](../yrender/README.md) — where the device handed
  over by this module gets used.
