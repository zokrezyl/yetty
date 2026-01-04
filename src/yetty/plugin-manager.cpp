#include "yetty/plugin-manager.h"
#include "yetty/grid.h"
#include <yetty/webgpu-context.h>
#include <yetty/wgpu-compat.h>
#include <spdlog/spdlog.h>
#include <iostream>
#include <filesystem>
#include <dlfcn.h>

namespace yetty {

Result<PluginManager::Ptr> PluginManager::create() noexcept {
    auto mgr = Ptr(new PluginManager());
    if (auto res = mgr->init(); !res) {
        return Err<Ptr>("Failed to initialize PluginManager", res);
    }
    return Ok(std::move(mgr));
}

PluginManager::PluginManager() noexcept = default;

Result<void> PluginManager::init() noexcept {
    // Nothing to initialize currently
    return Ok();
}

PluginManager::~PluginManager() {
    // Dispose all plugins (which disposes their layers)
    for (auto& [name, plugin] : plugins_) {
        if (plugin) (void)plugin->dispose();
    }
    plugins_.clear();
    pluginMetas_.clear();

    // Close dynamic library handles
    for (void* handle : handles_) {
        if (handle) dlclose(handle);
    }
    handles_.clear();

    // Clean up frame renderer resources
    if (frameBindGroup_) wgpuBindGroupRelease(frameBindGroup_);
    if (frameBindGroupLayout_) wgpuBindGroupLayoutRelease(frameBindGroupLayout_);
    if (frameUniformBuffer_) wgpuBufferRelease(frameUniformBuffer_);
    if (framePipeline_) wgpuRenderPipelineRelease(framePipeline_);
}

void PluginManager::registerPlugin(const std::string& name, BuiltinPluginFactory factory) {
    PluginMeta meta;
    meta._name = name;
    meta._factory = std::move(factory);
    pluginMetas_[name] = std::move(meta);
    std::cout << "Registered built-in plugin: " << name << std::endl;
}

void PluginManager::loadPluginsFromDirectory(const std::string& path) {
    namespace fs = std::filesystem;

    if (!fs::exists(path) || !fs::is_directory(path)) {
        std::cerr << "Plugin directory not found: " << path << std::endl;
        return;
    }

    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_regular_file()) {
            const auto& filePath = entry.path();
            std::string ext = filePath.extension().string();

#ifdef __APPLE__
            if (ext != ".dylib") continue;
#else
            if (ext != ".so") continue;
#endif

            void* handle = dlopen(filePath.c_str(), RTLD_LAZY);
            if (!handle) {
                std::cerr << "Failed to load plugin " << filePath << ": " << dlerror() << std::endl;
                continue;
            }

            auto nameFn = reinterpret_cast<PluginNameFn>(dlsym(handle, "name"));
            auto createFn = reinterpret_cast<PluginCreateFn>(dlsym(handle, "create"));

            if (!nameFn || !createFn) {
                std::cerr << "Plugin " << filePath << " missing name() or create() symbol" << std::endl;
                dlclose(handle);
                continue;
            }

            PluginMeta meta;
            meta._name = nameFn();
            meta._handle = handle;
            meta._createFn = createFn;

            std::cout << "Loaded plugin: " << meta._name << " from " << filePath << std::endl;
            pluginMetas_[meta._name] = std::move(meta);
            handles_.push_back(handle);
        }
    }
}

Result<PluginPtr> PluginManager::getOrCreatePlugin(const std::string& name) {
    // Check if already created
    auto it = plugins_.find(name);
    if (it != plugins_.end()) {
        return Ok(it->second);
    }

    // Find meta
    auto metaIt = pluginMetas_.find(name);
    if (metaIt == pluginMetas_.end()) {
        return Err<PluginPtr>("Unknown plugin: " + name);
    }

    const auto& meta = metaIt->second;
    Result<PluginPtr> result;

    if (meta._factory) {
        result = meta._factory(engine_);
    } else if (meta._createFn) {
        result = meta._createFn(engine_);
    } else {
        return Err<PluginPtr>("Plugin has no factory: " + name);
    }

    if (!result) {
        return Err<PluginPtr>("Failed to create plugin: " + name, result);
    }

    PluginPtr plugin = *result;
    plugin->setFont(font_);  // Pass font for text-rendering plugins

    // Plugin is already initialized by create()
    plugins_[name] = plugin;
    return Ok(plugin);
}

