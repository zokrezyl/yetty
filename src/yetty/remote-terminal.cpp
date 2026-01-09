#include "remote-terminal.h"
#include "grid-renderer.h"
#include "yetty/plugin-manager.h"

#include <chrono>
#include <spdlog/spdlog.h>

namespace yetty {

//=============================================================================
// Factory
//=============================================================================

Result<RemoteTerminal::Ptr> RemoteTerminal::create(
    uint32_t id, uint32_t cols, uint32_t rows, Font* font, uv_loop_t* loop) noexcept {
    
    if (!font) {
        return Err<Ptr>("RemoteTerminal::create: null Font");
    }
    if (!loop) {
        return Err<Ptr>("RemoteTerminal::create: null libuv loop");
    }
    
    auto term = Ptr(new RemoteTerminal(id, cols, rows, font, loop));
    if (auto res = term->init(); !res) {
        return Err<Ptr>("Failed to initialize RemoteTerminal", res);
    }
    return Ok(std::move(term));
}

RemoteTerminal::RemoteTerminal(
    uint32_t id, uint32_t cols, uint32_t rows, Font* font, uv_loop_t* loop) noexcept
    : id_(id)
    , name_("remote-terminal-" + std::to_string(id))
    , loop_(loop)
    , font_(font)
    , cols_(cols)
    , rows_(rows) {}

Result<void> RemoteTerminal::init() noexcept {
    // Create remote backend
    auto backendResult = RemoteTerminalBackend::create(cols_, rows_, loop_);
    if (!backendResult) {
        return Err<void>("Failed to create RemoteTerminalBackend", backendResult);
    }
    backend_ = *backendResult;
    
    // Pass Font for codepoint-to-glyph conversion
    backend_->setFont(font_);
    
    // Set up callbacks for server notifications
    TerminalBackendCallbacks callbacks;
    callbacks.onBell = []() {
#ifndef _WIN32
        write(STDOUT_FILENO, "\a", 1);
#endif
    };
    callbacks.onTitleChange = [](const std::string& title) {
        spdlog::debug("RemoteTerminal: title changed to '{}'", title);
    };
    backend_->setCallbacks(callbacks);
    
    return Ok();
}

RemoteTerminal::~RemoteTerminal() {
    stop();
}

//=============================================================================
// Lifecycle
//=============================================================================

void RemoteTerminal::start() {
    spdlog::info("RemoteTerminal: starting...");
    
    // Start backend (connects to server or spawns one)
    if (auto res = backend_->start(shell_); !res) {
        spdlog::error("RemoteTerminal: failed to start backend: {}", res.error().message());
        return;
    }
    
    // Now that SharedGridView exists, set the Font for codepoint conversion
    backend_->setFont(font_);
    
    // Create cursor blink timer
    cursorTimer_ = new uv_timer_t;
    uv_timer_init(loop_, cursorTimer_);
    cursorTimer_->data = this;
    uv_timer_start(cursorTimer_, onTimer, 16, 16);  // ~60 FPS for cursor blink
    
    spdlog::info("RemoteTerminal: started");
}

void RemoteTerminal::stop() {
    if (cursorTimer_) {
        uv_timer_stop(cursorTimer_);
        uv_close(reinterpret_cast<uv_handle_t*>(cursorTimer_), [](uv_handle_t* h) {
            delete reinterpret_cast<uv_timer_t*>(h);
        });
        cursorTimer_ = nullptr;
    }
    
    if (backend_) {
        backend_->stop();
    }
}

//=============================================================================
// Rendering
//=============================================================================

Result<void> RemoteTerminal::render(WebGPUContext& ctx) {
    (void)ctx;  // We use renderer_ directly

    if (!backend_ || !backend_->isRunning()) {
        return Ok();
    }

    if (!renderer_) {
        return Ok();
    }

    // Sync cursor/state from shared memory header
    backend_->syncToGrid();

    // Check if plugins need rendering
    bool pluginsActive = pluginManager_ && !pluginManager_->getAllWidgets().empty();

    bool hasDamage = backend_->hasDamage();
    bool fullDamage = backend_->hasFullDamage();

    // Skip rendering if no damage AND no plugins need base
    if (!hasDamage && !pluginsActive) {
        return Ok();
    }

    // Get damage info
    const auto& damageRects = backend_->getDamageRects();

    // Render the grid - getGrid() returns SharedGridView which reads directly from shm
    renderer_->render(backend_->getGrid(), damageRects, fullDamage,
                      backend_->getCursorCol(), backend_->getCursorRow(),
                      backend_->isCursorVisible() && cursorBlink_);

    // Clear damage after rendering
    backend_->clearDamageRects();
    backend_->clearFullDamage();

    return Ok();
}

//=============================================================================
// Input
//=============================================================================

void RemoteTerminal::sendKey(uint32_t codepoint, VTermModifier mod) {
    if (backend_) {
        backend_->sendKey(codepoint, mod);
    }
}

void RemoteTerminal::sendSpecialKey(VTermKey key, VTermModifier mod) {
    if (backend_) {
        backend_->sendSpecialKey(key, mod);
    }
}

void RemoteTerminal::sendRaw(const char* data, size_t len) {
    if (backend_) {
        backend_->sendRaw(data, len);
    }
}

void RemoteTerminal::resize(uint32_t cols, uint32_t rows) {
    cols_ = cols;
    rows_ = rows;
    if (backend_) {
        backend_->resize(cols, rows);
    }
}

//=============================================================================
// Cell size
//=============================================================================

void RemoteTerminal::setCellSize(uint32_t width, uint32_t height) {
    cellWidth_ = width;
    cellHeight_ = height;
}

void RemoteTerminal::setBaseCellSize(float width, float height) {
    baseCellWidth_ = width;
    baseCellHeight_ = height;
}

void RemoteTerminal::setZoomLevel(float zoom) {
    zoomLevel_ = zoom;
}

//=============================================================================
// Timer callback
//=============================================================================

void RemoteTerminal::onTimer(uv_timer_t* handle) {
    auto* self = static_cast<RemoteTerminal*>(handle->data);
    
    // Update cursor blink
    auto now = std::chrono::steady_clock::now();
    double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();
    self->updateCursorBlink(currentTime);
}

void RemoteTerminal::updateCursorBlink(double currentTime) {
    if (currentTime - lastBlinkTime_ >= blinkInterval_) {
        cursorBlink_ = !cursorBlink_;
        lastBlinkTime_ = currentTime;
    }
}

} // namespace yetty
