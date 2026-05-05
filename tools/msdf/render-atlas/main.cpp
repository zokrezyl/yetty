/**
 * MSDF-WGSL Atlas Viewer - Grid-based glyph viewer with scrolling
 */

#include <msdf-wgsl.h>
#include <yetty/wgpu-compat.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>

#include <iostream>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cmath>

int windowWidth = 1280;
int windowHeight = 720;

WGPUInstance instance = nullptr;
WGPUSurface surface = nullptr;
WGPUAdapter adapter = nullptr;
WGPUDevice device = nullptr;
WGPUQueue queue = nullptr;
WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8Unorm;

std::unique_ptr<msdf::Context> msdfContext;
std::unique_ptr<msdf::Font> font;

WGPURenderPipeline pipeline = nullptr;
WGPUBindGroup bindGroup = nullptr;
WGPUSampler sampler = nullptr;
WGPUBuffer uniformBuffer = nullptr;
WGPUBuffer glyphBuffer = nullptr;

// Pan and zoom state (world coordinates are in cell units)
float panX = 0.0f;
float panY = 0.0f;
float zoom = 1.0f;
float scrollY = 0.0f;
bool dragging = false;
double lastMouseX = 0, lastMouseY = 0;

// Grid parameters
int gridCols = 80;
int totalGlyphCount = 0;

bool adapterReady = false;
bool deviceReady = false;

// Shader: grid-based glyph viewer with storage buffer for glyph atlas rects
const char* shaderCode = R"(
struct Uniforms {
    panX: f32,
    panY: f32,
    zoom: f32,
    aspect: f32,
    atlasW: f32,
    atlasH: f32,
    scrollY: f32,
    cols: f32,
    totalGlyphs: f32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
};

struct GlyphRect {
    x: f32,
    y: f32,
    w: f32,
    h: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var atlasSampler: sampler;
@group(0) @binding(2) var atlasTexture: texture_2d<f32>;
@group(0) @binding(3) var<storage, read> glyphs: array<GlyphRect>;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> VertexOutput {
    // Fullscreen triangle
    var pos = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>(3.0, -1.0),
        vec2<f32>(-1.0, 3.0)
    );
    var uv = array<vec2<f32>, 3>(
        vec2<f32>(0.0, 1.0),
        vec2<f32>(2.0, 1.0),
        vec2<f32>(0.0, -1.0)
    );

    var out: VertexOutput;
    out.position = vec4<f32>(pos[idx], 0.0, 1.0);
    out.uv = uv[idx];
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let bg_color = vec4<f32>(0.15, 0.15, 0.15, 1.0);
    let grid_color = vec4<f32>(0.25, 0.25, 0.25, 1.0);
    let empty_color = vec4<f32>(0.1, 0.1, 0.12, 1.0);

    // Screen coordinates: uv (0,0)=top-left, (1,1)=bottom-right
    let screen = vec2<f32>(
        (in.uv.x - 0.5) * 2.0 * uniforms.aspect,
        (in.uv.y - 0.5) * 2.0
    );

    // World coordinates in cell units
    let world = vec2<f32>(
        screen.x / uniforms.zoom + uniforms.panX,
        screen.y / uniforms.zoom + uniforms.panY + uniforms.scrollY
    );

    // Grid cell
    let col = floor(world.x);
    let row = floor(world.y);

    // Glyph index (clamped for safe buffer access)
    let rawIdx = u32(max(row, 0.0)) * u32(uniforms.cols) + u32(max(col, 0.0));
    let safeIdx = min(rawIdx, u32(max(uniforms.totalGlyphs - 1.0, 0.0)));

    // Local UV within cell (0 to 1)
    let localUV = fract(world);

    // Get glyph rect from storage buffer
    let glyph = glyphs[safeIdx];

    // Compute atlas UV (safe even if glyph is empty, will just sample somewhere)
    let margin = 0.02;
    let innerUV = (localUV - margin) / (1.0 - 2.0 * margin);
    let glyphW = max(glyph.w, 1.0);
    let glyphH = max(glyph.h, 1.0);
    let atlasUV = vec2<f32>(
        (glyph.x + clamp(innerUV.x, 0.0, 1.0) * glyphW) / uniforms.atlasW,
        (glyph.y + clamp(innerUV.y, 0.0, 1.0) * glyphH) / uniforms.atlasH
    );

