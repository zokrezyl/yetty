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
    pluginRegistry_.clear();

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
    PluginRegistryEntry regEntry;
    regEntry.name = name;
    regEntry.factory = std::move(factory);
    pluginRegistry_[name] = std::move(regEntry);
    std::cout << "Registered built-in plugin: " << name << std::endl;
}

void PluginManager::loadPluginsFromDirectory(const std::string& path) {
    namespace fs = std::filesystem;

    if (!fs::exists(path) || !fs::is_directory(path)) {
        std::cerr << "Plugin directory not found: " << path << std::endl;
        return;
    }

    for (const auto& dirEntry : fs::directory_iterator(path)) {
        if (dirEntry.is_regular_file()) {
            const auto& filePath = dirEntry.path();
            std::string ext = filePath.extension().string();

#ifdef __APPLE__
            if (ext != ".dylib") continue;
#else
            if (ext != ".so") continue;
#endif

            // Use RTLD_GLOBAL for python plugin so libpython symbols are available to numpy extensions
            int flags = RTLD_LAZY;
            std::string filename = filePath.filename().string();
            if (filename.find("python") != std::string::npos) {
                flags |= RTLD_GLOBAL;
                spdlog::info("Loading {} with RTLD_GLOBAL flag", filename);
            }
            
            void* handle = dlopen(filePath.c_str(), flags);
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

            PluginRegistryEntry regEntry;
            regEntry.name = nameFn();
            regEntry.handle = handle;
            regEntry.createFn = createFn;

            std::cout << "Loaded plugin: " << regEntry.name << " from " << filePath << std::endl;
            pluginRegistry_[regEntry.name] = std::move(regEntry);
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

    // Find registry entry
    auto regIt = pluginRegistry_.find(name);
    if (regIt == pluginRegistry_.end()) {
        return Err<PluginPtr>("Unknown plugin: " + name);
    }

    const auto& regEntry = regIt->second;
    Result<PluginPtr> result;

    if (regEntry.factory) {
        result = regEntry.factory(engine_);
    } else if (regEntry.createFn) {
        result = regEntry.createFn(engine_);
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

Result<WidgetPtr> PluginManager::createWidget(const std::string& pluginName,
                                                   PositionMode mode,
                                                   int32_t x, int32_t y,
                                                   int32_t widthCells, int32_t heightCells,
                                                   const std::string& payload,
                                                   Grid* grid,
                                                   uint32_t cellWidth, uint32_t cellHeight) {
    spdlog::info("PluginManager::createWidget: plugin={} payload_size={}", pluginName, payload.size());
    
    // Get or create the plugin
    auto pluginResult = getOrCreatePlugin(pluginName);
    if (!pluginResult) {
        spdlog::error("PluginManager::createWidget: getOrCreatePlugin failed for '{}'", pluginName);
        return Err<WidgetPtr>("Failed to get plugin", pluginResult);
    }
    PluginPtr plugin = *pluginResult;

    // Create the widget
    spdlog::info("PluginManager::createWidget: calling plugin->createLayer with payload_size={}", payload.size());
    auto widgetResult = plugin->createLayer(payload);  // Uses legacy name for compatibility
    if (!widgetResult) {
        spdlog::error("PluginManager::createWidget: createLayer failed: {}", error_msg(widgetResult));
        return Err<WidgetPtr>("Failed to create widget", widgetResult);
    }
    WidgetPtr widget = *widgetResult;

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

    // Configure the widget
    widget->setId(nextLayerId_++);
    widget->setHashId(oscParser_.generateId());  // Generate 8-char hash ID
    widget->setPositionMode(mode);
    widget->setScreenType(isAltScreen_ ? ScreenType::Alternate : ScreenType::Main);
    widget->setPosition(x, y);
    widget->setCellSize(finalWidth, finalHeight);
    widget->setPixelSize(finalWidth * cellWidth, finalHeight * cellHeight);

    // Add to plugin
    plugin->addWidget(widget);

    // Add to hash ID lookup map
    widgetsByHashId_[widget->hashId()] = widget;

    // Mark grid cells
    if (grid) {
        markGridCells(grid, widget.get());
    }

    return Ok(widget);
}

Result<void> PluginManager::updateLayer(const std::string& hashId, const std::string& payload) {
    PluginLayerPtr layer = getLayer(hashId);
    if (!layer) {
        return Err<void>("Layer not found: " + hashId);
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

Result<void> PluginManager::removeLayer(const std::string& hashId, Grid* grid) {
    auto it = widgetsByHashId_.find(hashId);
    if (it == widgetsByHashId_.end()) {
        return Err<void>("Layer not found: " + hashId);
    }

    PluginLayerPtr layer = it->second;
    uint32_t numericId = layer->id();

    if (grid) {
        clearGridCells(grid, layer.get());
    }

    // Remove from plugin
    for (auto& [name, plugin] : plugins_) {
        if (plugin->getLayer(numericId)) {
            if (auto res = plugin->removeLayer(numericId); !res) {
                return Err<void>("Failed to remove layer from plugin", res);
            }
            break;
        }
    }

    // Remove from hash ID map
    widgetsByHashId_.erase(it);

    return Ok();
}

PluginLayerPtr PluginManager::getLayer(const std::string& hashId) {
    auto it = widgetsByHashId_.find(hashId);
    return (it != widgetsByHashId_.end()) ? it->second : nullptr;
}

PluginLayerPtr PluginManager::getLayerById(uint32_t id) {
    for (auto& [name, plugin] : plugins_) {
        if (auto layer = plugin->getLayer(id)) {
            return layer;
        }
    }
    return nullptr;
}

Result<void> PluginManager::stopLayer(const std::string& hashId) {
    PluginLayerPtr layer = getLayer(hashId);
    if (!layer) {
        return Err<void>("Layer not found: " + hashId);
    }
    layer->stop();
    return Ok();
}

Result<void> PluginManager::stopLayersByPlugin(const std::string& pluginName) {
    auto it = plugins_.find(pluginName);
    if (it == plugins_.end()) {
        return Err<void>("Plugin not found: " + pluginName);
    }
    for (auto& layer : it->second->getLayers()) {
        layer->stop();
    }
    return Ok();
}

Result<void> PluginManager::startLayer(const std::string& hashId) {
    PluginLayerPtr layer = getLayer(hashId);
    if (!layer) {
        return Err<void>("Layer not found: " + hashId);
    }
    layer->start();
    return Ok();
}

Result<void> PluginManager::startLayersByPlugin(const std::string& pluginName) {
    auto it = plugins_.find(pluginName);
    if (it == plugins_.end()) {
        return Err<void>("Plugin not found: " + pluginName);
    }
    for (auto& layer : it->second->getLayers()) {
        layer->start();
    }
    return Ok();
}

Result<void> PluginManager::killLayersByPlugin(const std::string& pluginName, Grid* grid) {
    auto it = plugins_.find(pluginName);
    if (it == plugins_.end()) {
        return Err<void>("Plugin not found: " + pluginName);
    }

    // Collect hash IDs first (can't modify while iterating)
    std::vector<std::string> hashIds;
    for (auto& layer : it->second->getLayers()) {
        hashIds.push_back(layer->hashId());
    }

    // Remove each layer
    for (const auto& hashId : hashIds) {
        if (auto res = removeLayer(hashId, grid); !res) {
            return res;
        }
    }

    return Ok();
}

std::vector<std::string> PluginManager::getAvailablePlugins() const {
    std::vector<std::string> names;
    names.reserve(pluginRegistry_.size());
    for (const auto& [name, regEntry] : pluginRegistry_) {
        names.push_back(name);
    }
    return names;
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

bool PluginManager::handleOSCSequence(const std::string& sequence,
                                       Grid* grid,
                                       int cursorCol, int cursorRow,
                                       uint32_t cellWidth, uint32_t cellHeight,
                                       std::string* response,
                                       uint32_t* linesToAdvance) {
    // Parse the OSC sequence using the new command parser
    auto parseResult = oscParser_.parse(sequence);
    if (!parseResult) {
        if (response) {
            *response = OscResponse::error(error_msg(parseResult));
        }
        return false;
    }

    OscCommand cmd = *parseResult;
    if (!cmd.isValid()) {
        if (response) {
            *response = OscResponse::error(cmd.error);
        }
        return false;
    }

    switch (cmd.type) {
        case OscCommandType::Create: {
            int32_t x = cmd.create.x;
            int32_t y = cmd.create.y;

            // Adjust for relative positioning
            if (cmd.create.relative) {
                x += cursorCol;
                y += cursorRow;
            }

            PositionMode mode = cmd.create.relative ? PositionMode::Relative : PositionMode::Absolute;

            // Decode payload if provided in plugin args
            std::string payload = cmd.payload;

            auto result = createWidget(cmd.create.plugin, mode, x, y,
                                      cmd.create.width, cmd.create.height,
                                      payload, grid, cellWidth, cellHeight);
            if (!result) {
                spdlog::error("PluginManager: createWidget failed: {}", error_msg(result));
                if (response) {
                    *response = OscResponse::error(error_msg(result));
                }
                return false;
            }

            WidgetPtr widget = *result;
            std::cout << "Created widget " << widget->hashId() << " plugin=" << cmd.create.plugin
                      << " at (" << x << "," << y << ") size "
                      << cmd.create.width << "x" << cmd.create.height << std::endl;

            if (linesToAdvance && cmd.create.relative) {
                *linesToAdvance = std::abs(cmd.create.height);
            }
            return true;
        }

        case OscCommandType::List: {
            if (response) {
                std::vector<std::tuple<std::string, std::string, int, int, int, int, bool>> layers;
                for (auto& layer : getAllLayers()) {
                    if (!cmd.list.all && !layer->isRunning()) {
                        continue;  // Skip stopped layers unless --all
                    }
                    layers.emplace_back(
                        layer->hashId(),
                        layer->getParent()->pluginName(),
                        layer->getX(),
                        layer->getY(),
                        layer->getWidthCells(),
                        layer->getHeightCells(),
                        layer->isRunning()
                    );
                }
                *response = OscResponse::layerList(layers);
            }
            return true;
        }

        case OscCommandType::Plugins: {
            if (response) {
                *response = OscResponse::pluginList(getAvailablePlugins());
            }
            return true;
        }

        case OscCommandType::Kill: {
            Result<void> result;
            if (!cmd.target.id.empty()) {
                result = removeLayer(cmd.target.id, grid);
            } else if (!cmd.target.plugin.empty()) {
                result = killLayersByPlugin(cmd.target.plugin, grid);
            } else {
                result = Err<void>("kill: --id or --plugin required");
            }

            if (!result) {
                if (response) {
                    *response = OscResponse::error(error_msg(result));
                }
                return false;
            }
            return true;
        }

        case OscCommandType::Stop: {
            Result<void> result;
            if (!cmd.target.id.empty()) {
                result = stopLayer(cmd.target.id);
            } else if (!cmd.target.plugin.empty()) {
                result = stopLayersByPlugin(cmd.target.plugin);
            } else {
                result = Err<void>("stop: --id or --plugin required");
            }

            if (!result) {
                if (response) {
                    *response = OscResponse::error(error_msg(result));
                }
                return false;
            }
            return true;
        }

        case OscCommandType::Start: {
            Result<void> result;
            if (!cmd.target.id.empty()) {
                result = startLayer(cmd.target.id);
            } else if (!cmd.target.plugin.empty()) {
                result = startLayersByPlugin(cmd.target.plugin);
            } else {
                result = Err<void>("start: --id or --plugin required");
            }

            if (!result) {
                if (response) {
                    *response = OscResponse::error(error_msg(result));
                }
                return false;
            }
            return true;
        }

        case OscCommandType::Update: {
            if (cmd.target.id.empty()) {
                if (response) {
                    *response = OscResponse::error("update: --id required");
                }
                return false;
            }

            auto result = updateLayer(cmd.target.id, cmd.payload);
            if (!result) {
                if (response) {
                    *response = OscResponse::error(error_msg(result));
                }
                return false;
            }
            return true;
        }

        default:
            if (response) {
                *response = OscResponse::error("unknown command");
            }
            return false;
    }
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

    // PRE-RENDER PHASE: Let plugins render to their intermediate textures
    // This happens BEFORE the shared render pass, so plugins can submit their own commands
    for (auto& [name, plugin] : plugins_) {
        for (auto& layer : plugin->getLayers()) {
            if (!layer->isVisible()) continue;
            if (layer->getScreenType() != currentScreen) continue;
            
            // prepareFrame() allows plugins to render to textures (ThorVG, pygfx, etc.)
            spdlog::info("PluginManager: calling prepareFrame on layer {}", layer->name());
            layer->prepareFrame(ctx);
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

    // Render ALL plugin layers in this single pass (blit pre-rendered textures)
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
    return getLayerById(layerId);
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

    spdlog::debug("PluginManager::onMouseScroll: pixel({:.1f},{:.1f}) -> cell({},{}) logicalRow={} scrollOffset={}", 
                  pixelX, pixelY, col, row, logicalRow, scrollOffset);

    PluginLayerPtr layer = layerAtCell(col, logicalRow, grid);
    if (layer) {
        spdlog::debug("PluginManager::onMouseScroll: Found layer '{}' wantsMouse={}", 
                      layer->name(), layer->wantsMouse());
        if (layer->wantsMouse()) {
            bool handled = layer->onMouseScroll(xoffset, yoffset, mods);
            spdlog::debug("PluginManager::onMouseScroll: Layer handled scroll={}", handled);
            return handled;
        }
    } else {
        spdlog::debug("PluginManager::onMouseScroll: No layer at cell({},{})", col, logicalRow);
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
