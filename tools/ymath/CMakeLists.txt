# ymath tool — TeX-subset math → YDRAW_BIN OSC envelope. Same link shape
# as tools/yplot.

add_executable(ymath
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ymath PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(ymath PRIVATE
    yetty_ymath
    Threads::Threads
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(ymath PRIVATE -Wall -Wextra)
endif()
