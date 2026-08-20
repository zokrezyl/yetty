# ymux — the tmux-like terminal session server (#695). Independent of
# yvterm by design: the module links the shared libvterm READ-ONLY through
# its public API and carries no GPU, renderer, or figure dependency — the
# daemon side must stay linkable without Dawn.
#
# Build ownership follows the role-split codegen layout (see yscene):
#   yetty_api_ymux   the generated typed object-API stubs — what consumers
#                    link.
#   yetty_ymux       the hand-written class TUs (each foot-#includes its
#                    generated impl glue from src/yetty/gen/impl/ymux/).

add_library(yetty_api_ymux STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/ymux/engine.c
    ${YETTY_ROOT}/src/yetty/gen/api/ymux/history.c
    ${YETTY_ROOT}/src/yetty/gen/api/ymux/pane.c
    ${YETTY_ROOT}/src/yetty/gen/api/ymux/attachment.c
    ${YETTY_ROOT}/src/yetty/gen/api/ymux/rich.c
    ${YETTY_ROOT}/src/yetty/gen/api/ymux/projector.c
    ${YETTY_ROOT}/src/yetty/gen/api/ymux/session.c
    ${YETTY_ROOT}/src/yetty/gen/api/ymux/daemon.c
    ${YETTY_ROOT}/src/yetty/gen/api/ymux/client.c
    ${YETTY_ROOT}/src/yetty/gen/api/ymux/vtsink.c
)

target_include_directories(yetty_api_ymux PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_api_ymux PUBLIC
    yetty_ycore
    yetty_yclass
)

add_library(yetty_ymux STATIC
    attachment.c
    client.c
    daemon.c
    engine.c
    history.c
    key-encode.c
    pane.c
    projector.c
    resource.c
    rich.c
    session.c
    rpc-lane.c
    tty-render.c
    tty-term.c
    tx-queue.c
    vtsink.c
)

target_include_directories(yetty_ymux PUBLIC
    ${YETTY_ROOT}/include
)
# engine.c #includes its generated impl glue via "yetty/gen/impl/...".
target_include_directories(yetty_ymux PRIVATE ${YETTY_ROOT}/src)

# yetty_yplatform_core is the client-safe POSIX layer (ipc-socket seam for
# daemon.c/client.c) — no GPU, no libuv, no yvterm; the closure proof in
# test/ut/ymux still holds against Dawn/renderer/figure dependencies.
target_link_libraries(yetty_ymux PUBLIC
    yetty_api_ymux
    yetty_ycore
    yetty_yclass
    yetty_yplatform_core
    vterm
    lz4_static
)

# The class TUs carry `[[clang::annotate(...)]]` (C23 attribute syntax);
# see yscene/CMakeLists.txt for the rationale.
if(NOT MSVC)
    set_target_properties(yetty_ymux PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

# The ymux executable (daemon role included) lives in tools/ymux — this directory is
# the library only.

# Debug info even in release: the daemon runs long-lived user sessions —
# a field crash must yield a line-numbered backtrace.
if(NOT MSVC)
    target_compile_options(yetty_ymux PRIVATE -g)
    # Warning GATE (#699 review cycle 19): the frame-handler UB class
    # (falling off a non-void function) and nested-comment slips FAIL the
    # build instead of scrolling past in a log.
    target_compile_options(yetty_ymux PRIVATE -Werror=return-type -Werror=comment)
endif()

# Register with the yclass codegen graph — the module list IS these call
# sites (see build-tools/yetty/yclass-codegen.cmake).
yetty_yclass_module(ymux)
