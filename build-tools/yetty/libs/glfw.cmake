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

# Private headers from the glfw source tree, staged into the prebuilt
# tarball under include-private/ (see build-tools/3rdparty/glfw/_build.sh).
# Exposed via a SEPARATE interface target so only the one TU that needs
# them (yetty's Wayland interactive-move helper) opts in — the rest of the
# codebase keeps using GLFW's public API. The target is created only when
# the tarball actually contains the directory (Linux Wayland builds only;
# macOS / Windows / mobile / web tarballs don't ship it).
if(NOT TARGET glfw_private_headers AND EXISTS "${_GLFW_DIR}/include-private/internal.h")
    add_library(glfw_private_headers INTERFACE IMPORTED GLOBAL)
    set_target_properties(glfw_private_headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_GLFW_DIR}/include-private"
        # Reuses the same Wayland deps glfw already pulls in (xdg-shell
        # protocol headers reference wl_proxy, wl_seat, etc.).
        INTERFACE_LINK_LIBRARIES "glfw")
    message(STATUS "glfw: private headers available at ${_GLFW_DIR}/include-private")
endif()

# yetty_yplatform_move_resize — protocol-correct interactive window
# move/resize on Wayland. Lives in its own static lib so the private GLFW
# headers stay scoped to one TU and don't pollute the rest of the codebase.
# When the prebuilt tarball doesn't ship private headers (macOS / Windows /
# mobile / web), the consumer falls back to a stub TU that no-ops the same
# public functions — keeps the caller (window-manager) platform-agnostic.
if(NOT TARGET yetty_yplatform_move_resize)
    if(TARGET glfw_private_headers)
        add_library(yetty_yplatform_move_resize STATIC
            ${YETTY_ROOT}/src/yetty/yplatform/move-resize/wayland.c)
        target_link_libraries(yetty_yplatform_move_resize
            PRIVATE glfw_private_headers glfw)
    else()
        add_library(yetty_yplatform_move_resize STATIC
            ${YETTY_ROOT}/src/yetty/yplatform/move-resize/null.c)
        target_link_libraries(yetty_yplatform_move_resize PRIVATE glfw)
    endif()
    target_include_directories(yetty_yplatform_move_resize PUBLIC
        ${YETTY_ROOT}/include)
    target_link_libraries(yetty_yplatform_move_resize PUBLIC yetty_ycore)
endif()

# yetty_yplatform_window_manager — yclass class `yplatform:window_manager`:
# the render→main marshaling for OS window control (move/resize/min/max/close/
# cursor). Its own static lib (like move_resize above) so the GLFW dependency
# and the C23 `[[clang::annotate]]` codegen attributes stay scoped to this TU
# set instead of leaking into the main exec's source list. window-manager.gen.c
# (method stubs + rpc skeletons + create/register, consolidated) is #included
# at the foot of window-manager.c.
if(NOT TARGET yetty_yplatform_window_manager)
    # window-manager.c #includes window-manager.gen.c at its foot, which now
    # carries the method stubs, rpc skeletons, create() and registration. The
    # whole class — GLFW impl included — builds as this one desktop-only TU.
    add_library(yetty_yplatform_window_manager STATIC
        ${YETTY_ROOT}/src/yetty/yplatform/window-manager.c)
    target_include_directories(yetty_yplatform_window_manager
        PUBLIC ${YETTY_ROOT}/include
        PRIVATE ${YETTY_ROOT}/src)
    target_link_libraries(yetty_yplatform_window_manager
        PUBLIC yetty_ycore yetty_yclass
        PRIVATE glfw yetty_yplatform_move_resize)
    # window-manager.c carries C23 `[[clang::annotate(...)]]` attributes.
    if(NOT MSVC)
        set_target_properties(yetty_yplatform_window_manager PROPERTIES
            C_STANDARD 23
            C_STANDARD_REQUIRED ON)
    endif()
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
