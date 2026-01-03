#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>

namespace yetty {

//-----------------------------------------------------------------------------
// SDF Primitive Types
//-----------------------------------------------------------------------------

enum class SDFPrimitiveType : uint32_t {
    Circle = 0,
    Box = 1,
    Segment = 2,
    Triangle = 3,
    Bezier = 4,       // Quadratic bezier
    Arc = 5,          // Circular arc
    Ellipse = 6,
    CubicBezier = 7,
    EllipseArc = 8,   // Elliptical arc (for SVG arc command)
    Polygon = 9,      // Not yet implemented in shader
};

struct SDFStyle {
    float fill[4] = {1.0f, 1.0f, 1.0f, 1.0f};      // RGBA
    float stroke[4] = {0.0f, 0.0f, 0.0f, 0.0f};    // RGBA (alpha 0 = no stroke)
    float stroke_width = 0.0f;
    float round = 0.0f;
    float rotate = 0.0f;  // degrees
    float _pad = 0.0f;
};

// GPU-side primitive data (padded to 16-byte alignment)
struct SDFPrimitiveGPU {
    uint32_t type;
    float params[15];  // Flexible params based on type
    SDFStyle style;    // 48 bytes
};

//-----------------------------------------------------------------------------
// YAML AST Types
//-----------------------------------------------------------------------------

struct SDFStruct {
    std::string name;
    std::vector<std::string> args;
    std::unordered_map<std::string, std::string> defaults;
    std::string body_yaml;  // Raw YAML for lazy evaluation
};

struct SDFWidget {
    std::string name;
    std::vector<std::string> args;
    std::unordered_map<std::string, std::string> defaults;
    std::string body_yaml;  // Raw YAML for lazy evaluation
};

//-----------------------------------------------------------------------------
// SDFPrimitivesPlugin
//-----------------------------------------------------------------------------

class SDFPrimitivesLayer;

class SDFPrimitivesPlugin : public Plugin {
public:
    ~SDFPrimitivesPlugin() override;

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "sdf-primitives"; }

    Result<void> dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

    Result<void> renderAll(WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                           uint32_t screenWidth, uint32_t screenHeight,
                           float cellWidth, float cellHeight,
                           int scrollOffset, uint32_t termRows,
                           bool isAltScreen = false) override;

private:
    explicit SDFPrimitivesPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> init() noexcept override;
};

//-----------------------------------------------------------------------------
// SDFPrimitivesLayer
//-----------------------------------------------------------------------------

class SDFPrimitivesLayer : public PluginLayer {
public:
    static constexpr size_t MAX_PRIMITIVES = 256;

    SDFPrimitivesLayer();
    ~SDFPrimitivesLayer() override;

    Result<void> init(const std::string& payload) override;
    Result<void> dispose() override;
    Result<void> update(double deltaTime) override;

    Result<void> render(WebGPUContext& ctx,
                        WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                        uint32_t screenWidth, uint32_t screenHeight,
                        float pixelX, float pixelY, float pixelW, float pixelH);

    bool onMouseMove(float localX, float localY) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onKey(int key, int scancode, int action, int mods) override;
    bool onChar(unsigned int codepoint) override;
    bool wantsMouse() const override { return true; }
    bool wantsKeyboard() const override { return true; }

private:
    Result<void> createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);
    Result<void> parseYAML(const std::string& yaml);
    Result<void> parseSVG(const std::string& svg);
    Result<void> processBody();

    // YAML processing helpers
    void registerStruct(const SDFStruct& s);
    void registerWidget(const SDFWidget& w);
    Result<std::vector<SDFPrimitiveGPU>> expandWidget(const std::string& name,
                                                       const std::unordered_map<std::string, std::string>& args);
    SDFStyle expandStruct(const std::string& name,
                          const std::vector<std::string>& positional_args);

public:
    static bool parseColor(const std::string& color, float out[4]);
    static bool parseVec2(const std::string& str, float out[2]);

private:

    // Definitions
    std::unordered_map<std::string, SDFStruct> _structs;
    std::unordered_map<std::string, SDFWidget> _widgets;

    // Primitives to render
    std::vector<SDFPrimitiveGPU> _primitives;
    bool _primitives_dirty = true;

    float _time = 0.0f;

    // GPU resources
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroup _bind_group = nullptr;
    WGPUBuffer _uniform_buffer = nullptr;
    WGPUBuffer _primitive_buffer = nullptr;

    bool _gpu_initialized = false;
    bool _failed = false;
};

using SDFPrimitives = SDFPrimitivesPlugin;

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
