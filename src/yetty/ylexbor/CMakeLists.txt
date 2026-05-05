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
    ylexbor-js.c
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

# QuickJS is opt-in — when present, ylexbor's <script> walker calls
# JS_Eval per inline script block. When absent, ylexbor-js.c compiles
# to no-op stubs so the rest of the lib stays the same shape.
if(TARGET qjs)
    target_link_libraries(yetty_ylexbor PUBLIC qjs)
    target_compile_definitions(yetty_ylexbor PRIVATE YETTY_HAVE_QUICKJS=1)
    message(STATUS "yetty_ylexbor: QuickJS enabled")
else()
    target_compile_definitions(yetty_ylexbor PRIVATE YETTY_HAVE_QUICKJS=0)
    message(STATUS "yetty_ylexbor: QuickJS disabled (qjs target missing)")
endif()

target_compile_options(yetty_ylexbor PRIVATE -Wall -Wno-unused-parameter)
