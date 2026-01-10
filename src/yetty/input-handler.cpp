#include "input-handler.h"
#include <yetty/yetty.h>
#include <yetty/widget.h>
#include "grid-renderer.h"
#include "grid.h"

#include <iostream>

#if !YETTY_WEB
#include "terminal.h"
#include "remote-terminal.h"
#include <vterm.h>
#endif

#include <ytrace/ytrace.hpp>
#include <cstring>

namespace yetty {

// Helper macro to check Result in callbacks where we can't return errors
#define CHECK_RESULT(expr) \
    do { \
        if (auto _res = (expr); !_res) { \
            yerror("{}: {}", #expr, _res.error().message()); \
        } \
    } while(0)

//-----------------------------------------------------------------------------
// InputHandler
//-----------------------------------------------------------------------------

InputHandler::InputHandler(Yetty* engine) noexcept
    : _engine(engine) {
    yfunc();
}

Result<InputHandler::Ptr> InputHandler::create(Yetty* engine) noexcept {
    yfunc();
    auto p = Ptr(new InputHandler(engine));
    if (auto res = p->init(); !res) {
        return Err<Ptr>("Failed to init InputHandler", res);
    }
    return Ok(p);
}

Result<void> InputHandler::init() noexcept {
    yfunc();
    return Ok();
}

#if !YETTY_WEB
// Helper template to dispatch to either Terminal or RemoteTerminal
// Both have the same input API so this works seamlessly
template<typename TerminalType>
struct TerminalHelper {
    TerminalType* term;

    explicit TerminalHelper(TerminalType* t) : term(t) {}

    bool valid() const { return term != nullptr; }
    void extendSelection(int row, int col) { term->extendSelection(row, col); }
    int getScrollOffset() const { return term->getScrollOffset(); }
    const Grid& getGrid() const { return term->getGrid(); }
    Grid& getGridMutable() { return term->getGridMutable(); }
    bool wantsMouseEvents() const { return term->wantsMouseEvents(); }
    void clearSelection() { term->clearSelection(); }
    void sendRaw(const char* data, size_t len) { term->sendRaw(data, len); }
    void startSelection(int row, int col, SelectionMode mode) { term->startSelection(row, col, mode); }
    bool hasSelection() const { return term->hasSelection(); }
    std::string getSelectedText() { return term->getSelectedText(); }
    void sendKey(uint32_t cp, VTermModifier mod = VTERM_MOD_NONE) { term->sendKey(cp, mod); }
    void sendSpecialKey(VTermKey key, VTermModifier mod = VTERM_MOD_NONE) { term->sendSpecialKey(key, mod); }
    void scrollUp(int lines = 1) { term->scrollUp(lines); }
    void scrollDown(int lines = 1) { term->scrollDown(lines); }
};

// Macro to get active terminal and run code with it
// Uses template helper to provide unified interface
#define WITH_ACTIVE_TERMINAL(code) \
    do { \
        if (auto rt = _engine->remoteTerminal()) { \
            TerminalHelper<RemoteTerminal> terminal(rt.get()); \
            code \
        } else if (auto t = _engine->terminal()) { \
            TerminalHelper<Terminal> terminal(t.get()); \
            code \
        } \
    } while(0)

// Helper to get Grid pointer from active terminal
inline const Grid* getActiveGrid(Yetty* engine) {
    if (auto rt = engine->remoteTerminal()) return &rt->getGrid();
    if (auto t = engine->terminal()) return &t->getGrid();
    return nullptr;
}

inline int getActiveScrollOffset(Yetty* engine) {
    if (auto rt = engine->remoteTerminal()) return rt->getScrollOffset();
    if (auto t = engine->terminal()) return t->getScrollOffset();
    return 0;
}

// Helper to get child widgets from active terminal
inline const std::vector<WidgetPtr>* getActiveWidgets(Yetty* engine) {
    if (auto t = engine->terminal()) return &t->getChildWidgets();
    return nullptr;
}
#endif

//-----------------------------------------------------------------------------
// Widget mouse dispatching
//-----------------------------------------------------------------------------

WidgetPtr InputHandler::widgetAtPixel(float pixelX, float pixelY) {
    yfunc();
#if !YETTY_WEB
    auto t = _engine->terminal();
    if (!t) return nullptr;

    float cellWidth = _engine->cellWidth();
    float cellHeight = _engine->cellHeight();
    int scrollOffset = t->getScrollOffset();

    // Check each widget's pixel bounds (compute on-the-fly for scroll accuracy)
    for (const auto& widget : t->getChildWidgets()) {
        if (!widget->isVisible()) continue;

        // Compute pixel position from cell position
        float wx = static_cast<float>(widget->getX()) * cellWidth;
        float wy = static_cast<float>(widget->getY()) * cellHeight;
        float ww = static_cast<float>(widget->getWidthCells()) * cellWidth;
        float wh = static_cast<float>(widget->getHeightCells()) * cellHeight;

        // Adjust for scroll (relative widgets move with content)
        if (widget->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
            wy += scrollOffset * cellHeight;
        }

        if (pixelX >= wx && pixelX < wx + ww &&
            pixelY >= wy && pixelY < wy + wh) {
            ydebug("widgetAtPixel({:.1f},{:.1f}) -> hit '{}' at ({:.1f},{:.1f}) size {:.0f}x{:.0f}",
                   pixelX, pixelY, widget->name(), wx, wy, ww, wh);
            return widget;
        }
    }
#else
    (void)pixelX; (void)pixelY;
#endif
    return nullptr;
}

bool InputHandler::dispatchMouseMoveToWidget(float pixelX, float pixelY) {
    yfunc();
#if !YETTY_WEB
    WidgetPtr widget = widgetAtPixel(pixelX, pixelY);

    // Track hovered widget
    _hoveredWidget = widget;

    if (widget && widget->wantsMouse()) {
        auto t = _engine->terminal();
        float cellWidth = _engine->cellWidth();
        float cellHeight = _engine->cellHeight();
        int scrollOffset = t ? t->getScrollOffset() : 0;

        // Compute widget pixel position on-the-fly
        float wx = static_cast<float>(widget->getX()) * cellWidth;
        float wy = static_cast<float>(widget->getY()) * cellHeight;
        if (widget->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
            wy += scrollOffset * cellHeight;
        }

        float localX = pixelX - wx;
        float localY = pixelY - wy;

        return widget->onMouseMove(localX, localY);
    }
#else
    (void)pixelX; (void)pixelY;
#endif
    return false;
}

bool InputHandler::dispatchMouseButtonToWidget(int button, bool pressed, float pixelX, float pixelY) {
    yfunc();
#if !YETTY_WEB
    WidgetPtr widget = widgetAtPixel(pixelX, pixelY);

    // Handle focus changes on left click
    if (pressed && button == 0) {
        if (widget != _focusedWidget) {
            if (_focusedWidget) {
                _focusedWidget->onMouseButton(-1, false);
            }
            clearWidgetFocus();
            if (widget && (widget->wantsKeyboard() || widget->wantsMouse())) {
                _focusedWidget = widget;
                widget->setFocus(true);
                ydebug("Widget '{}' gained focus", widget->name());
            }
        }
    }

    if (widget && widget->wantsMouse()) {
        auto t = _engine->terminal();
        if (!t) return false;

        float cellWidth = _engine->cellWidth();
        float cellHeight = _engine->cellHeight();
        int scrollOffset = t->getScrollOffset();

        float widgetPixelX = widget->getX() * cellWidth;
        float widgetPixelY = widget->getY() * cellHeight;

        if (widget->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
            widgetPixelY += scrollOffset * cellHeight;
        }

        float localX = pixelX - widgetPixelX;
        float localY = pixelY - widgetPixelY;

        widget->onMouseMove(localX, localY);
        return widget->onMouseButton(button, pressed);
    }
#else
    (void)button; (void)pressed; (void)pixelX; (void)pixelY;
#endif
    return false;
}

bool InputHandler::dispatchMouseScrollToWidget(float xoffset, float yoffset, int mods, float pixelX, float pixelY) {
    yfunc();
#if !YETTY_WEB
    WidgetPtr widget = widgetAtPixel(pixelX, pixelY);

    ydebug("dispatchMouseScrollToWidget: pixel({:.1f},{:.1f}) widget={}",
           pixelX, pixelY, widget ? widget->name() : "none");

    if (widget && widget->wantsMouse()) {
        bool handled = widget->onMouseScroll(xoffset, yoffset, mods);
        ydebug("Widget '{}' handled scroll={}", widget->name(), handled);
        return handled;
    }
#else
    (void)xoffset; (void)yoffset; (void)mods; (void)pixelX; (void)pixelY;
#endif
    return false;
}

void InputHandler::clearWidgetFocus() {
    yfunc();
    if (_focusedWidget) {
        _focusedWidget->setFocus(false);
        ydebug("Widget '{}' lost focus", _focusedWidget->name());
        _focusedWidget = nullptr;
    }
}

//-----------------------------------------------------------------------------
// Event handlers
//-----------------------------------------------------------------------------

void InputHandler::onMouseMove(double xpos, double ypos) noexcept {
    _mouseX = xpos;
    _mouseY = ypos;

#if !YETTY_WEB
    // First, try to dispatch to widget
    if (dispatchMouseMoveToWidget(static_cast<float>(xpos), static_cast<float>(ypos))) {
        return;
    }

    // Handle selection extension while dragging
    if (_selecting) {
        float cellWidth = _engine->cellWidth();
        float cellHeight = _engine->cellHeight();
        int col = static_cast<int>(xpos / cellWidth);
        int row = static_cast<int>(ypos / cellHeight);

        WITH_ACTIVE_TERMINAL(
            terminal.extendSelection(row, col);
            return;
        );
    }
#endif
}

void InputHandler::onMouseButton(int button, int action, int mods) noexcept {
    yfunc();
#if !YETTY_WEB
    GLFWwindow* window = _engine->window();

    // First, try to dispatch to widget
    bool pressed = (action == GLFW_PRESS);
    if (dispatchMouseButtonToWidget(button, pressed, static_cast<float>(_mouseX), static_cast<float>(_mouseY))) {
        return;
    }

    const Grid* grid = getActiveGrid(_engine);
    if (!grid) return;

    float cellWidth = _engine->cellWidth();
    float cellHeight = _engine->cellHeight();

    // Check if Shift is held - force selection mode even if app wants mouse
    bool shiftHeld = (mods & GLFW_MOD_SHIFT) != 0;

    // If app wants mouse events and Shift is not held, pass to app
    bool wantsMouse = false;
    WITH_ACTIVE_TERMINAL(
        wantsMouse = terminal.wantsMouseEvents();
    );

    if (!shiftHeld && wantsMouse) {
        // Convert pixel coords to cell coords for vterm
        int col = static_cast<int>(_mouseX / cellWidth);
        int row = static_cast<int>(_mouseY / cellHeight);

        // Send mouse event to vterm (which sends to PTY)
        VTermModifier vtermMod = VTERM_MOD_NONE;
        if (mods & GLFW_MOD_CONTROL) vtermMod = (VTermModifier)(vtermMod | VTERM_MOD_CTRL);
        if (mods & GLFW_MOD_ALT) vtermMod = (VTermModifier)(vtermMod | VTERM_MOD_ALT);
        if (mods & GLFW_MOD_SHIFT) vtermMod = (VTermModifier)(vtermMod | VTERM_MOD_SHIFT);

        // Note: vterm buttons are 1-indexed (1=left, 2=middle, 3=right)
        int vtermButton = button + 1;
        (void)vtermButton;
        (void)vtermMod;
        (void)row;
        (void)col;
    }

    // Handle middle mouse button for paste
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
        const char* clipboard = glfwGetClipboardString(window);
        if (clipboard && *clipboard) {
            WITH_ACTIVE_TERMINAL(
                terminal.clearSelection();
                terminal.sendRaw(clipboard, strlen(clipboard));
            );
            ydebug("Pasted {} bytes from clipboard (middle-click)", strlen(clipboard));
        }
        return;
    }

    // Handle left mouse button for selection
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        int col = static_cast<int>(_mouseX / cellWidth);
        int row = static_cast<int>(_mouseY / cellHeight);

        if (action == GLFW_PRESS) {
            // Detect double/triple click
            double currentTime = glfwGetTime();
            double timeSinceLastClick = currentTime - _lastClickTime;

            if (timeSinceLastClick < 0.3) {  // 300ms threshold
                _clickCount++;
                if (_clickCount > 3) _clickCount = 1;
            } else {
                _clickCount = 1;
            }
            _lastClickTime = currentTime;

            // Clear any existing selection on new click
            WITH_ACTIVE_TERMINAL(terminal.clearSelection(););

            // Start selection based on click count
            SelectionMode mode = SelectionMode::Character;
            if (_clickCount == 2) {
                mode = SelectionMode::Word;
            } else if (_clickCount == 3) {
                mode = SelectionMode::Line;
            }

            WITH_ACTIVE_TERMINAL(terminal.startSelection(row, col, mode););
            _selecting = true;

        } else if (action == GLFW_RELEASE) {
            _selecting = false;

            // Copy selection to clipboard if any
            bool hasSel = false;
            WITH_ACTIVE_TERMINAL(hasSel = terminal.hasSelection(););
            if (hasSel) {
                std::string selectedText;
                WITH_ACTIVE_TERMINAL(selectedText = terminal.getSelectedText(););
                if (!selectedText.empty()) {
                    glfwSetClipboardString(window, selectedText.c_str());
                    ydebug("Copied {} bytes to clipboard", selectedText.size());
                }
            }
        }
    }
#else
    (void)button; (void)action; (void)mods;
#endif
}