Result<PluginLayerPtr> PluginManager::createLayer(const std::string& pluginName,
                                                   PositionMode mode,
                                                   int32_t x, int32_t y,
                                                   int32_t widthCells, int32_t heightCells,
                                                   const std::string& payload,
                                                   Grid* grid,
                                                   uint32_t cellWidth, uint32_t cellHeight) {
    // Get or create the plugin
    auto pluginResult = getOrCreatePlugin(pluginName);
    if (!pluginResult) {
        return Err<PluginLayerPtr>("Failed to get plugin", pluginResult);
    }
    PluginPtr plugin = *pluginResult;

    // Create the layer
    auto layerResult = plugin->createLayer(payload);
    if (!layerResult) {
        return Err<PluginLayerPtr>("Failed to create layer", layerResult);
    }
    PluginLayerPtr layer = *layerResult;

    // Resolve flexible dimensions using terminal size
    uint32_t termCols = grid ? grid->getCols() : 80;
    uint32_t termRows = grid ? grid->getRows() : 24;

    uint32_t finalWidth, finalHeight;

    // Width: 0 = stretch to edge, negative = termCols - abs(value)
    if (widthCells == 0) {
        finalWidth = (x >= 0 && static_cast<uint32_t>(x) < termCols) ? termCols - x : 1;
    } else if (widthCells < 0) {
        int32_t w = static_cast<int32_t>(termCols) + widthCells;  // termCols - abs(widthCells)
        finalWidth = (w > 0) ? static_cast<uint32_t>(w) : 1;
    } else {
        finalWidth = static_cast<uint32_t>(widthCells);
    }

    // Height: 0 = stretch to edge, negative = termRows - abs(value)
    if (heightCells == 0) {
        finalHeight = (y >= 0 && static_cast<uint32_t>(y) < termRows) ? termRows - y : 1;
    } else if (heightCells < 0) {
        int32_t h = static_cast<int32_t>(termRows) + heightCells;  // termRows - abs(heightCells)
        finalHeight = (h > 0) ? static_cast<uint32_t>(h) : 1;
    } else {
        finalHeight = static_cast<uint32_t>(heightCells);
    }

    // Configure the layer
    layer->setId(nextLayerId_++);
    layer->setPositionMode(mode);
    layer->setScreenType(isAltScreen_ ? ScreenType::Alternate : ScreenType::Main);
    layer->setPosition(x, y);
    layer->setCellSize(finalWidth, finalHeight);
    layer->setPixelSize(finalWidth * cellWidth, finalHeight * cellHeight);

    // Add to plugin
    plugin->addLayer(layer);

    // Mark grid cells
    if (grid) {
        markGridCells(grid, layer.get());
    }

    return Ok(layer);
}

Result<void> PluginManager::updateLayer(uint32_t id, const std::string& payload) {
    PluginLayerPtr layer = getLayer(id);
    if (!layer) {
        return Err<void>("Layer not found: " + std::to_string(id));
    }

    // Re-init
    if (auto res = layer->dispose(); !res) {
        return Err<void>("Failed to dispose layer during update", res);
    }
    if (auto res = layer->init(payload); !res) {
        return Err<void>("Layer re-init failed", res);
    }

    return Ok();
}

Result<void> PluginManager::removeLayer(uint32_t id, Grid* grid) {
    for (auto& [name, plugin] : plugins_) {
        PluginLayerPtr layer = plugin->getLayer(id);
        if (layer) {
            if (grid) {
                clearGridCells(grid, layer.get());
            }
            if (auto res = plugin->removeLayer(id); !res) {
                return Err<void>("Failed to remove layer from plugin", res);
            }
            return Ok();
        }
    }
    return Err<void>("Layer not found: " + std::to_string(id));
}

PluginLayerPtr PluginManager::getLayer(uint32_t id) {
    for (auto& [name, plugin] : plugins_) {
        if (auto layer = plugin->getLayer(id)) {
            return layer;
        }
    }
    return nullptr;
}

std::vector<PluginLayerPtr> PluginManager::getAllLayers() const {
    std::vector<PluginLayerPtr> layers;
    for (auto& [name, plugin] : plugins_) {
        for (auto& layer : plugin->getLayers()) {
            layers.push_back(layer);
        }
    }
    return layers;
}

