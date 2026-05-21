add_executable(demo-ywasm-09-glyph-render
    main.c
    $<TARGET_OBJECTS:demo_ywasm_support>
)
target_include_directories(demo-ywasm-09-glyph-render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(demo-ywasm-09-glyph-render PRIVATE
    yetty_ywasm
    yetty_yplatform_core
    Threads::Threads
)
target_compile_features(demo-ywasm-09-glyph-render PRIVATE c_std_11)
target_compile_definitions(demo-ywasm-09-glyph-render PRIVATE
    YETTY_GLYPH_SHADERS_DIR="${YETTY_ROOT}/src/yetty/yfont/glyph-shaders"
)
