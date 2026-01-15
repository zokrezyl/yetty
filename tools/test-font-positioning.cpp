// Test font positioning with WebGPU rendering using yetty's Font class
// Usage: ./test-font-positioning <font-cache-prefix>
// Example: ./test-font-positioning ~/.cache/yetty/msdf-font-cache/WZZSSZ_CenturySchoolbook-32-172434592e9f388f

#include <iostream>
#include <vector>
#include <cmath>

#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <webgpu/webgpu.h>

#include <yetty/font.h>
#include <yetty/wgpu-compat.h>

using namespace yetty;

// Instance data for GPU
struct GlyphInstance {
    float posX, posY;
    float sizeX, sizeY;
    float uvX, uvY, uvW, uvH;
    float r, g, b, a;
};

static const char* shaderCode = R"(
struct Uniforms {
    viewSize: vec2f,
    atlasSize: vec2f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var atlasTex: texture_2d<f32>;
@group(0) @binding(2) var atlasSampler: sampler;

struct VertexInput {
    @location(0) pos: vec2f,
    @location(1) size: vec2f,
    @location(2) uv: vec4f,
    @location(3) color: vec4f,
    @builtin(vertex_index) vertexIndex: u32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) texCoord: vec2f,
    @location(1) color: vec4f,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var corners = array<vec2f, 6>(
        vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0),
        vec2f(1.0, 0.0), vec2f(1.0, 1.0), vec2f(0.0, 1.0)
    );
    let corner = corners[in.vertexIndex % 6];

    let pixelPos = in.pos + corner * in.size;
    let ndcPos = (pixelPos / uniforms.viewSize) * 2.0 - 1.0;

    var out: VertexOutput;
    out.position = vec4f(ndcPos.x, -ndcPos.y, 0.0, 1.0);
    out.texCoord = in.uv.xy + corner * in.uv.zw;
    out.color = in.color;
    return out;
}

