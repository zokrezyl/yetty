# yetty_ysixel_core — sixel DCS-payload decoder → RGBA8 → yimage prim.
# GPU-less: reuses the yimage prim serializer and the yimage factory renders it,
# so no pipeline of its own.
add_library(yetty_ysixel_core STATIC
    sixel.c
)

target_include_directories(yetty_ysixel_core PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ysixel_core PUBLIC
    # yetty_yimage_core brings yetty_ydraw_list + yetty_yface transitively; the
    # ydraw_list dep is spelled out too since sixel.c uses drawable-list directly.
    yetty_yimage_core
    yetty_ydraw_list
)
