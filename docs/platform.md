# Platform Abstraction

Yetty targets Linux, macOS, Windows, Android, iOS/tvOS, and WebAssembly from one
pure-C codebase. Platform-specific code is isolated in the `yplatform` module
behind C ops interfaces; everything above it is platform-agnostic.

See [Platform PTY](platform-pty.md) and [Platform Pipe](platform-pipe.md) for the
two abstractions covered in their own depth, and [Contexts](contexts.md) for how
platform objects reach the rest of the system.

---

## Organization: capability families, not OS folders

`yplatform` is organized by **capability**, not by operating system. Each
capability is a directory under `src/yetty/yplatform/` containing a `default.c`
(the POSIX/shared implementation) plus per-OS override files only where an OS
genuinely differs:

```
src/yetty/yplatform/
├── pty-factory/   default.c  webasm.c
├── window/        default.c  webasm.c
├── webgpu-surface/default.c  android.c  ios-tvos.m  webasm.c
├── pipe/          default.c  windows.c  webasm.c
├── thread/        default.c  windows.c
├── term/          default.c  windows.c
├── socket/        default.c  windows.c
├── process/       default.c  windows.c
├── fs/            default.c  windows.c
├── io/            default.c  windows.c
├── time/          default.c  windows.c
├── tty/           default.c  windows.c
├── ipc-socket/    default.c  windows.c
├── paths/         linux.c  macos.c  windows.c  android.c  webasm.c  ios-tvos.m  singleton.c
├── os-event-loop/      libuv-event-loop/   clipboard/   coroutine/
├── audio/   move-resize/   window-manager/   extract-assets/   webasm/   yworkpool/
└── getopt.c  compat.h
```

The build picks the right file per target (see the CMake rule below). There is
**no** `shared/` directory and **no** `.cpp`/`.mm` C++ entry points — the entire
module is C (the one exception is `paths/ios-tvos.m`, Objective-C, needed for
`NSBundle`/`NSSearchPathForDirectoriesInDomains`).

### Per-OS selection (CMake)

```cmake
# Most capabilities: Windows gets windows.c, everything else gets default.c
if(WIN32)
    list(APPEND _core_per_os thread/windows.c term/windows.c fs/windows.c ...)
else()
    list(APPEND _core_per_os thread/default.c term/default.c fs/default.c ...)
endif()

# paths/ picks one file per platform
if(YETTY_IOS OR YETTY_TVOS)   list(APPEND _core_per_os paths/ios-tvos.m)
elseif(APPLE)                 list(APPEND _core_per_os paths/macos.c)
elseif(WIN32)                 list(APPEND _core_per_os paths/windows.c)
elseif(EMSCRIPTEN)            list(APPEND _core_per_os paths/webasm.c)
elseif(YETTY_ANDROID)         list(APPEND _core_per_os paths/android.c)
else()                        list(APPEND _core_per_os paths/linux.c)
endif()
```

### Library split

- **`yetty_yplatform_core`** — POSIX-flavour utilities with no GPU or event-loop
  dependency: paths, fs, io, time, process, socket, ipc-socket, thread, term,
  tty, getopt. Cheap to link from standalone tools.
- **Server-side facets** — event loops, WebGPU surface, clipboard, coroutine,
  PTY factory. These carry heavy transitive deps (glfw3webgpu, libuv, telnet)
  and currently live in the main executable's source set rather than a separate
  static library; a future cleanup will extract them.

---

## Public interfaces

Public headers live in `include/yetty/yplatform/` and are shared across all
platforms. Each capability is a C ops vtable (see
[C Coding Style](c-coding-style.md) for the pattern):

| Header | What it abstracts |
|---|---|
| `pty.h` | PTY backends + factory (`read`/`write`/`resize`/`stop`/`pipe_source`) |
| `pty-poll-source.h`, `pty-pipe-source.h` | Readiness sources for the event loop |
| `platform-input-pipe.h` | Cross-thread event pipe (main thread → worker) |
| `window-manager.h`, `move-resize.h` | Window creation, move/resize requests |
| `ywebgpu.h` | WebGPU instance/surface creation glue |
| `clipboard-manager.h` | Clipboard get/set |
| `ycoroutine.h` | Coroutine primitives (see [Coroutines](coroutines.md)) |
| `audio.h` | Audio output device |
| `fs.h`, `io.h`, `process.h`, `socket.h`, `ipc-socket.h`, `time.h`, `thread.h`, `tty.h`, `term.h` | OS utilities |
| `paths.h` | Cache / config / runtime directories per OS |
| `extract-assets.h` | Unpack the brotli-compressed bundled assets |

