include(${YETTY_ROOT}/build-tools/cmake/libs/msdfgen.cmake)
include(${YETTY_ROOT}/build-tools/cmake/libs/freetype.cmake)

add_library(yetty_ymsdf_gen STATIC
    ymsdf-gen.cpp
)

target_include_directories(yetty_ymsdf_gen PUBLIC
    ${YETTY_ROOT}/include
    ${FREETYPE_INCLUDE_DIR}
)

target_link_libraries(yetty_ymsdf_gen PUBLIC
    msdfgen::msdfgen-core
    msdfgen::msdfgen-ext
    ${FREETYPE_ALL_LIBS}
    yetty_ycdb
)

# CLI tool
add_executable(yetty-ymsdf-gen
    ${YETTY_ROOT}/tools/msdf/gen-msdf/main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_YPLATFORM_THREAD_SOURCES}
)
target_include_directories(yetty-ymsdf-gen PRIVATE ${YETTY_ROOT}/include)
target_link_libraries(yetty-ymsdf-gen PRIVATE yetty_ymsdf_gen)
