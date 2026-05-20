# yetty_yplatform_audio — cross-platform audio playback device.
#
# Same shape as yetty_yplatform_wayland_move: a standalone static lib
# living under src/yetty/yplatform/audio/, that confines a heavyweight
# single-header dependency (miniaudio here, glfw's private headers
# there) to one TU. Consumers (yetty_yvideo, future yacodec demos)
# link this target and see only the abstract API in
# <yetty/yplatform/audio.h> — no miniaudio symbols leak out.
#
# Requires the `miniaudio` IMPORTED target — gate the include in
# shared.cmake on YETTY_ENABLE_LIB_MINIAUDIO and include
# miniaudio.cmake before this file.

include_guard(GLOBAL)

if(TARGET yetty_yplatform_audio)
    return()
endif()
if(NOT TARGET miniaudio)
    message(FATAL_ERROR
        "yplatform-audio: miniaudio target missing — include miniaudio.cmake first")
endif()

add_library(yetty_yplatform_audio STATIC
    ${YETTY_ROOT}/src/yetty/yplatform/audio/default.c
)
target_include_directories(yetty_yplatform_audio PUBLIC
    ${YETTY_ROOT}/include
)
# miniaudio is PRIVATE — its symbols are owned by default.c only; the
# public include set must not leak the implementation header.
target_link_libraries(yetty_yplatform_audio
    PUBLIC  yetty_ycore
    PRIVATE miniaudio
)

# default.c uses <stdatomic.h>. MSVC defines __STDC_NO_ATOMICS__ unless
# /std:clatest (or /std:c11) and /experimental:c11atomics are passed —
# same dance as the yetty exe target.
if(MSVC)
    target_compile_options(yetty_yplatform_audio PRIVATE
        $<$<COMPILE_LANGUAGE:C>:/std:clatest>
        $<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>)
endif()
