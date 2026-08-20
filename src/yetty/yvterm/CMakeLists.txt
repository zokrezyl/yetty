add_library(yetty_yvterm STATIC
    grid.c
    vterm.c
    # Tiered scroll buffer archive engine: line serialization, lz4 warm
    # segments, the session-scoped cold spill file, materialization cache.
    scroll-tiers.c
    # yvterm's own SDF/glyph/text renderer for the raw ydraw records (ycat
    # PDF/SVG/markdown) stored on the grid ring. Standalone — does not use ygrid.
    grid-sdf-layer.c
    # Animated procedural "shader glyphs" for PUA-B cells. Plain-C sub-renderer
    # owned by the vterm figure, like grid-sdf-layer.
    grid-shader-glyph-layer.c
    # Module-level registration + RPC dispatch (yetty_yvterm_register lives here)
    # and the cross-class method tables. grid.c/vterm.c each only #include their
    # own <name>.gen.c at the foot, so these are compiled as standalone units —
    # mirrors ygrid/CMakeLists.txt.
)

target_include_directories(yetty_yvterm PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) sit under src/yetty/yvterm/.
# PRIVATE so the path does not leak to consumers — the public API is the per-class
# headers under include/yetty/yvterm/.
target_include_directories(yetty_yvterm PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yvterm PUBLIC
    yetty_api_yvterm
    yetty_api_ytermsink  # grid/vterm/grid-shader-glyph-layer dispatch the terminal-host sink
    yetty_ycore
    yetty_yclass
    yetty_yfigure
    yetty_ysdf           # grid-sdf-layer.c: yetty_ysdf_compute_aabb (record indexing)
    yetty_ydraw_core
    yetty_ydraw_factory  # yetty_ydraw_complex_destroy (anchored complex teardown)
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

# Object-API library (role-split codegen layout): the typed stubs alone,
# with no strong references to the implementation, accessors, skels or
# registration. Pure wire consumers link THIS; the impl lib links it too
# (skels re-enter the public stubs). Header: <yetty/api/yvterm/*.h>.
add_library(yetty_api_yvterm STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/yvterm/grid.c
    ${YETTY_ROOT}/src/yetty/gen/api/yvterm/vterm.c
)

target_include_directories(yetty_api_yvterm PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_api_yvterm PUBLIC
    yetty_ycore
    yetty_yclass
)

# Register with the yclass codegen graph — the module list IS these
# call sites (see build-tools/yetty/yclass-codegen.cmake).
yetty_yclass_module(yvterm)
