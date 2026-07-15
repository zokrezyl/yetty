# yplot-stream — stdin number stream → live scrolling yplot figure
# (ring-buffer CMD_UPDATE envelopes). Pure C frontend over the GPU-less
# yetty_yplot_core, same link shape as tools/yplot.

add_executable(yplot-stream
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yplot-stream PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(yplot-stream PRIVATE
    yetty_yplot_core
    Threads::Threads
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(yplot-stream PRIVATE -Wall -Wextra)
endif()
