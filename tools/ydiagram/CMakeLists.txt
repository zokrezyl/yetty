# ydiagram CLI — Mermaid file → ypaint OSC envelope (or raw serialized
# buffer). Mirrors the wiring of tools/ymaze and tools/yzoo.

add_executable(ydiagram-cli
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

set_target_properties(ydiagram-cli PROPERTIES OUTPUT_NAME ydiagram)

target_include_directories(ydiagram-cli PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(ydiagram-cli PRIVATE
    yetty_ydiagram
    yetty_yface
    yetty_ydraw_core
    yetty_ysdf
    yetty_yfont_core
    yetty_ycore
    yetty_yplatform_core
    Threads::Threads
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:m>
)

if(NOT MSVC)
    target_compile_options(ydiagram-cli PRIVATE -Wall -Wextra)
endif()
