# ylexbor — permissively-licensed HTML/CSS renderer on top of lexbor.
#
# Unlike yetty_ynetsurf (which pulls in GPL NetSurf), ylexbor uses only
# Apache-2.0 lexbor + yetty's own code, so it can be linked directly
# into the main yetty binary without license conflict.

if(NOT TARGET lexbor_static)
    message(STATUS "yetty_ylexbor: lexbor_static target missing — skipping")
    return()
endif()

add_library(yetty_ylexbor STATIC
    ylexbor.c
    ylexbor-box.c
    ylexbor-layout.c
    ylexbor-paint.c
)

target_include_directories(yetty_ylexbor PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_ylexbor
    PUBLIC
        lexbor_static
        yetty_ypaint_core
        yetty_ysdf
        yetty_ycore
)

target_compile_options(yetty_ylexbor PRIVATE -Wall -Wno-unused-parameter)
