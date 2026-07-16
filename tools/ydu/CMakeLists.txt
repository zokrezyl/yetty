# ydu — visual disk-usage explorer on the ygui widget engine.
#
# The scan backend (scan.c) uses opendir/readdir/lstat; the current build is
# gated to Linux to match the other /proc- and POSIX-oriented tools. A macOS
# backend is a drop-in later (the scanner itself is portable POSIX).

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

# ytrace is compiled per-final-target (not a standalone library); the rest
# come from yetty_yplatform_core, pulled in transitively by ygui.
add_executable(ydu
    main.c
    ui.c
    scan.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ydu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)

# yetty_ygui pulls in ydraw-core, ysdf, ycore, yface, yfont, yfigure, yplot,
# yplatform_core, etc. as transitive PUBLIC deps. ydu drives its own libuv
# client loop, so libuv (uv_a) is listed explicitly.
target_link_libraries(ydu PRIVATE yetty_ygui uv_a Threads::Threads)

if(NOT MSVC)
    target_compile_options(ydu PRIVATE -Wall -Wextra)
endif()
