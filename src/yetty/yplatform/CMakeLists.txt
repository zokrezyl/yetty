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
#     pipe / pty-factory plumbing, clipboard manager, window-manager,
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
    paths/singleton.c
    ${_core_per_os}
)

target_include_directories(yetty_yplatform_core PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# yetty_ycore provides the result-type macros every API uses.
target_link_libraries(yetty_yplatform_core PUBLIC yetty_ycore)

# Windows winsock + advapi (used by socket/ipc-socket/process impls).
if(WIN32)
    target_link_libraries(yetty_yplatform_core PUBLIC ws2_32 advapi32)
endif()

# Apple Foundation framework for the iOS/tvOS paths impl (NSBundle,
# NSFileManager, NSSearchPathForDirectoriesInDomains in paths/ios-tvos.m).
if(YETTY_IOS OR YETTY_TVOS)
    target_link_libraries(yetty_yplatform_core PUBLIC "-framework Foundation")
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