void InputHandler::onKey(int key, int scancode, int action, int mods) noexcept {
    yfunc();
#if !YETTY_WEB
    GLFWwindow* window = _engine->window();

    const Grid* grid = getActiveGrid(_engine);
    if (!grid) return;

    // Handle clipboard shortcuts (Ctrl+Shift+C/V or Shift+Insert)
    if (action == GLFW_PRESS) {
        bool ctrlShift = (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_SHIFT);

        // Ctrl+Shift+C: Copy selection
        if (ctrlShift && key == GLFW_KEY_C) {
            bool hasSel = false;
            WITH_ACTIVE_TERMINAL(hasSel = terminal.hasSelection(););
            if (hasSel) {
                std::string selectedText;
                WITH_ACTIVE_TERMINAL(selectedText = terminal.getSelectedText(););
                if (!selectedText.empty()) {
                    glfwSetClipboardString(window, selectedText.c_str());
                    ydebug("Copied {} bytes to clipboard (Ctrl+Shift+C)", selectedText.size());
                }
            }
            return;
        }

        // Ctrl+Shift+V or Shift+Insert: Paste
        if ((ctrlShift && key == GLFW_KEY_V) ||
            ((mods & GLFW_MOD_SHIFT) && key == GLFW_KEY_INSERT)) {
            const char* clipboard = glfwGetClipboardString(window);
            if (clipboard && *clipboard) {
                // Clear selection before pasting
                WITH_ACTIVE_TERMINAL(
                    terminal.clearSelection();
                    terminal.sendRaw(clipboard, strlen(clipboard));
                );
                ydebug("Pasted {} bytes from clipboard", strlen(clipboard));
            }
            return;
        }

        // Escape clears selection
        bool hasSel = false;
        WITH_ACTIVE_TERMINAL(hasSel = terminal.hasSelection(););
        if (key == GLFW_KEY_ESCAPE && hasSel) {
            WITH_ACTIVE_TERMINAL(terminal.clearSelection(););
            // Don't return - still send ESC to terminal
        }
    }

    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    VTermModifier vtermMod = VTERM_MOD_NONE;
    if (mods & GLFW_MOD_CONTROL) vtermMod = (VTermModifier)(vtermMod | VTERM_MOD_CTRL);
    if (mods & GLFW_MOD_ALT) vtermMod = (VTermModifier)(vtermMod | VTERM_MOD_ALT);
    if (mods & GLFW_MOD_SHIFT) vtermMod = (VTermModifier)(vtermMod | VTERM_MOD_SHIFT);

    // Handle Ctrl/Alt combinations - use glfwGetKeyName for correct keyboard layout
    if (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) {
        const char* keyName = glfwGetKeyName(key, scancode);
        if (keyName && keyName[0] != '\0') {
            // Single character key name - send it with modifier
            if (keyName[1] == '\0') {
                uint32_t ch = keyName[0];
                ydebug("Sending Ctrl/Alt+'{}'", (char)ch);
                WITH_ACTIVE_TERMINAL(terminal.sendKey(ch, vtermMod););
                return;
            }
        }
    }

    // Map GLFW keys to VTerm keys
    switch (key) {
        case GLFW_KEY_ENTER:
            WITH_ACTIVE_TERMINAL(terminal.sendSpecialKey(VTERM_KEY_ENTER, vtermMod););
            break;
        case GLFW_KEY_BACKSPACE:
            WITH_ACTIVE_TERMINAL(terminal.sendSpecialKey(VTERM_KEY_BACKSPACE, vtermMod););
            break;
        case GLFW_KEY_TAB:
            WITH_ACTIVE_TERMINAL(terminal.sendSpecialKey(VTERM_KEY_TAB, vtermMod););
            break;
        case GLFW_KEY_ESCAPE:
            WITH_ACTIVE_TERMINAL(terminal.sendSpecialKey(VTERM_KEY_ESCAPE, vtermMod););
            break;
        case GLFW_KEY_UP:
            WITH_ACTIVE_TERMINAL(terminal.sendSpecialKey(VTERM_KEY_UP, vtermMod););
            break;
        case GLFW_KEY_DOWN:
            WITH_ACTIVE_TERMINAL(terminal.sendSpecialKey(VTERM_KEY_DOWN, vtermMod););
            break;
        case GLFW_KEY_LEFT:
            WITH_ACTIVE_TERMINAL(terminal.sendSpecialKey(VTERM_KEY_LEFT, vtermMod););
            break;
        case GLFW_KEY_RIGHT:
            WITH_ACTIVE_TERMINAL(terminal.sendSpecialKey(VTERM_KEY_RIGHT, vtermMod););
            break;
        case GLFW_KEY_HOME:
            WITH_ACTIVE_TERMINAL(terminal.sendSpecialKey(VTERM_KEY_HOME, vtermMod););
            break;
        case GLFW_KEY_END:
            WITH_ACTIVE_TERMINAL(terminal.sendSpecialKey(VTERM_KEY_END, vtermMod););
            break;
        case GLFW_KEY_PAGE_UP:
            WITH_ACTIVE_TERMINAL(terminal.sendSpecialKey(VTERM_KEY_PAGEUP, vtermMod););
            break;
        case GLFW_KEY_PAGE_DOWN:
            WITH_ACTIVE_TERMINAL(terminal.sendSpecialKey(VTERM_KEY_PAGEDOWN, vtermMod););
            break;
        case GLFW_KEY_INSERT:
            WITH_ACTIVE_TERMINAL(terminal.sendSpecialKey(VTERM_KEY_INS, vtermMod););
            break;
        case GLFW_KEY_DELETE:
            WITH_ACTIVE_TERMINAL(terminal.sendSpecialKey(VTERM_KEY_DEL, vtermMod););
            break;
        default:
            break;
    }
#else
    (void)key; (void)scancode; (void)action; (void)mods;
#endif
}

