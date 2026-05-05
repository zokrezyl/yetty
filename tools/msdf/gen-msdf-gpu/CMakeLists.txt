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
