# ymesh tool — emits a ymesh complex OSC envelope from a .glb file.
# Pure C frontend over the yetty_ymesh library.

add_executable(ymesh
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ymesh PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(ymesh PRIVATE
    yetty_ymesh_core
    Threads::Threads
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(ymesh PRIVATE -Wall -Wextra)
endif()
