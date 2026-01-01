#include "pdf.h"
#include <yetty/webgpu-context.h>
#include <yetty/wgpu-compat.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <spdlog/spdlog.h>

// FreeType for font access - MUST be included before msdfgen-ext.h
// for adoptFreetypeFont() to be available
#include <ft2build.h>
#include FT_FREETYPE_H

// MSDF generation
#include <msdfgen-ext.h>
#include <msdfgen.h>

// MuPDF headers
extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

// Helper macros for accessing typed MuPDF pointers from void*
#define MCTX static_cast<fz_context *>(mupdfCtx_)
#define MDOC static_cast<fz_document *>(doc_)

namespace yetty {

//-----------------------------------------------------------------------------
// PDFPlugin
//-----------------------------------------------------------------------------

PDFPlugin::PDFPlugin() = default;

PDFPlugin::~PDFPlugin() { (void)dispose(); }

Result<PluginPtr> PDFPlugin::create() {
  return Ok<PluginPtr>(std::make_shared<PDFPlugin>());
}

Result<void> PDFPlugin::init(WebGPUContext *ctx) {
  (void)ctx;

  // Create MuPDF context
  fz_context *mctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
  if (!mctx) {
    return Err<void>("Failed to create MuPDF context");
  }
  ctx_ = mctx;

  // Register document handlers
  fz_try(mctx) { fz_register_document_handlers(mctx); }
  fz_catch(mctx) {
    fz_drop_context(mctx);
    ctx_ = nullptr;
    return Err<void>("Failed to register MuPDF document handlers");
  }

  _initialized = true;
  return Ok();
}

Result<void> PDFPlugin::dispose() {
  if (ctx_) {
    fz_drop_context(static_cast<fz_context *>(ctx_));
    ctx_ = nullptr;
  }

  if (auto res = Plugin::dispose(); !res) {
    return Err<void>("Failed to dispose PDFPlugin", res);
  }
  _initialized = false;
  return Ok();
}

Result<PluginLayerPtr> PDFPlugin::createLayer(const std::string &payload) {
  auto layer = std::make_shared<PDFLayer>(ctx_);
  auto result = layer->init(payload);
  if (!result) {
    return Err<PluginLayerPtr>("Failed to init PDFLayer", result);
  }
  return Ok<PluginLayerPtr>(layer);
}

Result<void>
PDFPlugin::renderAll(WebGPUContext &ctx, WGPUTextureView targetView,
                     WGPUTextureFormat targetFormat, uint32_t screenWidth,
                     uint32_t screenHeight, float cellWidth, float cellHeight,
                     int scrollOffset, uint32_t termRows, bool isAltScreen) {
  spdlog::debug("PDFPlugin::renderAll: {} layers, isAltScreen={}", _layers.size(), isAltScreen);
  ScreenType currentScreen =
      isAltScreen ? ScreenType::Alternate : ScreenType::Main;
  for (auto &layerBase : _layers) {
    spdlog::debug("PDFPlugin::renderAll: layer visible={}, screenType={}",
                  layerBase->isVisible(), static_cast<int>(layerBase->getScreenType()));
    if (!layerBase->isVisible())
      continue;
    if (layerBase->getScreenType() != currentScreen)
      continue;

    auto layer = std::static_pointer_cast<PDFLayer>(layerBase);

    float pixelX = layer->getX() * cellWidth;
    float pixelY = layer->getY() * cellHeight;
    float pixelW = layer->getWidthCells() * cellWidth;
    float pixelH = layer->getHeightCells() * cellHeight;

    if (layer->getPositionMode() == PositionMode::Relative &&
        scrollOffset > 0) {
      pixelY += scrollOffset * cellHeight;
    }

    if (termRows > 0) {
      float screenPixelHeight = termRows * cellHeight;
      if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
        continue;
      }
    }

    if (auto res = layer->render(ctx, targetView, targetFormat, screenWidth,
                                 screenHeight, pixelX, pixelY, pixelW, pixelH);
        !res) {
      return Err<void>("Failed to render PDFLayer", res);
    }
  }
  return Ok();
}

//-----------------------------------------------------------------------------
// PDFLayer
//-----------------------------------------------------------------------------

PDFLayer::PDFLayer(void *ctx) : mupdfCtx_(ctx) {}

PDFLayer::~PDFLayer() { (void)dispose(); }

Result<void> PDFLayer::init(const std::string &payload) {
  if (payload.empty()) {
    return Err<void>("PDFLayer: empty payload");
  }

  _payload = payload;
  (void)dispose();

  return loadPDF(payload);
}

Result<void> PDFLayer::dispose() {
  if (doc_) {
    fz_drop_document(MCTX, MDOC);
    doc_ = nullptr;
  }

  // Release image textures
  for (auto &page : pages_) {
    for (auto &img : page.images) {
      if (img.textureView)
        wgpuTextureViewRelease(img.textureView);
      if (img.texture)
        wgpuTextureRelease(img.texture);
    }
  }
  pages_.clear();

  // Release PDF font textures and per-font GPU resources
  for (auto &pdfFont : fonts_) {
    if (pdfFont.bindGroup) {
      wgpuBindGroupRelease(pdfFont.bindGroup);
      pdfFont.bindGroup = nullptr;
    }
    if (pdfFont.glyphBuffer) {
      wgpuBufferRelease(pdfFont.glyphBuffer);
      pdfFont.glyphBuffer = nullptr;
    }
    if (pdfFont.textureView) {
      wgpuTextureViewRelease(pdfFont.textureView);
      pdfFont.textureView = nullptr;
    }
    if (pdfFont.texture) {
      wgpuTextureRelease(pdfFont.texture);
      pdfFont.texture = nullptr;
    }
    pdfFont.glyphInstances.clear();
    pdfFont.glyphCount = 0;
    // Note: Don't destroy fzFont/ftFace - MuPDF owns them via the document
  }
  fonts_.clear();

  // Release msdfgen freetype handle
  if (freetypeHandle_) {
    msdfgen::deinitializeFreetype(
        static_cast<msdfgen::FreetypeHandle *>(freetypeHandle_));
    freetypeHandle_ = nullptr;
  }

  if (textBindGroup_) {
    wgpuBindGroupRelease(textBindGroup_);
    textBindGroup_ = nullptr;
  }
  if (textBindGroupLayout_) {
    wgpuBindGroupLayoutRelease(textBindGroupLayout_);
    textBindGroupLayout_ = nullptr;
  }
  if (textPipeline_) {
    wgpuRenderPipelineRelease(textPipeline_);
    textPipeline_ = nullptr;
  }
  if (imagePipeline_) {
    wgpuRenderPipelineRelease(imagePipeline_);
    imagePipeline_ = nullptr;
  }
  if (uniformBuffer_) {
    wgpuBufferRelease(uniformBuffer_);
    uniformBuffer_ = nullptr;
  }
  if (glyphBuffer_) {
    wgpuBufferRelease(glyphBuffer_);
    glyphBuffer_ = nullptr;
  }
  if (sampler_) {
    wgpuSamplerRelease(sampler_);
    sampler_ = nullptr;
  }
  gpuInitialized_ = false;
  return Ok();
}

