# libyetty_ffi.so — shared library exposing yclass module symbols for the
# language bindings under bindings/<lang>/. Built ONLY in the PIC double-build
# tree (YETTY_BUILD_FFI_SHARED=ON); never linked by the static application, so
# the non-PIC application archives keep their full speed.

# ytrace.c is compiled per-consumer (not a static lib target — see
# tools/ycat/CMakeLists.txt), so include it directly to satisfy the
# trace/log symbols (ytrace_output, …) the module archives reference.
add_library(yetty_ffi SHARED
    yffi.c
    # yclass-generated packages such as yguiapp / yrich expose app classes
    # whose archive objects reference the concrete desktop platform and
    # yframework host. The main yetty executable and standalone ygui apps
    # compile these sources inline; the FFI shared object must do the same so
    # generated bindings can import every emitted package without dlopen holes.
    ${YETTY_APP_BOOTSTRAP_SOURCES}
    ${YETTY_ROOT}/src/yetty/yframework/yframework.c
    ${YETTY_ROOT}/src/yetty/yplatform/os-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/yworkpool/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/pipe/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/ywindow-chrome/glfw.c
    ${YETTY_ROOT}/src/yetty/yplatform/move-resize/null.c
    ${YETTY_ROOT}/src/yetty/yncbin/incbin-assets.c
    ${YETTY_ROOT}/src/yetty/ypty/forkpty.c
    ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
    ${YETTY_ROOT}/src/yetty/yconfig/config.c
    ${YETTY_ROOT}/src/yetty/ynotify/ynotify.c
    ${YETTY_ROOT}/src/yetty/yrdawn/figure.c
    ${YETTY_ROOT}/src/yetty/yrdawn/session.c
    ${YETTY_ROOT}/src/yetty/yrdawn/rpc.gen.c
    ${YETTY_ROOT}/src/yetty/yrdawn/server-dispatch-stub.c
    ${YETTY_ROOT}/src/yetty/yrich/yrich-shell.c
    ${YETTY_ROOT}/src/yetty/yrich/app.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yetty_ffi PRIVATE
    ${YETTY_ROOT}/include/yetty/yrdawn
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
    $<TARGET_PROPERTY:glfw,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:glfw3webgpu,INTERFACE_INCLUDE_DIRECTORIES>
)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(YETTY_FFI_FONTCONFIG REQUIRED fontconfig)

target_include_directories(yetty_ffi PRIVATE ${YETTY_FFI_FONTCONFIG_INCLUDE_DIRS})

target_compile_definitions(yetty_ffi PRIVATE
    YETTY_WEB=0
    YETTY_ANDROID=0
    YETTY_USE_FORKPTY=1
    YETTY_PLATFORM_GLFW=1
)

# The imported GLFW targets are tuned for final executables and, on this Linux
# build, propagate non-PIC static X11 archives. FFI is a shared object, so link
# the GLFW archives as files and pair them with dynamic platform libs.
set(YETTY_FFI_GLFW_DYNAMIC_LIBS
    X11
    Xrandr
    Xinerama
    Xcursor
    Xi
    Xext
    wayland-client
    wayland-cursor
    wayland-egl
    xkbcommon
    ${CMAKE_DL_LIBS}
    m
)

if(TARGET webgpu AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(webgpu PROPERTIES
        INTERFACE_LINK_LIBRARIES "${YETTY_FFI_GLFW_DYNAMIC_LIBS};pthread")
endif()

if(TARGET yetty_yrender AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(yetty_yrender PROPERTIES
        INTERFACE_LINK_LIBRARIES
        "yetty_yrender_core;webgpu;yetty_yrender_utils;${YETTY_FFI_GLFW_DYNAMIC_LIBS};yetty_ywebgpu")
endif()

# Force-include each FFI-exposed module's whole archive so its
# `yetty_<module>_*` stubs are exported even though nothing in this TU
# references them. Transitive deps (ycore / yclass / yface / ydraw-core) are
# pulled normally to satisfy the references. yplatform_core provides the
# thread / timestamp helpers ytrace depends on AND the generated yapp symbols
# (yapp is compiled into yplatform_core, not a standalone target). Keep this in
# sync with bindings/python/yetty/generated/__init__.py: if codegen emits a
# Python package for a yclass module, libyetty_ffi.so must export its C symbols.
target_link_libraries(yetty_ffi PRIVATE
    -Wl,--whole-archive
    yetty_ychrome
    yetty_ycircuit
    yetty_yfigure
    yetty_yflame
    yetty_ygrid
    yetty_ygui
    yetty_yguiapp
    yetty_ymap
    yetty_ymgui
    yetty_ymusic
    yetty_yrdawn
    yetty_yrich
    yetty_yshadertoy
    yetty_yview
    yetty_yvterm
    yetty_yplatform_core
    # The multiplexed PTY endpoint surface the bindings drive directly (the #380
    # reactor seam): transport-pty (yclass) + connection/channel (ywire). These
    # are plain-C functions, not yclass stubs, so nothing else in the .so
    # references them — whole-archive keeps their symbols exported for ctypes.
    yetty_yclass
    yetty_ywire
    -Wl,--no-whole-archive
    yetty_ydraw     # full ydraw (cell-ref-table, …), referenced by yvterm
    yetty_yframework
    yetty_yface
    yetty_yctl
    yetty_vnc
    yetty_yrender
    yetty_yrender_core
    yetty_yfont_core
    yetty_ydraw_factory
    yetty_yimage
    yetty_yplot
    $<TARGET_FILE:glfw3webgpu>
    $<TARGET_FILE:glfw>
    yetty_yco
    yetty_telnet
    uv_a            # ygui framework is libuv-driven
    ${YETTY_FFI_FONTCONFIG_LIBRARIES}
    ${YETTY_FFI_GLFW_DYNAMIC_LIBS}
    rt
    util
    Threads::Threads
)

# Fail the link on any unresolved symbol so a missing transitive module shows
# up here at build time, not as a dlopen error in the bindings.
target_link_options(yetty_ffi PRIVATE -Wl,--no-undefined)

set_target_properties(yetty_ffi PROPERTIES
    OUTPUT_NAME yetty_ffi
    POSITION_INDEPENDENT_CODE ON
)

if(NOT MSVC)
    set_target_properties(yetty_ffi PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
