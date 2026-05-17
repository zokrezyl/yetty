# yrich-runner — shared interactive driver for ydoc / ysheet / yslide.
add_library(yrich_runner STATIC
    yrich-runner.c
)

target_include_directories(yrich_runner PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${YETTY_ROOT}/include
)

target_link_libraries(yrich_runner PUBLIC
    yetty_yrich
    yetty_ydraw_core
    yetty_yface
    yetty_ycore
)
if(NOT WIN32)
    target_link_libraries(yrich_runner PUBLIC m)
endif()

if(NOT MSVC)
    target_compile_options(yrich_runner PRIVATE -Wall -Wextra)
endif()
