# ydoc — interactive rich-text editor (pure C).
add_executable(ydoc
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/getopt.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/thread.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/term.c
)

find_package(Threads REQUIRED)

target_include_directories(ydoc PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(ydoc PRIVATE
    yrich_runner
    yetty_yrich
    yetty_ypaint_core
    yetty_ycore
    Threads::Threads
)

target_compile_options(ydoc PRIVATE -Wall -Wextra)
