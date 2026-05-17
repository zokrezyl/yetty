# ydraw unit tests — scene-canvas direct API + grid/staging rebuild.

enable_testing()

add_executable(scene-canvas-test
    scene-canvas-test.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    # Coroutine impl lives outside any static lib (see comments in
    # src/yetty/yplatform/CMakeLists.txt). Compile it in directly so we
    # can resolve yetty_yplatform_coro_* without dragging in the libuv
    # / GPU pieces from yplatform-server.
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c
)

target_include_directories(scene-canvas-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# Use the yetty_ydraw library — provides scene-canvas, scrolling-canvas,
# the flyweight registry, the SDF handler, and the cmd handler. Pulls in
# ydraw-core transitively for the draw-list and the iterator. m for libm.
target_link_libraries(scene-canvas-test PRIVATE
    yetty_ydraw
    yetty_ydraw_core
    yetty_ysdf
    yetty_yfont_core
    yetty_ywire
    yetty_ydraw_core
    yetty_yco
    yetty_yplatform_core
    yetty_ywire
    yetty_yplatform_core
    yetty_ycore
    m
)

add_test(NAME scene_canvas COMMAND scene-canvas-test)
