# cdb-diff — compare two yetty MSDF .cdb files glyph-by-glyph.
# Pure C, no library dependencies (parses CDB format directly, like cdb-viewer).
# Used to narrow down per-glyph divergence between the CPU (msdfgen) reference
# and the GPU (WGSL compute shader) MSDF generator.

add_executable(cdb-diff
    main.c
)

target_include_directories(cdb-diff PRIVATE
    ${YETTY_ROOT}/include
)

if(NOT MSVC)
    target_compile_options(cdb-diff PRIVATE -Wall -Wextra)
    target_link_libraries(cdb-diff PRIVATE m)
endif()
