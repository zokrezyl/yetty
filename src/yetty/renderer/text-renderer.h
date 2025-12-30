#pragma once

#include "webgpu-context.h"
#include "../result.hpp"
#include "../terminal/grid.h"
#include "../terminal/font.h"
#include "../config.h"
#include <glm/glm.hpp>
#include <vector>

namespace yetty {

struct DamageRect;

class TextRenderer {
public:
    TextRenderer();
    ~TextRenderer();

    Result<void> init(WebGPUContext& ctx, Font& font);
    void resize(uint32_t width, uint32_t height);
    void render(WebGPUContext& ctx, const Grid& grid,
                int cursorCol = -1, int cursorRow = -1, bool cursorVisible = false);

    void setCellSize(float width, float height);
    void setScale(float scale) { scale_ = scale; }
    glm::vec2 getCellSize() const { return cellSize_; }
    float getScale() const { return scale_; }

    void setConfig(const Config* config) { config_ = config; }

    // Update font bindings (call after font atlas/metadata changes)
    void updateFontBindings(Font& font);

    // Render with damage tracking (only updates changed regions)
    void render(WebGPUContext& ctx, const Grid& grid,
                const std::vector<DamageRect>& damageRects, bool fullDamage,
                int cursorCol = -1, int cursorRow = -1, bool cursorVisible = false);

private:
    Result<void> createShaderModule(WGPUDevice device);
    Result<void> createPipeline(WGPUDevice device, WGPUTextureFormat format);
    Result<void> createBuffers(WGPUDevice device);
    Result<void> createCellTextures(WGPUDevice device, uint32_t cols, uint32_t rows);
    Result<void> createBindGroupLayout(WGPUDevice device);
    Result<void> createBindGroup(WGPUDevice device, Font& font);

    void updateUniformBuffer(WGPUQueue queue, const Grid& grid,
                             int cursorCol, int cursorRow, bool cursorVisible);
    void updateCellTextures(WGPUQueue queue, const Grid& grid);
    void updateCellTextureRegion(WGPUQueue queue, const Grid& grid, const DamageRect& rect);

    // Uniforms struct - must match shader
    struct Uniforms {
        glm::mat4 projection;     // 64 bytes, offset 0
        glm::vec2 screenSize;     // 8 bytes, offset 64
        glm::vec2 cellSize;       // 8 bytes, offset 72
        glm::vec2 gridSize;       // 8 bytes, offset 80 (cols, rows)
        float pixelRange;         // 4 bytes, offset 88
        float scale;              // 4 bytes, offset 92
        glm::vec2 cursorPos;      // 8 bytes, offset 96 (col, row)
        float cursorVisible;      // 4 bytes, offset 104
        float _pad;               // 4 bytes, offset 108
    };  // Total: 112 bytes

    WGPUShaderModule shaderModule_ = nullptr;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;

    // Buffers
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUBuffer quadVertexBuffer_ = nullptr;     // Fullscreen quad vertices

    // Cell data textures (sized to grid dimensions)
    WGPUTexture cellGlyphTexture_ = nullptr;    // R16Uint - glyph index per cell
    WGPUTextureView cellGlyphView_ = nullptr;
    WGPUTexture cellFgColorTexture_ = nullptr;  // RGBA8Unorm - FG color per cell
    WGPUTextureView cellFgColorView_ = nullptr;
    WGPUTexture cellBgColorTexture_ = nullptr;  // RGBA8Unorm - BG color per cell
    WGPUTextureView cellBgColorView_ = nullptr;
    WGPUTexture cellAttrsTexture_ = nullptr;    // R8Uint - attributes per cell (bold, italic, underline, strike)
    WGPUTextureView cellAttrsView_ = nullptr;

    Uniforms uniforms_;
    glm::vec2 cellSize_ = {10.0f, 20.0f};
    float scale_ = 1.0f;
    uint32_t screenWidth_ = 800;
    uint32_t screenHeight_ = 600;

    uint32_t textureCols_ = 0;  // Current texture dimensions
    uint32_t textureRows_ = 0;
    uint32_t gridCols_ = 0;
    uint32_t gridRows_ = 0;

    WGPUDevice device_ = nullptr;  // Cached for recreating bind groups
    Font* font_ = nullptr;
    const Config* config_ = nullptr;
    bool needsBindGroupRecreation_ = false;  // Deferred bind group recreation
};

} // namespace yetty
