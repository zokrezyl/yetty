/*
 * ydummy-server — standalone SERVER half of the ydummy pilot.
 *
 * The canvas class is pure contract + state; RENDERING IT IS THIS
 * PROGRAM'S BUSINESS. The renderer below owns the WebGPU device, the
 * pipeline and the WGSL assembly outright, and observes the canvas
 * exclusively through the object API's exposed read accessors
 * (shader_text / rect / time / shader_generation) — the same way a
 * hosting yetty would render the class with its own machinery. Flow:
 *
 *   1. headless WGPU bring-up (no surface, no window);
 *   2. create the canvas locally (constructor runs here), publish it as
 *      the RPC root;
 *   3. spawn the pure client over a socketpair and serve its yclass-RPC
 *      calls (set_shader / set_rect / set_time arrive over the wire and
 *      mutate the canvas state through the skels);
 *   4. after the client disconnects, render the state, read the frame
 *      back, and write a binary PPM.
 *
 * Exit 0 only when the pixels prove the CLIENT's shader ran: the readback
 * must be non-uniform inside the rect, clear-color outside, and the red
 * channel must oscillate across the center row (the client ships
 * concentric rings; the built-in default gradient is monotonic — a
 * monotonic result means the wire bytes never reached the pipeline).
 *
 * Usage: ydummy-server <client-binary> [output.ppm]
 */

#include <yetty/api/ydummy/canvas.h>
#include <yetty/ywebgpu/request.h>

#include <yetty/yclass/rpc.h>
#include <yetty/yclass/transport-fd.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <webgpu/webgpu.h>

/* Implementation-side registration hook (accessor + skel lookups). Lives in
 * the generated impl glue linked via yetty_ydummy; deliberately NOT part of
 * the object-API header — only a serving process needs it. */
struct yetty_ycore_void_result yetty_ydummy_register(void);

enum {
    SERVER_TARGET_WIDTH = 512,
    SERVER_TARGET_HEIGHT = 512,
    SERVER_RECT_MIN = 64,
    SERVER_RECT_MAX = 448,
};

static void print_uncaptured_error(WGPUDevice const *device, WGPUErrorType type,
                                   WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)device;
    (void)userdata1;
    (void)userdata2;
    fprintf(stderr, "ydummy-server: device error (type=%d): %.*s\n", (int)type, (int)message.length,
            message.data ? message.data : "");
}

static void map_done_callback(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1,
                              void *userdata2)
{
    (void)message;
    (void)userdata2;
    *(WGPUMapAsyncStatus *)userdata1 = status;
}

static void check(struct yetty_ycore_void_result result, const char *what)
{
    if (YETTY_IS_ERR(result)) {
        yetty_ycore_error_print(stderr, what, result.error);
        yetty_ycore_error_destroy(result.error);
        exit(1);
    }
}

/*=============================================================================
 * The renderer — this program's own GPU machinery. The canvas class knows
 * nothing about it; the renderer reads the class state via the object API.
 *===========================================================================*/

/* Uniform block layout — must match `struct YdummyUniforms` in the WGSL
 * preamble (vec2f pairs then two f32; 32 bytes, no implicit padding). */
struct canvas_uniforms {
    float rect_min[2];
    float rect_max[2];
    float resolution[2];
    float time_seconds;
    float pad;
};

struct canvas_renderer {
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;
    /* Shader generation the current pipeline was compiled from; rebuilt
     * when the canvas reports a different one. */
    uint32_t compiled_generation;
    int has_pipeline;
    WGPUShaderModule shader_module;
    WGPUBindGroupLayout bind_group_layout;
    WGPUPipelineLayout pipeline_layout;
    WGPURenderPipeline pipeline;
    WGPUBuffer uniform_buffer;
    WGPUBindGroup bind_group;
};

