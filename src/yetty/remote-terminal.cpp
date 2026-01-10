#include "remote-terminal.h"
#include "grid-renderer.h"

#include <chrono>
#include <ytrace/ytrace.hpp>

namespace yetty {

//=============================================================================
// Factory
//=============================================================================

Result<RemoteTerminal::Ptr> RemoteTerminal::create(
    uint32_t id, uint32_t cols, uint32_t rows, Font* font, uv_loop_t* loop) noexcept {
    (void)id;  // Ignored - Widget base class auto-assigns IDs

    if (!font) {
        return Err<Ptr>("RemoteTerminal::create: null Font");
    }
    if (!loop) {
        return Err<Ptr>("RemoteTerminal::create: null libuv loop");
    }

    auto term = Ptr(new RemoteTerminal(cols, rows, font, loop));
    if (auto res = term->init(); !res) {
        return Err<Ptr>("Failed to initialize RemoteTerminal", res);
    }
    return Ok(std::move(term));
}

RemoteTerminal::RemoteTerminal(
    uint32_t cols, uint32_t rows, Font* font, uv_loop_t* loop) noexcept
    : Widget()
    , _loop(loop)
    , _font(font)
    , _cols(cols)
    , _rows(rows)
{
    _name = "remote-terminal-" + std::to_string(id());
}

Result<void> RemoteTerminal::init() noexcept {
    // Create remote backend
    auto backendResult = RemoteTerminalBackend::create(_cols, _rows, _loop);
    if (!backendResult) {
        return Err<void>("Failed to create RemoteTerminalBackend", backendResult);
    }
    _backend = *backendResult;

    // Pass Font for codepoint-to-glyph conversion
    _backend->setFont(_font);

    // Set up callbacks for server notifications
    TerminalBackendCallbacks callbacks;
    callbacks.onBell = []() {
#ifndef _WIN32
        write(STDOUT_FILENO, "\a", 1);
#endif
    };
    callbacks.onTitleChange = [](const std::string& title) {
        ydebug("RemoteTerminal: title changed to '{}'", title);
    };
    _backend->setCallbacks(callbacks);

    return Ok();
}

RemoteTerminal::~RemoteTerminal() {
    stop();
}

//=============================================================================
// Lifecycle
//=============================================================================

void RemoteTerminal::start() {
    yinfo("RemoteTerminal: starting...");

    // Start backend (connects to server or spawns one)
    if (auto res = _backend->start(_shell); !res) {
        yerror("RemoteTerminal: failed to start backend: {}", res.error().message());
        return;
    }

    // Now that SharedGridView exists, set the Font for codepoint conversion
    _backend->setFont(_font);

    // Create cursor blink timer
    _cursorTimer = new uv_timer_t;
    uv_timer_init(_loop, _cursorTimer);
    _cursorTimer->data = this;
    uv_timer_start(_cursorTimer, onTimer, 16, 16);  // ~60 FPS for cursor blink

    yinfo("RemoteTerminal: started");
}

void RemoteTerminal::stop() {
    if (_cursorTimer) {
        uv_timer_stop(_cursorTimer);
        uv_close(reinterpret_cast<uv_handle_t*>(_cursorTimer), [](uv_handle_t* h) {
            delete reinterpret_cast<uv_timer_t*>(h);
        });
        _cursorTimer = nullptr;
    }

    if (_backend) {
        _backend->stop();
    }
}

//=============================================================================
// Rendering
//=============================================================================

Result<void> RemoteTerminal::render(WebGPUContext& ctx) {
    (void)ctx;  // We use _renderer directly

    if (!_backend || !_backend->isRunning()) {
        return Ok();
    }

    if (!_renderer) {
        return Ok();
    }

    // Sync cursor/state from shared memory header
    _backend->syncToGrid();

    bool hasDamage = _backend->hasDamage();
    bool fullDamage = _backend->hasFullDamage();

    // Skip rendering if no damage
    if (!hasDamage) {
        return Ok();
    }

    // Get damage info
    const auto& damageRects = _backend->getDamageRects();

    // Render the grid - getGrid() returns SharedGridView which reads directly from shm
    _renderer->render(_backend->getGrid(), damageRects, fullDamage,
                      _backend->getCursorCol(), _backend->getCursorRow(),
                      _backend->isCursorVisible() && _cursorBlink);

    // Clear damage after rendering
    _backend->clearDamageRects();
    _backend->clearFullDamage();

    return Ok();
}

//=============================================================================
// Input
//=============================================================================

void RemoteTerminal::sendKey(uint32_t codepoint, VTermModifier mod) {
    if (_backend) {
        _backend->sendKey(codepoint, mod);
    }
}

void RemoteTerminal::sendSpecialKey(VTermKey key, VTermModifier mod) {
    if (_backend) {
        _backend->sendSpecialKey(key, mod);
    }
}

void RemoteTerminal::sendRaw(const char* data, size_t len) {
    if (_backend) {
        _backend->sendRaw(data, len);
    }
}

void RemoteTerminal::resize(uint32_t cols, uint32_t rows) {
    _cols = cols;
    _rows = rows;
    if (_backend) {
        _backend->resize(cols, rows);
    }
}

//=============================================================================
// Cell size
//=============================================================================

void RemoteTerminal::setCellSize(uint32_t width, uint32_t height) {
    _cellWidth = width;
    _cellHeight = height;
}

void RemoteTerminal::setBaseCellSize(float width, float height) {
    _baseCellWidth = width;
    _baseCellHeight = height;
}

void RemoteTerminal::setZoomLevel(float zoom) {
    _zoomLevel = zoom;
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
    if (currentTime - _lastBlinkTime >= _blinkInterval) {
        _cursorBlink = !_cursorBlink;
        _lastBlinkTime = currentTime;
    }
}

} // namespace yetty