//-----------------------------------------------------------------------------
// PDF Loading
//-----------------------------------------------------------------------------

Result<void> PDFLayer::loadPDF(const std::string &path) {
  if (!mupdfCtx_) {
    return Err<void>("MuPDF context not initialized");
  }

  fz_try(MCTX) { doc_ = fz_open_document(MCTX, path.c_str()); }
  fz_catch(MCTX) { return Err<void>("Failed to open PDF: " + path); }

  pageCount_ = fz_count_pages(MCTX, MDOC);
  if (pageCount_ <= 0) {
    return Err<void>("PDF has no pages");
  }

  spdlog::info("PDFLayer: loaded {} with {} pages", path, pageCount_);

  // Extract first page content
  return extractPageContent(0);
}

Result<void> PDFLayer::extractPageContent(int pageNum) {
  if (!doc_ || pageNum < 0 || pageNum >= pageCount_) {
    return Err<void>("Invalid page number");
  }

  currentPage_ = pageNum;
  pages_.clear();

  fz_page *page = nullptr;
  fz_stext_page *textPage = nullptr;

  // Collect codepoints per font for atlas generation
  std::map<int, std::set<uint32_t>> fontCodepoints;

  fz_try(MCTX) {
    page = fz_load_page(MCTX, MDOC, pageNum);
    fz_rect bounds = fz_bound_page(MCTX, page);

    PDFPage pdfPage;
    pdfPage.width = bounds.x1 - bounds.x0;
    pdfPage.height = bounds.y1 - bounds.y0;

    // Extract text using structured text
    fz_stext_options opts = {0};
    textPage = fz_new_stext_page_from_page(MCTX, page, &opts);

    for (fz_stext_block *block = textPage->first_block; block;
         block = block->next) {
      if (block->type != FZ_STEXT_BLOCK_TEXT)
        continue;

      for (fz_stext_line *line = block->u.t.first_line; line;
           line = line->next) {
        // Debug: show line bbox vs character origins
        static int lineDebugCount = 0;
        if (lineDebugCount++ < 5) {
          fz_rect lbbox = line->bbox;
          fz_stext_char* firstCh = line->first_char;
          if (firstCh) {
            spdlog::debug("Line bbox: y0={:.2f} y1={:.2f}, first char origin.y={:.2f}, size={:.2f}",
                          lbbox.y0, lbbox.y1, firstCh->origin.y, firstCh->size);
          }
        }
        for (fz_stext_char *ch = line->first_char; ch; ch = ch->next) {
          PDFTextChar textChar;
          textChar.codepoint = ch->c;
          textChar.x = ch->origin.x;
          textChar.y = ch->origin.y;
          textChar.size = ch->size;
          textChar.color = 0xFF000000; // Default black

          // Track font for this character
          if (ch->font) {
            textChar.bold = fz_font_is_bold(MCTX, ch->font);
            textChar.italic = fz_font_is_italic(MCTX, ch->font);
            textChar.fontIndex = getOrCreateFont(ch->font);
            fontCodepoints[textChar.fontIndex].insert(ch->c);
          }

          pdfPage.chars.push_back(textChar);
        }
      }
    }

    pages_.push_back(std::move(pdfPage));
    spdlog::info("PDFLayer: extracted {} characters from page {} using {} fonts",
                 pages_[0].chars.size(), pageNum, fonts_.size());
  }
  fz_always(MCTX) {
    if (textPage)
      fz_drop_stext_page(MCTX, textPage);
    if (page)
      fz_drop_page(MCTX, page);
  }
  fz_catch(MCTX) { return Err<void>("Failed to extract page content"); }

  // Generate MSDF atlases for fonts with new codepoints
  spdlog::debug("PDFLayer: generating MSDF atlases for {} fonts", fontCodepoints.size());
  for (auto &[fontIdx, codepoints] : fontCodepoints) {
    if (fontIdx >= 0 && fontIdx < static_cast<int>(fonts_.size())) {
      spdlog::debug("PDFLayer: generating atlas for font {} with {} codepoints",
                    fontIdx, codepoints.size());
      std::vector<uint32_t> cpVec(codepoints.begin(), codepoints.end());
      if (auto res = generateFontAtlas(fonts_[fontIdx], cpVec); !res) {
        return Err<void>("Failed to generate atlas for font " + fonts_[fontIdx].name, res);
      }
    }
  }

  spdlog::debug("PDFLayer: page extraction complete");
  return Ok();
}

//-----------------------------------------------------------------------------
// Font Extraction and MSDF Generation
//-----------------------------------------------------------------------------

int PDFLayer::getOrCreateFont(void *fzFont) {
  if (!fzFont)
    return -1;

  fz_font *font = static_cast<fz_font *>(fzFont);

  // Check if we already have this font
  for (size_t i = 0; i < fonts_.size(); ++i) {
    if (fonts_[i].fzFont == fzFont) {
      return static_cast<int>(i);
    }
  }

  // Create new font entry
  PDFFont pdfFont;
  pdfFont.fzFont = fzFont;
  pdfFont.name = fz_font_name(MCTX, font);
  pdfFont.bold = fz_font_is_bold(MCTX, font);
  pdfFont.italic = fz_font_is_italic(MCTX, font);

  // Get FreeType face from MuPDF font (fz_font_ft_face returns void*)
  FT_Face ftFace = static_cast<FT_Face>(fz_font_ft_face(MCTX, font));
  if (ftFace) {
    pdfFont.ftFace = ftFace;
    spdlog::debug("PDFLayer: extracted font '{}' (bold={}, italic={})",
                  pdfFont.name, pdfFont.bold, pdfFont.italic);
  } else {
    spdlog::warn("PDFLayer: no FreeType face for font '{}'", pdfFont.name);
  }

  fonts_.push_back(std::move(pdfFont));
  return static_cast<int>(fonts_.size() - 1);
}

