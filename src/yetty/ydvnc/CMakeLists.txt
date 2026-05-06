# ydvnc — desktop VNC client (RFB 3.8).

add_library(yetty_ydvnc STATIC
    rfb-client.c
    transport-tcp.c
    keysyms.c
    ydvnc-viewer.c
)

target_include_directories(yetty_ydvnc PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(yetty_ydvnc PUBLIC
    webgpu
    yetty_ycore
)
