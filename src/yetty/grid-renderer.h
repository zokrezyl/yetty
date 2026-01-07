#pragma once

#include "yetty/emoji-atlas.h"
#include "yetty/grid.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <yetty/font-manager.h>
#include <yetty/font.h>
#include <yetty/result.hpp>
#include <yetty/webgpu-context.h>

namespace yetty {

class Config;
struct DamageRect;

class GridRenderer {
public:
  using Ptr = std::shared_ptr<GridRenderer>;

  static Result<Ptr> create(WebGPUContext::Ptr ctx,
                            FontManager::Ptr fontManager,
                            const std::string& fontFamily = "default") noexcept;

  ~GridRenderer();

  // Non-copyable
  GridRenderer(const GridRenderer &) = delete;
  GridRenderer &operator=(const GridRenderer &) = delete;

  void resize(uint32_t width, uint32_t height) noexcept;
  void render(const Grid &grid, int cursorCol = -1, int cursorRow = -1,
              bool cursorVisible = false) noexcept;

  void setCellSize(float width, float height) noexcept;
  void setScale(float scale) noexcept { scale_ = scale; }
  glm::vec2 getCellSize() const noexcept { return cellSize_; }
  float getScale() const noexcept { return scale_; }

  void setConfig(const Config *config) noexcept { config_ = config; }

  // Get emoji atlas for dynamic loading
  EmojiAtlas *getEmojiAtlas() const noexcept { return emojiAtlas_.get(); }

  // Update font bindings (call after font atlas/metadata changes)
  void updateFontBindings(Font &font) noexcept;

  // Render with damage tracking (only updates changed regions)
  void render(const Grid &grid, const std::vector<DamageRect> &damageRects,
              bool fullDamage, int cursorCol = -1, int cursorRow = -1,
              bool cursorVisible = false) noexcept;

  // Render from CPU buffer data (used by RenderGridCmd)
  void renderFromBuffers(uint32_t cols, uint32_t rows,
                         const uint16_t* glyphs,
                         const uint8_t* fgColors,
                         const uint8_t* bgColors,
                         const uint8_t* attrs,
                         int cursorCol, int cursorRow,
                         bool cursorVisible) noexcept;

private:
  GridRenderer(WebGPUContext::Ptr ctx, FontManager::Ptr fontManager,
               const std::string& fontFamily) noexcept;
  Result<void> init() noexcept;
  Result<void> createShaderModule(WGPUDevice device);
  Result<void> createPipeline(WGPUDevice device, WGPUTextureFormat format);
  Result<void> createBuffers(WGPUDevice device);
  Result<void> createCellTextures(WGPUDevice device, uint32_t cols,
                                  uint32_t rows);
  Result<void> createBindGroupLayout(WGPUDevice device);
  Result<void> createBindGroup(WGPUDevice device, Font &font);

  void updateUniformBuffer(WGPUQueue queue, const Grid &grid, int cursorCol,
                           int cursorRow, bool cursorVisible);
  void updateCellTextures(WGPUQueue queue, const Grid &grid);
  void updateCellTextureRegion(WGPUQueue queue, const Grid &grid,
                               const DamageRect &rect);

  // Uniforms struct - must match shader
  struct Uniforms {
    glm::mat4 projection; // 64 bytes, offset 0
    glm::vec2 screenSize; // 8 bytes, offset 64
    glm::vec2 cellSize;   // 8 bytes, offset 72
    glm::vec2 gridSize;   // 8 bytes, offset 80 (cols, rows)
    float pixelRange;     // 4 bytes, offset 88
    float scale;          // 4 bytes, offset 92
    glm::vec2 cursorPos;  // 8 bytes, offset 96 (col, row)
    float cursorVisible;  // 4 bytes, offset 104
    float _pad;           // 4 bytes, offset 108
  }; // Total: 112 bytes

  WGPUShaderModule shaderModule_ = nullptr;
  WGPURenderPipeline pipeline_ = nullptr;
  WGPUBindGroupLayout bindGroupLayout_ = nullptr;
  WGPUBindGroup bindGroup_ = nullptr;
  WGPUPipelineLayout pipelineLayout_ = nullptr;

  // Buffers
  WGPUBuffer uniformBuffer_ = nullptr;
  WGPUBuffer quadVertexBuffer_ = nullptr; // Fullscreen quad vertices

  // Cell data textures (sized to grid dimensions)
  WGPUTexture cellGlyphTexture_ = nullptr; // R16Uint - glyph index per cell
  WGPUTextureView cellGlyphView_ = nullptr;
  WGPUTexture cellFgColorTexture_ = nullptr; // RGBA8Unorm - FG color per cell
  WGPUTextureView cellFgColorView_ = nullptr;
  WGPUTexture cellBgColorTexture_ = nullptr; // RGBA8Unorm - BG color per cell
  WGPUTextureView cellBgColorView_ = nullptr;
  WGPUTexture cellAttrsTexture_ =
      nullptr; // R8Uint - attributes per cell (bold, italic, underline, strike)
  WGPUTextureView cellAttrsView_ = nullptr;

  Uniforms uniforms_;
  glm::vec2 cellSize_ = {10.0f, 20.0f};
  float scale_ = 1.0f;
  uint32_t screenWidth_ = 800;
  uint32_t screenHeight_ = 600;

  uint32_t textureCols_ = 0; // Current texture dimensions
  uint32_t textureRows_ = 0;
  uint32_t gridCols_ = 0;
  uint32_t gridRows_ = 0;

  WebGPUContext::Ptr ctx_;
  FontManager::Ptr fontManager_;
  std::string fontFamily_;
  Font *font_ = nullptr;       // Cached pointer from FontManager
  EmojiAtlas::Ptr emojiAtlas_; // Color emoji atlas
  const Config *config_ = nullptr;
  bool needsBindGroupRecreation_ = false; // Deferred bind group recreation
};

} // namespace yetty