// Simple shelf-based atlas packer (same as in font.cpp)
class PDFShelfPacker {
public:
  PDFShelfPacker(int width, int height, int padding = 2)
      : width_(width), height_(height), padding_(padding), shelfX_(padding),
        shelfY_(padding), shelfHeight_(0) {}

  bool pack(int w, int h, int &outX, int &outY) {
    int pw = w + padding_;
    int ph = h + padding_;

    if (shelfX_ + pw > width_) {
      shelfX_ = padding_;
      shelfY_ += shelfHeight_ + padding_;
      shelfHeight_ = 0;
    }

    if (shelfY_ + ph > height_) {
      return false;
    }

    outX = shelfX_;
    outY = shelfY_;
    shelfX_ += pw;
    shelfHeight_ = std::max(shelfHeight_, ph);
    return true;
  }

private:
  int width_, height_, padding_;
  int shelfX_, shelfY_, shelfHeight_;
};

Result<void>
PDFLayer::generateFontAtlas(PDFFont &font,
                            const std::vector<uint32_t> &codepoints) {
  spdlog::debug("PDFLayer::generateFontAtlas: font='{}', codepoints={}",
                font.name, codepoints.size());

  if (!font.ftFace || codepoints.empty()) {
    spdlog::debug("PDFLayer::generateFontAtlas: nothing to do (ftFace={}, codepoints={})",
                  font.ftFace != nullptr, codepoints.size());
    return Ok(); // Nothing to do
  }

  FT_Face ftFace = static_cast<FT_Face>(font.ftFace);
  spdlog::debug("PDFLayer::generateFontAtlas: FT_Face num_glyphs={}, units_per_EM={}, "
                "ascender={}, descender={}, height={}, face_flags=0x{:x}",
                ftFace->num_glyphs, ftFace->units_per_EM,
                ftFace->ascender, ftFace->descender, ftFace->height,
                static_cast<unsigned long>(ftFace->face_flags));

  // Initialize msdfgen's FreeType wrapper if needed
  if (!freetypeHandle_) {
    spdlog::debug("PDFLayer::generateFontAtlas: initializing msdfgen FreeType");
    freetypeHandle_ = msdfgen::initializeFreetype();
    if (!freetypeHandle_) {
      return Err<void>("Failed to initialize msdfgen FreeType");
    }
  }

  // Adopt the existing FT_Face into msdfgen
  spdlog::debug("PDFLayer::generateFontAtlas: adopting FreeType font");
  msdfgen::FontHandle *msdfFont = msdfgen::adoptFreetypeFont(ftFace);
  if (!msdfFont) {
    return Err<void>("Failed to adopt FreeType font for MSDF");
  }
  spdlog::debug("PDFLayer::generateFontAtlas: adopted FreeType font successfully");

  // Get font metrics with proper scaling (not legacy 1/64 scaling)
  spdlog::debug("PDFLayer::generateFontAtlas: getting font metrics");
  msdfgen::FontMetrics metrics;
  bool gotMetrics = msdfgen::getFontMetrics(metrics, msdfFont, msdfgen::FONT_SCALING_NONE);
  spdlog::debug("PDFLayer::generateFontAtlas: getFontMetrics returned {}", gotMetrics);
  double unitsPerEm = metrics.emSize > 0 ? metrics.emSize : 1000.0;
  spdlog::debug("PDFLayer::generateFontAtlas: unitsPerEm={}, ascender={}, descender={}",
                unitsPerEm, metrics.ascenderY, metrics.descenderY);

  // Use a larger font size for better MSDF quality
  double atlasSize = 64.0; // Base size in pixels (larger = better quality)
  double fontScale = atlasSize / unitsPerEm;
  spdlog::debug("PDFLayer::generateFontAtlas: fontScale={}", fontScale);

  // Determine atlas dimensions
  uint32_t atlasWidth = 512;
  uint32_t atlasHeight = 512;
  spdlog::debug("PDFLayer::generateFontAtlas: atlas dimensions {}x{}", atlasWidth, atlasHeight);

  // If atlas already exists, we're adding to it - skip for now
  if (!font.atlasData.empty()) {
    spdlog::debug("PDFLayer::generateFontAtlas: clearing existing atlas");
    // For simplicity, regenerate the entire atlas
    // In production, we'd want to add glyphs incrementally
  }

  font.atlasWidth = atlasWidth;
  font.atlasHeight = atlasHeight;
  spdlog::debug("PDFLayer::generateFontAtlas: resizing atlas data to {} bytes",
                atlasWidth * atlasHeight * 4);
  font.atlasData.resize(atlasWidth * atlasHeight * 4, 0);
  spdlog::debug("PDFLayer::generateFontAtlas: atlas data resized");

  PDFShelfPacker packer(atlasWidth, atlasHeight);
  spdlog::debug("PDFLayer::generateFontAtlas: packer created, processing {} codepoints",
                codepoints.size());

  for (uint32_t codepoint : codepoints) {
    spdlog::debug("PDFLayer::generateFontAtlas: processing codepoint {}", codepoint);
    // Skip if already in atlas
    if (font.glyphs.find(codepoint) != font.glyphs.end()) {
      spdlog::debug("PDFLayer::generateFontAtlas: codepoint {} already in atlas", codepoint);
      continue;
    }

    // Get the glyph index for this codepoint from the font's charmap
    FT_UInt glyphIdx = FT_Get_Char_Index(ftFace, codepoint);
    if (glyphIdx == 0 && codepoint != 0) {
      // Not in charmap - for subset fonts, try direct glyph access by iterating
      // Some PDF fonts have broken cmaps but valid glyphs
      spdlog::debug("PDFLayer::generateFontAtlas: codepoint {} not in charmap, skipping", codepoint);
      continue;
    }

    msdfgen::Shape shape;
    double advance;
    spdlog::debug("PDFLayer::generateFontAtlas: loading codepoint {} (glyph index {})", codepoint, glyphIdx);
    // Use the unicode overload which handles the charmap lookup
    if (!msdfgen::loadGlyph(shape, msdfFont, codepoint, msdfgen::FONT_SCALING_NONE, &advance)) {
      spdlog::debug("PDFLayer::generateFontAtlas: codepoint {} failed to load", codepoint);
      continue; // Glyph not in font
    }
    spdlog::debug("PDFLayer::generateFontAtlas: codepoint {} loaded, advance={}", codepoint, advance);

    if (shape.contours.empty()) {
      // Empty glyph (like space)
      spdlog::trace("PDFLayer::generateFontAtlas: glyph {} is empty (space)", codepoint);
      PDFFont::GlyphInfo info = {};
      info.advance = static_cast<float>(advance * fontScale);
      font.glyphs[codepoint] = info;
      continue;
    }

    shape.normalize();
    msdfgen::Shape::Bounds bounds = shape.getBounds();
    spdlog::trace("PDFLayer::generateFontAtlas: glyph {} bounds: l={}, b={}, r={}, t={}",
                  codepoint, bounds.l, bounds.b, bounds.r, bounds.t);

    double bearingX = bounds.l * fontScale;
    double bearingY = bounds.t * fontScale;
    double sizeX = (bounds.r - bounds.l) * fontScale;
    double sizeY = (bounds.t - bounds.b) * fontScale;

    // Validate size - skip degenerate glyphs
    if (sizeX <= 0 || sizeY <= 0 || !std::isfinite(sizeX) || !std::isfinite(sizeY)) {
      spdlog::trace("PDFLayer::generateFontAtlas: glyph {} has invalid size {}x{}, skipping",
                    codepoint, sizeX, sizeY);
      continue;
    }

    int padding = static_cast<int>(std::ceil(pixelRange_));
    int atlasW = static_cast<int>(std::ceil(sizeX)) + padding * 2;
    int atlasH = static_cast<int>(std::ceil(sizeY)) + padding * 2;

    // Sanity check atlas dimensions
    if (atlasW <= 0 || atlasH <= 0 || atlasW > 512 || atlasH > 512) {
      spdlog::warn("PDFLayer::generateFontAtlas: glyph {} has invalid atlas size {}x{}, skipping",
                   codepoint, atlasW, atlasH);
      continue;
    }

    int atlasX, atlasY;
    if (!packer.pack(atlasW, atlasH, atlasX, atlasY)) {
      spdlog::warn("PDFLayer: atlas full, skipping codepoint {}", codepoint);
      continue;
    }
    spdlog::trace("PDFLayer::generateFontAtlas: glyph {} packed at ({}, {}), size {}x{}",
                  codepoint, atlasX, atlasY, atlasW, atlasH);

    // Generate MSDF
    spdlog::trace("PDFLayer::generateFontAtlas: generating MSDF for glyph {}", codepoint);
    msdfgen::edgeColoringSimple(shape, 3.0);
    msdfgen::Bitmap<float, 3> msdf(atlasW, atlasH);
    msdfgen::Vector2 translate(padding - bounds.l * fontScale,
                               padding - bounds.b * fontScale);
    msdfgen::generateMSDF(msdf, shape, pixelRange_, fontScale, translate);
    spdlog::trace("PDFLayer::generateFontAtlas: MSDF generated for glyph {}", codepoint);

    // Copy to atlas with Y-flip
    for (int y = 0; y < atlasH; ++y) {
      for (int x = 0; x < atlasW; ++x) {
        int ax = atlasX + x;
        int ay = atlasY + (atlasH - 1 - y); // Flip Y

        if (ax >= 0 && ax < (int)atlasWidth && ay >= 0 &&
            ay < (int)atlasHeight) {
          size_t idx = (ay * atlasWidth + ax) * 4;
          font.atlasData[idx + 0] = static_cast<uint8_t>(
              std::clamp(msdf(x, y)[0] * 255.0f, 0.0f, 255.0f));
          font.atlasData[idx + 1] = static_cast<uint8_t>(
              std::clamp(msdf(x, y)[1] * 255.0f, 0.0f, 255.0f));
          font.atlasData[idx + 2] = static_cast<uint8_t>(
              std::clamp(msdf(x, y)[2] * 255.0f, 0.0f, 255.0f));
          font.atlasData[idx + 3] = 255;
        }
      }
    }

    // Store glyph info
    // The quad includes padding around the glyph for the MSDF distance field
    // bearingY = distance from baseline to TOP of quad (including padding)
    // After Y-flip in atlas, baseline is at: atlasH - padding + bounds.b * fontScale from top
    float paddingF = static_cast<float>(padding);

    // Distance from quad top to baseline (where baseline should align with screenY)
    float baselineFromTop = static_cast<float>(atlasH) - paddingF + static_cast<float>(bounds.b * fontScale);

    PDFFont::GlyphInfo info;
    info.uvMinX = static_cast<float>(atlasX) / atlasWidth;
    info.uvMinY = static_cast<float>(atlasY) / atlasHeight;
    info.uvMaxX = static_cast<float>(atlasX + atlasW) / atlasWidth;
    info.uvMaxY = static_cast<float>(atlasY + atlasH) / atlasHeight;
    // Include padding in size so UV mapping is correct
    info.sizeX = static_cast<float>(atlasW);
    info.sizeY = static_cast<float>(atlasH);
    // bearingX: distance from pen position to left edge of quad
    info.bearingX = static_cast<float>(bearingX) - paddingF;
    // Get FreeType metrics for bearingY
    float ftBearingY = 0.0f;
    if (FT_Load_Glyph(ftFace, glyphIdx, FT_LOAD_NO_SCALE) == 0) {
      // horiBearingY is distance from baseline to top of glyph, in font units
      ftBearingY = static_cast<float>(ftFace->glyph->metrics.horiBearingY) * fontScale;
    }

    // bearingY = distance from baseline to top of quad
    // = glyph top from baseline (bounds.t * fontScale) + padding for quad edge
    info.bearingY = static_cast<float>(bounds.t * fontScale) + paddingF;
    info.advance = static_cast<float>(advance * fontScale);

    // Debug ALL lowercase letters to compare x-heights
    if ((codepoint >= 'a' && codepoint <= 'z') || codepoint == 'A') {
      // Compare msdfgen bounds.t vs FreeType horiBearingY
      float ftTop = 0;
      if (FT_Load_Glyph(ftFace, glyphIdx, FT_LOAD_NO_SCALE) == 0) {
        ftTop = static_cast<float>(ftFace->glyph->metrics.horiBearingY);
      }
      spdlog::debug("Glyph '{}': bounds.t={:.0f} FT.bearingY={:.0f} diff={:.0f}",
                    (char)codepoint, bounds.t, ftTop, ftTop - bounds.t);
    }

    font.glyphs[codepoint] = info;
  }

  // Note: Don't destroy msdfFont - it shares the FT_Face with MuPDF
  // msdfgen::destroyFont(msdfFont) would free the face which MuPDF still owns

  // Debug: check if atlas has non-zero data
  size_t nonZeroCount = 0;
  float maxVal = 0.0f;
  for (size_t i = 0; i < font.atlasData.size(); i += 4) {
    uint8_t r = font.atlasData[i];
    uint8_t g = font.atlasData[i + 1];
    uint8_t b = font.atlasData[i + 2];
    if (r > 0 || g > 0 || b > 0) {
      nonZeroCount++;
      maxVal = std::max(maxVal, std::max({(float)r, (float)g, (float)b}));
    }
  }
  spdlog::debug("PDFLayer::generateFontAtlas: atlas has {} non-zero pixels (total {}), maxVal={}",
                nonZeroCount, font.atlasData.size() / 4, maxVal);

  spdlog::info("PDFLayer: generated atlas for font '{}' with {} glyphs",
               font.name, font.glyphs.size());

  return Ok();
}

