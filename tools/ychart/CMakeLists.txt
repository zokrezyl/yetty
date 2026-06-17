# ychart CLI — data file (CSV/JSON/YAML) → ydraw OSC envelope (or raw
# serialized buffer). Mirrors the wiring of tools/ydiagram.

add_executable(ychart-cli
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

set_target_properties(ychart-cli PROPERTIES OUTPUT_NAME ychart)

target_include_directories(ychart-cli PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(ychart-cli PRIVATE
    yetty_ychart
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
    target_compile_options(ychart-cli PRIVATE -Wall -Wextra)
endif()
