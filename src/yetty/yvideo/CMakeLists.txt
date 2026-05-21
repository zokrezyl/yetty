# yvideo — drawable video figure. Mirrors yimage's two-tier build:
#
#   yetty_yvideo_core   — wire serializer + OSC emit. CPU-only, no Dawn.
#                          Used by client tools / demos / cross-targets.
#   yetty_yvideo        — server-side factory (compile/create/update/render),
#                          pulls Dawn / WebGPU and the yvcodec H.264 decoder.

add_library(yetty_yvideo_core STATIC
    yvideo.c
    yvideo-gen-wire.c
    yvideo-mp4.c
)

target_include_directories(yetty_yvideo_core PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yvideo_core PUBLIC
    yetty_ydraw_core
    yetty_yface
)

# yvideo-mp4.c is single source of truth for sender-side MP4 →
# Annex-B + SPS dimension extraction (yetty_yvideo_render_from_mp4_*
# in yvideo-mp4.h). The TU always builds; minimp4 + the
# YETTY_HAVE_MINIMP4 define wire the real implementation in. When
# minimp4 is off the same TU emits stub implementations that return
# ERR, so the public ABI stays stable across platforms and consumers
# (ygui_yvideo, tools/yvideo, ...) don't need their own #ifdef.
#
# Future work: move execution server-side (see yvideo-mp4.h comment).
if(YETTY_ENABLE_LIB_MINIMP4)
    include(${YETTY_ROOT}/build-tools/yetty/minimp4.cmake)
    target_link_libraries(yetty_yvideo_core PUBLIC minimp4)
    target_compile_definitions(yetty_yvideo_core PRIVATE YETTY_HAVE_MINIMP4)
endif()

if(YETTY_ENABLE_LIB_WEBGPU AND YETTY_ENABLE_LIB_OPENH264)
    # yvideo-gen.c is the generated factory (CPU/GPU sync from the schema).
    # yvideo-hooks.c implements the four hook entry points the factory
    # calls — schema flag `hooks: true` in yvideo.yaml turns those calls on.
    add_library(yetty_yvideo STATIC
        yvideo-gen.c
        yvideo-hooks.c
    )

    target_include_directories(yetty_yvideo PUBLIC
        ${YETTY_ROOT}/include
        ${YETTY_ROOT}/src
    )

    target_link_libraries(yetty_yvideo PUBLIC
        yetty_yvideo_core
        yetty_ydraw_factory
        yetty_yrender
        yetty_yvcodec
        yetty_yplatform_core
    )

    # v2 audio (#198 item 2). Gate so a build with audio disabled still
    # compiles the video-only path — yvideo-hooks.c just doesn't get
    # called with audio_codec!=0 in that case (and the linker would
    # error on the missing symbols if it tried).
    if(YETTY_ENABLE_FEATURE_YACODEC AND YETTY_ENABLE_LIB_LIBOPUS AND YETTY_ENABLE_LIB_MINIAUDIO)
        target_link_libraries(yetty_yvideo PUBLIC
            yetty_yacodec
            yetty_yplatform_audio
        )
    endif()

    # Embed shaders via incbin — same pattern as yimage / yplot.
    incbin_add_resources(yetty_yvideo
        yvideo_shader "${CMAKE_CURRENT_SOURCE_DIR}/yvideo.wgsl"
        yvideo_lib_shader "${CMAKE_CURRENT_SOURCE_DIR}/yvideo-gen.wgsl"
    )
endif()
