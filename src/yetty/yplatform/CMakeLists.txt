# yplatform — split into two libraries following the established
# XXX-core convention (see yetty_yimage_core / yetty_yimage, etc.):
#
#   yetty_yplatform_core
#     Client-safe POSIX-flavour abstractions: fs, term, time, thread,
#     process, socket, ipc-socket, paths, getopt, tty. No libuv, no
#     glfw, no WebGPU. Any standalone tool (ycat, yplot, ymesh,
#     ygreeter, …) links this and gets the dirs/threading/getopt it
#     needs.
#
#   yetty_yplatform
#     Server-side: libuv/os event loops, coroutine + workpool,
#     pipe / pty-factory plumbing, clipboard manager, window-chrome,
#     WebGPU surface + decoder glue, extract-assets, webasm shims.
#     Pulls in libuv / wgpu / glfw — only the main yetty exec (and a
#     few tools that actually need a GPU/event loop) should link this.
#
# Public headers under include/yetty/yplatform/ are shared; the split
# is purely at the link-target level. Both libs PUBLIC-link
# yetty_ycore for the result-type machinery.

# ---------------------------------------------------------------------
# yetty_yplatform_core — POSIX-flavour utilities, no GPU/event-loop
# ---------------------------------------------------------------------

# Per-OS picks: WIN32 → windows.c sources, anything else → default.c.
# Apple/iOS/tvOS paths impl ships as Objective-C (paths/ios-tvos.m).
if(WIN32)
    set(_core_per_os
        thread/windows.c
        term/windows.c
        fs/windows.c
        time/windows.c
        process/windows.c
        socket/windows.c
        ipc-socket/windows.c
        tty/windows.c
        io/windows.c
        vulkan-driver/windows.c
    )
else()
    set(_core_per_os
        thread/default.c
        term/default.c
        fs/default.c
        time/default.c
        process/default.c
        socket/default.c
        ipc-socket/default.c
        tty/default.c
        io/default.c
        vulkan-driver/default.c
    )
endif()

# Per-OS paths impl.
if(YETTY_IOS OR YETTY_TVOS)
    list(APPEND _core_per_os paths/ios-tvos.m)
elseif(APPLE)
    list(APPEND _core_per_os paths/macos.c)
elseif(WIN32)
    list(APPEND _core_per_os paths/windows.c)
elseif(EMSCRIPTEN)
    list(APPEND _core_per_os paths/webasm.c)
elseif(YETTY_ANDROID OR ANDROID)
    list(APPEND _core_per_os paths/android.c)
else()
    list(APPEND _core_per_os paths/linux.c)
endif()

add_library(yetty_yplatform_core STATIC
    getopt.c
    paths/create.c
    # Fatal-error surfacing to the hosting environment (webasm page dialog;
    # no-op elsewhere). Single TU, platform split inside the file.
    fatal-report/fatal-report.c
    # Base (platform-independent) window-chrome yclass: the render→main producer
    # slots + dispatch stubs. Lives here so any module that drives the window
    # chrome (ychrome, yui's tabbar) links the stubs regardless of whether the
    # platform's concrete window-chrome (glfw_window_chrome) is in the link. The
    # GLFW handle_event override is a separate TU in the desktop window-chrome lib.
    ywindow-chrome/window-chrome.c
    # Base (platform-independent) clipboard yclass: the set_text / request_paste /
    # drain dispatch stubs. Same rationale as window-chrome above — terminal.c and
    # yrich link the stubs from core; the GLFW marshalling subclass (glfw_clipboard)
    # is a separate TU built into the desktop platform sources.
    yclipboard/clipboard.c
    # Base (platform-independent) app yclass: the yapp:app abstraction — init /
    # run dispatch stubs plus the weak app factory that concrete apps override.
    # In core so the platform can host any yapp:app subclass without naming it.
    ${YETTY_ROOT}/src/yetty/yapp/app.c
    ${YETTY_ROOT}/src/yetty/gen/api/yapp/app.c
    # Submodule accessor-lookup registrations (split codegen: the standalone
    # per-submodule aggregators live under src/yetty/gen/impl/yplatform/). The
    # module aggregator (gen/impl/yplatform/rpc.gen.c, compiled into
    # yetty_yplatform_window_chrome) calls each of these; the references are
    # declared weak so absence is tolerated on ELF where missing weak externs
    # resolve to NULL. Mach-O's static linker does not honor that — it rejects
    # any unresolved reference, weak or not — so the definitions must actually
    # be present in the link. The lookup tables themselves are guarded by
    # YETTY_PLATFORM_* macros (which are not globally set), so each definition
    # compiles down to registering a lookup hook that just returns NULL for
    # class names that don't belong to it.
    ${YETTY_ROOT}/src/yetty/gen/impl/yplatform/yclipboard/rpc.gen.c
    ${YETTY_ROOT}/src/yetty/gen/impl/yplatform/ywindow/rpc.gen.c
    # The window-chrome submodule's accessor-lookup registration. Its base
    # class (window-chrome.c) is already compiled into core above, but the
    # module aggregator now chains every submodule register as a strong extern,
    # so this shim must be present in every link too — the desktop build also
    # pulls a YETTY_PLATFORM_GLFW-flavoured copy via glfw.cmake, which the
    # linker prefers there; targets that don't (webasm, headless tools)
    # resolve the strong ref from this core copy.
    ${YETTY_ROOT}/src/yetty/gen/impl/yplatform/ywindow-chrome/rpc.gen.c
    ${YETTY_ROOT}/src/yetty/gen/impl/yplatform/yplatform/rpc.gen.c
    ${_core_per_os}
)

