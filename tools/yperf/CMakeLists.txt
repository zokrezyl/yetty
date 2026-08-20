# yperf — perf / flame-graph workbench on the ygui widget engine.
#
# Ingestion (folded parse + perf-script collapse) is portable, but the client
# uses libuv + termios and the `--perf` path shells out to perf, so the build is
# gated to Linux like the other ygui-client tools. The flame render is reused
# from the yetty_yflame library (gated by FEATURE_YFLAME in tools/CMakeLists).

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

add_executable(yperf
    main.c
    ui.c
    profile.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yperf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)

# yetty_ygui pulls in ydraw-list, ysdf, ycore, yface, yfont, yfigure, yplot,
# yplatform_core transitively; yetty_yflame provides the flame-graph class;
# uv_a for the client loop.
target_link_libraries(yperf PRIVATE yetty_ygui yetty_yflame uv_a Threads::Threads)

if(NOT MSVC)
    target_compile_options(yperf PRIVATE -Wall -Wextra)
endif()
