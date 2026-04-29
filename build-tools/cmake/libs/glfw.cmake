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

if(WIN32)
    set(_GLFW_LIB "${_GLFW_DIR}/lib/glfw3.lib")
else()
    set(_GLFW_LIB "${_GLFW_DIR}/lib/libglfw3.a")
endif()

if(NOT EXISTS "${_GLFW_LIB}")
    message(FATAL_ERROR "glfw: archive not found at ${_GLFW_LIB} — tarball layout changed?")
endif()

add_library(glfw STATIC IMPORTED GLOBAL)
set_target_properties(glfw PROPERTIES
    IMPORTED_LOCATION "${_GLFW_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_GLFW_DIR}/include"
)
# Platform link deps glfw needs from its consumers (mirrors what the
# upstream cmake config exports as INTERFACE_LINK_LIBRARIES).
if(APPLE)
    set_target_properties(glfw PROPERTIES
        INTERFACE_LINK_LIBRARIES "-framework Cocoa;-framework IOKit;-framework CoreFoundation;-framework QuartzCore"
    )
elseif(WIN32)
    # System libs glfw needs on Windows (gdi32 for window creation, opengl32
    # default loader, user32 for input, etc.). Mirrors upstream glfw3-config.
    set_target_properties(glfw PROPERTIES
        INTERFACE_LINK_LIBRARIES "gdi32;user32;shell32;opengl32"
    )
elseif(UNIX)
    set_target_properties(glfw PROPERTIES
        INTERFACE_LINK_LIBRARIES "pthread;dl;m;rt"
    )
endif()

message(STATUS "glfw: prebuilt v${YETTY_3RDPARTY_glfw_VERSION} (${_GLFW_LIB})")

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
