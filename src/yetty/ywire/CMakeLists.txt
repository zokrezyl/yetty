# Coroutine implementation is platform-specific (libco on desktop,
# emscripten fiber on webasm). Picked here so every consumer of
# yetty_ywire (including yface and the one-shot emitter tools) gets
# the right impl without each having to add the source file themselves.
if(EMSCRIPTEN)
    set(_ywire_coro_impl ${YETTY_ROOT}/src/yetty/yplatform/coroutine/webasm.c)
else()
    set(_ywire_coro_impl ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c)
endif()

add_library(yetty_ywire STATIC
    wire-statemachine.c
    ${_ywire_coro_impl}
)

target_include_directories(yetty_ywire PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_ywire PUBLIC
    yetty_ycore
    yetty_yetty
    lz4_static
)

# yetty_yco wraps libco. The webasm coroutine impl above uses setjmp/longjmp
# directly and never pulls libco in, so the wrapper target isn't built on
# emscripten (see src/yetty/CMakeLists.txt). Link it only where it exists.
if(NOT EMSCRIPTEN)
    target_link_libraries(yetty_ywire PUBLIC yetty_yco)
endif()
