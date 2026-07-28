# yguiapp — generic ygui application host (yclass).
#
#   app.c   the hand-written annotated `yguiapp:app` class (a yapp:app
#           subclass; app.gen.c is #included at the foot). It owns the
#           standalone bring-up — window/GPU via yframework, a local
#           yfigure container, a ygui framework wired onto it, a styled
#           root/body, window chrome, and the event loop — generalizing
#           demo/ygui/runner.c. app.gen.c + app.h are codegen output;
#           model.yaml drives the FFI / host-language bindings.
#   run.c   plain-C sibling runtime: the dual-mode launcher
#           (yetty_yguiapp_run_main), the in-terminal host
#           (yetty_yguiapp_run_terminal — the #380 transport-pty + ywire path),
#           and the shared root/body builder. Not annotated — not seen by codegen.

add_library(yetty_api_yguiapp STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/yguiapp/app.c
)
target_include_directories(yetty_api_yguiapp PUBLIC ${YETTY_ROOT}/include)
target_link_libraries(yetty_api_yguiapp PUBLIC yetty_ycore yetty_yclass)

add_library(yetty_yguiapp STATIC
    app.c
    run.c
)

target_include_directories(yetty_yguiapp PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) are #included from app.c.
target_include_directories(yetty_yguiapp PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yguiapp PUBLIC
    yetty_api_yguiapp
    yetty_ycore
    yetty_yclass
    yetty_ygui
    yetty_yfigure
    yetty_ygrid
    yetty_yframework
    yetty_ydraw_factory
    yetty_yevent
    # Standalone host extras ported from the demo runner: the shader figure
    # gallery, window chrome, the wire link for the terminal host, the MSDF font
    # backend, the producer figure kinds, coroutines + telnet (wire deps).
    yetty_yshadertoy
    yetty_ychrome
    yetty_ywire
    yetty_yfont_core
    yetty_yimage
    yetty_yplot
    yetty_yco
    yetty_telnet
    # The yapp:app base + platform bring-up symbols (yetty_yapp is compiled into
    # yetty_yplatform_core, not a standalone target) and window-chrome gestures.
    yetty_yplatform_core
    yetty_yplatform_move_resize
    yetty_yplatform_window_chrome
)

# The in-terminal host (run.c) is libuv-driven; gate it the same way every other
# dual-mode app does. On non-Windows targets define YETTY_YGUI_HAS_UV and link
# libuv; without it the terminal path compiles out to a clear runtime error.
if(NOT WIN32)
    target_compile_definitions(yetty_yguiapp PUBLIC YETTY_YGUI_HAS_UV)
    target_link_libraries(yetty_yguiapp PUBLIC uv_a)
endif()

# app.c carries `[[clang::annotate(...)]]` (C23 attribute syntax) — same
# constraint as every yclass module. run.c is plain C but compiles fine as C23.
if(NOT MSVC)
    set_target_properties(yetty_yguiapp PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
