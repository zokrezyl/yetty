# yview — client emitter for server-side scrollable content surfaces.
#
# Builds figure-tree wire records (DCS YCOMPOSITOR_BIN) from a ydraw-core
# drawable list and ships them via yface. Consumed by the yless tool and by
# FFI embedders (e.g. an nvim lua plugin).

add_library(yetty_yview STATIC
    yview.c
)

target_include_directories(yetty_yview PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yview PUBLIC
    yetty_ycore
    yetty_yface
    yetty_ydraw_core
)
