# yzoo tool — animated control-point zoo, emits ypaint OSC envelopes.
# Pure C frontend over the yetty_yzoo library.

add_executable(yzoo
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_YPLATFORM_THREAD_SOURCES}
)

target_include_directories(yzoo PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(yzoo PRIVATE
    yetty_yzoo
    yetty_yface
    Threads::Threads
)

if(NOT MSVC)
    target_compile_options(yzoo PRIVATE -Wall -Wextra)
endif()
