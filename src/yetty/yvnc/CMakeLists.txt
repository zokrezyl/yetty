# VNC-like remote display client/server

add_library(yetty_vnc STATIC
    vnc-client.c
    vnc-server.c
    vnc-viewer.c
)

target_include_directories(yetty_vnc PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(yetty_vnc PUBLIC
    webgpu
    turbojpeg-static
    yetty_ycore
    yetty_yrender_utils
)

if(YETTY_ENABLE_LIB_OPENH264)
    target_compile_definitions(yetty_vnc PUBLIC YETTY_HAS_YVCODEC=1)
    target_link_libraries(yetty_vnc PUBLIC yetty_yvcodec)

    # MP4 recording (--record FILE) muxes encoded H.264 NALs into an MP4
    # container. minimp4 is header-only; we instantiate the implementation
    # in vnc-server.c via MINIMP4_IMPLEMENTATION.
    include(${YETTY_ROOT}/build-tools/yetty/minimp4.cmake)
    target_link_libraries(yetty_vnc PRIVATE minimp4)
endif()