# These base yclass TUs carry C23 [[clang::annotate(...)]] attributes.
# Older clang (e.g. Apple clang 16) only knows the pre-rename `gnu2x` spelling;
# newer clang accepts `gnu23`. Detect once and reuse the supported flag.
if(NOT MSVC)
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-std=gnu23" YETTY_HAS_STD_GNU23)
    if(YETTY_HAS_STD_GNU23)
        set(_yetty_yplatform_c23_flag "-std=gnu23")
    else()
        set(_yetty_yplatform_c23_flag "-std=gnu2x")
    endif()
    set_source_files_properties(
        ywindow-chrome/window-chrome.c
        yclipboard/clipboard.c
        ${YETTY_ROOT}/src/yetty/yapp/app.c
        PROPERTIES COMPILE_OPTIONS "${_yetty_yplatform_c23_flag}")
endif()

target_include_directories(yetty_yplatform_core PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# yetty_api_yplatform — the object-API stubs (split codegen, gen/api). GPU- and
# platform-free by construction (each stub only marshals into the yclass
# runtime), so the whole tree compiles on every platform. Core PUBLIC-links it
# so every existing consumer of a yplatform method keeps resolving the stub
# transitively — no consumer link line changes.
file(GLOB_RECURSE YETTY_API_YPLATFORM_SOURCES
    ${YETTY_ROOT}/src/yetty/gen/api/yplatform/*.c)
add_library(yetty_api_yplatform STATIC ${YETTY_API_YPLATFORM_SOURCES})
target_include_directories(yetty_api_yplatform PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_link_libraries(yetty_api_yplatform PUBLIC yetty_ycore yetty_yclass)

# yetty_ycore provides the result-type macros every API uses; yetty_yclass
# provides the dispatch runtime the window_chrome method stubs call.
target_link_libraries(yetty_yplatform_core PUBLIC yetty_ycore yetty_yclass yetty_api_yplatform)

# Windows winsock + advapi (used by socket/ipc-socket/process impls).
if(WIN32)
    target_link_libraries(yetty_yplatform_core PUBLIC ws2_32 advapi32)
endif()

# Apple Foundation framework for the iOS/tvOS paths impl (NSBundle,
# NSFileManager, NSSearchPathForDirectoriesInDomains in paths/ios-tvos.m).
if(YETTY_IOS OR YETTY_TVOS)
    target_link_libraries(yetty_yplatform_core PUBLIC "-framework Foundation")
endif()

# YETTY_IOS / YETTY_TVOS as preprocessor macros for sources in this lib
# (process/default.c #if-guards the prohibited fork()/execvp() calls,
# similarly extract-assets and others may branch on them). The yetty exec
# target sets these definitions PRIVATEly on itself; without setting them
# on the static lib too, these sources compile as if they were on Linux.
if(YETTY_IOS)
    target_compile_definitions(yetty_yplatform_core PUBLIC YETTY_IOS=1)
endif()
if(YETTY_TVOS)
    target_compile_definitions(yetty_yplatform_core PUBLIC YETTY_TVOS=1)
endif()

# NOTE: a `yetty_yplatform` *server-side* library (event loops, GPU
# surface, clipboard, coroutine, pty-factory, etc.) would be the
# natural symmetric counterpart, but those sources have heavy
# transitive deps (glfw3webgpu, libuv, yetty_telnet, …) that aren't
# trivial to express as link/include declarations on an isolated
# static library — they're currently resolved by the all-inclusive
# include / link set on the main yetty exec target.
#
# For now those sources stay in YETTY_PLATFORM_SOURCES (per platform
# cmake.cmake). A future cleanup can extract them once we untangle
# the transitive deps. Tools that need server-side facets can't link
# them today — they have to be moved into yetty_yplatform_core first
# or get their own targeted shim library.
