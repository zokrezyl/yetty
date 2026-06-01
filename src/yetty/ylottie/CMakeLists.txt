# ylottie — Lottie (Bodymovin subset) → ydraw buffer rendering (single frame)
#
# Self-contained: parses JSON itself (ylottie-json.c) and flattens shape
# geometry to ydraw SDF primitives + MSDF text spans the same way ysvg does.
# No external vector/animation library.

add_library(yetty_ylottie STATIC
    ylottie-json.c
    ylottie-prop.c
    ylottie-path.c
    ylottie-paint.c
    ylottie.c
)

target_include_directories(yetty_ylottie PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ylottie
    PUBLIC
        yetty_ycore
        yetty_ydraw_core
        yetty_ysdf
)
