# ymusic — yclass class `ymusic:music`: a LilyPond-subset music renderer.
#
# music.c is the hand-written annotated source (#includes music.gen.c at the
# foot); the per-class <stem>.gen.c files are codegen output (#included into each .c). It parses a LilyPond
# subset into an editable score model and renders a ydraw drawable list: staff
# lines / stems / ledger lines / barlines as SDF segments, and clefs / noteheads
# / rests / accidentals / flags as Emmentaler MSDF glyphs. The yclass model.yaml
# drives the FFI / host-language bindings. GPU-less — produces generic ydraw
# primitives, shipped via yview (like yflame).

add_library(yetty_ymusic STATIC
    music.c
    rpc.gen.c
)

target_include_directories(yetty_ymusic PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) live under src/yetty/ymusic/.
target_include_directories(yetty_ymusic PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_ymusic PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_yface
    yetty_ydraw_core
    yetty_ysdf
)

# music.c carries `[[clang::annotate(...)]]` (C23 attribute syntax).
if(NOT MSVC)
    set_target_properties(yetty_ymusic PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
