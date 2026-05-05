# msdf-render-atlas — interactive atlas viewer for yetty MSDF .cdb files.
#
# Pure C; reads the CDB directly (same format walker idea as cdb-viewer
# and cdb-diff), packs every glyph into a single RGBA8 atlas texture and
# draws the grid through a fragment shader. Useful for visually QA-ing
# the output of either generator (yetty-ymsdf-gen / yetty-ymsdf-gen-gpu).

add_executable(msdf-render-atlas
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_YPLATFORM_THREAD_SOURCES}
)

target_include_directories(msdf-render-atlas PRIVATE
    ${YETTY_ROOT}/include
)

target_link_libraries(msdf-render-atlas PRIVATE
    yetty_ywebgpu
    glfw
    glfw3webgpu
    webgpu
    m
)

# Dawn (libwebgpu_dawn.a) is C++ — force the C++ stdlib at link time
# even though all our sources here are C.
set_target_properties(msdf-render-atlas PROPERTIES LINKER_LANGUAGE CXX)