// Helper functions for parsing
static size_t findNthSemicolon(const std::string& s, int n) {
    size_t pos = 0;
    for (int i = 0; i < n; i++) {
        pos = s.find(';', pos);
        if (pos == std::string::npos) return pos;
        pos++;
    }
    return pos;
}

static std::string getField(const std::string& s, int fieldIndex) {
    size_t start = (fieldIndex == 0) ? 0 : findNthSemicolon(s, fieldIndex);
    if (start == std::string::npos) return "";
    size_t end = s.find(';', start);
    if (end == std::string::npos) return s.substr(start);
    return s.substr(start, end - start);
}

bool PluginManager::handleOSCSequence(const std::string& sequence,
                                       Grid* grid,
                                       int cursorCol, int cursorRow,
                                       uint32_t cellWidth, uint32_t cellHeight,
                                       std::string* response,
                                       uint32_t* linesToAdvance) {
    std::string vendorStr = getField(sequence, 0);
    if (vendorStr.empty()) return false;

    int vendorId = 0;
    try {
        vendorId = std::stoi(vendorStr);
    } catch (...) {
        return false;
    }

    if (vendorId != YETTY_OSC_VENDOR_ID) return false;

    std::string pluginId = getField(sequence, 1);
    std::string mode = getField(sequence, 2);

    if (pluginId.empty() || mode.empty()) return false;

    if (mode == "D") {
        std::string idStr = getField(sequence, 3);
        if (idStr.empty()) return false;
        uint32_t layerId = std::stoul(idStr);
        auto result = removeLayer(layerId, grid);
        if (!result) {
            std::cerr << "PluginManager: " << error_msg(result) << std::endl;
            return false;
        }
        return true;
    }

    if (mode == "U") {
        std::string idStr = getField(sequence, 3);
        if (idStr.empty()) return false;
        uint32_t layerId = std::stoul(idStr);

        size_t payloadStart = findNthSemicolon(sequence, 4);
        std::string encodedPayload = (payloadStart != std::string::npos)
            ? sequence.substr(payloadStart) : "";
        std::string payload = base94Decode(encodedPayload);
        auto result = updateLayer(layerId, payload);
        if (!result) {
            std::cerr << "PluginManager: " << error_msg(result) << std::endl;
            return false;
        }
        return true;
    }

    if (mode == "Q") {
        if (response) {
            std::string result = "\033]";
            result += std::to_string(YETTY_OSC_VENDOR_ID);
            result += ";!;Q";
            for (auto& layer : getAllLayers()) {
                result += ";";
                result += std::to_string(layer->id());
                result += ",";
                result += layer->getParent()->pluginName();
                result += ",";
                result += std::to_string(layer->getX());
                result += ",";
                result += std::to_string(layer->getY());
                result += ",";
                result += std::to_string(layer->getWidthCells());
                result += ",";
                result += std::to_string(layer->getHeightCells());
            }
            result += "\033\\";
            *response = result;
        }
        return true;
    }

    if (mode == "A" || mode == "R") {
        std::string xStr = getField(sequence, 3);
        std::string yStr = getField(sequence, 4);
        std::string wStr = getField(sequence, 5);
        std::string hStr = getField(sequence, 6);

        if (xStr.empty() || yStr.empty() || wStr.empty() || hStr.empty()) {
            return false;
        }

        PositionMode posMode = (mode == "A") ? PositionMode::Absolute : PositionMode::Relative;
        int32_t x = std::stoi(xStr);
        int32_t y = std::stoi(yStr);
        int32_t w = std::stoi(wStr);  // 0 = stretch to edge, negative = termSize - abs(value)
        int32_t h = std::stoi(hStr);

        size_t payloadStart = findNthSemicolon(sequence, 7);
        std::string encodedPayload = (payloadStart != std::string::npos)
            ? sequence.substr(payloadStart) : "";
        std::string payload = base94Decode(encodedPayload);

        if (posMode == PositionMode::Relative) {
            x += cursorCol;
            y += cursorRow;
        }

        auto result = createLayer(pluginId, posMode, x, y, w, h, payload,
                                  grid, cellWidth, cellHeight);
        if (!result) {
            std::cerr << "PluginManager: " << error_msg(result) << std::endl;
            return false;
        }
        std::cout << "Created layer id=" << (*result)->id() << " plugin=" << pluginId
                  << " at (" << x << "," << y << ") size " << w << "x" << h << std::endl;

        if (linesToAdvance && posMode == PositionMode::Relative) {
            *linesToAdvance = h;
        }
        return true;
    }

    return false;
}

