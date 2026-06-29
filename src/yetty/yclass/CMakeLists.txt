# yclass — tiny class/object runtime with optional binary RPC.

add_library(yetty_yclass STATIC
    class.c
    rpc.c
    rpc-dcs-server.c
    transport-fd.c
    transport-dcs.c
    transport-pty.c
)

target_include_directories(yetty_yclass PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yclass PUBLIC
    yetty_ycore
    yetty_ywire
)
