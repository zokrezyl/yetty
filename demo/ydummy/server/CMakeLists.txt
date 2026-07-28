# ydummy-server — standalone server half of the ydummy pilot (see
# src/yetty/ydummy/README.md). The canvas module is pure state; THIS
# program owns the renderer (WebGPU pipeline + WGSL assembly) and reads
# the canvas through the object API. Spawns the pure client
# (demo/ydummy/client) over a socketpair, serves its yclass-RPC calls,
# renders the resulting state headless and verifies the readback. Needs
# the real Dawn library; skipped when webgpu is headers-only. The
# registration/serve calls here are exactly what a hosting yetty makes
# when the module is integrated there.

if(NOT YETTY_ENABLE_LIB_WEBGPU)
    return()
endif()
if(WIN32)
    # socketpair/fork-based harness — POSIX only, same policy as
    # tools/msdf/gen-msdf-gpu.
    return()
endif()

add_executable(ydummy-server
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ydummy-server PRIVATE ${YETTY_ROOT}/include)

target_link_libraries(ydummy-server PRIVATE
    yetty_ydummy
    yetty_ywebgpu
    webgpu
    yetty_yplatform_core
)

# Dawn (libwebgpu_dawn.a) is C++ — force the C++ stdlib at link time even
# though all our sources here are C.
set_target_properties(ydummy-server PROPERTIES LINKER_LANGUAGE CXX)

if(NOT MSVC)
    target_compile_options(ydummy-server PRIVATE -Wall -Wextra)
endif()
