# yplot tool — emits a yplot complex-prim OSC envelope from an expression.
# Pure C frontend over the yetty_yplot library.

add_executable(yplot
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/getopt.c
    ${YETTY_YPLATFORM_THREAD_SOURCES}
)

target_include_directories(yplot PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# Threads::Threads picks the right thing per platform (linux/macos: pthread;
# android: bionic libc). Same pattern as tools/yecho and tools/ycat.
find_package(Threads REQUIRED)
target_link_libraries(yplot PRIVATE
    yetty_yplot
    yetty_webgpu
    Threads::Threads
)

if(NOT MSVC)
    target_compile_options(yplot PRIVATE -Wall -Wextra)
endif()