Result<void> PDFLayer::uploadFontTexture(WebGPUContext &ctx, PDFFont &font) {
  spdlog::debug("PDFLayer::uploadFontTexture: font='{}', atlasSize={}x{}",
                font.name, font.atlasWidth, font.atlasHeight);

  if (font.atlasData.empty() || font.atlasWidth == 0 || font.atlasHeight == 0) {
    spdlog::debug("PDFLayer::uploadFontTexture: no atlas data");
    return Ok(); // No atlas data
  }

  // Release old texture if exists
  if (font.textureView) {
    wgpuTextureViewRelease(font.textureView);
    font.textureView = nullptr;
  }
  if (font.texture) {
    wgpuTextureRelease(font.texture);
    font.texture = nullptr;
  }

  // Create texture
  WGPUTextureDescriptor texDesc = {};
  texDesc.size.width = font.atlasWidth;
  texDesc.size.height = font.atlasHeight;
  texDesc.size.depthOrArrayLayers = 1;
  texDesc.mipLevelCount = 1;
  texDesc.sampleCount = 1;
  texDesc.dimension = WGPUTextureDimension_2D;
  texDesc.format = WGPUTextureFormat_RGBA8Unorm;
  texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

  font.texture = wgpuDeviceCreateTexture(ctx.getDevice(), &texDesc);
  if (!font.texture) {
    return Err<void>("Failed to create PDF font texture");
  }

  // Upload data
  WGPUTexelCopyTextureInfo dst = {};
  dst.texture = font.texture;
  dst.mipLevel = 0;
  dst.origin = {0, 0, 0};
  dst.aspect = WGPUTextureAspect_All;

  WGPUTexelCopyBufferLayout layout = {};
  layout.offset = 0;
  layout.bytesPerRow = font.atlasWidth * 4;
  layout.rowsPerImage = font.atlasHeight;

  WGPUExtent3D extent = {font.atlasWidth, font.atlasHeight, 1};
  wgpuQueueWriteTexture(ctx.getQueue(), &dst, font.atlasData.data(),
                        font.atlasData.size(), &layout, &extent);

  // Create texture view
  WGPUTextureViewDescriptor viewDesc = {};
  viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
  viewDesc.dimension = WGPUTextureViewDimension_2D;
  viewDesc.mipLevelCount = 1;
  viewDesc.arrayLayerCount = 1;
  font.textureView = wgpuTextureCreateView(font.texture, &viewDesc);

  return Ok();
}

