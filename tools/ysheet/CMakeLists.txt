# ysheet — interactive spreadsheet (pure C, was yspreadsheet in poc).
add_executable(ysheet
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/getopt.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/thread.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/term.c
)

find_package(Threads REQUIRED)

target_include_directories(ysheet PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(ysheet PRIVATE
    yrich_runner
    yetty_yrich
    yetty_ypaint_core
    yetty_ycore
    Threads::Threads
)

target_compile_options(ysheet PRIVATE -Wall -Wextra)
