# Copy runtime assets to build directory
set(ASSETS_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ASSETS_DST_DIR ${CMAKE_BINARY_DIR}/assets)
set(FONTS_DST_DIR ${ASSETS_DST_DIR}/fonts)

# Font files to copy
set(FONT_FILES
    ${ASSETS_SRC_DIR}/fonts/DejaVuSansMNerdFontMono-Regular.ttf
    ${ASSETS_SRC_DIR}/fonts/DejaVuSansMNerdFontMono-Bold.ttf
    ${ASSETS_SRC_DIR}/fonts/DejaVuSansMNerdFontMono-Oblique.ttf
    ${ASSETS_SRC_DIR}/fonts/DejaVuSansMNerdFontMono-BoldOblique.ttf
    # Emmentaler — LilyPond's music-notation font (SIL OFL 1.1), used by ymusic
    # for clefs, noteheads, rests, accidentals and flags. See Emmentaler-OFL.txt.
    ${ASSETS_SRC_DIR}/fonts/Emmentaler-20.otf
    ${ASSETS_SRC_DIR}/fonts/Emmentaler-OFL.txt
)

# Create custom target to copy assets
add_custom_target(copy-assets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ASSETS_DST_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${FONTS_DST_DIR}
    COMMENT "Copying assets to build directory"
)

foreach(FONT_FILE ${FONT_FILES})
    get_filename_component(FONT_NAME ${FONT_FILE} NAME)
    add_custom_command(TARGET copy-assets POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FONT_FILE} ${FONTS_DST_DIR}/${FONT_NAME}
    )
endforeach()
