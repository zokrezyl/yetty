# cdb-viewer — read-only walker for yetty MSDF .cdb files. Pure C, no
# external libs needed: it parses the CDB file format directly and just
# pulls the glyph header struct from yetty/ymsdf-gen/ymsdf-gen.h.

add_executable(cdb-viewer
    main.c
)

target_include_directories(cdb-viewer PRIVATE
    ${YETTY_ROOT}/include
)

if(NOT MSVC)
    target_compile_options(cdb-viewer PRIVATE -Wall -Wextra)
endif()
