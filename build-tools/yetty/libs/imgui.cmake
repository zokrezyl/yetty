# imgui — Immediate-mode GUI.
#
# Consumes a prebuilt static lib (5 core .cpp files) + headers + backend
# SOURCE files from the 3rdparty release tarball published by
# build-3rdparty-imgui.yml. The from-source build lives in
# build-tools/3rdparty/imgui/_build.sh.
#
# Why ship backend SOURCE rather than prebuilt: the imgui_impl_glfw and
# imgui_impl_wgpu backends need target-specific compile defines
# (IMGUI_IMPL_WEBGPU_BACKEND_DAWN/WGPU, -x objective-c++ on macOS for
# wgpu, etc.). Prebuilding them per-host doesn't match yetty's flag
# matrix; we compile them fresh here.
#
# Exposed targets:
#   imgui       — full target (core + platform-appropriate backends)
#   imgui_core  — lean target (just the 5 core .cpp), for consumers
#                 that bring their own backend (e.g. ymgui_frontend).

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET imgui)
    return()
endif()

yetty_3rdparty_fetch(imgui _IMGUI_DIR)

if(WIN32)
    set(_IMGUI_LIB "${_IMGUI_DIR}/lib/libimgui_core.lib")
else()
    set(_IMGUI_LIB "${_IMGUI_DIR}/lib/libimgui_core.a")
endif()

if(NOT EXISTS "${_IMGUI_LIB}")
    message(FATAL_ERROR "imgui: archive not found at ${_IMGUI_LIB} — tarball layout changed?")
endif()

#-----------------------------------------------------------------------------
# imgui_core — the prebuilt lean target.
#-----------------------------------------------------------------------------
add_library(imgui_core STATIC IMPORTED GLOBAL)
set_target_properties(imgui_core PROPERTIES
    IMPORTED_LOCATION "${_IMGUI_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_IMGUI_DIR}/include"
)

#-----------------------------------------------------------------------------
# imgui — full target. Built fresh from the staged backend sources +
# imgui_core, with target-specific compile defines applied.
#-----------------------------------------------------------------------------
set(_IMGUI_BACKEND_SOURCES "")

# imgui_impl_wgpu.cpp: skip on iOS/tvOS — the backend unconditionally
# includes <Cocoa/Cocoa.h> in its __APPLE__ branch, which doesn't exist
# on iOS/tvOS. Same exclusion the from-source consumer had. Also skip
# when WebGPU is disabled (e.g. linux-riscv64 cross-build) or when the
# backend file isn't present in the tarball (pure-core builds).
if(NOT (YETTY_IOS OR YETTY_TVOS
        OR CMAKE_SYSTEM_NAME STREQUAL "iOS"
        OR CMAKE_SYSTEM_NAME STREQUAL "tvOS")
        AND YETTY_ENABLE_LIB_WEBGPU
        AND EXISTS "${_IMGUI_DIR}/src-backends/imgui_impl_wgpu.cpp")
    list(APPEND _IMGUI_BACKEND_SOURCES "${_IMGUI_DIR}/src-backends/imgui_impl_wgpu.cpp")
endif()

# Platform backend: GLFW on desktop + emscripten (USE_GLFW=3 stub),
# none on iOS/tvOS/Android (custom yetty backend). Skip when GLFW is
# disabled or the backend file isn't shipped (pure-core tarballs).
if(EMSCRIPTEN)
    if(EXISTS "${_IMGUI_DIR}/src-backends/imgui_impl_glfw.cpp")
        list(APPEND _IMGUI_BACKEND_SOURCES "${_IMGUI_DIR}/src-backends/imgui_impl_glfw.cpp")
    endif()
elseif(YETTY_ANDROID OR YETTY_IOS OR YETTY_TVOS
        OR CMAKE_SYSTEM_NAME STREQUAL "iOS"
        OR CMAKE_SYSTEM_NAME STREQUAL "tvOS")
    # no platform backend
elseif(YETTY_ENABLE_LIB_GLFW
        AND EXISTS "${_IMGUI_DIR}/src-backends/imgui_impl_glfw.cpp")
    list(APPEND _IMGUI_BACKEND_SOURCES "${_IMGUI_DIR}/src-backends/imgui_impl_glfw.cpp")
