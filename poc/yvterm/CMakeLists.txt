# poc/yvterm — standalone text-grid performance probe.
#
# Drives a terminal from the libvterm STATE layer into one contiguous cell
# ring and renders it through the production MSDF/cdb text path, so we can
# measure throughput at 4K with small cells before committing to yvterm-new.
# Modeled on tools/ymaze; reuses the same platform-bootstrap sources.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

if(NOT YETTY_ENABLE_LIB_WEBGPU)
    return()
endif()

set(POC_YVTERM_PLATFORM_SOURCES
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

add_executable(poc-yvterm
    main.c
    grid.c
    shader.c
    ${POC_YVTERM_PLATFORM_SOURCES}
)

target_include_directories(poc-yvterm BEFORE PRIVATE
    ${YETTY_ROOT}/src
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src/libvterm-0.3.3/include
)

target_compile_definitions(poc-yvterm PRIVATE
    YETTY_WEB=0
    YETTY_ANDROID=0
    YETTY_USE_FORKPTY=1
)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)

target_link_libraries(poc-yvterm PRIVATE
    yetty_yfont_core
    yetty_ycdb
    vterm
    yetty_yframework
    ${YETTY_LIBS}
    yetty_ywire
    yetty_ydraw_core
    yetty_yplatform_core
    yetty_yplatform_move_resize
    yetty_yplatform_window_manager
    yetty_yco
    yetty_telnet
    ${BROTLIDEC_LIBRARIES}
    Threads::Threads
    ${FONTCONFIG_LIBRARIES}
    rt
    util
)

target_link_options(poc-yvterm PRIVATE
    "-Wl,--start-group"
)
target_link_libraries(poc-yvterm PRIVATE
    "-Wl,--end-group"
)
target_include_directories(poc-yvterm PRIVATE ${FONTCONFIG_INCLUDE_DIRS})

set_target_properties(poc-yvterm PROPERTIES LINKER_LANGUAGE CXX)

if(NOT MSVC)
    target_compile_options(poc-yvterm PRIVATE -Wall -Wextra)
endif()