    // Sample MSDF (must be in uniform control flow - no early returns before this)
    let msdf = textureSample(atlasTexture, atlasSampler, atlasUV);

    // Median of RGB (standard MSDF decoding)
    let median = max(min(msdf.r, msdf.g), min(max(msdf.r, msdf.g), msdf.b));
    let glyph_color = vec4<f32>(median, median, median, 1.0);

    // Grid lines at cell edges
    let edgeX = min(localUV.x, 1.0 - localUV.x);
    let edgeY = min(localUV.y, 1.0 - localUV.y);
    let edge = min(edgeX, edgeY);
    let is_grid_line = edge < margin;

    // Determine what to show
    let out_of_bounds = col < 0.0 || col >= uniforms.cols || row < 0.0 || rawIdx >= u32(uniforms.totalGlyphs);
    let is_empty = glyph.w <= 0.0 || glyph.h <= 0.0;

    // Compose final color using select (no branching)
    var color = glyph_color;
    color = select(color, empty_color, is_empty);
    color = select(color, grid_color, is_grid_line && !out_of_bounds);
    color = select(color, bg_color, out_of_bounds);

    return color;
}
)";

void onAdapterReady(WGPURequestAdapterStatus status, WGPUAdapter result,
                    WGPUStringView message, void* userdata1, void* userdata2) {
    if (status == WGPURequestAdapterStatus_Success) {
        adapter = result;
    }
    adapterReady = true;
}

void onDeviceReady(WGPURequestDeviceStatus status, WGPUDevice result,
                   WGPUStringView message, void* userdata1, void* userdata2) {
    if (status == WGPURequestDeviceStatus_Success) {
        device = result;
    }
    deviceReady = true;
}

void onUncapturedError(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*) {
    const char* typeStr = "Unknown";
    switch(type) {
        case WGPUErrorType_Validation: typeStr = "Validation"; break;
        case WGPUErrorType_OutOfMemory: typeStr = "OutOfMemory"; break;
        case WGPUErrorType_Internal: typeStr = "Internal"; break;
        default: break;
    }
    std::cerr << "[WebGPU ERROR] " << typeStr << ": ";
    if (message.data && message.length > 0) {
        std::cerr << std::string(message.data, message.length);
    }
    std::cerr << std::endl;
}

bool initWebGPU(GLFWwindow* window) {
    std::cout << "[WebGPU] Initializing..." << std::endl;

    WGPUInstanceDescriptor instanceDesc = {};
    instance = wgpuCreateInstance(&instanceDesc);

    surface = glfwCreateWindowWGPUSurface(instance, window);

    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = surface;

    WGPURequestAdapterCallbackInfo adapterCallback = {};
    adapterCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    adapterCallback.callback = onAdapterReady;
    wgpuInstanceRequestAdapter(instance, &adapterOpts, adapterCallback);
    while (!adapterReady) { glfwPollEvents(); }
    if (!adapter) return false;

    // Set up error callback in device descriptor
    WGPUUncapturedErrorCallbackInfo errorInfo = {};
    errorInfo.callback = onUncapturedError;

    // Request higher buffer size limit for large atlases
    WGPULimits requiredLimits = WGPU_LIMITS_INIT;
    requiredLimits.maxBufferSize = 2147483648ULL;  // 2 GB
    requiredLimits.maxStorageBufferBindingSize = 2147483648ULL;

    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.requiredLimits = &requiredLimits;
    deviceDesc.uncapturedErrorCallbackInfo = errorInfo;

    WGPURequestDeviceCallbackInfo deviceCallback = {};
    deviceCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    deviceCallback.callback = onDeviceReady;
    wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCallback);
    while (!deviceReady) { glfwPollEvents(); }
    if (!device) return false;

    queue = wgpuDeviceGetQueue(device);

    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(surface, adapter, &caps);
    surfaceFormat = caps.formats[0];

    WGPUSurfaceConfiguration surfaceConfig = {};
    surfaceConfig.device = device;
    surfaceConfig.format = surfaceFormat;
    surfaceConfig.usage = WGPUTextureUsage_RenderAttachment;
    surfaceConfig.width = windowWidth;
    surfaceConfig.height = windowHeight;
    surfaceConfig.presentMode = WGPUPresentMode_Fifo;
    surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Auto;
    wgpuSurfaceConfigure(surface, &surfaceConfig);

    std::cout << "[WebGPU] Initialized" << std::endl;
    return true;
}

