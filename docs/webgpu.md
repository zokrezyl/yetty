# WebGPU Concepts

A short primer on the WebGPU objects yetty uses, with C snippets against the
Dawn C header (`webgpu/webgpu.h`). For how yetty *owns* these objects see
[WebGPU Architecture](webgpu-architecture.md); for how it binds resources see
[GPU Resource Binding](gpu-resource-binding.md).

## Core objects (creation order)

### Instance
Entry point to WebGPU, created once. The platform creates it (it needs the OS)
and passes it via the yinit runtime.
```c
WGPUInstance instance = wgpuCreateInstance(NULL);
```

### Surface
The window's drawable area; platform-specific. Created by the platform from the
native window handle (GLFW helper, Android `ANativeWindow`, canvas on WebASM).
NULL in headless mode.

### Adapter
A physical GPU, requested from the instance by yframework.
```c
wgpuInstanceRequestAdapter(instance, &options, callbackInfo); /* → WGPUAdapter */
```

### Device + Queue
The logical GPU connection and its single command queue, both owned by
yframework.
```c
wgpuAdapterRequestDevice(adapter, &desc, callbackInfo); /* → WGPUDevice */
WGPUQueue queue = wgpuDeviceGetQueue(device);
```

## Resource objects

### Buffer
GPU memory for vertices, uniforms, or storage data.
```c
WGPUBufferDescriptor desc = {
    .size = bytes,
    .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
};
WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &desc);
```
Usage flags combine `Vertex`, `Index`, `Uniform`, `Storage`, `CopyDst`,
`CopySrc`. Yetty's large per-frame data (cells, primitives, glyph metadata) lives
in `Storage` buffers; small per-frame constants live in a `Uniform` buffer.

### Texture / TextureView / Sampler
```c
WGPUTextureDescriptor td = {
    .size = {width, height, 1},
    .format = WGPUTextureFormat_RGBA8Unorm,
    .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
};
WGPUTexture     tex  = wgpuDeviceCreateTexture(device, &td);
WGPUTextureView view = wgpuTextureCreateView(tex, NULL);
WGPUSampler     samp = wgpuDeviceCreateSampler(device, &(WGPUSamplerDescriptor){
    .magFilter = WGPUFilterMode_Linear, .minFilter = WGPUFilterMode_Linear });
```
Font atlases are `R8Unorm` (MSDF / raster glyph coverage); color content is
`RGBA8Unorm`.

### ShaderModule
Compiled WGSL. Yetty generates part of the WGSL (binding declarations, offset and
region constants) and prepends it to each provider's shader body before
compiling — see [GPU Resource Binding](gpu-resource-binding.md).
```c
WGPUShaderSourceWGSL wgsl = { .chain = {.sType = WGPUSType_ShaderSourceWGSL},
                              .code = {.data = src, .length = len} };
WGPUShaderModuleDescriptor sd = { .nextInChain = &wgsl.chain };
WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &sd);
```

## Binding and pipelines

### BindGroupLayout / BindGroup / PipelineLayout
A bind group layout declares what resources a shader expects; a bind group binds
the actual resources. Yetty's binder builds these automatically from the
flattened resource-set tree, so there is **no fixed shared/per-view group split**
— the binding layout is derived from whatever resource sets were submitted. See
[GPU Resource Binding](gpu-resource-binding.md).

### RenderPipeline
The full draw configuration: shader, vertex layout, blending, primitive
topology. Yetty compiles a pipeline once and recompiles only when the shader code
hash changes (see [Render Pipeline](../src/yetty/yrender/README.md)).

## Per-frame commands

```c
WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device, NULL);

WGPURenderPassDescriptor pass = {
    .colorAttachmentCount = 1,
    .colorAttachments = &(WGPURenderPassColorAttachment){
        .view = target_view, .loadOp = WGPULoadOp_Clear, .storeOp = WGPUStoreOp_Store },
};
WGPURenderPassEncoder rp = wgpuCommandEncoderBeginRenderPass(enc, &pass);

wgpuRenderPassEncoderSetPipeline(rp, pipeline);
wgpuRenderPassEncoderSetBindGroup(rp, 0, bind_group, 0, NULL);
wgpuRenderPassEncoderSetVertexBuffer(rp, 0, quad_vb, 0, WGPU_WHOLE_SIZE);
wgpuRenderPassEncoderDraw(rp, 6, instance_count, 0, 0); /* full-pane quad */
wgpuRenderPassEncoderEnd(rp);

WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, NULL);
wgpuQueueSubmit(queue, 1, &cmd);          /* one queue, see webgpu-architecture.md */
wgpuSurfacePresent(surface);              /* or copy to readback in headless mode */
```

Most layers draw a single full-pane quad and do all the work in the fragment
shader (SDF evaluation, glyph sampling). `instance_count > 1` is used where each
instance maps to one cell — see [ydraw](../src/yetty/ydraw/README.md).

## Where ownership lives

The authoritative ownership table (who creates instance/surface/adapter/device/
queue/allocator/render-target) is in
[WebGPU Architecture](webgpu-architecture.md). In short: the platform creates
instance + surface; yframework owns the device, queue, allocator, and render
target; each renderer owns its own pipelines and per-frame buffers.