endif()

# On iOS/tvOS we exclude every upstream backend (Metal+Cocoa unavailable
# from the wgpu backend, no GLFW). With no sources cmake refuses
# `add_library STATIC`. Use INTERFACE there — consumers still get headers
# and link to imgui_core. Custom backend lives in src/yetty/ymgui/frontend/.
if(_IMGUI_BACKEND_SOURCES)
    add_library(imgui STATIC ${_IMGUI_BACKEND_SOURCES})
    target_link_libraries(imgui PUBLIC imgui_core)
    target_include_directories(imgui PUBLIC "${_IMGUI_DIR}/include" "${_IMGUI_DIR}/src-backends")
    set_target_properties(imgui PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    target_compile_options(imgui PRIVATE -w)
else()
    add_library(imgui INTERFACE)
    target_link_libraries(imgui INTERFACE imgui_core)
    target_include_directories(imgui INTERFACE "${_IMGUI_DIR}/include" "${_IMGUI_DIR}/src-backends")
endif()

# Platform-specific link + compile flags. Mirrors the from-source
# imgui.cmake exactly — only the source-of-truth changed (now staged
# files instead of CPM-fetched).
if(EMSCRIPTEN)
    target_link_libraries(imgui PUBLIC webgpu)
    target_compile_options(imgui PUBLIC --use-port=emdawnwebgpu)
    target_link_options(imgui PUBLIC -sUSE_GLFW=3)
    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_DAWN=1)
elseif(YETTY_IOS OR YETTY_TVOS
        OR CMAKE_SYSTEM_NAME STREQUAL "iOS"
        OR CMAKE_SYSTEM_NAME STREQUAL "tvOS")
    # On iOS/tvOS imgui is an INTERFACE library (no upstream backend sources
    # compile there) — must use INTERFACE keyword, not PUBLIC.
    target_link_libraries(imgui INTERFACE webgpu)
    if(WEBGPU_BACKEND STREQUAL "wgpu")
        target_compile_definitions(imgui INTERFACE IMGUI_IMPL_WEBGPU_BACKEND_WGPU=1)
    else()
        target_compile_definitions(imgui INTERFACE IMGUI_IMPL_WEBGPU_BACKEND_DAWN=1)
    endif()
elseif(YETTY_ANDROID)
    # Android: imgui_impl_wgpu compiles fine, library is STATIC.
    target_link_libraries(imgui PUBLIC webgpu)
    if(WEBGPU_BACKEND STREQUAL "wgpu")
        target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_WGPU=1)
    else()
        target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_DAWN=1)
    endif()
elseif(_IMGUI_BACKEND_SOURCES)
    # Desktop: glfw + webgpu, plus Obj-C++ flag for wgpu impl on macOS.
    include(${CMAKE_CURRENT_LIST_DIR}/glfw.cmake)
    if(APPLE)
        target_link_libraries(imgui PUBLIC glfw webgpu)
        set_source_files_properties(
            "${_IMGUI_DIR}/src-backends/imgui_impl_wgpu.cpp"
            PROPERTIES COMPILE_FLAGS "-x objective-c++"
        )
    elseif(WIN32)
        # No X11 on Windows; glfw's INTERFACE_LINK_LIBRARIES already pulls in
        # the Win32 GDI/USER bits the platform backend needs.
        target_link_libraries(imgui PUBLIC glfw webgpu)
    else()
        find_package(X11 REQUIRED)
        target_link_libraries(imgui PUBLIC glfw webgpu X11::X11)
    endif()
    if(WEBGPU_BACKEND STREQUAL "wgpu")
        target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_WGPU=1)
    else()
        target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_DAWN=1)
    endif()
endif()
# else: pure-core INTERFACE imgui (no backends, no X11/GLFW/WebGPU deps) —
# used by cross-builds (linux-riscv64) that consume only imgui_core.

message(STATUS "imgui: prebuilt v${YETTY_3RDPARTY_imgui_VERSION} (core: ${_IMGUI_DIR}/lib/libimgui_core.a)")
