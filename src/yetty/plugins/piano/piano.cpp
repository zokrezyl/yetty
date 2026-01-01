#include "piano.h"
#include <yetty/webgpu-context.h>
#include <yetty/wgpu-compat.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace yetty {

//-----------------------------------------------------------------------------
// PianoPlugin
//-----------------------------------------------------------------------------

PianoPlugin::PianoPlugin() = default;

PianoPlugin::~PianoPlugin() {
    (void)dispose();
}

Result<PluginPtr> PianoPlugin::create() {
    return Ok<PluginPtr>(std::make_shared<PianoPlugin>());
}

Result<void> PianoPlugin::init(WebGPUContext* ctx) {
    (void)ctx;
    _initialized = true;
    return Ok();
}

Result<void> PianoPlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose PianoPlugin", res);
    }
    _initialized = false;
    return Ok();
}

Result<PluginLayerPtr> PianoPlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<PianoLayer>();
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to initialize Piano layer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

Result<void> PianoPlugin::renderAll(WebGPUContext& ctx,
                                     WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                     uint32_t screenWidth, uint32_t screenHeight,
                                     float cellWidth, float cellHeight,
                                     int scrollOffset, uint32_t termRows,
                                     bool isAltScreen) {
    ScreenType currentScreen = isAltScreen ? ScreenType::Alternate : ScreenType::Main;
    for (auto& layerBase : _layers) {
        if (!layerBase->isVisible()) continue;
        if (layerBase->getScreenType() != currentScreen) continue;

        auto layer = std::static_pointer_cast<PianoLayer>(layerBase);

        float pixelX = layer->getX() * cellWidth;
        float pixelY = layer->getY() * cellHeight;
        float pixelW = layer->getWidthCells() * cellWidth;
        float pixelH = layer->getHeightCells() * cellHeight;

        if (layer->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
            pixelY += scrollOffset * cellHeight;
        }

        if (termRows > 0) {
            float screenPixelHeight = termRows * cellHeight;
            if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
                continue;
            }
        }

        if (auto res = layer->render(ctx, targetView, targetFormat,
                                      screenWidth, screenHeight,
                                      pixelX, pixelY, pixelW, pixelH); !res) {
            return Err<void>("Failed to render Piano layer", res);
        }
    }
    return Ok();
}

//-----------------------------------------------------------------------------
// PianoLayer
//-----------------------------------------------------------------------------

PianoLayer::PianoLayer() {
    _key_states.reset();
}

PianoLayer::~PianoLayer() {
    (void)dispose();
}

Result<void> PianoLayer::init(const std::string& payload) {
    _payload = payload;

    // Parse payload: "octaves[,startOctave]"
    if (!payload.empty()) {
        int octaves = 2, start = 4;
        if (sscanf(payload.c_str(), "%d,%d", &octaves, &start) >= 1) {
            _num_octaves = std::clamp(octaves, 1, MAX_OCTAVES);
            if (sscanf(payload.c_str(), "%d,%d", &octaves, &start) >= 2) {
                _start_octave = std::clamp(start, 0, 9);
            }
        }
    }

    spdlog::info("PianoLayer: initialized ({} octaves starting at C{})",
                 _num_octaves, _start_octave);
    return Ok();
}

Result<void> PianoLayer::dispose() {
    if (_bind_group) { wgpuBindGroupRelease(_bind_group); _bind_group = nullptr; }
    if (_pipeline) { wgpuRenderPipelineRelease(_pipeline); _pipeline = nullptr; }
    if (_uniform_buffer) { wgpuBufferRelease(_uniform_buffer); _uniform_buffer = nullptr; }
    if (_key_state_buffer) { wgpuBufferRelease(_key_state_buffer); _key_state_buffer = nullptr; }
    _gpu_initialized = false;
    return Ok();
}

Result<void> PianoLayer::update(double deltaTime) {
    _time += static_cast<float>(deltaTime);
    return Ok();
}

void PianoLayer::setKeyPressed(int key, bool pressed) {
    if (key >= 0 && key < 128) {
        _key_states[key] = pressed;
    }
}

bool PianoLayer::isKeyPressed(int key) const {
    return (key >= 0 && key < 128) && _key_states[key];
}

void PianoLayer::clearAllKeys() {
    _key_states.reset();
}

