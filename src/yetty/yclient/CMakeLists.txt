# yclient-lib — shared client-side support library.
#
# Lives outside src/yetty so it can be consumed by both standalone client
# tools (ymgui frontends, ygui, yrich, ycat) and the main yetty modules
# without dragging the yetty-server abstractions back in. Same pattern as
# src/yrender-utils.
#
# Currently exposes one piece — a libuv-backed event loop with GLFW-style
# input callbacks, frame coalescing, and extension slots for app fds /
# timers / posted tasks. The loop owns a yface for stream decoding so any
# byte stream from yetty (mouse events, resize, future keyboard, etc.)
# arrives as typed callbacks.
#
# Skipped on emscripten — no libuv there.

if(EMSCRIPTEN)
    return()
endif()

include(${YETTY_ROOT}/build-tools/cmake/libs/libuv.cmake)

add_library(yetty_yclient STATIC
    event-loop.c
)

target_include_directories(yetty_yclient PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yclient PUBLIC
    yetty_yface     # stream decoder + wire-struct definitions transitive via wire.h
    yetty_ycore
    uv_a
)

set_target_properties(yetty_yclient PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

install(FILES ${YETTY_ROOT}/include/yetty/yclient-lib/event-loop.h
        DESTINATION include/yetty/yclient-lib)
install(TARGETS yetty_yclient ARCHIVE DESTINATION lib)
