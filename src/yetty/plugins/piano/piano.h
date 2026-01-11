#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>
#include <vector>
#include <bitset>

namespace yetty {

class PianoW;

//-----------------------------------------------------------------------------
// PianoPlugin - renders a piano keyboard with WebGPU
//-----------------------------------------------------------------------------
class PianoPlugin : public Plugin {
public:
    ~PianoPlugin() override;

    static Result<PluginPtr> create() noexcept;

    const char* pluginName() const override { return "piano"; }

    Result<void> dispose() override;

    Result<WidgetPtr> createWidget(
        const std::string& widgetName,
        WidgetFactory* factory,
        FontManager* fontManager,
        uv_loop_t* loop,
        int32_t x,
        int32_t y,
        uint32_t widthCells,
        uint32_t heightCells,
        const std::string& pluginArgs,
        const std::string& payload
    ) override;

    Result<void> renderAll(WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                           uint32_t screenWidth, uint32_t screenHeight,
                           float cellWidth, float cellHeight,
                           int scrollOffset, uint32_t termRows,
                           bool isAltScreen = false) override;

private:
    PianoPlugin() noexcept = default;
    Result<void> pluginInit() noexcept;
};

//-----------------------------------------------------------------------------
// PianoW - a single piano keyboard instance
//
// Payload format: "octaves[,startOctave]"
//   e.g., "2" = 2 octaves starting from C4
//   e.g., "3,3" = 3 octaves starting from C3
//-----------------------------------------------------------------------------
class PianoW : public Widget {
public:
    static constexpr int MAX_OCTAVES = 8;
    static constexpr int KEYS_PER_OCTAVE = 12;  // 7 white + 5 black
    static constexpr int WHITE_KEYS_PER_OCTAVE = 7;
    static constexpr int BLACK_KEYS_PER_OCTAVE = 5;

    static Result<WidgetPtr> create(const std::string& payload);

    ~PianoW() override;

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
    explicit PianoW(const std::string& payload);
    Result<void> init() override;

    Result<void> createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);
    int getKeyAtPosition(float x, float y) const;  // Returns MIDI note or -1

    // Configuration
    int numOctaves_ = 2;
    int startOctave_ = 4;  // C4 = middle C

    // Key states (128 MIDI notes)
    std::bitset<128> keyStates_;

    // Mouse state
    float mouseX_ = 0;
    float mouseY_ = 0;
    int hoverKey_ = -1;
    int pressedKey_ = -1;

    // Animation
    float time_ = 0;

    // GPU resources
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUBuffer keyStateBuffer_ = nullptr;

    bool gpuInitialized_ = false;
    bool failed_ = false;
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