void InputHandler::onChar(unsigned int codepoint) noexcept {
    yfunc();
#if !YETTY_WEB
    const Grid* grid = getActiveGrid(_engine);
    if (!grid) return;

    WITH_ACTIVE_TERMINAL(terminal.sendKey(codepoint););
#else
    (void)codepoint;
#endif
}

void InputHandler::onScroll(double xoffset, double yoffset) noexcept {
    yfunc();
    if (!_engine->renderer()) return;

    bool ctrlPressed = glfwGetKey(_engine->window(), GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                       glfwGetKey(_engine->window(), GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

#if !YETTY_WEB
    // First, try to dispatch scroll to widget (only if not zooming)
    if (!ctrlPressed) {
        int mods = 0;
        if (glfwGetKey(_engine->window(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(_engine->window(), GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
            mods |= GLFW_MOD_SHIFT;
        }
        if (glfwGetKey(_engine->window(), GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
            glfwGetKey(_engine->window(), GLFW_KEY_RIGHT_ALT) == GLFW_PRESS) {
            mods |= GLFW_MOD_ALT;
        }

        if (dispatchMouseScrollToWidget(static_cast<float>(xoffset), static_cast<float>(yoffset),
                                         mods, static_cast<float>(_mouseX), static_cast<float>(_mouseY))) {
            return;
        }
    }

    const Grid* grid = getActiveGrid(_engine);

    // Scroll through scrollback (no Ctrl)
    if (!ctrlPressed && grid) {
        int lines = static_cast<int>(yoffset * 3);  // 3 lines per scroll notch
        ydebug("onScroll: Scrolling terminal by {} lines", lines);
        if (lines > 0) {
            WITH_ACTIVE_TERMINAL(terminal.scrollUp(lines););
        } else if (lines < 0) {
            WITH_ACTIVE_TERMINAL(terminal.scrollDown(-lines););
        }
        return;
    }
#endif

    (void)xoffset;

    // Zoom grid (Ctrl + wheel, not over plugin)
    float newZoom = _engine->zoomLevel() + static_cast<float>(yoffset) * 0.1f;
    newZoom = glm::clamp(newZoom, 0.2f, 5.0f);
    _engine->setZoomLevel(newZoom);

    // Recalculate grid size based on new cell size
    int w, h;
    glfwGetFramebufferSize(_engine->window(), &w, &h);
    float cellWidth = _engine->baseCellWidth() * newZoom;
    float cellHeight = _engine->baseCellHeight() * newZoom;
    uint32_t newCols = static_cast<uint32_t>(w / cellWidth);
    uint32_t newRows = static_cast<uint32_t>(h / cellHeight);

    if (newCols != _engine->cols() || newRows != _engine->rows()) {
        _engine->updateGridSize(newCols, newRows);
        yinfo("Zoom: {:.0f}% -> Grid {}x{}", newZoom * 100.0f, newCols, newRows);
    }
}

//-----------------------------------------------------------------------------
// GLFW Callback Functions
//-----------------------------------------------------------------------------

void glfwCursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    (void)xpos; (void)ypos;
    auto* engine = static_cast<Yetty*>(glfwGetWindowUserPointer(window));
    if (!engine) return;

    // The InputHandler is stored in the Yetty engine, we access it indirectly
    // through a static method or by storing it externally
    // TODO: Clean this up - for now use g_inputHandler
}

void glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    (void)window; (void)button; (void)action; (void)mods;
    // Implemented in Yetty::initCallbacks via lambda
}

void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window; (void)key; (void)scancode; (void)action; (void)mods;
    // Implemented in Yetty::initCallbacks via lambda
}

void glfwCharCallback(GLFWwindow* window, unsigned int codepoint) {
    (void)window; (void)codepoint;
    // Implemented in Yetty::initCallbacks via lambda
}

void glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    (void)window; (void)xoffset; (void)yoffset;
    // Implemented in Yetty::initCallbacks via lambda
}

void glfwFramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    (void)window; (void)width; (void)height;
    // Implemented in Yetty::initCallbacks via lambda
}

} // namespace yetty
