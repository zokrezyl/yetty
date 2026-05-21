# yrdawn-demo — smoke test for the WebGPU-over-OSC bridge.
#
# Pure-C frontend that links libyetty_yrdawn and talks to the local
# yetty's yrdawn-layer over stdin/stdout. Built when
# YETTY_ENABLE_TOOL_YRDAWN_DEMO is ON; the tools/CMakeLists.txt guard
# selects.

add_executable(yrdawn-demo
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yrdawn-demo PRIVATE
    ${YETTY_ROOT}/include
)

find_package(Threads REQUIRED)
target_link_libraries(yrdawn-demo PRIVATE
    yetty_yrdawn
    yetty_yplatform_core
    Threads::Threads
)

if(NOT MSVC)
    target_compile_options(yrdawn-demo PRIVATE -Wall -Wextra)
endif()