int PianoLayer::getKeyAtPosition(float x, float y) const {
    // Normalize to [0,1]
    float normX = x / static_cast<float>(_pixel_width);
    float normY = y / static_cast<float>(_pixel_height);

    int totalWhiteKeys = _num_octaves * WHITE_KEYS_PER_OCTAVE;
    float whiteKeyWidth = 1.0f / totalWhiteKeys;

    // Black key dimensions (relative)
    float blackKeyWidth = whiteKeyWidth * 0.6f;
    float blackKeyHeight = 0.6f;

    // Check black keys first (they're on top)
    if (normY < blackKeyHeight) {
        // Black key pattern per octave: positions after white keys 0,1,3,4,5 (C#,D#,F#,G#,A#)
        // White key indices: C=0, D=1, E=2, F=3, G=4, A=5, B=6
        // Black keys are between: C-D, D-E, F-G, G-A, A-B
        static const int blackKeyOffsets[] = {1, 2, 4, 5, 6};  // After which white key

        for (int oct = 0; oct < _num_octaves; ++oct) {
            for (int i = 0; i < BLACK_KEYS_PER_OCTAVE; ++i) {
                int whiteKeyIndex = oct * WHITE_KEYS_PER_OCTAVE + blackKeyOffsets[i];
                float blackKeyCenter = whiteKeyIndex * whiteKeyWidth;
                float blackKeyLeft = blackKeyCenter - blackKeyWidth / 2;
                float blackKeyRight = blackKeyCenter + blackKeyWidth / 2;

                if (normX >= blackKeyLeft && normX < blackKeyRight) {
                    // Convert to MIDI note
                    // Black keys in octave: C#=1, D#=3, F#=6, G#=8, A#=10
                    static const int blackKeyNotes[] = {1, 3, 6, 8, 10};
                    int midiNote = (_start_octave + oct) * 12 + blackKeyNotes[i];
                    return midiNote;
                }
            }
        }
    }

    // Check white keys
    int whiteKeyIndex = static_cast<int>(normX / whiteKeyWidth);
    if (whiteKeyIndex >= 0 && whiteKeyIndex < totalWhiteKeys) {
        int octave = whiteKeyIndex / WHITE_KEYS_PER_OCTAVE;
        int keyInOctave = whiteKeyIndex % WHITE_KEYS_PER_OCTAVE;
        // White keys in octave: C=0, D=2, E=4, F=5, G=7, A=9, B=11
        static const int whiteKeyNotes[] = {0, 2, 4, 5, 7, 9, 11};
        int midiNote = (_start_octave + octave) * 12 + whiteKeyNotes[keyInOctave];
        return midiNote;
    }

    return -1;
}

bool PianoLayer::onMouseMove(float localX, float localY) {
    _mouse_x = localX;
    _mouse_y = localY;
    _hover_key = getKeyAtPosition(localX, localY);

    // If dragging, update pressed key
    if (_pressed_key >= 0 && _hover_key != _pressed_key) {
        setKeyPressed(_pressed_key, false);
        _pressed_key = _hover_key;
        if (_pressed_key >= 0) {
            setKeyPressed(_pressed_key, true);
        }
    }

    return true;
}

bool PianoLayer::onMouseButton(int button, bool pressed) {
    if (button == 0) {
        if (pressed && _hover_key >= 0) {
            _pressed_key = _hover_key;
            setKeyPressed(_pressed_key, true);
            spdlog::debug("Piano: key {} pressed (MIDI {})", _hover_key, _pressed_key);
        } else {
            if (_pressed_key >= 0) {
                setKeyPressed(_pressed_key, false);
                spdlog::debug("Piano: key {} released", _pressed_key);
            }
            _pressed_key = -1;
        }
        return true;
    }
    if (button == -1) {
        // Focus lost
        if (_pressed_key >= 0) {
            setKeyPressed(_pressed_key, false);
            _pressed_key = -1;
        }
    }
    return false;
}

