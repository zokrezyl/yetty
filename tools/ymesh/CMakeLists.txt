# ymesh tool — emits a ymesh complex-prim OSC envelope from a .glb file.
# Pure C frontend over the yetty_ymesh library.

add_executable(ymesh
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/getopt.c
    ${YETTY_YPLATFORM_THREAD_SOURCES}
)

target_include_directories(ymesh PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(ymesh PRIVATE
    yetty_ymesh
    Threads::Threads
)

if(NOT MSVC)
    target_compile_options(ymesh PRIVATE -Wall -Wextra)
endif()
