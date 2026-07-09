# ymux — the GPU-free logical-terminal owner (ymux_pane). Extracted from
# yterminal so the same terminal model core serves the legacy monolith, the
# headless ymux server, and (later) remote clients. Plain C; symbol prefix
# yetty_ymux_. See docs/ymux.md.
add_library(yetty_ymux STATIC
    pane.c
    server.c
    client-pty.c
    bootstrap.c
)

target_include_directories(yetty_ymux PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_ymux PUBLIC
    yetty_ycore
    yetty_yvterm         # the bare yvterm:grid model the pane owns + drives
    yetty_ywire          # wire statemachine + host-side channel connection
    yetty_yface          # emit yface for input events to the inferior
    yetty_yplatform_core # PTY factory/ops vtable + ytime backoff
    yetty_yctl           # msgpack-RPC control channel for the headless server
)
