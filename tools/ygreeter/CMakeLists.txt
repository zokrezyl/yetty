# ygreeter — first-contact tool. Dual-mode:
#   - TERM_PROGRAM=yetty  → CLIENT mode. Ships OSC envelopes over stdout
#                           to the host yetty. libuv-driven, no GPU.
#   - otherwise           → STANDALONE mode. Opens its own GPU window
#                           via yinit_run + yframework_create, renders
#                           the figure tree locally through an in-process
#                           wire SM → yfigure_container → render_target.
#
# Standalone mode requires WebGPU/Dawn and GLFW. When YETTY_ENABLE_LIB_WEBGPU
# is OFF (e.g. the riscv64 cross-build — Dawn has no riscv64 prebuilt),
# we still build ygreeter, but only the client path. main.c gates the
# standalone block on YETTY_YGREETER_HAS_STANDALONE.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

# Sources shared by both modes — libuv pieces, pty plumbing, config,
# trace, notification, asset extraction. None of these pull in webgpu
# or glfw, so they build fine on the riscv cross.
set(YGREETER_SOURCES
    main.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/yworkpool/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/pipe/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/extract-assets/default.c
    ${YETTY_ROOT}/src/yetty/yncbin/incbin-assets.c
    ${YETTY_ROOT}/src/yetty/ypty/forkpty.c
    ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
    ${YETTY_ROOT}/src/yetty/yconfig/config.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/ynotify/ynotify.c
)

# Standalone-only sources: anything GLFW- or WebGPU-backed (the OS
# event loop, clipboard, window glue, surface, device). Drop when
# webgpu is off — client mode doesn't open a window or run a GPU
# event loop.
if(YETTY_ENABLE_LIB_WEBGPU)
    list(APPEND YGREETER_SOURCES
        ${YETTY_ROOT}/src/yetty/yinit/glfw.c
        ${YETTY_ROOT}/src/yetty/yframework/yframework.c
        ${YETTY_ROOT}/src/yetty/yplatform/os-event-loop/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/clipboard/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/window/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/window-manager/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/webgpu/default.c
    )
endif()

add_executable(ygreeter ${YGREETER_SOURCES})

target_include_directories(ygreeter BEFORE PRIVATE
    ${YETTY_ROOT}/src
    ${YETTY_ROOT}/include
)

target_compile_definitions(ygreeter PRIVATE
    YETTY_WEB=0
    YETTY_ANDROID=0
    YETTY_USE_FORKPTY=1
    YETTY_YGUI_HAS_UV
)
if(YETTY_ENABLE_LIB_WEBGPU)
    target_compile_definitions(ygreeter PRIVATE YETTY_YGREETER_HAS_STANDALONE)
endif()

find_package(Threads REQUIRED)
# fontconfig is only used by the standalone-mode font lookup path. Skip
# the pkg-config probe when standalone mode isn't being built — riscv64
# cross-builds don't ship fontconfig.pc.
if(YETTY_ENABLE_LIB_WEBGPU)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
endif()

# Client-mode link line — what's needed to build the OSC-emitting
# binary that runs inside a hosting yetty. Pulls in ygui (which
# transitively brings yfigure, the *_core helpers, ydraw-core,
# ysdf, etc.), the wire SM, libuv glue, coroutines, telnet, and
# the system libs. None of these reach for wgpu functions, so they
# all link on the riscv64 cross (with the headers-only webgpu stub
# defined in build-tools/yetty/platform/shared.cmake).
target_link_libraries(ygreeter PRIVATE
    ygui
    yetty_ywire
    yetty_yplatform_core
    yetty_yco
    yetty_telnet
    uv_a
    ${BROTLIDEC_LIBRARIES}
    Threads::Threads
    rt
    util
)

# Standalone-mode link line — Dawn-bound libs (ygrid/yimage/yplot
# call wgpu functions), the GLFW window helper, the full YETTY_LIBS
# (yetty_yrender / yetty_ywebgpu / yetty_yterm are only declared
# when webgpu is on), fontconfig for font discovery, ydraw-factory
# for the figure factory. Only linked when webgpu is on.
if(YETTY_ENABLE_LIB_WEBGPU)
    target_link_libraries(ygreeter PRIVATE
        yetty_yfigure
        yetty_ygrid
        yetty_yfont_core
        yetty_ydraw_factory
        yetty_yimage
        yetty_yplot
        ${YETTY_LIBS}
        yetty_yplatform_move_resize
        ${FONTCONFIG_LIBRARIES}
    )
    target_include_directories(ygreeter PRIVATE ${FONTCONFIG_INCLUDE_DIRS})
endif()

# Dawn (libwebgpu_dawn) is C++ — needs C++ runtime even though our
# sources are C. Only relevant when webgpu is in the link line; harmless
# to keep otherwise (CXX linker driver still works for pure C).
set_target_properties(ygreeter PROPERTIES LINKER_LANGUAGE CXX)

if(NOT MSVC)
    target_compile_options(ygreeter PRIVATE -Wall -Wextra)
endif()
