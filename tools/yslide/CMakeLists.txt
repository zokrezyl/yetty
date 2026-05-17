# yslide — interactive presentation editor (pure C, was yslides in poc).
add_executable(yslide
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)

find_package(Threads REQUIRED)

target_include_directories(yslide PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yslide PRIVATE
    yrich_runner
    yetty_yrich
    yetty_ydraw_core
    yetty_ycore
    Threads::Threads
    yetty_yplatform_core
)

target_compile_options(yslide PRIVATE -Wall -Wextra)