bool PianoLayer::onKey(int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;

    // Map keyboard to piano keys (QWERTY layout)
    // Bottom row: A-L = C4 to B4 (white keys)
    // Top row: W,E,T,Y,U = C#4, D#4, F#4, G#4, A#4 (black keys)
    static const std::pair<int, int> keyMap[] = {
        // White keys (bottom row)
        {65, 0},   // A -> C
        {83, 2},   // S -> D
        {68, 4},   // D -> E
        {70, 5},   // F -> F
        {71, 7},   // G -> G
        {72, 9},   // H -> A
        {74, 11},  // J -> B
        {75, 12},  // K -> C (next octave)
        {76, 14},  // L -> D (next octave)
        // Black keys (top row)
        {87, 1},   // W -> C#
        {69, 3},   // E -> D#
        {84, 6},   // T -> F#
        {89, 8},   // Y -> G#
        {85, 10},  // U -> A#
        {79, 13},  // O -> C# (next octave)
        {80, 15},  // P -> D# (next octave)
    };

    for (const auto& [keyCode, noteOffset] : keyMap) {
        if (key == keyCode) {
            int midiNote = _start_octave * 12 + noteOffset;
            if (action == 1) {  // Press
                setKeyPressed(midiNote, true);
                spdlog::debug("Piano: keyboard key {} -> MIDI {}", key, midiNote);
            } else if (action == 0) {  // Release
                setKeyPressed(midiNote, false);
            }
            break;
        }
    }

    // When focused, consume ALL key input - never let it leak to terminal
    return true;
}

bool PianoLayer::onChar(unsigned int codepoint) {
    (void)codepoint;
    // When focused, consume ALL character input - never let it leak to terminal
    return true;
}

Result<void> PianoLayer::render(WebGPUContext& ctx,
                                 WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                 uint32_t screenWidth, uint32_t screenHeight,
                                 float pixelX, float pixelY, float pixelW, float pixelH) {
    if (_failed) return Err<void>("PianoLayer already failed");

    if (!_gpu_initialized) {
        auto result = createPipeline(ctx, targetFormat);
        if (!result) {
            _failed = true;
            return Err<void>("Failed to create pipeline", result);
        }
        _gpu_initialized = true;
    }

    if (!_pipeline || !_uniform_buffer || !_bind_group) {
        _failed = true;
        return Err<void>("PianoLayer pipeline not initialized");
    }

    // Update uniforms
    float ndcX = (pixelX / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / screenHeight) * 2.0f;
    float ndcW = (pixelW / screenWidth) * 2.0f;
    float ndcH = (pixelH / screenHeight) * 2.0f;

    struct Uniforms {
        float rect[4];        // 16 bytes, offset 0
        float resolution[2];  // 8 bytes, offset 16
        float time;           // 4 bytes, offset 24
        float numOctaves;     // 4 bytes, offset 28
        float hoverKey;       // 4 bytes, offset 32
        float _pad1;          // 4 bytes, offset 36
        float _pad2;          // 4 bytes, offset 40
        float _pad3;          // 4 bytes, offset 44 (total 48)
    } uniforms;

    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;
    uniforms.resolution[0] = pixelW;
    uniforms.resolution[1] = pixelH;
    uniforms.time = _time;
    uniforms.numOctaves = static_cast<float>(_num_octaves);
    uniforms.hoverKey = static_cast<float>(_hover_key);

    wgpuQueueWriteBuffer(ctx.getQueue(), _uniform_buffer, 0, &uniforms, sizeof(uniforms));

    // Update key states buffer (128 bits = 4 x uint32)
    uint32_t keyStates[4] = {0, 0, 0, 0};
    for (int i = 0; i < 128; ++i) {
        if (_key_states[i]) {
            keyStates[i / 32] |= (1u << (i % 32));
        }
    }
    wgpuQueueWriteBuffer(ctx.getQueue(), _key_state_buffer, 0, keyStates, sizeof(keyStates));

    // Render
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) return Err<void>("Failed to create command encoder");

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return Err<void>("Failed to begin render pass");
    }

    wgpuRenderPassEncoderSetPipeline(pass, _pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _bind_group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    if (cmdBuffer) {
        wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
    }
    wgpuCommandEncoderRelease(encoder);
    return Ok();
}

Result<void> PianoLayer::createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat) {
    WGPUDevice device = ctx.getDevice();

    // Uniform buffer (48 bytes)
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 48;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _uniform_buffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!_uniform_buffer) return Err<void>("Failed to create uniform buffer");

    // Key state buffer (128 bits = 16 bytes, round up to 16)
    WGPUBufferDescriptor keyBufDesc = {};
    keyBufDesc.size = 16;
    keyBufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    _key_state_buffer = wgpuDeviceCreateBuffer(device, &keyBufDesc);
    if (!_key_state_buffer) return Err<void>("Failed to create key state buffer");

    // Shader
    const char* shaderCode = R"(
