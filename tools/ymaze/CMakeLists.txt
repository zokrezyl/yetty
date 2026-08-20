# tools/ymaze — animated maze, IN-TERMINAL ONLY (no WebGPU, no window).
#
# A plain-C app.c forwarding to the GPU-free yguiapp terminal host
# (yetty_yguiapp_terminal): inside a hosting yetty the ymaze ygui widget
# renders into the pane over ywire; outside yetty the tool refuses to run.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

add_executable(ymaze
    app.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ymaze PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(ymaze PRIVATE
    yetty_yguiapp_terminal
    Threads::Threads
    rt
    util
)

if(NOT MSVC)
    target_compile_options(ymaze PRIVATE -Wall -Wextra)
endif()