static void renderer_release(struct canvas_renderer *renderer)
{
    if (renderer->bind_group) {
        wgpuBindGroupRelease(renderer->bind_group);
        renderer->bind_group = NULL;
    }
    if (renderer->uniform_buffer) {
        wgpuBufferRelease(renderer->uniform_buffer);
        renderer->uniform_buffer = NULL;
    }
    if (renderer->pipeline) {
        wgpuRenderPipelineRelease(renderer->pipeline);
        renderer->pipeline = NULL;
    }
    if (renderer->pipeline_layout) {
        wgpuPipelineLayoutRelease(renderer->pipeline_layout);
        renderer->pipeline_layout = NULL;
    }
    if (renderer->bind_group_layout) {
        wgpuBindGroupLayoutRelease(renderer->bind_group_layout);
        renderer->bind_group_layout = NULL;
    }
    if (renderer->shader_module) {
        wgpuShaderModuleRelease(renderer->shader_module);
        renderer->shader_module = NULL;
    }
    renderer->has_pipeline = 0;
}

/* Assemble the full WGSL: renderer-owned preamble (uniforms + vertex
 * stage) + the canvas's user fragment (or the built-in default) + the
 * renderer-owned fs_main wrapper. Returned buffer is malloc'd. */
static char *renderer_assemble_wgsl(const char *fragment_text, size_t fragment_length,
                                    size_t *out_length)
{
    static const char preamble[] =
        "struct YdummyUniforms {\n"
        "    rect_min: vec2f,\n"
        "    rect_max: vec2f,\n"
        "    resolution: vec2f,\n"
        "    time: f32,\n"
        "    pad: f32,\n"
        "}\n"
        "@group(0) @binding(0) var<uniform> ydummy: YdummyUniforms;\n"
        "\n"
        "@vertex\n"
        "fn vs_main(@builtin(vertex_index) vertex_index: u32) -> @builtin(position) vec4f {\n"
        "    let corner = vec2f(f32(vertex_index & 1u), f32(vertex_index >> 1u));\n"
        "    let px = mix(ydummy.rect_min, ydummy.rect_max, corner);\n"
        "    let ndc = vec2f(px.x / ydummy.resolution.x * 2.0 - 1.0,\n"
        "                    1.0 - px.y / ydummy.resolution.y * 2.0);\n"
        "    return vec4f(ndc, 0.0, 1.0);\n"
        "}\n"
        "\n";
    static const char default_fragment[] =
        "fn ydummy_fragment(uv: vec2f, time: f32) -> vec4f {\n"
        "    return vec4f(uv.x, uv.y, 0.5 + 0.5 * sin(time), 1.0);\n"
        "}\n";
    static const char epilogue[] =
        "\n"
        "@fragment\n"
        "fn fs_main(@builtin(position) position: vec4f) -> @location(0) vec4f {\n"
        "    let extent = max(ydummy.rect_max - ydummy.rect_min, vec2f(1.0, 1.0));\n"
        "    let uv = (position.xy - ydummy.rect_min) / extent;\n"
        "    return ydummy_fragment(uv, ydummy.time);\n"
        "}\n";

    const char *fragment_source = fragment_text ? fragment_text : default_fragment;
    size_t source_length = fragment_text ? fragment_length : sizeof(default_fragment) - 1;
    size_t total = (sizeof(preamble) - 1) + source_length + (sizeof(epilogue) - 1);

    char *wgsl = malloc(total + 1);
    if (!wgsl) {
        return NULL;
    }
    memcpy(wgsl, preamble, sizeof(preamble) - 1);
    memcpy(wgsl + sizeof(preamble) - 1, fragment_source, source_length);
    memcpy(wgsl + sizeof(preamble) - 1 + source_length, epilogue, sizeof(epilogue) - 1);
    wgsl[total] = '\0';
    if (out_length) {
        *out_length = total;
    }
    return wgsl;
}

