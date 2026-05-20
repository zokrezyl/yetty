# yacodec — audio codec facade. Mirrors yvcodec (the H.264 wrapper):
# packet-in → PCM-out, codec-agnostic API. v1 implements Opus
# (xiph/libopus, BSD 3-Clause). The reserved enum slots for
# WAV/FLAC/MP3/Vorbis (vendored inside miniaudio) light up once a
# sibling TU per codec lands.

add_library(yetty_yacodec STATIC
    decoder.c
)

target_include_directories(yetty_yacodec PUBLIC
    ${YETTY_ROOT}/include
)

# libopus is the v1 backend — link unconditionally for now. When more
# codecs land, gate per-backend TUs the same way yetty_yvcodec gates
# encoder.c vs decoder.c (single lib, multiple object files).
target_link_libraries(yetty_yacodec
    PUBLIC  yetty_ycore
    PRIVATE libopus
)