Result<void> PluginManager::update(double deltaTime) {
    // Plugins no longer have update() - they render directly
    (void)deltaTime;
    return Ok();
}

Result<void> PluginManager::render(WebGPUContext& ctx, WGPUTextureView targetView,
                                    uint32_t screenWidth, uint32_t screenHeight,
                                    float cellWidth, float cellHeight,
                                    int scrollOffset, uint32_t termRows) {
    if (!targetView) return Err<void>("PluginManager::render: targetView is null");

    // Early return if no layers to render
    if (getAllLayers().empty()) {
        return Ok();
    }

    // Initialize frame renderer on first use
    if (!frameRendererInitialized_) {
        if (auto res = initFrameRenderer(ctx.getDevice(), ctx.getSurfaceFormat()); !res) {
            return Err<void>("Failed to init frame renderer", res);
        }
        frameRendererInitialized_ = true;
    }

    // DEBUG: Skip frame rendering - causes separate GPU submits per layer
    // TODO: Batch frame rendering or make it configurable
    constexpr bool RENDER_DEBUG_FRAMES = false;

    constexpr float framePadding = 2.0f;  // Pixels larger than layer bounds

    ScreenType currentScreen = isAltScreen_ ? ScreenType::Alternate : ScreenType::Main;

    for (auto& [name, plugin] : plugins_) {
        // Plugin is already initialized by create()

        // Render debug frame for each layer (disabled by default - expensive!)
        if (RENDER_DEBUG_FRAMES) {
            for (auto& layer : plugin->getLayers()) {
                if (!layer->isVisible()) continue;

                // Skip layers that belong to a different screen
                if (layer->getScreenType() != currentScreen) continue;

                float pixelX = layer->getX() * cellWidth;
                float pixelY = layer->getY() * cellHeight;
                float pixelW = layer->getWidthCells() * cellWidth;
                float pixelH = layer->getHeightCells() * cellHeight;

                // Adjust for scroll offset
                if (layer->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
                    pixelY += scrollOffset * cellHeight;
                }

                // Skip if off-screen
                if (termRows > 0) {
                    float screenPixelHeight = termRows * cellHeight;
                    if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
                        continue;
                    }
                }

                // Determine frame color based on state
                float r, g, b, a;
                if (layer == focusedLayer_) {
                    r = 0.0f; g = 1.0f; b = 0.0f; a = 1.0f;  // Green for focused
                } else if (layer == hoveredLayer_) {
                    r = 1.0f; g = 1.0f; b = 0.0f; a = 1.0f;  // Yellow for hovered
                } else {
                    r = 0.3f; g = 0.5f; b = 0.7f; a = 0.8f;  // Dim cyan for default
                }

                // Render frame slightly larger than layer
                renderFrame(ctx, targetView, screenWidth, screenHeight,
                            pixelX - framePadding, pixelY - framePadding,
                            pixelW + framePadding * 2, pixelH + framePadding * 2,
                            r, g, b, a);
            }
        }

        // Set RenderContext for each layer (needed for position calculations)
        for (auto& layer : plugin->getLayers()) {
            RenderContext rc;
            rc.targetView = targetView;
            rc.targetFormat = ctx.getSurfaceFormat();
            rc.screenWidth = screenWidth;
            rc.screenHeight = screenHeight;
            rc.cellWidth = cellWidth;
            rc.cellHeight = cellHeight;
            rc.scrollOffset = scrollOffset;
            rc.termRows = termRows;
            rc.isAltScreen = isAltScreen_;
            rc.deltaTime = 0.016;  // ~60fps default
            layer->setRenderContext(rc);
        }
    }

    // Create ONE command encoder for ALL plugins
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) {
        return Err<void>("PluginManager: Failed to create command encoder");
    }

    // Begin ONE render pass for ALL plugins
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;  // Preserve terminal content
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return Err<void>("PluginManager: Failed to begin render pass");
    }

    // Render ALL plugin layers in this single pass
    for (auto& [name, plugin] : plugins_) {
        for (auto& layer : plugin->getLayers()) {
            if (!layer->isVisible()) continue;
            if (layer->getScreenType() != currentScreen) continue;

            // Use batched renderToPass - just draws, no encoder/submit overhead!
            layer->renderToPass(pass, ctx);
        }
    }

    // End pass and submit ONCE
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