fn median(r: f32, g: f32, b: f32) -> f32 {
    return max(min(r, g), min(max(r, g), b));
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Debug: just show red boxes to test pipeline
    return vec4f(1.0, 0.0, 0.0, 1.0);
}
)";

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <font-cache-prefix>" << std::endl;
        std::cout << "Example: " << argv[0] << " ~/.cache/yetty/msdf-font-cache/WZZSSZ_CenturySchoolbook-32-172434592e9f388f" << std::endl;
        return 1;
    }

    std::string prefix = argv[1];
    std::string atlasPath = prefix + ".lz4";
    std::string metricsPath = prefix + ".json";

    // Load font using yetty's Font class
    Font font;
    if (!font.loadAtlas(atlasPath, metricsPath)) {
        std::cerr << "Failed to load font from: " << prefix << std::endl;
        return 1;
    }

    std::cout << "Loaded font: " << font.getAtlasWidth() << "x" << font.getAtlasHeight() << std::endl;
    std::cout << "Font size: " << font.getFontSize() << ", Line height: " << font.getLineHeight() << std::endl;
    std::cout << "Glyph count: " << font.getGlyphCount() << std::endl;

    // Print metrics for test glyphs
    const char testChars[] = {'A', 'g', 'p', 'q', 'x', 'y'};
    for (char c : testChars) {
        const GlyphMetrics* m = font.getGlyph(c);
        if (m) {
            float descender = m->_size.y - m->_bearing.y;
            std::cout << "'" << c << "': bearingY=" << m->_bearing.y
                      << " sizeY=" << m->_size.y << " descender=" << descender << std::endl;
        }
    }

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW" << std::endl;
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Font Positioning Test", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return 1;
    }

    // Create WebGPU instance
    WGPUInstanceDescriptor instanceDesc = {};
    WGPUInstance instance = wgpuCreateInstance(&instanceDesc);
    WGPUSurface surface = glfwCreateWindowWGPUSurface(instance, window);

    // Request adapter
    WGPUAdapter adapter = nullptr;
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = surface;
    adapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;

    WGPURequestAdapterCallbackInfo adapterCallbackInfo = {};
    adapterCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    adapterCallbackInfo.callback = [](WGPURequestAdapterStatus status, WGPUAdapter a, WGPUStringView msg, void* ud1, void* ud2) {
        if (status == WGPURequestAdapterStatus_Success) *(WGPUAdapter*)ud1 = a;
    };
    adapterCallbackInfo.userdata1 = &adapter;
    wgpuInstanceRequestAdapter(instance, &adapterOpts, adapterCallbackInfo);
    if (!adapter) {
        std::cerr << "Failed to get adapter" << std::endl;
        return 1;
    }

    // Request device
    WGPUDevice device = nullptr;
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.label = WGPU_STR("test device");

    WGPURequestDeviceCallbackInfo deviceCallbackInfo = {};
    deviceCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    deviceCallbackInfo.callback = [](WGPURequestDeviceStatus status, WGPUDevice d, WGPUStringView msg, void* ud1, void* ud2) {
        if (status == WGPURequestDeviceStatus_Success) *(WGPUDevice*)ud1 = d;
    };
    deviceCallbackInfo.userdata1 = &device;
    wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCallbackInfo);
    if (!device) {
        std::cerr << "Failed to get device" << std::endl;
        return 1;
    }

    WGPUQueue queue = wgpuDeviceGetQueue(device);

    // Configure surface
    WGPUSurfaceConfiguration surfaceConfig = {};
    surfaceConfig.device = device;
    surfaceConfig.format = WGPUTextureFormat_BGRA8Unorm;
    surfaceConfig.usage = WGPUTextureUsage_RenderAttachment;
    surfaceConfig.width = 800;
    surfaceConfig.height = 600;
    surfaceConfig.presentMode = WGPUPresentMode_Fifo;
    surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Opaque;
    wgpuSurfaceConfigure(surface, &surfaceConfig);

    // Create font texture
    if (!font.createTexture(device, queue)) {
        std::cerr << "Failed to create font texture" << std::endl;
        return 1;
    }

    // Use font's sampler
    WGPUSampler sampler = font.getSampler();
    std::cout << "Texture view: " << font.getTextureView() << ", Sampler: " << sampler << std::endl;

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPU_STR(shaderCode);
    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Create uniform buffer
    float uniforms[4] = {800.0f, 600.0f, (float)font.getAtlasWidth(), (float)font.getAtlasHeight()};
    WGPUBufferDescriptor uniformBufDesc = {};
    uniformBufDesc.size = 16;
    uniformBufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformBufDesc);
    wgpuQueueWriteBuffer(queue, uniformBuffer, 0, uniforms, 16);

    // Create bind group layout
    WGPUBindGroupLayoutEntry bglEntries[3] = {};
    bglEntries[0].binding = 0;
    bglEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    bglEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    bglEntries[1].binding = 1;
    bglEntries[1].visibility = WGPUShaderStage_Fragment;
    bglEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
    bglEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    bglEntries[2].binding = 2;
    bglEntries[2].visibility = WGPUShaderStage_Fragment;
    bglEntries[2].sampler.type = WGPUSamplerBindingType_Filtering;
    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 3;
    bglDesc.entries = bglEntries;
    WGPUBindGroupLayout bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);

    // Create bind group
    WGPUBindGroupEntry bgEntries[3] = {};
    bgEntries[0].binding = 0;
    bgEntries[0].buffer = uniformBuffer;
    bgEntries[0].size = 16;
    bgEntries[1].binding = 1;
    bgEntries[1].textureView = font.getTextureView();
    bgEntries[2].binding = 2;
    bgEntries[2].sampler = sampler;
    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bindGroupLayout;
    bgDesc.entryCount = 3;
    bgDesc.entries = bgEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

    // Vertex attributes
    WGPUVertexAttribute attrs[4] = {};
    attrs[0].format = WGPUVertexFormat_Float32x2;
    attrs[0].offset = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x2;
    attrs[1].offset = 8;
    attrs[1].shaderLocation = 1;
    attrs[2].format = WGPUVertexFormat_Float32x4;
    attrs[2].offset = 16;
    attrs[2].shaderLocation = 2;
    attrs[3].format = WGPUVertexFormat_Float32x4;
    attrs[3].offset = 32;
    attrs[3].shaderLocation = 3;

    WGPUVertexBufferLayout vbLayout = {};
    vbLayout.arrayStride = sizeof(GlyphInstance);
    vbLayout.stepMode = WGPUVertexStepMode_Instance;
    vbLayout.attributeCount = 4;
    vbLayout.attributes = attrs;

    // Create render pipeline
    WGPURenderPipelineDescriptor rpDesc = {};
    rpDesc.layout = pipelineLayout;
    rpDesc.vertex.module = shaderModule;
    rpDesc.vertex.entryPoint = WGPU_STR("vs_main");
    rpDesc.vertex.bufferCount = 1;
    rpDesc.vertex.buffers = &vbLayout;

    WGPUFragmentState fragState = {};
    fragState.module = shaderModule;
    fragState.entryPoint = WGPU_STR("fs_main");
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_BGRA8Unorm;
    WGPUBlendState blend = {};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;
    colorTarget.blend = &blend;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    rpDesc.fragment = &fragState;

    rpDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rpDesc.multisample.count = 1;
    rpDesc.multisample.mask = ~0u;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &rpDesc);
    if (!pipeline) {
        std::cerr << "Failed to create render pipeline!" << std::endl;
        return 1;
    }
    std::cout << "Pipeline created: " << pipeline << std::endl;

    // Build glyph instances for test string
    std::string testStr = "Agpqxy Typography";
    std::vector<GlyphInstance> instances;

    float x = 50.0f;
    float baseline = 200.0f;
    float scale = 2.0f;

    for (char c : testStr) {
        const GlyphMetrics* m = font.getGlyph(c);
        if (!m) {
            x += font.getFontSize() * scale * 0.5f;
            continue;
        }

        GlyphInstance inst;
        inst.posX = x + m->_bearing.x * scale;
        inst.posY = baseline - m->_bearing.y * scale;
        inst.sizeX = m->_size.x * scale;
        inst.sizeY = m->_size.y * scale;
        inst.uvX = m->_uvMin.x;
        inst.uvY = m->_uvMin.y;
        inst.uvW = m->_uvMax.x - m->_uvMin.x;
        inst.uvH = m->_uvMax.y - m->_uvMin.y;
        inst.r = 0.0f; inst.g = 0.0f; inst.b = 0.0f; inst.a = 1.0f;

        if (c == 'A' || c == 'g') {
            std::cout << "'" << c << "': pos=(" << inst.posX << "," << inst.posY << ") size=(" << inst.sizeX << "," << inst.sizeY << ")"
                      << " uv=(" << inst.uvX << "," << inst.uvY << "," << inst.uvW << "," << inst.uvH << ")" << std::endl;
        }
        instances.push_back(inst);

        x += m->_advance * scale;
    }

    // Second line
    x = 50.0f;
    baseline = 350.0f;
    testStr = "gjpqy descenders";
    for (char c : testStr) {
        const GlyphMetrics* m = font.getGlyph(c);
        if (!m) {
            x += font.getFontSize() * scale * 0.5f;
            continue;
        }

        GlyphInstance inst;
        inst.posX = x + m->_bearing.x * scale;
        inst.posY = baseline - m->_bearing.y * scale;
        inst.sizeX = m->_size.x * scale;
        inst.sizeY = m->_size.y * scale;
        inst.uvX = m->_uvMin.x;
        inst.uvY = m->_uvMin.y;
        inst.uvW = m->_uvMax.x - m->_uvMin.x;
        inst.uvH = m->_uvMax.y - m->_uvMin.y;
        inst.r = 0.0f; inst.g = 0.0f; inst.b = 0.0f; inst.a = 1.0f;
        instances.push_back(inst);

        x += m->_advance * scale;
    }

    std::cout << "Created " << instances.size() << " glyph instances" << std::endl;

    // Create instance buffer
    WGPUBufferDescriptor instBufDesc = {};
    instBufDesc.size = instances.size() * sizeof(GlyphInstance);
    instBufDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    WGPUBuffer instanceBuffer = wgpuDeviceCreateBuffer(device, &instBufDesc);
    wgpuQueueWriteBuffer(queue, instanceBuffer, 0, instances.data(), instances.size() * sizeof(GlyphInstance));

    // Main loop
    int frameCount = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        WGPUSurfaceTexture surfaceTex;
        wgpuSurfaceGetCurrentTexture(surface, &surfaceTex);
        if (surfaceTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            surfaceTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            std::cerr << "Surface texture status: " << (int)surfaceTex.status << std::endl;
            continue;
        }
        if (frameCount++ == 0) {
            std::cout << "First frame rendering..." << std::endl;
        }

        WGPUTextureView view = wgpuTextureCreateView(surfaceTex.texture, nullptr);

        WGPUCommandEncoderDescriptor encDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);

        WGPURenderPassColorAttachment colorAtt = {};
        colorAtt.view = view;
        colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAtt.loadOp = WGPULoadOp_Clear;
        colorAtt.storeOp = WGPUStoreOp_Store;
        colorAtt.clearValue = {0.95, 0.95, 0.95, 1.0};

        WGPURenderPassDescriptor rpDescPass = {};
        rpDescPass.colorAttachmentCount = 1;
        rpDescPass.colorAttachments = &colorAtt;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rpDescPass);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, instanceBuffer, 0, instances.size() * sizeof(GlyphInstance));
        wgpuRenderPassEncoderDraw(pass, 6, instances.size(), 0, 0);
        wgpuRenderPassEncoderEnd(pass);

        WGPUCommandBufferDescriptor cbDesc = {};
        WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &cbDesc);
        wgpuQueueSubmit(queue, 1, &cmdBuf);

        wgpuSurfacePresent(surface);
        wgpuTextureViewRelease(view);
        wgpuCommandBufferRelease(cmdBuf);
        wgpuCommandEncoderRelease(encoder);
        wgpuRenderPassEncoderRelease(pass);
    }

    // Cleanup
    wgpuBufferRelease(instanceBuffer);
    wgpuBufferRelease(uniformBuffer);
    wgpuBindGroupRelease(bindGroup);
    wgpuBindGroupLayoutRelease(bindGroupLayout);
    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuRenderPipelineRelease(pipeline);
    wgpuShaderModuleRelease(shaderModule);
    // sampler is owned by Font class
    wgpuSurfaceUnconfigure(surface);
    wgpuSurfaceRelease(surface);
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
