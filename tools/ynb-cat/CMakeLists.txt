# ynb-cat tool — render a .ipynb notebook's cells + MIME outputs to a yetty
# terminal. Drives the yetty_ynotebook model and ships each rich output through
# the ymime prologue codec + yface file-envelope emitter.
#
# Like ycat, it compiles ytrace.c directly (it is not part of any exported
# static lib) and links Threads + yplatform_core for the helpers ytrace needs.

add_executable(ynb-cat
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ynb-cat PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(ynb-cat PRIVATE
    yetty_ynotebook
    yetty_ymime
    yetty_yface
    yetty_yclass
    yetty_ycore
    Threads::Threads
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(ynb-cat PRIVATE -Wall -Wextra)
endif()