Result<void> PluginManager::onTerminalResize(uint32_t newCols, uint32_t newRows,
                                              uint32_t cellWidth, uint32_t cellHeight) {
    (void)newCols;
    (void)newRows;

    for (auto& [name, plugin] : plugins_) {
        if (auto res = plugin->onTerminalResize(cellWidth, cellHeight); !res) {
            return Err<void>("Failed to resize plugin " + name, res);
        }
    }
    return Ok();
}

void PluginManager::onScroll(int lines, Grid* grid) {
    for (auto& [name, plugin] : plugins_) {
        for (auto& layer : plugin->getLayers()) {
            if (layer->getPositionMode() == PositionMode::Relative) {
                if (grid) {
                    clearGridCells(grid, layer.get());
                }
                layer->setPosition(layer->getX(), layer->getY() - lines);
                if (grid) {
                    markGridCells(grid, layer.get());
                }
            }
        }
    }
}

void PluginManager::markGridCells(Grid* grid, PluginLayer* layer) {
    if (!grid || !layer) return;

    int32_t startX = layer->getX();
    int32_t startY = layer->getY();
    uint32_t w = layer->getWidthCells();
    uint32_t h = layer->getHeightCells();
    uint16_t id = static_cast<uint16_t>(layer->id());

    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            int32_t gridCol = startX + col;
            int32_t gridRow = startY + row;

            if (gridCol >= 0 && gridCol < (int32_t)grid->getCols() &&
                gridRow >= 0 && gridRow < (int32_t)grid->getRows()) {
                grid->setPluginId(gridCol, gridRow, id);
            }
        }
    }
}

void PluginManager::clearGridCells(Grid* grid, PluginLayer* layer) {
    if (!grid || !layer) return;

    int32_t startX = layer->getX();
    int32_t startY = layer->getY();
    uint32_t w = layer->getWidthCells();
    uint32_t h = layer->getHeightCells();

    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            int32_t gridCol = startX + col;
            int32_t gridRow = startY + row;

            if (gridCol >= 0 && gridCol < (int32_t)grid->getCols() &&
                gridRow >= 0 && gridRow < (int32_t)grid->getRows()) {
                grid->clearPluginId(gridCol, gridRow);
            }
        }
    }
}

std::string PluginManager::base94Decode(const std::string& encoded) {
    if (encoded.empty()) return "";

    const size_t len = encoded.size();
    std::string result;
    result.resize(len / 2);

    const char* src = encoded.data();
    char* dst = &result[0];
    size_t outIdx = 0;

    for (size_t i = 0; i + 1 < len; i += 2) {
        unsigned char c1 = src[i] - '!';
        unsigned char c2 = src[i + 1] - '!';
        dst[outIdx++] = static_cast<char>(c1 * 94 + c2);
    }

    result.resize(outIdx);
    return result;
}

std::string PluginManager::base94Encode(const std::string& data) {
    std::string result;
    result.reserve(data.size() * 2);

    for (unsigned char byte : data) {
        int value = byte;
        char c1 = '!' + (value / 94);
        char c2 = '!' + (value % 94);
        result.push_back(c1);
        result.push_back(c2);
    }

    return result;
}

//-----------------------------------------------------------------------------
// Input routing
//-----------------------------------------------------------------------------

PluginLayerPtr PluginManager::layerAtCell(int col, int row, const Grid* grid) {
    if (!grid) return nullptr;
    if (col < 0 || row < 0) return nullptr;
    if ((uint32_t)col >= grid->getCols() || (uint32_t)row >= grid->getRows()) return nullptr;

    uint16_t layerId = grid->getPluginId(col, row);
    if (layerId == 0) return nullptr;

    spdlog::debug("layerAtCell({},{}): FOUND layer ID={}", col, row, layerId);
    return getLayer(layerId);
}

void PluginManager::clearFocus() {
    if (focusedLayer_) {
        focusedLayer_->setFocus(false);
        focusedLayer_ = nullptr;
    }
}

void PluginManager::onAltScreenChange(bool isAltScreen) {
    isAltScreen_ = isAltScreen;
    // Clear focus when switching screens - focused layer may not be visible
    if (focusedLayer_) {
        ScreenType currentScreen = isAltScreen ? ScreenType::Alternate : ScreenType::Main;
        if (focusedLayer_->getScreenType() != currentScreen) {
            clearFocus();
        }
    }
    // Also clear hovered layer
    hoveredLayer_ = nullptr;
}

