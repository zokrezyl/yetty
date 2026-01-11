#pragma once

//=============================================================================
// InputHandler
//
// Handles all input events (mouse, keyboard, scroll) and routes them to
// terminal and plugins. Extracted from main.cpp callbacks.
//=============================================================================

#include <yetty/result.hpp>
#include <GLFW/glfw3.h>
#include <memory>

namespace yetty {

// Forward declarations
class Terminal;
class Yetty;
class Widget;
class Grid;
using WidgetPtr = std::shared_ptr<Widget>;

//-----------------------------------------------------------------------------
// InputHandler - Handles mouse, keyboard, and scroll input
//-----------------------------------------------------------------------------
class InputHandler : public std::enable_shared_from_this<InputHandler> {
public:
    using Ptr = std::shared_ptr<InputHandler>;

    ~InputHandler() = default;

    // Factory method
    static Result<Ptr> create(Yetty* engine) noexcept;

    // Event handlers (called from GLFW callbacks)
    void onMouseMove(double xpos, double ypos) noexcept;
    void onMouseButton(int button, int action, int mods) noexcept;
    void onKey(int key, int scancode, int action, int mods) noexcept;
    void onChar(unsigned int codepoint) noexcept;
    void onScroll(double xoffset, double yoffset) noexcept;

    // Mouse state
    double mouseX() const noexcept { return _mouseX; }
    double mouseY() const noexcept { return _mouseY; }

    // Selection state
    bool isSelecting() const noexcept { return _selecting; }

    // Widget focus
    WidgetPtr focusedWidget() const { return _focusedWidget; }
    void clearWidgetFocus();

private:
    explicit InputHandler(Yetty* engine) noexcept;
    Result<void> init() noexcept;

    // Widget mouse dispatching
    WidgetPtr widgetAtPixel(float pixelX, float pixelY);
    bool dispatchMouseMoveToWidget(float pixelX, float pixelY);
    bool dispatchMouseButtonToWidget(int button, bool pressed, float pixelX, float pixelY);
    bool dispatchMouseScrollToWidget(float xoffset, float yoffset, int mods, float pixelX, float pixelY);

    // Back-reference to engine (not owning)
    Yetty* _engine = nullptr;

    // Mouse state
    double _mouseX = 0.0;
    double _mouseY = 0.0;

    // Selection state
    bool _selecting = false;
    double _lastClickTime = 0.0;
    int _clickCount = 0;  // For double/triple click detection

    // Widget interaction
    WidgetPtr _hoveredWidget;
    WidgetPtr _focusedWidget;
};

//-----------------------------------------------------------------------------
// GLFW callback functions (static, forward to InputHandler)
//-----------------------------------------------------------------------------
void glfwCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
void glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void glfwCharCallback(GLFWwindow* window, unsigned int codepoint);
void glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
void glfwFramebufferSizeCallback(GLFWwindow* window, int width, int height);

} // namespace yetty