bool initRendering() {
    auto* atlas = font->getAtlas();
    if (!atlas) return false;

    const auto& glyphs = font->getGlyphs();
    totalGlyphCount = static_cast<int>(glyphs.size());

    std::cout << "[Render] Atlas size: " << atlas->getWidth() << "x"
              << atlas->getHeight() << ", glyphs: " << totalGlyphCount << std::endl;

    // Build glyph rect storage buffer (4 floats per glyph: x, y, w, h in atlas pixels)
    size_t glyphBufSize = std::max(totalGlyphCount, 1) * 4 * sizeof(float);
    std::vector<float> glyphRects(std::max(totalGlyphCount, 1) * 4, 0.0f);
    for (int i = 0; i < totalGlyphCount; i++) {
        glyphRects[i * 4 + 0] = static_cast<float>(glyphs[i].atlasX);
        glyphRects[i * 4 + 1] = static_cast<float>(glyphs[i].atlasY);
        glyphRects[i * 4 + 2] = static_cast<float>(glyphs[i].atlasW);
        glyphRects[i * 4 + 3] = static_cast<float>(glyphs[i].atlasH);
    }

    WGPUBufferDescriptor glyphBufDesc = {};
    glyphBufDesc.size = glyphBufSize;
    glyphBufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    glyphBuffer = wgpuDeviceCreateBuffer(device, &glyphBufDesc);
    wgpuQueueWriteBuffer(queue, glyphBuffer, 0, glyphRects.data(), glyphBufSize);

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPU_STR(shaderCode);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Create sampler - using nearest filter since RGBA32Float is unfilterable
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.magFilter = WGPUFilterMode_Nearest;
    samplerDesc.minFilter = WGPUFilterMode_Nearest;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    sampler = wgpuDeviceCreateSampler(device, &samplerDesc);

    // Create uniform buffer (48 bytes = 12 floats)
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = 48;
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);

    // Create bind group layout (4 entries: uniform, sampler, texture, storage)
    WGPUBindGroupLayoutEntry layoutEntries[4] = {};

    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Fragment;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].sampler.type = WGPUSamplerBindingType_NonFiltering;

    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = WGPUShaderStage_Fragment;
    layoutEntries[2].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    layoutEntries[3].binding = 3;
    layoutEntries[3].visibility = WGPUShaderStage_Fragment;
    layoutEntries[3].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 4;
    layoutDesc.entries = layoutEntries;
    WGPUBindGroupLayout bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Create bind group
    WGPUBindGroupEntry bindEntries[4] = {};

    bindEntries[0].binding = 0;
    bindEntries[0].buffer = uniformBuffer;
    bindEntries[0].size = 48;

    bindEntries[1].binding = 1;
    bindEntries[1].sampler = sampler;

    bindEntries[2].binding = 2;
    bindEntries[2].textureView = atlas->getTextureView();

    bindEntries[3].binding = 3;
    bindEntries[3].buffer = glyphBuffer;
    bindEntries[3].size = glyphBufSize;

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = bindGroupLayout;
    bindGroupDesc.entryCount = 4;
    bindGroupDesc.entries = bindEntries;
    bindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Create render pipeline
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = surfaceFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = WGPU_STR("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = WGPU_STR("vs_main");
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuBindGroupLayoutRelease(bindGroupLayout);
    wgpuPipelineLayoutRelease(pipelineLayout);

    std::cout << "[Render] Pipeline created" << std::endl;
    return pipeline != nullptr;
}