bool PluginManager::onMouseMove(float pixelX, float pixelY, const Grid* grid,
                                 float cellWidth, float cellHeight, int scrollOffset) {
    lastMouseX_ = pixelX;
    lastMouseY_ = pixelY;

    int col = static_cast<int>(pixelX / cellWidth);
    int row = static_cast<int>(pixelY / cellHeight);
    int logicalRow = row - scrollOffset;

    PluginLayerPtr layer = layerAtCell(col, logicalRow, grid);

    // Track hovered layer for debug frame coloring
    hoveredLayer_ = layer;

    if (layer && layer->wantsMouse()) {
        float layerPixelX = layer->getX() * cellWidth;
        float layerPixelY = layer->getY() * cellHeight;

        if (layer->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
            layerPixelY += scrollOffset * cellHeight;
        }

        float localX = pixelX - layerPixelX;
        float localY = pixelY - layerPixelY;

        return layer->onMouseMove(localX, localY);
    }

    return false;
}

bool PluginManager::onMouseButton(int button, bool pressed, float pixelX, float pixelY,
                                   const Grid* grid, float cellWidth, float cellHeight, int scrollOffset) {
    int col = static_cast<int>(pixelX / cellWidth);
    int row = static_cast<int>(pixelY / cellHeight);
    int logicalRow = row - scrollOffset;

    PluginLayerPtr layer = layerAtCell(col, logicalRow, grid);

    // Handle focus changes on left click
    if (pressed && button == 0) {
        if (layer != focusedLayer_) {
            if (focusedLayer_) {
                focusedLayer_->onMouseButton(-1, false);
            }
            clearFocus();
            if (layer && (layer->wantsKeyboard() || layer->wantsMouse())) {
                focusedLayer_ = layer;
                layer->setFocus(true);
            }
        }
    }

    if (layer && layer->wantsMouse()) {
        float layerPixelX = layer->getX() * cellWidth;
        float layerPixelY = layer->getY() * cellHeight;

        if (layer->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
            layerPixelY += scrollOffset * cellHeight;
        }

        float localX = pixelX - layerPixelX;
        float localY = pixelY - layerPixelY;

        layer->onMouseMove(localX, localY);
        return layer->onMouseButton(button, pressed);
    }

    return false;
}

bool PluginManager::onMouseScroll(float xoffset, float yoffset, int mods, float pixelX, float pixelY,
                                   const Grid* grid, float cellWidth, float cellHeight, int scrollOffset) {
    int col = static_cast<int>(pixelX / cellWidth);
    int row = static_cast<int>(pixelY / cellHeight);
    int logicalRow = row - scrollOffset;

    PluginLayerPtr layer = layerAtCell(col, logicalRow, grid);
    if (layer && layer->wantsMouse()) {
        return layer->onMouseScroll(xoffset, yoffset, mods);
    }

    return false;
}

bool PluginManager::onKey(int key, int scancode, int action, int mods) {
    if (focusedLayer_ && focusedLayer_->wantsKeyboard()) {
        return focusedLayer_->onKey(key, scancode, action, mods);
    }
    return false;
}

bool PluginManager::onChar(unsigned int codepoint) {
    if (focusedLayer_ && focusedLayer_->wantsKeyboard()) {
        return focusedLayer_->onChar(codepoint);
    }
    return false;
}

//-----------------------------------------------------------------------------
// Debug frame rendering
//-----------------------------------------------------------------------------

Result<void> PluginManager::initFrameRenderer(WGPUDevice device, WGPUTextureFormat format) {
    // Shader that draws a thin rectangular outline
    const char* shaderCode = R"(
struct Uniforms {
    rect: vec4<f32>,  // x, y, w, h in NDC
    color: vec4<f32>,
    screenSize: vec2<f32>,
    thickness: f32,
    _pad: f32,
}

@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> VertexOutput {
    // Full-screen quad vertices
    var positions = array<vec2<f32>, 6>(
        vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 0.0), vec2<f32>(1.0, 1.0),
        vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 1.0), vec2<f32>(0.0, 1.0)
    );
    let pos = positions[vi];

    var out: VertexOutput;
    out.position = vec4<f32>(
        u.rect.x + pos.x * u.rect.z,
        u.rect.y - pos.y * u.rect.w,
        0.0, 1.0
    );
    out.uv = pos;
    return out;
}