//-----------------------------------------------------------------------------
// Layout
//-----------------------------------------------------------------------------

void PDFLayer::layoutPage(float viewWidth, float viewHeight) {
  if (pages_.empty())
    return;
  (void)viewHeight;

  // Clear per-font glyph instances
  for (auto &font : fonts_) {
    font.glyphInstances.clear();
    font.glyphCount = 0;
  }
  glyphCount_ = 0;

  const auto &page = pages_[0];
  float pdfWidth = page.width;
  float pdfHeight = page.height;

  // Calculate scale to fit view width
  float scale = viewWidth / pdfWidth * zoom_;
  documentHeight_ = pdfHeight * scale;

  // Base font size used when generating MSDF atlas (matches generateFontAtlas)
  constexpr float atlasBaseSize = 64.0f;

  for (const auto &ch : page.chars) {
    if (ch.codepoint == '\n' || ch.codepoint == '\r')
      continue;

    // Get glyph metrics from PDF font atlas
    if (ch.fontIndex < 0 || ch.fontIndex >= static_cast<int>(fonts_.size())) {
      continue; // No font for this character
    }

    PDFFont &pdfFont = fonts_[ch.fontIndex];
    auto it = pdfFont.glyphs.find(ch.codepoint);
    if (it == pdfFont.glyphs.end()) {
      continue; // Glyph not in atlas
    }

    const PDFFont::GlyphInfo &glyph = it->second;

    // Scale factor: PDF font size * document scale / atlas base size
    float fontScale = (ch.size * scale) / atlasBaseSize;

    // MuPDF's structured text uses device coordinates (top-left origin, Y down)
    // which matches screen coordinates - no Y flip needed

    // Convert PDF coordinates to screen coordinates
    float screenX = ch.x * scale;
    float screenY = ch.y * scale; // No flip - MuPDF stext uses screen coords

    // Apply bearing for proper glyph positioning
    float glyphX = screenX + glyph.bearingX * fontScale;
    float glyphY = screenY - glyph.bearingY * fontScale;

    float glyphW = glyph.sizeX * fontScale;
    float glyphH = glyph.sizeY * fontScale;

    // Debug first 20 characters to see baseline alignment
    static int debugRenderCount = 0;
    if (debugRenderCount++ < 20) {
      spdlog::debug("Render '{}' (0x{:04x}): pdfY={:.2f} screenY={:.2f} bearingY={:.2f} -> glyphY={:.2f} bottom={:.2f}",
                    (ch.codepoint >= 32 && ch.codepoint < 127) ? (char)ch.codepoint : '?',
                    ch.codepoint, ch.y, screenY, glyph.bearingY * fontScale, glyphY, glyphY + glyphH);
    }

    // Skip empty glyphs (like spaces)
    if (glyphW < 0.1f || glyphH < 0.1f)
      continue;

    // Color
    float r = ((ch.color >> 16) & 0xFF) / 255.0f;
    float g = ((ch.color >> 8) & 0xFF) / 255.0f;
    float b = (ch.color & 0xFF) / 255.0f;
    float a = ((ch.color >> 24) & 0xFF) / 255.0f;

    // Default to white text if color is black (for dark theme)
    if (r < 0.1f && g < 0.1f && b < 0.1f) {
      r = g = b = 0.9f;
      a = 1.0f;
    }

    PDFGlyphInstance inst;
    inst.posX = glyphX;
    inst.posY = glyphY;
    inst.uvMinX = glyph.uvMinX;
    inst.uvMinY = glyph.uvMinY;
    inst.uvMaxX = glyph.uvMaxX;
    inst.uvMaxY = glyph.uvMaxY;
    inst.sizeX = glyphW;
    inst.sizeY = glyphH;
    inst.colorR = r;
    inst.colorG = g;
    inst.colorB = b;
    inst.colorA = a;

    pdfFont.glyphInstances.push_back(inst);
  }

  // Update glyph counts
  for (auto &font : fonts_) {
    font.glyphCount = static_cast<uint32_t>(font.glyphInstances.size());
    glyphCount_ += font.glyphCount;
  }

  spdlog::debug("PDFLayer::layoutPage: created {} total glyphs from {} chars",
                glyphCount_, page.chars.size());
  for (size_t i = 0; i < fonts_.size(); ++i) {
    if (fonts_[i].glyphCount > 0) {
      spdlog::debug("PDFLayer::layoutPage: font {} '{}' has {} glyphs",
                    i, fonts_[i].name, fonts_[i].glyphCount);
    }
  }
}