void updateUniforms() {
    auto* atlas = font->getAtlas();
    float windowAspect = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);

    // 12 floats = 48 bytes, all f32 scalars (no alignment issues)
    struct {
        float panX, panY;
        float zoom;
        float aspect;
        float atlasW, atlasH;
        float scrollY;
        float cols;
        float totalGlyphs;
        float _pad1, _pad2, _pad3;
    } uniforms = {
        panX, panY, zoom, windowAspect,
        static_cast<float>(atlas->getWidth()), static_cast<float>(atlas->getHeight()),
        scrollY,
        static_cast<float>(gridCols),
        static_cast<float>(totalGlyphCount),
        0, 0, 0
    };

    wgpuQueueWriteBuffer(queue, uniformBuffer, 0, &uniforms, 48);
}

void render() {
    updateUniforms();

    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) return;

    // Pass nullptr to use default view descriptor
    WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, nullptr);

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.1, 0.1, 0.1, 1.0};
#if !defined(__EMSCRIPTEN__)
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBufferDescriptor cmdBufDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufDesc);
    wgpuQueueSubmit(queue, 1, &cmdBuffer);

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuRenderPassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(targetView);

    wgpuSurfacePresent(surface);
}

void cleanup() {
    if (glyphBuffer) wgpuBufferRelease(glyphBuffer);
    if (uniformBuffer) wgpuBufferRelease(uniformBuffer);
    if (bindGroup) wgpuBindGroupRelease(bindGroup);
    if (pipeline) wgpuRenderPipelineRelease(pipeline);
    if (sampler) wgpuSamplerRelease(sampler);
    msdfContext.reset();
    font.reset();
}

void resetView() {
    float windowAspect = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    // Fit all gridCols columns in view
    zoom = 2.0f * windowAspect / static_cast<float>(gridCols);
    // Center horizontally on the grid
    panX = static_cast<float>(gridCols) / 2.0f;
    // Position so row 0 is near top of screen
    panY = 1.0f / zoom;
    scrollY = 0.0f;
}

void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    if (width == 0 || height == 0) return;  // Minimized

    windowWidth = width;
    windowHeight = height;

    // Reconfigure surface
    WGPUSurfaceConfiguration surfaceConfig = {};
    surfaceConfig.device = device;
    surfaceConfig.format = surfaceFormat;
    surfaceConfig.usage = WGPUTextureUsage_RenderAttachment;
    surfaceConfig.width = windowWidth;
    surfaceConfig.height = windowHeight;
    surfaceConfig.presentMode = WGPUPresentMode_Fifo;
    surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Auto;
    wgpuSurfaceConfigure(surface, &surfaceConfig);
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    bool ctrlPressed = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                       glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

    if (ctrlPressed) {
        // Zoom to cursor
        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        float windowAspect = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
        float screenX = (static_cast<float>(mouseX) / windowWidth - 0.5f) * 2.0f * windowAspect;
        float screenY = (static_cast<float>(mouseY) / windowHeight - 0.5f) * 2.0f;

        // World point under cursor
        float worldX = screenX / zoom + panX;
        float worldY = screenY / zoom + panY;

        float zoomFactor = (yoffset > 0) ? 0.8f : 1.25f;
        float newZoom = zoom * zoomFactor;
        newZoom = std::max(0.005f, std::min(newZoom, 50.0f));

        // Adjust pan to keep world point under cursor
        panX = worldX - screenX / newZoom;
        panY = worldY - screenY / newZoom;
        zoom = newZoom;
    } else {
        // Vertical scroll (3 rows per tick)
        scrollY -= static_cast<float>(yoffset) * 3.0f;
        scrollY = std::max(0.0f, scrollY);
        // Cap at max rows
        int totalRows = (totalGlyphCount + gridCols - 1) / gridCols;
        scrollY = std::min(scrollY, static_cast<float>(std::max(totalRows - 1, 0)));
    }
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        dragging = (action == GLFW_PRESS);
        glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
    }
}

void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    if (dragging) {
        float windowAspect = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
        float dx = static_cast<float>(xpos - lastMouseX) / windowWidth * 2.0f * windowAspect / zoom;
        float dy = static_cast<float>(ypos - lastMouseY) / windowHeight * 2.0f / zoom;
        panX -= dx;
        panY -= dy;
        lastMouseX = xpos;
        lastMouseY = ypos;
    }
}

