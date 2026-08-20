# yguiapp — generic ygui application host (yclass).
#
#   app.c      the hand-written annotated `yguiapp:app` class (a yapp:app
#              subclass; app.gen.c is #included at the foot). It owns the
#              standalone bring-up — window/GPU via yframework, a local
#              yfigure container, a ygui framework wired onto it, a styled
#              root/body, window chrome, and the event loop — generalizing
#              demo/ygui/runner.c. app.gen.c + app.h are codegen output;
#              model.yaml drives the FFI / host-language bindings.
#   run.c      GPU-FREE in-terminal host (yetty_yguiapp_run_terminal_build /
#              yetty_yguiapp_terminal_main — the #380 transport-pty + ywire
#              path) + the shared root/body builder. Compiled into
#              yetty_yguiapp_terminal for terminal-only tools.
#   run-app.c  dual-mode launcher (yetty_yguiapp_run_main) for yclass
#              yguiapp:app subclasses (demos) — references the platform/GPU
#              bring-up, so it lives with app.c in yetty_yguiapp.
#              run.c / run-app.c are plain C — not seen by codegen.

add_library(yetty_api_yguiapp STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/yguiapp/app.c
)
target_include_directories(yetty_api_yguiapp PUBLIC ${YETTY_ROOT}/include)
target_link_libraries(yetty_api_yguiapp PUBLIC yetty_ycore yetty_yclass)

# GPU-FREE terminal host: run.c only. Terminal-only tools (yzoo, ymaze,
# yjungle, …) link THIS target and must never pull the platform/GPU stack —
# keep every dependency here GPU-free (the *_core splits exist for this).
add_library(yetty_yguiapp_terminal STATIC
    run.c
    input-encode.c
)
target_include_directories(yetty_yguiapp_terminal PUBLIC
    ${YETTY_ROOT}/include
)
target_include_directories(yetty_yguiapp_terminal PRIVATE ${YETTY_ROOT}/src)
target_link_libraries(yetty_yguiapp_terminal PUBLIC
    yetty_api_yguiapp
    yetty_ycore
    yetty_yclass
    yetty_ygui
    yetty_ywire
    yetty_yfont_core
    yetty_yco
    yetty_telnet
)

# The in-terminal host (run.c) is libuv-driven. On non-Windows targets define
# YETTY_YGUI_HAS_UV and link libuv; without it the terminal path compiles out
# to a clear runtime error.
if(NOT WIN32)
    target_compile_definitions(yetty_yguiapp_terminal PUBLIC YETTY_YGUI_HAS_UV)
    target_link_libraries(yetty_yguiapp_terminal PUBLIC uv_a)
endif()

if(NOT MSVC)
    set_target_properties(yetty_yguiapp_terminal PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

# Full dual-mode host for yclass yguiapp:app subclasses (demos): the annotated
# app class with its standalone window/GPU run() (app.c) + the dual-mode
# launcher (run-app.c) on top of the terminal host above.
add_library(yetty_yguiapp STATIC
    app.c
    run-app.c
)

target_include_directories(yetty_yguiapp PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) are #included from app.c.
target_include_directories(yetty_yguiapp PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yguiapp PUBLIC
    yetty_yguiapp_terminal
    yetty_api_yguiapp
    yetty_ycore
    yetty_yclass
    yetty_ygui
    yetty_yfigure
    yetty_yscene
    yetty_yframework
    yetty_ydraw_factory
    yetty_yevent
    # Standalone host extras ported from the demo runner: the shader figure
    # gallery, window chrome, the MSDF font backend, the producer figure
    # kinds.
    yetty_yshadertoy
    yetty_ychrome
    yetty_yimage
    yetty_yplot
    # The yapp:app base + platform bring-up symbols (yetty_yapp is compiled into
    # yetty_yplatform_core, not a standalone target) and window-chrome gestures.
    yetty_yplatform_core
    yetty_yplatform_move_resize
    yetty_yplatform_window_chrome
)

# app.c carries `[[clang::annotate(...)]]` (C23 attribute syntax) — same
# constraint as every yclass module. run-app.c is plain C but compiles fine as
# C23.
if(NOT MSVC)
    set_target_properties(yetty_yguiapp PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

# Register with the yclass codegen graph — the module list IS these
# call sites (see build-tools/yetty/yclass-codegen.cmake).
yetty_yclass_module(yguiapp)
