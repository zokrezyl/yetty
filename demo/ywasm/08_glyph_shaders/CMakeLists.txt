add_executable(demo-ywasm-08-glyph-shaders
    main.c
    $<TARGET_OBJECTS:demo_ywasm_support>
)
target_include_directories(demo-ywasm-08-glyph-shaders PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(demo-ywasm-08-glyph-shaders PRIVATE
    yetty_ywasm
    yetty_yplatform_core
    Threads::Threads
)
target_compile_features(demo-ywasm-08-glyph-shaders PRIVATE c_std_11)
# Embed the glyph-shaders directory path so the demo can walk it at
# runtime — these shaders are the real yetty/yfont fragment bodies, not
# a fixture copy.
target_compile_definitions(demo-ywasm-08-glyph-shaders PRIVATE
    YETTY_GLYPH_SHADERS_DIR="${YETTY_ROOT}/src/yetty/yfont/glyph-shaders"
)
