# glfw + glfw3webgpu — desktop windowing.
#
# Consumes prebuilt static libs + headers from the 3rdparty releases
# published by build-3rdparty-glfw.yml + build-3rdparty-glfw3webgpu.yml.
# The from-source builds live in build-tools/3rdparty/{glfw,glfw3webgpu}/.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET glfw)
    return()
endif()

yetty_3rdparty_fetch(glfw _GLFW_DIR)

# Use upstream's exported cmake config — it already encodes the right
# INTERFACE_LINK_LIBRARIES for whatever was enabled at build time
# (X11 deps, Wayland deps, xkbcommon, pthread/dl/m/rt on linux; Cocoa/
# IOKit/CoreFoundation/QuartzCore on macOS; gdi32/user32/shell32 on
# Windows). Avoids duplicating that list — and getting it wrong — here.
find_package(glfw3 CONFIG REQUIRED PATHS "${_GLFW_DIR}" NO_DEFAULT_PATH)

if(NOT TARGET glfw)
    message(FATAL_ERROR
        "glfw: glfw3Config.cmake did not export the `glfw` target — \
tarball layout changed? expected ${_GLFW_DIR}/lib/cmake/glfw3/")
endif()

# find_package's imported target defaults to directory scope. Promote it to
# global so consumers in sibling directories (imgui.cmake, the top-level
# yetty link list) resolve `glfw` to the imported lib instead of falling
# through to the -lglfw library-search path (where libglfw doesn't exist —
# we ship libglfw3 only).
set_target_properties(glfw PROPERTIES IMPORTED_GLOBAL TRUE)

message(STATUS "glfw: prebuilt v${YETTY_3RDPARTY_glfw_VERSION} (via glfw3Config)")

#------------------------------------------------------------------------------
# glfw3webgpu — adapter that creates a WGPUSurface from a glfw window.
#------------------------------------------------------------------------------
if(NOT TARGET glfw3webgpu)
    yetty_3rdparty_fetch(glfw3webgpu _GLFW3WEBGPU_DIR)

    if(WIN32)
        set(_GLFW3WGPU_LIB "${_GLFW3WEBGPU_DIR}/lib/libglfw3webgpu.lib")
    else()
        set(_GLFW3WGPU_LIB "${_GLFW3WEBGPU_DIR}/lib/libglfw3webgpu.a")
    endif()

    if(NOT EXISTS "${_GLFW3WGPU_LIB}")
        message(FATAL_ERROR "glfw3webgpu: archive not found at ${_GLFW3WGPU_LIB} — tarball layout changed?")
    endif()

    add_library(glfw3webgpu STATIC IMPORTED GLOBAL)
    set_target_properties(glfw3webgpu PROPERTIES
        IMPORTED_LOCATION "${_GLFW3WGPU_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${_GLFW3WEBGPU_DIR}/include"
        INTERFACE_LINK_LIBRARIES "glfw"
    )

    message(STATUS "glfw3webgpu: prebuilt @${YETTY_3RDPARTY_glfw3webgpu_VERSION} (${_GLFW3WGPU_LIB})")
endif()
