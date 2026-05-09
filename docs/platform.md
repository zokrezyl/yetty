# Platform Abstraction

## Design Principle: Avoid Code Duplication

Platform-specific code is organized to **maximize code reuse**. Instead of duplicating similar logic across platforms, common code lives in `shared/` and each platform directory contains only what's truly unique to that platform.

**Example: GLFW**

GLFW provides window, surface, input, and clipboard handling that works across Linux, macOS, and Windows. Instead of writing separate code for each OS:

```
shared/glfw-main.cpp              <- Entry point (Linux, macOS, Windows)
shared/glfw-window.cpp            <- Window creation
shared/glfw-surface.cpp           <- WebGPU surface via glfwCreateWindowWGPUSurface()
shared/glfw-event-loop.cpp        <- Event polling
shared/glfw-clipboard-manager.cpp <- Clipboard access
```

**Example: Platform Paths**

The only difference between Linux, macOS, and Windows is how to determine platform paths (cache, config, runtime directories):

```
linux/platform-paths.cpp    <- XDG directories
macos/platform-paths.cpp    <- ~/Library/...
windows/platform-paths.cpp  <- AppData
```

The linker picks the correct implementation based on the target platform.

**Example: libuv EventLoop**

All native platforms (Linux, macOS, Windows, Android) use libuv for the event loop:

```
shared/libuv-event-loop.cpp <- Used by all native platforms
```

Only WebASM needs a different implementation (emscripten requestAnimationFrame).

## File Structure

```
src/yetty/yplatform/
├── shared/                      # Reusable code across platforms
│   ├── glfw-main.cpp            # main() for GLFW platforms (Linux, macOS, Windows)
│   ├── glfw-window.cpp          # Window creation
│   ├── glfw-surface.cpp         # WebGPU surface via GLFW
│   ├── glfw-event-loop.cpp      # GLFW event polling
│   ├── glfw-clipboard-manager.cpp
│   ├── libuv-event-loop.cpp     # Event loop (all native platforms)
│   ├── unix-pty.cpp             # forkpty() PTY (Linux, macOS)
│   ├── unix-pipe.cpp            # pipe() for PlatformInputPipe (Unix)
│   └── fd-pty-poll-source.hpp   # PTY poll source wrapping fd
│
├── linux/                       # Linux-specific only
│   └── platform-paths.cpp       # XDG directories
│
├── macos/                       # macOS-specific only
│   └── platform-paths.cpp       # ~/Library/... paths
│
├── windows/                     # Windows-specific only
│   ├── main.cpp                 # Windows entry point (GLFW)
│   ├── platform-paths.cpp       # AppData paths
│   ├── conpty.cpp               # ConPTY (Windows PTY)
│   └── pipe.cpp                 # Windows pipe via HANDLEs
│
├── android/                     # Android-specific (no GLFW)
│   ├── main.cpp                 # NativeActivity entry point
│   ├── platform-paths.cpp       # App internal storage
│   └── surface.cpp              # ANativeWindow surface
│
├── ios/                         # iOS-specific (no GLFW)
│   ├── main.mm                  # UIKit entry point
│   ├── platform-paths.mm        # NSSearchPathForDirectoriesInDomains
│   └── surface.mm               # CAMetalLayer surface
│
└── webasm/                      # WebASM (no GLFW, no libuv)
    ├── main.cpp                 # Emscripten entry point
    ├── event-loop.cpp           # requestAnimationFrame loop
    ├── surface.cpp              # Canvas-based surface
    ├── window.cpp               # Canvas setup
    ├── pipe.cpp                 # In-memory PlatformInputPipe
    └── pty-io.cpp               # JSLinux PTY via iframe
```

## What Goes Where

| Code | Location | Why |
|------|----------|-----|
| GLFW main/window/surface/input | `shared/` | Works on Linux, macOS, Windows |
| libuv event loop | `shared/` | Works on all native platforms |
| Unix PTY (forkpty) | `shared/` | Works on Linux, macOS |
| Unix pipe | `shared/` | Works on Linux, macOS |
| getPlatformPaths() | Each platform dir | Different paths per OS |
| Windows ConPTY/pipe | `windows/` | Windows-only APIs |
| Android/iOS surface/main | Platform dir | No GLFW, different window system |
| WebASM everything | `webasm/` | No GLFW, no libuv, emscripten-specific |

## Key Abstractions

Platform code implements these interfaces:

| Interface | What it does | Implementations |
|-----------|--------------|-----------------|
| `EventLoop` | Runs main loop | `shared/libuv-event-loop.cpp`, `webasm/event-loop.cpp` |
| `PlatformInputPipe` | Transfers input events | `shared/unix-pipe.cpp`, `windows/pipe.cpp`, `webasm/pipe.cpp` |
| `Pty` + `PtyPollSource` | Shell I/O + polling | `shared/unix-pty.cpp`, `windows/conpty.cpp`, `webasm/pty-io.cpp` |
| `createSurface()` | WebGPU surface | `shared/glfw-surface.cpp`, `android/surface.cpp`, `ios/surface.mm`, `webasm/surface.cpp` |
| `getCacheDir()`, `getRuntimeDir()` | Platform directories | Each platform implements |

### Polling Model: Unix vs WebASM

Unix platforms have file descriptors. libuv polls them. WebASM has no fds - uses in-memory buffers with async callbacks.

**PTY Polling:**

```
include/yetty/yplatform/pty-poll-source.hpp   <- Base class (opaque handle)
shared/fd-pty-poll-source.hpp                <- Unix: wraps PTY master fd
webasm/pty-io.cpp (WebasmPtyPollSource)      <- WebASM: buffer + JS interop
```

- `Pty::pollSource()` returns a `PtyPollSource*`
- EventLoop static_casts to the concrete type it knows
- Unix: `FdPtyPollSource` wraps fd -> libuv polls it
- WebASM: `WebasmPtyPollSource` holds buffer -> JS pushes data via postMessage

**PlatformInputPipe:**

```
shared/unix-pipe.cpp    <- Unix: real pipe() fd, libuv polls read end
windows/pipe.cpp        <- Windows: HANDLE-based pipe
webasm/pipe.cpp         <- WebASM: in-memory buffer + emscripten_async_call
```

- Unix: main thread writes to pipe fd, render thread polls read fd via libuv
- WebASM: single thread, write() adds to buffer, schedules async callback to notify listener

## Threading Model

| Platform | Threads | Main Thread | Render Thread |
|----------|---------|-------------|---------------|
| Linux/macOS/Windows | 2 | GLFW event loop | Yetty (creates EventLoop) |
| Android | 2 | ALooper events | Yetty (creates EventLoop) |
| iOS | 1 | UIKit + CADisplayLink | - |
| WebASM | 1 | Everything | - |

Desktop threading (2 threads):
```
MAIN THREAD                     RENDER THREAD
-------------------------------------------
glfwWaitEvents()                Yetty creates EventLoop
  |                               |
Input callbacks -------------> PlatformInputPipe
                                  |
                                Terminal runs EventLoop
                                  |
                                Yetty render
```

## Startup Sequence

1. **Config::create()** - FIRST. Parse command line, load config (determines headless mode, window size)
2. **Create window** - GLFW window or HTML canvas (based on config)
3. **Create PlatformInputPipe** - For input event transfer
4. **Create PtyFactory** - For creating PTY instances
5. **Create WebGPU surface** - Platform-specific, requires window handle. Instance created internally.
6. **Pack into AppContext** - All objects bundled for Yetty
7. **Yetty::create()** - Creates WebGPU instance, requests adapter/device
8. **Spawn render thread** (desktop) - Calls yetty->run()
9. **Run OS event loop** - Platform-specific (GLFW, ALooper, UIKit, emscripten)

## AppContext

Struct passed from platform main() to Yetty. Contains only what platform must create:

```cpp
struct AppContext {
    Config *config;
    PlatformInputPipe *platformInputPipe;
    ClipboardManager *clipboardManager;  // optional
    PtyFactory *ptyFactory;
    WGPUInstance instance;  // Created by platform (needed for surface), reused by Yetty
    WGPUSurface surface;    // Platform creates surface (requires window handle)
};
```

**What Yetty creates internally:**
- WebGPU adapter, device, queue (using instance from AppContext)
- EventLoop (created by Terminal)

Platform creates instance once, uses it to create surface, passes both to Yetty for reuse.

## Surface Creation

Each platform creates instance + surface:

| Platform | Surface Source | Function |
|----------|---------------|----------|
| Linux/macOS/Windows | GLFWwindow | `glfwCreateWindowWGPUSurface(instance, window)` |
| Android | ANativeWindow | `wgpuInstanceCreateSurface(instance, &androidDesc)` |
| iOS | CAMetalLayer | `wgpuInstanceCreateSurface(instance, &metalDesc)` |
| WebASM | HTML canvas | `wgpuInstanceCreateSurface(instance, &canvasDesc)` |

Instance is created once in main(), passed to `createSurface()`, then both are passed through AppContext to Yetty.