struct Uniforms {
    rect: vec4<f32>,           // 16 bytes, offset 0
    resolution: vec2<f32>,     // 8 bytes, offset 16
    time: f32,                 // 4 bytes, offset 24
    numOctaves: f32,           // 4 bytes, offset 28
    hoverKey: f32,             // 4 bytes, offset 32
    _pad1: f32,                // 4 bytes, offset 36
    _pad2: f32,                // 4 bytes, offset 40
    _pad3: f32,                // 4 bytes, offset 44 (total 48)
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var<storage, read> keyStates: array<u32, 4>;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vi: u32) -> VertexOutput {
    var p = array<vec2<f32>, 6>(
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    let pos = p[vi];
    var o: VertexOutput;
    o.position = vec4(u.rect.x + pos.x * u.rect.z, u.rect.y - pos.y * u.rect.w, 0.0, 1.0);
    o.uv = pos;
    return o;
}

fn isKeyPressed(midiNote: i32) -> bool {
    if (midiNote < 0 || midiNote >= 128) { return false; }
    let idx = u32(midiNote) / 32u;
    let bit = u32(midiNote) % 32u;
    return (keyStates[idx] & (1u << bit)) != 0u;
}

// Check if a semitone is a black key
fn isBlackKey(semitone: i32) -> bool {
    let s = semitone % 12;
    return s == 1 || s == 3 || s == 6 || s == 8 || s == 10;
}

// Get white key index for a semitone (0-6 within octave)
fn whiteKeyIndex(semitone: i32) -> i32 {
    let s = semitone % 12;
    // C=0, D=1, E=2, F=3, G=4, A=5, B=6
    if (s <= 0) { return 0; }
    if (s <= 2) { return 1; }
    if (s <= 4) { return 2; }
    if (s <= 5) { return 3; }
    if (s <= 7) { return 4; }
    if (s <= 9) { return 5; }
    return 6;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let uv = in.uv;
    let fragCoord = uv * u.resolution;

    let numWhiteKeys = i32(u.numOctaves) * 7;
    let whiteKeyWidth = 1.0 / f32(numWhiteKeys);

    // Black key dimensions (real piano: ~13.7mm / ~23.5mm ≈ 0.58)
    let blackKeyWidthRatio = 0.58;
    let blackKeyHeight = 0.62;

    // Determine which white key we're over
    let whiteKeyIdx = i32(floor(uv.x / whiteKeyWidth));

    // Gap between keys (real piano: ~1.25mm / ~23.5mm ≈ 0.05)
    let gapSize = 0.025;  // Half the gap on each side

    // Check if we're in a gap between white keys
    let posInWhiteKey = fract(uv.x / whiteKeyWidth);
    let inGap = posInWhiteKey < gapSize || posInWhiteKey > (1.0 - gapSize);

    // Default: background
    var color = vec3<f32>(0.15, 0.15, 0.18);

    // Check black keys first (they're on top)
    var onBlackKey = false;
    var blackKeyMidi = -1;

    if (uv.y < blackKeyHeight) {
        // Black key positions: after white keys at indices 0,1,3,4,5 (C#,D#,F#,G#,A#)
        for (var oct = 0; oct < i32(u.numOctaves); oct = oct + 1) {
            let baseWhite = oct * 7;

            // C# (between C and D)
            var bkCenter = (f32(baseWhite) + 1.0) * whiteKeyWidth;
            var bkHalfW = whiteKeyWidth * blackKeyWidthRatio * 0.5;
            if (uv.x >= bkCenter - bkHalfW && uv.x < bkCenter + bkHalfW) {
                onBlackKey = true;
                blackKeyMidi = oct * 12 + 1;  // C#
            }

            // D# (between D and E)
            bkCenter = (f32(baseWhite) + 2.0) * whiteKeyWidth;
            if (uv.x >= bkCenter - bkHalfW && uv.x < bkCenter + bkHalfW) {
                onBlackKey = true;
                blackKeyMidi = oct * 12 + 3;  // D#
            }

            // F# (between F and G)
            bkCenter = (f32(baseWhite) + 4.0) * whiteKeyWidth;
            if (uv.x >= bkCenter - bkHalfW && uv.x < bkCenter + bkHalfW) {
                onBlackKey = true;
                blackKeyMidi = oct * 12 + 6;  // F#
            }

            // G# (between G and A)
            bkCenter = (f32(baseWhite) + 5.0) * whiteKeyWidth;
            if (uv.x >= bkCenter - bkHalfW && uv.x < bkCenter + bkHalfW) {
                onBlackKey = true;
                blackKeyMidi = oct * 12 + 8;  // G#
            }

            // A# (between A and B)
            bkCenter = (f32(baseWhite) + 6.0) * whiteKeyWidth;
            if (uv.x >= bkCenter - bkHalfW && uv.x < bkCenter + bkHalfW) {
                onBlackKey = true;
                blackKeyMidi = oct * 12 + 10;  // A#
            }
        }
    }

    if (onBlackKey) {
        // Black key
        let pressed = isKeyPressed(blackKeyMidi + 48);  // Offset for middle octaves
        let hover = (f32(blackKeyMidi + 48) == u.hoverKey);

        if (pressed) {
            color = vec3<f32>(0.4, 0.6, 1.0);  // Blue when pressed
        } else if (hover) {
            color = vec3<f32>(0.35, 0.35, 0.4);  // Lighter on hover
        } else {
            color = vec3<f32>(0.1, 0.1, 0.12);  // Dark gray/black
        }

        // Subtle 3D effect
        let blackKeyLocalY = uv.y / blackKeyHeight;
        let shade = 1.0 - blackKeyLocalY * 0.2;
        color = color * shade;

        // Bottom edge highlight
        if (uv.y > blackKeyHeight - 0.02) {
            color = color * 0.7;
        }
    } else if (whiteKeyIdx >= 0 && whiteKeyIdx < numWhiteKeys && !inGap) {
        // White key
        let octave = whiteKeyIdx / 7;
        let keyInOctave = whiteKeyIdx % 7;
        // Map to semitone: C=0, D=2, E=4, F=5, G=7, A=9, B=11
        var semitone = 0;
        if (keyInOctave == 0) { semitone = 0; }
        else if (keyInOctave == 1) { semitone = 2; }
        else if (keyInOctave == 2) { semitone = 4; }
        else if (keyInOctave == 3) { semitone = 5; }
        else if (keyInOctave == 4) { semitone = 7; }
        else if (keyInOctave == 5) { semitone = 9; }
        else { semitone = 11; }

        let midiNote = octave * 12 + semitone + 48;  // +48 for C4 base
        let pressed = isKeyPressed(midiNote);
        let hover = (f32(midiNote) == u.hoverKey);

        if (pressed) {
            color = vec3<f32>(0.5, 0.7, 1.0);  // Light blue when pressed
        } else if (hover) {
            color = vec3<f32>(0.92, 0.92, 0.95);  // Slightly darker on hover
        } else {
            color = vec3<f32>(0.98, 0.98, 1.0);  // Off-white
        }

        // Subtle gradient for 3D effect
        let shade = 1.0 - uv.y * 0.08;
        color = color * shade;

        // Bottom shadow
        if (uv.y > 0.95) {
            color = color * (1.0 - (uv.y - 0.95) * 4.0);
        }
    }

    // Border
    let border = 2.0 / u.resolution.y;
    if (uv.y < border || uv.y > 1.0 - border || uv.x < border || uv.x > 1.0 - border) {
        color = vec3<f32>(0.3, 0.3, 0.35);
    }

    return vec4<f32>(color, 1.0);
}
)";

    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPU_STR(shaderCode);
    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!shaderModule) return Err<void>("Failed to create shader module");

    // Bind group layout
    WGPUBindGroupLayoutEntry entries[2] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 2;
    bglDesc.entries = entries;
    WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    if (!bgl) {
        wgpuShaderModuleRelease(shaderModule);
        return Err<void>("Failed to create bind group layout");
    }

    // Pipeline layout
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &bgl;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

    // Bind group
    WGPUBindGroupEntry bgE[2] = {};
    bgE[0].binding = 0;
    bgE[0].buffer = _uniform_buffer;
    bgE[0].size = 48;
    bgE[1].binding = 1;
    bgE[1].buffer = _key_state_buffer;
    bgE[1].size = 16;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl;
    bgDesc.entryCount = 2;
    bgDesc.entries = bgE;
    _bind_group = wgpuDeviceCreateBindGroup(device, &bgDesc);

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = WGPU_STR("vs_main");

    WGPUFragmentState fragState = {};
    fragState.module = shaderModule;
    fragState.entryPoint = WGPU_STR("fs_main");

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = targetFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    _pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!_pipeline) return Err<void>("Failed to create render pipeline");

    spdlog::info("PianoLayer: pipeline created");
    return Ok();
}

} // namespace yetty

extern "C" {
    const char* name() { return "piano"; }
    yetty::Result<yetty::PluginPtr> create() { return yetty::PianoPlugin::create(); }
}
