# gen-msdf-gpu — standalone CLI driver for the WGSL compute-shader MSDF
# generator. Brings up a headless WGPU instance + adapter + device, then
# calls yetty_ymsdf_wgsl_config_generate(). Output CDB is byte-compatible
# with yetty-ymsdf-gen (CPU/msdfgen) so cdb-diff can compare them.

add_executable(yetty-ymsdf-gen-gpu
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_YPLATFORM_THREAD_SOURCES}
)

target_include_directories(yetty-ymsdf-gen-gpu PRIVATE ${YETTY_ROOT}/include)

target_link_libraries(yetty-ymsdf-gen-gpu PRIVATE
    yetty_ymsdf_wgsl
    yetty_ywebgpu
    webgpu
)

# Dawn (libwebgpu_dawn.a) is C++ — force the C++ stdlib at link time even
# though all our sources here are C. Mirrors what other yetty tools do
# implicitly when they have at least one .cpp source.
set_target_properties(yetty-ymsdf-gen-gpu PROPERTIES LINKER_LANGUAGE CXX)

# ── Build-time CDB generation ──────────────────────────────────────────
# Runs the just-built GPU MSDF generator over every TTF in assets/fonts/
# and drops the resulting .cdb files in build/cdb-gpu/. Reruns whenever
# a font, the generator binary, or the compute shader changes — handy
# for diffing against the CPU output (`make generate-gpu-cdb`).

set(GPU_CDB_OUTPUT_DIR "${CMAKE_BINARY_DIR}/cdb-gpu")
set(GPU_CDB_FONT_DIR   "${YETTY_ROOT}/assets/fonts")
set(GPU_CDB_SHADER     "${YETTY_ROOT}/src/yetty/ymsdf-wgsl/shaders/msdf_gen.wgsl")

# Glob CONFIGURE_DEPENDS so cmake re-runs when fonts are added/removed.
file(GLOB GPU_CDB_FONT_FILES CONFIGURE_DEPENDS "${GPU_CDB_FONT_DIR}/*.ttf")

set(GPU_CDB_OUTPUTS "")
set(_prev_cdb "")
foreach(font ${GPU_CDB_FONT_FILES})
    get_filename_component(font_name "${font}" NAME_WE)
    set(out_cdb "${GPU_CDB_OUTPUT_DIR}/${font_name}.cdb")
    # Chain each font's command on the previous one's output so ninja
    # cannot run multiple GPU generators in parallel. Several headless
    # WGPU adapters fighting for the same GPU on a single connection
    # produces silently-empty atlases (compute submits "succeed" but
    # the readback returns the texture's initial zeroed state). Serial
    # execution is the simplest fix and only adds a couple of seconds
    # for the four fonts we ship.
    add_custom_command(
        OUTPUT  "${out_cdb}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_CDB_OUTPUT_DIR}"
        COMMAND $<TARGET_FILE:yetty-ymsdf-gen-gpu>
                --shader "${GPU_CDB_SHADER}"
                --size 32 --range 4
                "${font}" "${out_cdb}"
        DEPENDS yetty-ymsdf-gen-gpu "${font}" "${GPU_CDB_SHADER}" ${_prev_cdb}
        COMMENT "Generating GPU MSDF CDB for ${font_name}"
        VERBATIM
    )
    list(APPEND GPU_CDB_OUTPUTS "${out_cdb}")
    set(_prev_cdb "${out_cdb}")
endforeach()

if(GPU_CDB_OUTPUTS)
    add_custom_target(generate-gpu-cdb DEPENDS ${GPU_CDB_OUTPUTS})
endif()