void PDFLayer::buildGlyphBuffer(float viewWidth, float viewHeight) {
  layoutPage(viewWidth, viewHeight);
}

//-----------------------------------------------------------------------------
// GPU Pipeline
//-----------------------------------------------------------------------------

Result<void> PDFLayer::createPipelines(WebGPUContext &ctx,
                                       WGPUTextureFormat targetFormat) {
  spdlog::debug("PDFLayer::createPipelines: starting");
  WGPUDevice device = ctx.getDevice();

  // Upload PDF font textures
  spdlog::debug("PDFLayer::createPipelines: uploading {} font textures", fonts_.size());
  for (auto &pdfFont : fonts_) {
    if (!pdfFont.atlasData.empty() && !pdfFont.textureView) {
      if (auto res = uploadFontTexture(ctx, pdfFont); !res) {
        return Err<void>("Failed to upload PDF font texture: " + pdfFont.name, res);
      }
    }
  }

  // Check we have at least one font with a texture
  bool hasAnyFont = false;
  for (const auto &pdfFont : fonts_) {
    if (pdfFont.textureView && !pdfFont.glyphs.empty()) {
      hasAnyFont = true;
      break;
    }
  }

  if (!hasAnyFont) {
    return Err<void>("PDFLayer: no font atlas available");
  }

  // Create sampler
  WGPUSamplerDescriptor samplerDesc = {};
  samplerDesc.minFilter = WGPUFilterMode_Linear;
  samplerDesc.magFilter = WGPUFilterMode_Linear;
  samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
  samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
  samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
  samplerDesc.maxAnisotropy = 1;
  sampler_ = wgpuDeviceCreateSampler(device, &samplerDesc);
  if (!sampler_)
    return Err<void>("Failed to create sampler");

  // Create uniform buffer
  WGPUBufferDescriptor bufDesc = {};
  bufDesc.size = 48;
  bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
  uniformBuffer_ = wgpuDeviceCreateBuffer(device, &bufDesc);
  if (!uniformBuffer_)
    return Err<void>("Failed to create uniform buffer");

  // Per-font glyph buffers will be created after we have the bind group layout

  // Shader (same as markdown for now)
  const char *shaderCode = R"(
struct Uniforms {
    rect: vec4<f32>,
    screenSize: vec2<f32>,
    scrollOffset: f32,
    pixelRange: f32,
}

struct GlyphInstance {
    posX: f32, posY: f32,
    uvMinX: f32, uvMinY: f32,
    uvMaxX: f32, uvMaxY: f32,
    sizeX: f32, sizeY: f32,
    colorR: f32, colorG: f32, colorB: f32, colorA: f32,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var fontSampler: sampler;
@group(0) @binding(2) var fontAtlas: texture_2d<f32>;
@group(0) @binding(3) var<storage, read> glyphs: array<GlyphInstance>;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) color: vec4<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VertexOutput {
    let corners = array<vec2<f32>, 6>(
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    let corner = corners[vi];
    let g = glyphs[ii];

    let layerX = g.posX + corner.x * g.sizeX;
    let layerY = g.posY + corner.y * g.sizeY - u.scrollOffset;

    let ndcX = u.rect.x + (layerX / u.screenSize.x) * 2.0;
    let ndcY = u.rect.y - (layerY / u.screenSize.y) * 2.0;

    var out: VertexOutput;
    out.position = vec4(ndcX, ndcY, 0.0, 1.0);
    out.uv = vec2(
        g.uvMinX + corner.x * (g.uvMaxX - g.uvMinX),
        g.uvMinY + corner.y * (g.uvMaxY - g.uvMinY)
    );
    out.color = vec4(g.colorR, g.colorG, g.colorB, g.colorA);
    return out;
}

fn median(r: f32, g: f32, b: f32) -> f32 {
    return max(min(r, g), min(max(r, g), b));
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let msd = textureSample(fontAtlas, fontSampler, in.uv);
    let sd = median(msd.r, msd.g, msd.b);
    let screenPxDistance = u.pixelRange * (sd - 0.5);
    let opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);
    return vec4(in.color.rgb, in.color.a * opacity);
}
)";

  WGPUShaderSourceWGSL wgslDesc = {};
  wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
  wgslDesc.code = WGPU_STR(shaderCode);
  WGPUShaderModuleDescriptor shaderDesc = {};
  shaderDesc.nextInChain = &wgslDesc.chain;
  WGPUShaderModule shaderModule =
      wgpuDeviceCreateShaderModule(device, &shaderDesc);
  if (!shaderModule)
    return Err<void>("Failed to create shader module");

  // Bind group layout
  WGPUBindGroupLayoutEntry entries[4] = {};
  entries[0].binding = 0;
  entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
  entries[0].buffer.type = WGPUBufferBindingType_Uniform;

  entries[1].binding = 1;
  entries[1].visibility = WGPUShaderStage_Fragment;
  entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

  entries[2].binding = 2;
  entries[2].visibility = WGPUShaderStage_Fragment;
  entries[2].texture.sampleType = WGPUTextureSampleType_Float;
  entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

  entries[3].binding = 3;
  entries[3].visibility = WGPUShaderStage_Vertex;
  entries[3].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

  WGPUBindGroupLayoutDescriptor bglDesc = {};
  bglDesc.entryCount = 4;
  bglDesc.entries = entries;
  textBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
  if (!textBindGroupLayout_) {
    wgpuShaderModuleRelease(shaderModule);
    return Err<void>("Failed to create bind group layout");
  }

  // Pipeline layout
  WGPUPipelineLayoutDescriptor plDesc = {};
  plDesc.bindGroupLayoutCount = 1;
  plDesc.bindGroupLayouts = &textBindGroupLayout_;
  WGPUPipelineLayout pipelineLayout =
      wgpuDeviceCreatePipelineLayout(device, &plDesc);

  // Create per-font glyph buffers and bind groups
  for (auto &pdfFont : fonts_) {
    if (!pdfFont.textureView || pdfFont.glyphs.empty()) {
      continue; // Skip fonts without atlas or glyphs
    }

    // Create glyph buffer for this font
    WGPUBufferDescriptor glyphBufDesc = {};
    size_t bufferSize = pdfFont.glyphInstances.empty()
                            ? sizeof(PDFGlyphInstance)
                            : pdfFont.glyphInstances.size() * sizeof(PDFGlyphInstance);
    glyphBufDesc.size = bufferSize;
    glyphBufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    pdfFont.glyphBuffer = wgpuDeviceCreateBuffer(device, &glyphBufDesc);
    if (!pdfFont.glyphBuffer) {
      return Err<void>("Failed to create glyph buffer for font: " + pdfFont.name);
    }

    // Upload glyph data
    if (!pdfFont.glyphInstances.empty()) {
      wgpuQueueWriteBuffer(ctx.getQueue(), pdfFont.glyphBuffer, 0,
                           pdfFont.glyphInstances.data(),
                           pdfFont.glyphInstances.size() * sizeof(PDFGlyphInstance));
    }

    // Create bind group for this font
    WGPUBindGroupEntry bgEntries[4] = {};
    bgEntries[0].binding = 0;
    bgEntries[0].buffer = uniformBuffer_;
    bgEntries[0].size = 48;

    bgEntries[1].binding = 1;
    bgEntries[1].sampler = sampler_;

    bgEntries[2].binding = 2;
    bgEntries[2].textureView = pdfFont.textureView;

    bgEntries[3].binding = 3;
    bgEntries[3].buffer = pdfFont.glyphBuffer;
    bgEntries[3].size = bufferSize;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = textBindGroupLayout_;
    bgDesc.entryCount = 4;
    bgDesc.entries = bgEntries;
    pdfFont.bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);

    spdlog::debug("PDFLayer::createPipelines: created bind group for font '{}' with {} glyphs",
                  pdfFont.name, pdfFont.glyphCount);
  }

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

  WGPUBlendState blend = {};
  blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
  blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
  blend.color.operation = WGPUBlendOperation_Add;
  blend.alpha.srcFactor = WGPUBlendFactor_One;
  blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
  blend.alpha.operation = WGPUBlendOperation_Add;
  colorTarget.blend = &blend;

  fragState.targetCount = 1;
  fragState.targets = &colorTarget;
  pipelineDesc.fragment = &fragState;
  pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
  pipelineDesc.multisample.count = 1;
  pipelineDesc.multisample.mask = ~0u;

  textPipeline_ = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

  wgpuShaderModuleRelease(shaderModule);
  wgpuPipelineLayoutRelease(pipelineLayout);

  if (!textPipeline_)
    return Err<void>("Failed to create render pipeline");

  spdlog::info("PDFLayer: pipeline created with {} glyphs", glyphCount_);
  return Ok();
}

