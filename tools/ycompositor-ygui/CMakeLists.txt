# tools/ycompositor-ygui — ygui rendered through the new ycompositor path.
#
# Sibling of tools/ycompositor (which is a pure direct-API ygrid demo).
# This one allocates an in-process (headless) ygui engine, builds a tiny
# widget tree, calls engine_rebuild to produce the canonical drawable_list
# bytes, and feeds them straight into the compositor via
# root container_process_records. No PTY / no OSC / no parent yetty.
#
# Used to validate the new ygui→compositor pipeline that will eventually
# replace scene-canvas inside the main yetty binary.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

if(NOT YETTY_ENABLE_LIB_WEBGPU)
    return()
endif()

set(YCOMP_YGUI_PLATFORM_SOURCES
    ${YETTY_ROOT}/src/yetty/yinit/glfw.c
    ${YETTY_ROOT}/src/yetty/yframework/yframework.c
    ${YETTY_ROOT}/src/yetty/yplatform/os-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/clipboard/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/window/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu/default.c
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

add_executable(ycompositor-ygui
    main.c
    ${YCOMP_YGUI_PLATFORM_SOURCES}
)

target_include_directories(ycompositor-ygui BEFORE PRIVATE
    ${YETTY_ROOT}/src
    ${YETTY_ROOT}/include
)

target_compile_definitions(ycompositor-ygui PRIVATE
    YETTY_WEB=0
    YETTY_ANDROID=0
    YETTY_USE_FORKPTY=1
)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)

target_link_libraries(ycompositor-ygui PRIVATE

    yetty_ygui
    yetty_ygrid
    yetty_yfigure
    yetty_ymgui
    yetty_yface
    yetty_ydraw_core
    yetty_yframework
    ${YETTY_LIBS}
    yetty_ywire
    yetty_ydraw_core
    yetty_yplatform_core
    yetty_yplatform_move_resize
    yetty_yplatform_window_manager
    yetty_ychrome
    yetty_yco
    yetty_telnet
    ${BROTLIDEC_LIBRARIES}
    Threads::Threads
    ${FONTCONFIG_LIBRARIES}
    rt
    util
)

target_link_options(ycompositor-ygui PRIVATE
    "-Wl,--start-group"
)
target_link_libraries(ycompositor-ygui PRIVATE
    "-Wl,--end-group"
)
target_include_directories(ycompositor-ygui PRIVATE ${FONTCONFIG_INCLUDE_DIRS})

set_target_properties(ycompositor-ygui PROPERTIES LINKER_LANGUAGE CXX)

if(NOT MSVC)
    target_compile_options(ycompositor-ygui PRIVATE -Wall -Wextra)
endif()