void printUsage(const char* prog) {
    std::cout << "MSDF-WGSL Atlas Viewer (Grid Mode)\n\n"
              << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  -f, --font FILE    Font file to load (default: system DejaVuSansMono)\n"
              << "  -n, --num N        Number of glyphs to generate (default: all in font)\n"
              << "  --nerd             Include Nerd Font symbols\n"
              << "  -c, --cols N       Grid columns (default: 80)\n"
              << "  -h, --help         Show this help\n\n"
              << "Controls:\n"
              << "  Mouse wheel        Scroll through glyphs\n"
              << "  Ctrl+wheel         Zoom to cursor\n"
              << "  Mouse drag         Pan the view\n"
              << "  R                  Reset view\n"
              << "  ESC                Exit\n";
}

int main(int argc, char* argv[]) {
    std::string fontPath = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
    int numGlyphs = -1;  // -1 = all glyphs in font
    bool nerdFonts = false;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-f" || arg == "--font") && i + 1 < argc) {
            fontPath = argv[++i];
        } else if ((arg == "-n" || arg == "--num") && i + 1 < argc) {
            numGlyphs = std::atoi(argv[++i]);
        } else if ((arg == "-c" || arg == "--cols") && i + 1 < argc) {
            gridCols = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--nerd") {
            nerdFonts = true;
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }

    std::cout << "========================================\n"
              << "MSDF-WGSL Atlas Viewer (Grid Mode)\n"
              << "========================================\n"
              << "Font: " << fontPath << "\n"
              << "Glyphs: " << (numGlyphs < 0 ? "all" : std::to_string(numGlyphs)) << "\n"
              << "Grid columns: " << gridCols << "\n"
              << "========================================\n" << std::endl;

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, "MSDF-WGSL Atlas Viewer", nullptr, nullptr);

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);

    if (!initWebGPU(window)) {
        std::cerr << "Failed to initialize WebGPU" << std::endl;
        glfwTerminate();
        return 1;
    }

    // Set shader path relative to executable
    msdf::setShaderPath("shaders/msdf_gen.wgsl");

    // Create MSDF context
    try {
        msdfContext = std::make_unique<msdf::Context>(device, instance);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create MSDF context: " << e.what() << std::endl;
        glfwTerminate();
        return 1;
    }

    // Load font
    msdf::FontConfig config;
    config.scale = 2.0f;
    config.range = 4.0f;
    font = msdfContext->loadFont(fontPath, config);
    if (!font) {
        std::cerr << "Failed to load font: " << fontPath << std::endl;
        glfwTerminate();
        return 1;
    }

    std::cout << "[Font] Loaded, generating glyphs..." << std::endl;

    // Generate glyphs
    std::vector<uint32_t> charset;
    if (nerdFonts) {
        charset = msdf::getDefaultCharset(true);
        if (numGlyphs > 0 && static_cast<size_t>(numGlyphs) < charset.size()) {
            charset.resize(numGlyphs);
        }
    } else if (numGlyphs < 0) {
        // All glyphs in the font
        charset = font->getAllCodepoints();
    } else {
        // Printable ASCII (limited)
        for (uint32_t c = 0x20; c <= 0x7E && charset.size() < static_cast<size_t>(numGlyphs); ++c) {
            charset.push_back(c);
        }
    }

    int result = msdfContext->generateGlyphs(*font, charset);
    std::cout << "[Font] Generated " << result << " glyphs" << std::endl;

    // Initialize rendering (also sets up glyph storage buffer)
    if (!initRendering()) {
        std::cerr << "Failed to initialize rendering" << std::endl;
        cleanup();
        glfwTerminate();
        return 1;
    }

    // Set initial view to fit grid
    resetView();

    int totalRows = (totalGlyphCount + gridCols - 1) / gridCols;
    std::cout << "[Grid] " << totalGlyphCount << " glyphs, "
              << gridCols << " cols, " << totalRows << " rows" << std::endl;

    std::cout << "[Main] Starting render loop...\n"
              << "  Scroll to navigate, Ctrl+scroll to zoom, drag to pan, R to reset, ESC to exit\n" << std::endl;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
            resetView();
        }

        render();
    }

    cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