static int renderer_build(struct canvas_renderer *renderer, const char *fragment_text,
                          size_t fragment_length)
{
    renderer_release(renderer);

    size_t wgsl_length = 0;
    char *wgsl_source = renderer_assemble_wgsl(fragment_text, fragment_length, &wgsl_length);
    if (!wgsl_source) {
        fprintf(stderr, "ydummy-server: wgsl assemble alloc failed\n");
        return -1;
    }

    WGPUShaderSourceWGSL wgsl = {0};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = (WGPUStringView){wgsl_source, wgsl_length};
    WGPUShaderModuleDescriptor shader_desc = {0};
    shader_desc.nextInChain = &wgsl.chain;
    renderer->shader_module = wgpuDeviceCreateShaderModule(renderer->device, &shader_desc);
    free(wgsl_source);
    if (!renderer->shader_module) {
        fprintf(stderr, "ydummy-server: shader module create failed\n");
        return -1;
    }

    WGPUBindGroupLayoutEntry layout_entry = {0};
    layout_entry.binding = 0;
    layout_entry.visibility = (WGPUShaderStage)(WGPUShaderStage_Vertex | WGPUShaderStage_Fragment);
    layout_entry.buffer.type = WGPUBufferBindingType_Uniform;
    layout_entry.buffer.minBindingSize = sizeof(struct canvas_uniforms);
    WGPUBindGroupLayoutDescriptor layout_desc = {0};
    layout_desc.entryCount = 1;
    layout_desc.entries = &layout_entry;
    renderer->bind_group_layout = wgpuDeviceCreateBindGroupLayout(renderer->device, &layout_desc);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {0};
    pipeline_layout_desc.bindGroupLayoutCount = 1;
    pipeline_layout_desc.bindGroupLayouts = &renderer->bind_group_layout;
    renderer->pipeline_layout =
        wgpuDeviceCreatePipelineLayout(renderer->device, &pipeline_layout_desc);

    WGPUColorTargetState color_target = {0};
    color_target.format = renderer->target_format;
    color_target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragment_state = {0};
    fragment_state.module = renderer->shader_module;
    fragment_state.entryPoint = (WGPUStringView){"fs_main", 7};
    fragment_state.targetCount = 1;
    fragment_state.targets = &color_target;

    WGPURenderPipelineDescriptor pipeline_desc = {0};
    pipeline_desc.layout = renderer->pipeline_layout;
    pipeline_desc.vertex.module = renderer->shader_module;
    pipeline_desc.vertex.entryPoint = (WGPUStringView){"vs_main", 7};
    pipeline_desc.primitive.topology = WGPUPrimitiveTopology_TriangleStrip;
    pipeline_desc.primitive.frontFace = WGPUFrontFace_CCW;
    pipeline_desc.primitive.cullMode = WGPUCullMode_None;
    pipeline_desc.fragment = &fragment_state;
    pipeline_desc.multisample.count = 1;
    pipeline_desc.multisample.mask = 0xFFFFFFFFu;
    renderer->pipeline = wgpuDeviceCreateRenderPipeline(renderer->device, &pipeline_desc);

    WGPUBufferDescriptor buffer_desc = {0};
    buffer_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    buffer_desc.size = sizeof(struct canvas_uniforms);
    renderer->uniform_buffer = wgpuDeviceCreateBuffer(renderer->device, &buffer_desc);

    WGPUBindGroupEntry bind_entry = {0};
    bind_entry.binding = 0;
    bind_entry.buffer = renderer->uniform_buffer;
    bind_entry.size = sizeof(struct canvas_uniforms);
    WGPUBindGroupDescriptor bind_desc = {0};
    bind_desc.layout = renderer->bind_group_layout;
    bind_desc.entryCount = 1;
    bind_desc.entries = &bind_entry;
    renderer->bind_group = wgpuDeviceCreateBindGroup(renderer->device, &bind_desc);

    if (!renderer->bind_group_layout || !renderer->pipeline_layout || !renderer->pipeline ||
        !renderer->uniform_buffer || !renderer->bind_group) {
        fprintf(stderr, "ydummy-server: pipeline build failed\n");
        renderer_release(renderer);
        return -1;
    }
    renderer->has_pipeline = 1;
    return 0;
}