@fragment fn fs_main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    // Calculate pixel position within rect
    let pixelW = u.rect.z * u.screenSize.x * 0.5;
    let pixelH = u.rect.w * u.screenSize.y * 0.5;
    let pixelX = uv.x * pixelW;
    let pixelY = uv.y * pixelH;

    // Check if pixel is on the border
    let t = u.thickness;
    let onBorder = pixelX < t || pixelX > pixelW - t ||
                   pixelY < t || pixelY > pixelH - t;

    if (onBorder) {
        return u.color;
    }
    discard;
}
)";

    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPU_STR(shaderCode);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!shaderModule) {
        return Err<void>("Failed to create frame shader module");
    }

    // Create uniform buffer (rect + color + screenSize + thickness)
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 48;  // 4 floats rect + 4 floats color + 2 floats screen + 1 float thickness + 1 pad = 12 floats = 48 bytes
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    frameUniformBuffer_ = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!frameUniformBuffer_) {
        wgpuShaderModuleRelease(shaderModule);
        return Err<void>("Failed to create frame uniform buffer");
    }

    // Bind group layout
    WGPUBindGroupLayoutEntry entry = {};
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entry.buffer.type = WGPUBufferBindingType_Uniform;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 1;
    bglDesc.entries = &entry;
    frameBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    if (!frameBindGroupLayout_) {
        wgpuShaderModuleRelease(shaderModule);
        return Err<void>("Failed to create frame bind group layout");
    }

    // Bind group
    WGPUBindGroupEntry bgEntry = {};
    bgEntry.binding = 0;
    bgEntry.buffer = frameUniformBuffer_;
    bgEntry.size = 48;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = frameBindGroupLayout_;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    frameBindGroup_ = wgpuDeviceCreateBindGroup(device, &bgDesc);
    if (!frameBindGroup_) {
        wgpuShaderModuleRelease(shaderModule);
        return Err<void>("Failed to create frame bind group");
    }

    // Pipeline layout
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &frameBindGroupLayout_;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = WGPU_STR("vs_main");

    WGPUBlendState blend = {};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = format;
    colorTarget.blend = &blend;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragState = {};
    fragState.module = shaderModule;
    fragState.entryPoint = WGPU_STR("fs_main");
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    framePipeline_ = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!framePipeline_) {
        return Err<void>("Failed to create frame render pipeline");
    }

    std::cout << "PluginManager: frame renderer initialized" << std::endl;
    return Ok();
}

void PluginManager::renderFrame(WebGPUContext& ctx, WGPUTextureView targetView,
                                 uint32_t screenWidth, uint32_t screenHeight,
                                 float x, float y, float w, float h,
                                 float r, float g, float b, float a) {
    if (!framePipeline_ || !frameUniformBuffer_ || !frameBindGroup_) return;

    // Convert pixel coords to NDC
    float ndcX = (x / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (y / screenHeight) * 2.0f;
    float ndcW = (w / screenWidth) * 2.0f;
    float ndcH = (h / screenHeight) * 2.0f;

    // Uniform data: rect, color, screenSize, thickness
    struct Uniforms {
        float rect[4];
        float color[4];
        float screenSize[2];
        float thickness;
        float pad;
    } uniforms;

    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;
    uniforms.color[0] = r;
    uniforms.color[1] = g;
    uniforms.color[2] = b;
    uniforms.color[3] = a;
    uniforms.screenSize[0] = static_cast<float>(screenWidth);
    uniforms.screenSize[1] = static_cast<float>(screenHeight);
    uniforms.thickness = 1.5f;  // Border thickness in pixels
    uniforms.pad = 0.0f;

    wgpuQueueWriteBuffer(ctx.getQueue(), frameUniformBuffer_, 0, &uniforms, sizeof(uniforms));

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) return;

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;  // v27: required for 2D textures

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return;
    }

    wgpuRenderPassEncoderSetPipeline(pass, framePipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, frameBindGroup_, 0, nullptr);
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
}

//-----------------------------------------------------------------------------
// Custom Glyph Plugin Support
//-----------------------------------------------------------------------------

void PluginManager::registerCustomGlyphPlugin(CustomGlyphPluginPtr plugin) {
    if (!plugin) return;

    customGlyphPlugins_.push_back(plugin);
    spdlog::info("Registered custom glyph plugin: {} with {} codepoint ranges",
                 plugin->pluginName(), plugin->getCodepointRanges().size());
}

