add_executable(demo-yrdawn-10-glyph-animate
    main.c
    $<TARGET_OBJECTS:demo_yrdawn_support>
)
target_include_directories(demo-yrdawn-10-glyph-animate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(demo-yrdawn-10-glyph-animate PRIVATE
    yetty_yrdawn
    yetty_yplatform_core
    Threads::Threads
)
target_compile_features(demo-yrdawn-10-glyph-animate PRIVATE c_std_11)
target_compile_definitions(demo-yrdawn-10-glyph-animate PRIVATE
    YETTY_GLYPH_SHADERS_DIR="${YETTY_ROOT}/src/yetty/yfont/glyph-shaders"
)
