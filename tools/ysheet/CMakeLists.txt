# ysheet — interactive spreadsheet (pure C, was yspreadsheet in poc).
add_executable(ysheet
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
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
    yetty_yplatform_core
)

target_compile_options(ysheet PRIVATE -Wall -Wextra)
