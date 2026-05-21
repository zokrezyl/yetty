add_executable(demo-yrdawn-08-glyph-shaders
    main.c
    $<TARGET_OBJECTS:demo_yrdawn_support>
)
target_include_directories(demo-yrdawn-08-glyph-shaders PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(demo-yrdawn-08-glyph-shaders PRIVATE
    yetty_yrdawn
    yetty_yplatform_core
    Threads::Threads
)
target_compile_features(demo-yrdawn-08-glyph-shaders PRIVATE c_std_11)
# Embed the glyph-shaders directory path so the demo can walk it at
# runtime — these shaders are the real yetty/yfont fragment bodies, not
# a fixture copy.
target_compile_definitions(demo-yrdawn-08-glyph-shaders PRIVATE
    YETTY_GLYPH_SHADERS_DIR="${YETTY_ROOT}/src/yetty/yfont/glyph-shaders"
)