/* Render the canvas's state into an open render pass — all state read
 * through the object API, nothing else shared with the class. */
static int renderer_draw(struct canvas_renderer *renderer, struct yetty_yclass_object *canvas,
                         WGPURenderPassEncoder render_pass, uint32_t target_width,
                         uint32_t target_height)
{
    struct yetty_ycore_uint32_result generation_res = yetty_ydummy_canvas_shader_generation(canvas);
    struct yetty_ycore_const_char_ptr_result text_res = yetty_ydummy_canvas_shader_text(canvas);
    struct yetty_ycore_size_result length_res = yetty_ydummy_canvas_shader_length(canvas);
    struct yetty_ycore_rectangle_result rect_res = yetty_ydummy_canvas_rect(canvas);
    struct yetty_ycore_float_result time_res = yetty_ydummy_canvas_time(canvas);
    if (YETTY_IS_ERR(generation_res) || YETTY_IS_ERR(text_res) || YETTY_IS_ERR(length_res) ||
        YETTY_IS_ERR(rect_res) || YETTY_IS_ERR(time_res)) {
        fprintf(stderr, "ydummy-server: canvas state accessors failed\n");
        return -1;
    }

    if (!renderer->has_pipeline || renderer->compiled_generation != generation_res.value) {
        if (renderer_build(renderer, text_res.value, length_res.value) != 0) {
            return -1;
        }
        renderer->compiled_generation = generation_res.value;
    }

    struct canvas_uniforms uniforms = {0};
    float rect_width = rect_res.value.max.x - rect_res.value.min.x;
    float rect_height = rect_res.value.max.y - rect_res.value.min.y;
    if (rect_width <= 0.0f || rect_height <= 0.0f) {
        uniforms.rect_max[0] = (float)target_width;
        uniforms.rect_max[1] = (float)target_height;
    } else {
        uniforms.rect_min[0] = rect_res.value.min.x;
        uniforms.rect_min[1] = rect_res.value.min.y;
        uniforms.rect_max[0] = rect_res.value.max.x;
        uniforms.rect_max[1] = rect_res.value.max.y;
    }
    uniforms.resolution[0] = (float)target_width;
    uniforms.resolution[1] = (float)target_height;
    uniforms.time_seconds = time_res.value;
    wgpuQueueWriteBuffer(renderer->queue, renderer->uniform_buffer, 0, &uniforms, sizeof(uniforms));

    wgpuRenderPassEncoderSetPipeline(render_pass, renderer->pipeline);
    wgpuRenderPassEncoderSetBindGroup(render_pass, 0, renderer->bind_group, 0, NULL);
    wgpuRenderPassEncoderDraw(render_pass, 4, 1, 0, 0);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <client-binary> [output.ppm]\n", argv[0]);
        return 2;
    }
    const char *client_path = argv[1];
    const char *ppm_path = argc > 2 ? argv[2] : "tmp/ydummy.ppm";

    /* --- headless WGPU bring-up (instance -> adapter -> device) -------- */
    WGPUInstanceFeatureName instance_features[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor instance_desc = {0};
    instance_desc.requiredFeatureCount = 1;
    instance_desc.requiredFeatures = instance_features;
    WGPUInstance instance = wgpuCreateInstance(&instance_desc);
    if (!instance) {
        fprintf(stderr, "ydummy-server: wgpuCreateInstance failed\n");
        return 1;
    }

    WGPUAdapter adapter = NULL;
    int adapter_ready = 0;
    WGPURequestAdapterOptions adapter_opts = {0};
    adapter_opts.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo adapter_callback_info = {0};
    adapter_callback_info.mode = WGPUCallbackMode_WaitAnyOnly;
    adapter_callback_info.callback = yetty_ywebgpu_adapter_request_callback;
    adapter_callback_info.userdata1 = &adapter;
    adapter_callback_info.userdata2 = &adapter_ready;
    WGPUFutureWaitInfo adapter_wait = {0};
    adapter_wait.future =
        wgpuInstanceRequestAdapter(instance, &adapter_opts, adapter_callback_info);
    if (wgpuInstanceWaitAny(instance, 1, &adapter_wait, UINT64_MAX) != WGPUWaitStatus_Success ||
        !adapter) {
        fprintf(stderr, "ydummy-server: failed to acquire WGPU adapter\n");
        return 1;
    }

    WGPUDevice device = NULL;
    struct yetty_ywebgpu_request_state device_state = {{0}, 0};
    WGPUDeviceDescriptor device_desc = {0};
    device_desc.uncapturedErrorCallbackInfo.callback = print_uncaptured_error;
    WGPURequestDeviceCallbackInfo device_callback_info = {0};
    device_callback_info.mode = WGPUCallbackMode_WaitAnyOnly;
    device_callback_info.callback = yetty_ywebgpu_device_request_callback;
    device_callback_info.userdata1 = &device;
    device_callback_info.userdata2 = &device_state;
    WGPUFutureWaitInfo device_wait = {0};
    device_wait.future = wgpuAdapterRequestDevice(adapter, &device_desc, device_callback_info);
    if (wgpuInstanceWaitAny(instance, 1, &device_wait, UINT64_MAX) != WGPUWaitStatus_Success ||
        !device) {
        fprintf(stderr, "ydummy-server: failed to acquire WGPU device: %s\n",
                device_state.error_msg[0] ? device_state.error_msg : "(no message)");
        return 1;
    }
    WGPUQueue queue = wgpuDeviceGetQueue(device);

    /* --- the served canvas ---------------------------------------------- */
    /* rpc_init first — it seeds the handle counter at 1; without it the
     * first minted handle is 0, which the wire treats as "no root". Then
     * the implementation-side registration (accessor + skel lookups), the
     * local create (constructor lifecycle runs here) and the root publish. */
    check(yetty_yclass_rpc_init(), "ydummy-server: rpc_init");
    check(yetty_ydummy_register(), "ydummy-server: register");
    struct yetty_yclass_object_ptr_result canvas_res = yetty_ydummy_canvas_create(NULL);
    if (YETTY_IS_ERR(canvas_res)) {
        yetty_ycore_error_print(stderr, "ydummy-server: canvas_create", canvas_res.error);
        yetty_ycore_error_destroy(canvas_res.error);
        return 1;
    }
    struct yetty_yclass_object *canvas = canvas_res.value;
    struct yetty_yclass_handle_result root_res = yetty_yclass_rpc_set_root(canvas);
    if (YETTY_IS_ERR(root_res)) {
        yetty_ycore_error_print(stderr, "ydummy-server: set_root", root_res.error);
        yetty_ycore_error_destroy(root_res.error);
        return 1;
    }

    /* --- spawn the pure client over a socketpair and serve it ----------- */
    int socket_fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds) != 0) {
        perror("ydummy-server: socketpair");
        return 1;
    }
    pid_t client_pid = fork();
    if (client_pid < 0) {
        perror("ydummy-server: fork");
        return 1;
    }
    if (client_pid == 0) {
        close(socket_fds[0]);
        char fd_text[16];
        snprintf(fd_text, sizeof(fd_text), "%d", socket_fds[1]);
        execl(client_path, client_path, fd_text, (char *)NULL);
        perror("ydummy-server: execl client");
        _exit(127);
    }
    close(socket_fds[1]);

    struct yetty_yclass_transport_ptr_result transport_res =
        yetty_yclass_transport_fd_create(socket_fds[0]);
    if (YETTY_IS_ERR(transport_res)) {
        yetty_ycore_error_print(stderr, "ydummy-server: transport_fd_create", transport_res.error);
        yetty_ycore_error_destroy(transport_res.error);
        return 1;
    }
    /* Serve until the client disconnects (clean EOF). Every set_shader /
     * set_rect / set_time lands on the canvas state through the skels. */
    check(yetty_yclass_rpc_server_run(transport_res.value), "ydummy-server: serve loop");

    int client_status = 0;
    if (waitpid(client_pid, &client_status, 0) < 0) {
        perror("ydummy-server: waitpid");
        return 1;
    }
    if (!WIFEXITED(client_status) || WEXITSTATUS(client_status) != 0) {
        fprintf(stderr, "ydummy-server: client failed (status %d)\n", client_status);
        return 1;
    }
    {
        struct yetty_ycore_void_result transport_destroy_res =
            transport_res.value->ops->destroy(transport_res.value);
        if (YETTY_IS_ERR(transport_destroy_res)) {
            yetty_ycore_error_destroy(transport_destroy_res.error);
        }
    }

    /* --- render the state the client configured -------------------------- */
    WGPUTextureDescriptor texture_desc = {0};
    texture_desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    texture_desc.dimension = WGPUTextureDimension_2D;
    texture_desc.size = (WGPUExtent3D){SERVER_TARGET_WIDTH, SERVER_TARGET_HEIGHT, 1};
    texture_desc.format = WGPUTextureFormat_RGBA8Unorm;
    texture_desc.mipLevelCount = 1;
    texture_desc.sampleCount = 1;
    WGPUTexture target_texture = wgpuDeviceCreateTexture(device, &texture_desc);
    WGPUTextureView target_view = wgpuTextureCreateView(target_texture, NULL);
    if (!target_texture || !target_view) {
        fprintf(stderr, "ydummy-server: offscreen target creation failed\n");
        return 1;
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
    WGPURenderPassColorAttachment color_attachment = {0};
    color_attachment.view = target_view;
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_attachment.loadOp = WGPULoadOp_Clear;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.clearValue = (WGPUColor){0.02, 0.02, 0.05, 1.0};
    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;
    WGPURenderPassEncoder render_pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);

    struct canvas_renderer renderer = {0};
    renderer.device = device;
    renderer.queue = queue;
    renderer.target_format = WGPUTextureFormat_RGBA8Unorm;
    if (renderer_draw(&renderer, canvas, render_pass, SERVER_TARGET_WIDTH, SERVER_TARGET_HEIGHT) !=
        0) {
        return 1;
    }

    wgpuRenderPassEncoderEnd(render_pass);
    wgpuRenderPassEncoderRelease(render_pass);

    /* --- readback -------------------------------------------------------- */
    uint32_t aligned_bytes_per_row = (SERVER_TARGET_WIDTH * 4 + 255) & ~255u;
    uint64_t readback_size = (uint64_t)aligned_bytes_per_row * SERVER_TARGET_HEIGHT;
    WGPUBufferDescriptor readback_desc = {0};
    readback_desc.size = readback_size;
    readback_desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(device, &readback_desc);
    if (!readback) {
        fprintf(stderr, "ydummy-server: readback buffer creation failed\n");
        return 1;
    }

    WGPUTexelCopyTextureInfo copy_src = {0};
    copy_src.texture = target_texture;
    WGPUTexelCopyBufferInfo copy_dst = {0};
    copy_dst.buffer = readback;
    copy_dst.layout.bytesPerRow = aligned_bytes_per_row;
    copy_dst.layout.rowsPerImage = SERVER_TARGET_HEIGHT;
    WGPUExtent3D copy_extent = {SERVER_TARGET_WIDTH, SERVER_TARGET_HEIGHT, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &copy_src, &copy_dst, &copy_extent);

    WGPUCommandBuffer command_buffer = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(queue, 1, &command_buffer);
    wgpuCommandBufferRelease(command_buffer);
    wgpuCommandEncoderRelease(encoder);

    WGPUMapAsyncStatus map_status = WGPUMapAsyncStatus_Error;
    WGPUBufferMapCallbackInfo map_callback_info = {0};
    map_callback_info.mode = WGPUCallbackMode_WaitAnyOnly;
    map_callback_info.callback = map_done_callback;
    map_callback_info.userdata1 = &map_status;
    WGPUFutureWaitInfo map_wait = {0};
    map_wait.future =
        wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, readback_size, map_callback_info);
    if (wgpuInstanceWaitAny(instance, 1, &map_wait, UINT64_MAX) != WGPUWaitStatus_Success ||
        map_status != WGPUMapAsyncStatus_Success) {
        fprintf(stderr, "ydummy-server: readback map failed\n");
        return 1;
    }
    const uint8_t *pixels = wgpuBufferGetConstMappedRange(readback, 0, readback_size);
    if (!pixels) {
        fprintf(stderr, "ydummy-server: GetConstMappedRange returned NULL\n");
        return 1;
    }

    /* --- verify + dump PPM ----------------------------------------------- */
    FILE *ppm = fopen(ppm_path, "wb");
    if (!ppm) {
        fprintf(stderr, "ydummy-server: cannot open %s for writing\n", ppm_path);
        return 1;
    }
    fprintf(ppm, "P6\n%d %d\n255\n", SERVER_TARGET_WIDTH, SERVER_TARGET_HEIGHT);

    uint8_t inside_min = 255;
    uint8_t inside_max = 0;
    int outside_clear_ok = 1;
    for (int y = 0; y < SERVER_TARGET_HEIGHT; y++) {
        const uint8_t *row = pixels + (size_t)y * aligned_bytes_per_row;
        for (int x = 0; x < SERVER_TARGET_WIDTH; x++) {
            const uint8_t *pixel = row + (size_t)x * 4;
            fwrite(pixel, 1, 3, ppm);
            int inside = x >= SERVER_RECT_MIN && x < SERVER_RECT_MAX && y >= SERVER_RECT_MIN &&
                         y < SERVER_RECT_MAX;
            if (inside) {
                if (pixel[0] < inside_min) {
                    inside_min = pixel[0];
                }
                if (pixel[0] > inside_max) {
                    inside_max = pixel[0];
                }
            } else if (pixel[0] > 20) {
                outside_clear_ok = 0;
            }
        }
    }
    fclose(ppm);

    /* The client ships RINGS: the red channel must oscillate across the
     * center row. The built-in default gradient is monotonic in x — if the
     * wire bytes never reached the pipeline, this count stays near zero. */
    int direction_changes = 0;
    {
        const uint8_t *row = pixels + (size_t)(SERVER_TARGET_HEIGHT / 2) * aligned_bytes_per_row;
        int previous_direction = 0;
        uint8_t previous_red = row[(size_t)SERVER_RECT_MIN * 4];
        for (int x = SERVER_RECT_MIN + 1; x < SERVER_RECT_MAX; x++) {
            uint8_t red = row[(size_t)x * 4];
            int delta = (int)red - (int)previous_red;
            if (delta > 8 || delta < -8) {
                int direction = delta > 0 ? 1 : -1;
                if (previous_direction && direction != previous_direction) {
                    direction_changes++;
                }
                previous_direction = direction;
                previous_red = red;
            }
        }
    }
    wgpuBufferUnmap(readback);

    check(yetty_ydummy_destroy(canvas), "ydummy-server: destroy");
    renderer_release(&renderer);

    wgpuBufferRelease(readback);
    wgpuTextureViewRelease(target_view);
    wgpuTextureRelease(target_texture);
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);

    int shader_ran = (inside_max - inside_min) > 100;
    int rings_arrived = direction_changes >= 4;
    int pass = shader_ran && rings_arrived && outside_clear_ok;
    printf("ydummy-server: %s (inside red %u..%u, %d ring oscillations, outside %s) -> %s\n",
           pass ? "PASS" : "FAIL", inside_min, inside_max, direction_changes,
           outside_clear_ok ? "clear" : "POLLUTED", ppm_path);
    return pass ? 0 : 1;
}