### Example: the PTY ops

```c
struct yetty_platform_pty_ops {
    struct yetty_ycore_void_result (*destroy)(struct yetty_platform_pty *self);
    struct yetty_ycore_size_result (*read)(struct yetty_platform_pty *self, char *buf, size_t max);
    struct yetty_ycore_size_result (*write)(struct yetty_platform_pty *self, const char *d, size_t n);
    struct yetty_ycore_void_result (*resize)(struct yetty_platform_pty *self,
                                             uint32_t cols, uint32_t rows,
                                             uint32_t pixel_w, uint32_t pixel_h);
    struct yetty_ycore_void_result (*stop)(struct yetty_platform_pty *self);
    struct yetty_platform_pty_pipe_source *(*pipe_source)(struct yetty_platform_pty *self);
};
```

### PTY backends

`yplatform` ships several PTY implementations behind one interface; the factory
picks one from config:

- `yetty_yplatform_fork_pty_create` — `forkpty()` on Unix / ConPTY on Windows.
- `yetty_yplatform_tinyemu_pty_create` — a console backed by an embedded TinyEMU
  RISC-V VM (see `yqemu`/`src/tinyemu`).
- `yetty_yplatform_iframe_pty_create` — WebAssembly only: the in-iframe TinyEMU
  console.
- `yetty_yplatform_memory_pty_pair_create` — an in-process PTY pair (two ring
  buffers) for same-process bridges.

SSH and Telnet PTYs are provided by the sibling `yssh` / `ytelnet` modules
through the same `yetty_platform_pty` interface.

---

## Polling model: native vs WebAssembly

Native platforms have file descriptors; libuv polls them. WebAssembly has no
fds — it uses in-memory buffers with async callbacks.

- **PTY readiness:** `pty->ops->pipe_source()` returns an opaque source. On Unix
  it wraps the PTY master fd for libuv; on WebAssembly the in-iframe bridge
  pushes bytes and a wake callback drains them. An in-memory PTY pair has no fd
  (`pipe_source` returns NULL) and instead fires a per-endpoint wake callback
  when the other end writes.
- **Input pipe:** on native, the main thread writes events to a real pipe and
  the worker polls the read end via libuv; on WebAssembly a single thread
  appends to a buffer and schedules an async callback.

## Threading model

| Platform | Threads | Main thread | Worker thread |
|---|---|---|---|
| Linux / macOS / Windows | 2 | OS event loop (GLFW) | yframework + app + event loop |
| Android | 2 | ALooper events | yframework + app + event loop |
| iOS / tvOS | 1 | UIKit + CADisplayLink | — |
| WebAssembly | 1 | everything (rAF-driven) | — |

---

## Startup

The platform layer no longer owns `main()`. Bootstrap is driven by **yinit**
(`include/yetty/yinit/yinit.h`), which the application enters via
`yetty_yinit_run(argc, argv, app_cfg, worker, user)`:

1. Set up platform paths and (optionally) extract bundled assets.
2. Parse the yconfig file (determines headless mode, window size, present mode).
3. Create the window + WebGPU surface (NULL surface in headless mode).
4. Create the cross-thread input pipe and any output pipe.
5. Start the OS event loop and run the supplied **worker** on a dedicated thread
   (or the main thread on WebAssembly).

The worker receives a fully-populated `struct yetty_yinit_runtime` (instance,
surface, dimensions, content scale, config, pipes, clipboard, window manager).
For yetty that worker is `src/yetty/ymain/glfw.c`, which builds the GPU/service
layer (`yframework`) and then the terminal app — see [Contexts](contexts.md) for
the full chain and the exact code.

## Pointers

- Bootstrap: `include/yetty/yinit/yinit.h`, `src/yetty/ymain/glfw.c`
- PTY: `include/yetty/yplatform/pty.h`, `src/yetty/yplatform/pty-factory/`
- Build selection: `src/yetty/yplatform/CMakeLists.txt`
