# ywasm-demo — smoke test for the WebGPU-over-OSC bridge.
#
# Pure-C frontend that links libyetty_ywasm and talks to the local
# yetty's ywasm-layer over stdin/stdout. Built when
# YETTY_ENABLE_TOOL_YWASM_DEMO is ON; the tools/CMakeLists.txt guard
# selects.

add_executable(ywasm-demo
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ywasm-demo PRIVATE
    ${YETTY_ROOT}/include
)

find_package(Threads REQUIRED)
target_link_libraries(ywasm-demo PRIVATE
    yetty_ywasm
    yetty_yplatform_core
    Threads::Threads
)

if(NOT MSVC)
    target_compile_options(ywasm-demo PRIVATE -Wall -Wextra)
endif()
