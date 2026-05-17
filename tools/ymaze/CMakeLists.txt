# ymaze tool — animated maze frontend, emits ydraw OSC envelopes.
# Pure C frontend over the yetty_ymaze library.

add_executable(ymaze
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ymaze PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(ymaze PRIVATE
    yetty_ymaze
    yetty_yface
    Threads::Threads
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(ymaze PRIVATE -Wall -Wextra)
endif()
