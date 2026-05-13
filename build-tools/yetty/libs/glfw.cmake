# glfw + glfw3webgpu — desktop windowing.
#
# Consumes prebuilt static libs + headers from the 3rdparty releases
# published by build-3rdparty-glfw.yml + build-3rdparty-glfw3webgpu.yml.
# The from-source builds live in build-tools/3rdparty/{glfw,glfw3webgpu}/.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET glfw)
    return()
endif()

yetty_3rdparty_fetch(glfw _GLFW_DIR)

# Prefer upstream's exported cmake config — it already encodes the right
# INTERFACE_LINK_LIBRARIES for whatever was enabled at build time
# (X11 deps, Wayland deps, xkbcommon, pthread/dl/m/rt on linux; Cocoa/
# IOKit/CoreFoundation/QuartzCore on macOS; gdi32/user32/shell32 on
# Windows). Some older prebuilts (notably glfw-linux-aarch64-3.4) ship
# only `lib/libglfw3.a` + headers — no `lib/cmake/glfw3/` — so we fall
# back to constructing the IMPORTED target by hand with the right
# platform link list. Keep the fallback list in sync with what GLFW's
# own `find_package(...)` resolves to upstream.
find_package(glfw3 CONFIG QUIET PATHS "${_GLFW_DIR}" NO_DEFAULT_PATH)

if(NOT TARGET glfw)
    if(WIN32)
        set(_GLFW_LIB "${_GLFW_DIR}/lib/glfw3.lib")
    else()
        set(_GLFW_LIB "${_GLFW_DIR}/lib/libglfw3.a")
    endif()
    if(NOT EXISTS "${_GLFW_LIB}")
        message(FATAL_ERROR
            "glfw: neither glfw3Config.cmake nor ${_GLFW_LIB} found in \
${_GLFW_DIR} — tarball layout changed?")
    endif()

    add_library(glfw STATIC IMPORTED GLOBAL)
    set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION "${_GLFW_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${_GLFW_DIR}/include")

    if(APPLE)
        # GLFW on macOS links the Cocoa stack.
        set_property(TARGET glfw APPEND PROPERTY INTERFACE_LINK_LIBRARIES
            "-framework Cocoa" "-framework IOKit"
            "-framework CoreFoundation" "-framework QuartzCore")
    elseif(WIN32)
        # GLFW on Win32 links the user/gdi/shell stack.
        set_property(TARGET glfw APPEND PROPERTY INTERFACE_LINK_LIBRARIES
            gdi32 user32 shell32)
    elseif(UNIX)
        # Linux/BSD: X11 + Wayland (both backends are compiled in upstream;
        # GLFW picks at runtime). Use pkg-config when available to keep the
        # exact lib names in sync with the host's xorg/wayland packages.
        find_package(Threads REQUIRED)
        set(_GLFW_LIBS Threads::Threads ${CMAKE_DL_LIBS} m)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            foreach(_pc x11 xrandr xinerama xcursor xi xext
                        wayland-client wayland-cursor wayland-egl
                        xkbcommon)
                pkg_check_modules(_GLFW_PC_${_pc} QUIET ${_pc})
                if(_GLFW_PC_${_pc}_FOUND)
                    list(APPEND _GLFW_LIBS ${_GLFW_PC_${_pc}_LIBRARIES})
                endif()
            endforeach()
        else()
            # No pkg-config — fall back to bare library names; the linker
            # search path on the runner has /usr/lib/<arch>-linux-gnu.
            list(APPEND _GLFW_LIBS X11 Xrandr Xinerama Xcursor Xi Xext
                                   xkbcommon)
        endif()
        list(REMOVE_DUPLICATES _GLFW_LIBS)
        set_property(TARGET glfw APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES ${_GLFW_LIBS})
    endif()

    message(STATUS "glfw: prebuilt v${YETTY_3RDPARTY_glfw_VERSION} (fallback, no glfw3Config.cmake in tarball)")
else()
    # find_package's imported target defaults to directory scope. Promote
    # it to global so consumers in sibling directories (imgui.cmake, the
    # top-level yetty link list) resolve `glfw` to the imported lib
    # instead of falling through to the -lglfw library-search path (where
    # libglfw doesn't exist — we ship libglfw3 only).
    set_target_properties(glfw PROPERTIES IMPORTED_GLOBAL TRUE)
    message(STATUS "glfw: prebuilt v${YETTY_3RDPARTY_glfw_VERSION} (via glfw3Config)")
endif()

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
