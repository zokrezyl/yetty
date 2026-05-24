# yetty_yplatform_audio — cross-platform audio playback device.
#
# Same shape as yetty_yplatform_move_resize: a standalone static lib
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

# Apple's miniaudio backend (CoreAudio / AudioUnit) reaches into
# Foundation/AVFoundation via Objective-C runtime types. Compile this
# TU as Objective-C on Apple so <Foundation/Foundation.h> resolves; on
# every other platform the implicit C language is correct.
if(APPLE)
    enable_language(OBJC)
    set_source_files_properties(
        ${YETTY_ROOT}/src/yetty/yplatform/audio/default.c
        PROPERTIES LANGUAGE OBJC)
endif()
# miniaudio is PRIVATE — its symbols are owned by default.c only; the
# public include set must not leak the implementation header.
target_link_libraries(yetty_yplatform_audio
    PUBLIC  yetty_ycore
    PRIVATE miniaudio
)

