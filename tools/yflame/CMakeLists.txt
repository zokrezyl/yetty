# yflame tool — emits a flame-graph OSC envelope from folded-stack input.
# Pure C frontend over the yetty_yflame library. First-party deps only.

add_executable(yflame
    main.c
    interactive.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yflame PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# Threads::Threads picks the right thing per platform (linux/macos: pthread;
# android: bionic libc). Same pattern as tools/yplot.
find_package(Threads REQUIRED)
target_link_libraries(yflame PRIVATE
    yetty_yflame
    yetty_yview
    Threads::Threads
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(yflame PRIVATE -Wall -Wextra)
endif()
