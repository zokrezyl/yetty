# yguiapp — generic ygui application host (yclass).
#
#   app.c   the hand-written annotated `yguiapp:app` class (a yapp:app
#           subclass; app.gen.c is #included at the foot). It owns the
#           standalone bring-up — window/GPU via yframework, a local
#           yfigure container, a ygui framework wired onto it, and the
#           event loop — generalizing demo/ygui/runner.c. app.gen.c +
#           app.h are codegen output; model.yaml drives the FFI /
#           host-language bindings.

add_library(yetty_yguiapp STATIC
    app.c
    rpc.gen.c
)

target_include_directories(yetty_yguiapp PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) live under src/yetty/ygui-yapp/.
target_include_directories(yetty_yguiapp PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yguiapp PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_yapp
    yetty_ygui
    yetty_yfigure
    yetty_ygrid
    yetty_yframework
    yetty_ydraw_factory
    yetty_yevent
)

# app.c carries `[[clang::annotate(...)]]` (C23 attribute syntax) — same
# constraint as every yclass module.
if(NOT MSVC)
    set_target_properties(yetty_yguiapp PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
