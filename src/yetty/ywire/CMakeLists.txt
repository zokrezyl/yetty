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
    yetty_yco
    lz4_static
)
