# ydvnc — desktop VNC client (RFB protocol, talks to TigerVNC/TurboVNC).
#
# Implemented from the public RFB spec (RFC 6143 + github.com/rfbproto). NO
# code from TigerVNC, TurboVNC, libvncclient, or noVNC was consulted. See
# build-tools/check-ydvnc-originality.sh for the verification tripwire.

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