CustomGlyphPluginPtr PluginManager::getCustomGlyphPluginForCodepoint(uint32_t codepoint) {
    for (auto& plugin : customGlyphPlugins_) {
        if (plugin->handlesCodepoint(codepoint)) {
            return plugin;
        }
    }
    return nullptr;
}

uint16_t PluginManager::onCellSync(uint32_t col, uint32_t row, uint32_t codepoint, uint32_t widthCells) {
    // Check if we already have a custom glyph at this position
    uint64_t posKey = makePositionKey(col, row);
    auto existingIt = customGlyphPositions_.find(posKey);
    if (existingIt != customGlyphPositions_.end()) {
        // Already have a custom glyph here - remove the old layer first
        onCellClear(col, row);
    }

    // Check if this codepoint should be rendered by a custom glyph plugin
    auto plugin = getCustomGlyphPluginForCodepoint(codepoint);
    if (!plugin) {
        return 0;  // No custom glyph for this codepoint
    }

    // Create a layer for this codepoint
    auto layerResult = plugin->createLayer(codepoint);
    if (!layerResult) {
        spdlog::warn("Failed to create custom glyph layer for U+{:04X}: {}",
                     codepoint, error_msg(layerResult));
        return 0;
    }

    auto layer = *layerResult;
    layer->setPosition(col, row);
    layer->setCellSize(widthCells, 1);
    plugin->addLayer(layer);

    // Assign a reserved glyph index
    uint16_t glyphIndex = nextCustomGlyphIndex_++;
    if (nextCustomGlyphIndex_ > GLYPH_CUSTOM_END) {
        // Wrap around (unlikely to ever happen)
        nextCustomGlyphIndex_ = GLYPH_CUSTOM_START;
    }

    customGlyphPositions_[posKey] = glyphIndex;

    spdlog::debug("Created custom glyph layer for U+{:04X} at ({},{}) -> index 0x{:04X}",
                  codepoint, col, row, glyphIndex);

    return glyphIndex;
}

void PluginManager::onCellClear(uint32_t col, uint32_t row) {
    uint64_t posKey = makePositionKey(col, row);
    auto it = customGlyphPositions_.find(posKey);
    if (it == customGlyphPositions_.end()) {
        return;  // No custom glyph at this position
    }

    // Find and remove the layer from its plugin
    for (auto& plugin : customGlyphPlugins_) {
        auto layer = plugin->getLayerAt(col, row);
        if (layer) {
            plugin->removeLayerAt(col, row);
            spdlog::debug("Removed custom glyph layer at ({},{})", col, row);
            break;
        }
    }

    customGlyphPositions_.erase(it);
}

Result<void> PluginManager::renderCustomGlyphs(WebGPUContext& ctx, WGPUTextureView targetView,
                                                uint32_t screenWidth, uint32_t screenHeight,
                                                float cellWidth, float cellHeight,
                                                int scrollOffset) {
    spdlog::debug("renderCustomGlyphs: {} plugins registered", customGlyphPlugins_.size());

    for (auto& plugin : customGlyphPlugins_) {
        // Lazy init plugin if needed
        if (!plugin->isInitialized()) {
            spdlog::info("Initializing custom glyph plugin: {}", plugin->pluginName());
            if (auto res = plugin->init(&ctx); !res) {
                spdlog::error("Failed to init custom glyph plugin {}: {}",
                             plugin->pluginName(), error_msg(res));
                continue;
            }
            spdlog::info("Custom glyph plugin {} initialized successfully", plugin->pluginName());
        }

        spdlog::debug("Rendering custom glyph plugin {} with {} layers",
                     plugin->pluginName(), plugin->getLayers().size());

        if (auto res = plugin->renderAll(targetView, ctx.getSurfaceFormat(),
                                          screenWidth, screenHeight,
                                          cellWidth, cellHeight, scrollOffset); !res) {
            spdlog::warn("Custom glyph plugin {} render failed: {}",
                        plugin->pluginName(), error_msg(res));
        }
    }

    return Ok();
}

void PluginManager::updateCustomGlyphs(double deltaTime) {
    for (auto& plugin : customGlyphPlugins_) {
        plugin->update(deltaTime);
    }
}

} // namespace yetty
