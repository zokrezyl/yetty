#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>
#include <vector>
#include <bitset>

namespace yetty {

class PianoLayer;

//-----------------------------------------------------------------------------
// PianoPlugin - renders a piano keyboard with WebGPU
//-----------------------------------------------------------------------------
class PianoPlugin : public Plugin {
public:
    ~PianoPlugin() override;

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "piano"; }

    Result<void> dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

    Result<void> renderAll(WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                           uint32_t screenWidth, uint32_t screenHeight,
                           float cellWidth, float cellHeight,
                           int scrollOffset, uint32_t termRows,
                           bool isAltScreen = false) override;

private:
    explicit PianoPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> init() noexcept override;
};

//-----------------------------------------------------------------------------
// PianoLayer - a single piano keyboard instance
//
// Payload format: "octaves[,startOctave]"
//   e.g., "2" = 2 octaves starting from C4
//   e.g., "3,3" = 3 octaves starting from C3
//-----------------------------------------------------------------------------
class PianoLayer : public PluginLayer {
public:
    static constexpr int MAX_OCTAVES = 8;
    static constexpr int KEYS_PER_OCTAVE = 12;  // 7 white + 5 black
    static constexpr int WHITE_KEYS_PER_OCTAVE = 7;
    static constexpr int BLACK_KEYS_PER_OCTAVE = 5;

    PianoLayer();
    ~PianoLayer() override;

    Result<void> init(const std::string& payload) override;
    Result<void> dispose() override;
    Result<void> update(double deltaTime) override;

    Result<void> render(WebGPUContext& ctx,
                        WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                        uint32_t screenWidth, uint32_t screenHeight,
                        float pixelX, float pixelY, float pixelW, float pixelH);

    // Key state management
    void setKeyPressed(int key, bool pressed);  // key = 0-127 (MIDI note)
    bool isKeyPressed(int key) const;
    void clearAllKeys();

    // Input handling
    bool onMouseMove(float localX, float localY) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onKey(int key, int scancode, int action, int mods) override;
    bool onChar(unsigned int codepoint) override;
    bool wantsMouse() const override { return true; }
    bool wantsKeyboard() const override { return true; }

private:
    Result<void> createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);
    int getKeyAtPosition(float x, float y) const;  // Returns MIDI note or -1

    // Configuration
    int _num_octaves = 2;
    int _start_octave = 4;  // C4 = middle C

    // Key states (128 MIDI notes)
    std::bitset<128> _key_states;

    // Mouse state
    float _mouse_x = 0;
    float _mouse_y = 0;
    int _hover_key = -1;
    int _pressed_key = -1;

    // Animation
    float _time = 0;

    // GPU resources
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroup _bind_group = nullptr;
    WGPUBuffer _uniform_buffer = nullptr;
    WGPUBuffer _key_state_buffer = nullptr;

    bool _gpu_initialized = false;
    bool _failed = false;
};

using Piano = PianoPlugin;

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
