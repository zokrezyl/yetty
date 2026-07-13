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

    # --record-audio: mic capture (via yetty_yplatform_audio's capture
    # API) + fdk-aac AAC-LC encoding into a second mp4 track. Both deps
    # are required together — miniaudio provides the capture backend,
    # fdk-aac produces the AAC frames the mp4 muxer wants.
    if(YETTY_ENABLE_LIB_MINIAUDIO AND YETTY_ENABLE_LIB_FDK_AAC)
        include(${YETTY_ROOT}/build-tools/yetty/miniaudio.cmake)
        include(${YETTY_ROOT}/build-tools/yetty/yplatform-audio.cmake)
        include(${YETTY_ROOT}/build-tools/yetty/fdk-aac.cmake)
        target_compile_definitions(yetty_vnc PRIVATE YETTY_HAS_YAUDIO_RECORD=1)
        target_link_libraries(yetty_vnc PRIVATE yetty_yplatform_audio fdk-aac)
    endif()
endif()