Result<void> PDFLayer::createImageTextures(WebGPUContext &ctx) {
  // TODO: Implement image extraction and texture creation
  return Ok();
}

//-----------------------------------------------------------------------------
// Render
//-----------------------------------------------------------------------------

Result<void> PDFLayer::render(WebGPUContext &ctx, WGPUTextureView targetView,
                              WGPUTextureFormat targetFormat,
                              uint32_t screenWidth, uint32_t screenHeight,
                              float pixelX, float pixelY, float pixelW,
                              float pixelH) {
  spdlog::debug("PDFLayer::render: screen={}x{}, rect=({},{}) {}x{}, glyphs={}",
                screenWidth, screenHeight, pixelX, pixelY, pixelW, pixelH, glyphCount_);
  if (failed_)
    return Err<void>("PDFLayer already failed");

  // Re-layout if view size changed
  if (lastViewWidth_ != pixelW || lastViewHeight_ != pixelH) {
    layoutPage(pixelW, pixelH);
    lastViewWidth_ = pixelW;
    lastViewHeight_ = pixelH;
    gpuInitialized_ = false;
  }

  if (!gpuInitialized_) {
    auto result = createPipelines(ctx, targetFormat);
    if (!result) {
      failed_ = true;
      return Err<void>("Failed to create pipeline", result);
    }
    gpuInitialized_ = true;
  }

  if (!textPipeline_ || !uniformBuffer_ || glyphCount_ == 0) {
    return Ok();
  }

  // Update uniforms
  float ndcX = (pixelX / screenWidth) * 2.0f - 1.0f;
  float ndcY = 1.0f - (pixelY / screenHeight) * 2.0f;

  struct Uniforms {
    float rect[4];
    float screenSize[2];
    float scrollOffset;
    float pixelRange;
    float padding[4];
  } uniforms;

  uniforms.rect[0] = ndcX;
  uniforms.rect[1] = ndcY;
  uniforms.rect[2] = pixelW;
  uniforms.rect[3] = pixelH;
  uniforms.screenSize[0] = static_cast<float>(screenWidth);
  uniforms.screenSize[1] = static_cast<float>(screenHeight);
  uniforms.scrollOffset = scrollOffset_;
  // Scale pixelRange by average font scale for proper MSDF edge rendering
  // Compute scale from view size and page size
  float pageWidth = pages_.empty() ? 612.0f : pages_[0].width;
  float docScale = (pixelW / pageWidth) * zoom_;
  float avgFontScale = (12.0f * docScale) / 64.0f;
  uniforms.pixelRange = pixelRange_ * avgFontScale;

  spdlog::debug("PDFLayer::render: uniforms ndc=({},{}), screenSize=({},{}), scrollOffset={}, pixelRange={}",
                uniforms.rect[0], uniforms.rect[1], uniforms.screenSize[0], uniforms.screenSize[1],
                uniforms.scrollOffset, uniforms.pixelRange);

  wgpuQueueWriteBuffer(ctx.getQueue(), uniformBuffer_, 0, &uniforms,
                       sizeof(uniforms));

  // Render
  WGPUCommandEncoderDescriptor encoderDesc = {};
  WGPUCommandEncoder encoder =
      wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
  if (!encoder)
    return Err<void>("Failed to create command encoder");

  WGPURenderPassColorAttachment colorAttachment = {};
  colorAttachment.view = targetView;
  colorAttachment.loadOp = WGPULoadOp_Load;
  colorAttachment.storeOp = WGPUStoreOp_Store;
  colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

  WGPURenderPassDescriptor passDesc = {};
  passDesc.colorAttachmentCount = 1;
  passDesc.colorAttachments = &colorAttachment;

  WGPURenderPassEncoder pass =
      wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
  if (!pass) {
    wgpuCommandEncoderRelease(encoder);
    return Err<void>("Failed to begin render pass");
  }

  // Set scissor rect to clip content to plugin bounds
  spdlog::debug("PDFLayer::render: scissor rect ({},{}) {}x{}",
                static_cast<uint32_t>(pixelX), static_cast<uint32_t>(pixelY),
                static_cast<uint32_t>(pixelW), static_cast<uint32_t>(pixelH));
  wgpuRenderPassEncoderSetScissorRect(
      pass, static_cast<uint32_t>(pixelX), static_cast<uint32_t>(pixelY),
      static_cast<uint32_t>(pixelW), static_cast<uint32_t>(pixelH));

  wgpuRenderPassEncoderSetPipeline(pass, textPipeline_);

  // Draw each font's glyphs with its own bind group
  for (const auto &pdfFont : fonts_) {
    if (!pdfFont.bindGroup || pdfFont.glyphCount == 0) {
      continue;
    }
    wgpuRenderPassEncoderSetBindGroup(pass, 0, pdfFont.bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, pdfFont.glyphCount, 0, 0);
  }

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

//-----------------------------------------------------------------------------
// Mouse Scroll
//-----------------------------------------------------------------------------

bool PDFLayer::onMouseScroll(float xoffset, float yoffset, int mods) {
  (void)xoffset;

  // Ctrl + scroll = zoom (GLFW_MOD_CONTROL = 0x0002)
  if (mods & 0x0002) {
    float zoomFactor = (yoffset > 0) ? 1.1f : 0.9f;
    zoom_ *= zoomFactor;
    zoom_ = std::clamp(zoom_, 0.1f, 10.0f);
    // Force re-layout by resetting view size tracking
    lastViewWidth_ = 0;
    lastViewHeight_ = 0;
    gpuInitialized_ = false;
    spdlog::debug("PDFLayer: zoom changed to {}", zoom_);
    return true;
  }

  // Regular scroll
  float scrollAmount = yoffset * 40.0f; // Scroll speed
  scrollOffset_ -= scrollAmount;

  // Clamp scroll
  float maxScroll =
      std::max(0.0f, documentHeight_ - static_cast<float>(_pixel_height));
  scrollOffset_ = std::clamp(scrollOffset_, 0.0f, maxScroll);

  return true;
}

//-----------------------------------------------------------------------------
// Keyboard (Page Navigation)
//-----------------------------------------------------------------------------

bool PDFLayer::onKey(int key, int scancode, int action, int mods) {
  (void)scancode;
  (void)mods;

  if (action != 1)
    return false; // GLFW_PRESS

  // Page Up/Down for navigation
  if (key == 266) { // GLFW_KEY_PAGE_UP
    if (currentPage_ > 0) {
      extractPageContent(currentPage_ - 1);
      scrollOffset_ = 0;
      gpuInitialized_ = false;
      return true;
    }
  } else if (key == 267) { // GLFW_KEY_PAGE_DOWN
    if (currentPage_ < pageCount_ - 1) {
      extractPageContent(currentPage_ + 1);
      scrollOffset_ = 0;
      gpuInitialized_ = false;
      return true;
    }
  }

  // Zoom with +/-
  if (key == 61 || key == 334) { // = or numpad +
    zoom_ *= 1.1f;
    gpuInitialized_ = false;
    return true;
  } else if (key == 45 || key == 333) { // - or numpad -
    zoom_ *= 0.9f;
    zoom_ = std::max(0.1f, zoom_);
    gpuInitialized_ = false;
    return true;
  }

  return false;
}

} // namespace yetty

extern "C" {
const char* name() { return "pdf"; }
yetty::Result<yetty::PluginPtr> create() {
  return yetty::PDFPlugin::create();
}
}
