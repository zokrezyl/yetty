# ychrome — yclass class `ychrome:chrome`: a widget-free window-chrome gesture
# engine (caption drag → move, edges → resize, double-click → maximize). It
# drives a yplatform:window_chrome object and depends on neither ygui nor yui,
# so any app can get draggable/resizable/maximizable borderless windows.
#
# chrome.c is the hand-written annotated source (#includes chrome.gen.c at the
# foot); the per-class <stem>.gen.c files are codegen output (#included into each .c). Pure logic — no GLFW, no
# rendering. The window_chrome gesture symbols it calls are resolved by the
# consumer, which links yetty_yplatform_window_chrome.
add_library(yetty_api_ychrome STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/ychrome/chrome.c
)
target_include_directories(yetty_api_ychrome PUBLIC ${YETTY_ROOT}/include)
target_link_libraries(yetty_api_ychrome PUBLIC yetty_ycore yetty_yclass)

add_library(yetty_ychrome STATIC
    chrome.c
    # host.c: reusable glue (engine + pinned caption figure) so any app gets
    # window chrome in a few calls. Plain C, no annotations.
    host.c
)

target_include_directories(yetty_ychrome PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) live under src/yetty/ychrome/.
target_include_directories(yetty_ychrome PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_ychrome PUBLIC
    yetty_api_ychrome
    yetty_ycore
    yetty_yclass
    # Framework-level draw primitives (the same layer ygui paints into) so
    # chrome can render its own titlebar without pulling in ygui.
    yetty_ydraw_core
    yetty_ysdf
    yetty_yface
    # host.c's WIRE sink emits figure-tree records over a producer (GPU-free);
    # yfigure carries the producer and is available on the headless cross
    # targets too.
    yetty_yfigure
    # chrome.c's gesture handlers call the yetty_yplatform_window_chrome_*
    # method stubs (move/resize/iconify/...). Those live in yetty_yplatform_core
    # (the generated method stubs) and early-return on the NULL window_chrome a wire/client
    # host carries. Declare the dependency explicitly so the static link
    # resolves on EVERY target: previously it only worked where yui happened to
    # pull yplatform_core ahead of ychrome, which never happens on the headless
    # no-GPU guest (yui isn't built), breaking the riscv link.
    yetty_yplatform_core
)

# The LOCAL sink composites the caption as a pinned ygrid figure. ygrid is
# GPU-backed and not built on the no-WebGPU cross targets (riscv guest), so the
# local path is compiled in only where ygrid exists. The WIRE sink — all the
# in-terminal/client chrome an app needs — builds without it, so an app running
# inside a hosting yetty gets its backdrop + caption even on a headless guest.
if(YETTY_ENABLE_LIB_WEBGPU)
    target_link_libraries(yetty_ychrome PUBLIC yetty_ygrid)
    target_compile_definitions(yetty_ychrome PUBLIC YETTY_YCHROME_HAS_LOCAL)
endif()

# chrome.c carries C23 `[[clang::annotate(...)]]` attributes.
if(NOT MSVC)
    set_target_properties(yetty_ychrome PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
