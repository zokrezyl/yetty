add_library(yetty_yvterm STATIC
    grid.c
    vterm.c
    # Tiered scroll buffer archive engine: line serialization, lz4 warm
    # segments, the session-scoped cold spill file, materialization cache.
    scroll-tiers.c
    # yvterm's own SDF/glyph/text renderer for the raw ydraw records (ycat
    # PDF/SVG/markdown) stored on the grid ring. Standalone — does not use ygrid.
    sdf-layer.c
    # Animated procedural "shader glyphs" for PUA-B cells. Plain-C sub-renderer
    # owned by the vterm figure, like sdf-layer.
    shader-glyph-layer.c
    # Module-level registration + RPC dispatch (yetty_yvterm_register lives here)
    # and the cross-class method tables. grid.c/vterm.c each only #include their
    # own <name>.gen.c at the foot, so these are compiled as standalone units —
    # mirrors ygrid/CMakeLists.txt.
    rpc.gen.c
)

target_include_directories(yetty_yvterm PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) sit under src/yetty/yvterm/.
# PRIVATE so the path does not leak to consumers — the public API is the per-class
# headers under include/yetty/yvterm/.
target_include_directories(yetty_yvterm PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yvterm PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_yfigure
    yetty_ydraw_core
    yetty_ydraw_factory  # yetty_ydraw_composite_destroy (anchored composite teardown)
    yetty_ywire          # wire statemachine: default raw-bytes sink → libvterm
    yetty_yfont_core     # MSDF font + font cache: text render AND SDF-layer wire fonts
    yetty_yfont_shader_glyph # PUA-B shader-glyph codepoint table (codepoint_exists)
    yetty_yrender        # gpu_resource_set / binder / font dispatcher (text + SDF layer)
    yetty_yplatform_core # SDF layer wire-font cache: cache dir + mkdir/file_exists
    webgpu               # wgpu* pipeline/buffers/draw for the text + SDF render
    vterm
    lz4_static           # scroll-tiers: warm-segment compression
)

# vterm.c carries `[[clang::annotate(...)]]` on its class struct (codegen reads
# it off the AST). The C2x attribute syntax needs C23 to parse — host clang/gcc
# accept it by default, NDK clang 17 does not. (Mirrors ygrid/CMakeLists.txt.)
if(NOT MSVC)
    set_target_properties(yetty_yvterm PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
