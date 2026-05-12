# tree-sitter — core lib + 15 grammars. Consumes the prebuilt bundle
# tarball published by build-3rdparty-tree-sitter.yml. The from-source
# build (download core + 15 grammar tarballs, compile each per target)
# now lives in build-tools/3rdparty/tree-sitter/_build.sh.
#
# Exposed targets (matched to what build-tools/cmake/TreeSitter.cmake
# previously created):
#   tree-sitter-core         imported static (lib/src/lib.c)
#   ts-grammar-<name>        imported static (parser.c [+scanner.{c,cc}])
# Plus the per-grammar TS_QUERIES_DIR_<name> cache vars pointing at the
# packaged queries dir.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET tree-sitter-core)
    return()
endif()

yetty_3rdparty_fetch(tree-sitter _TS_DIR)

# MSVC tarball ships <name>.lib (no lib prefix); unix ships lib<name>.a.
if(WIN32)
    set(_TS_LIB_PREFIX "")
    set(_TS_LIB_SUFFIX ".lib")
else()
    set(_TS_LIB_PREFIX "lib")
    set(_TS_LIB_SUFFIX ".a")
endif()

# Core
set(_TS_CORE_LIB "${_TS_DIR}/lib/${_TS_LIB_PREFIX}tree-sitter-core${_TS_LIB_SUFFIX}")
if(NOT EXISTS "${_TS_CORE_LIB}")
    message(FATAL_ERROR
        "tree-sitter: prebuilt core lib not found: ${_TS_CORE_LIB} — \
tarball layout changed? (check build-tools/3rdparty/tree-sitter/_build.sh)")
endif()
add_library(tree-sitter-core STATIC IMPORTED GLOBAL)
set_target_properties(tree-sitter-core PROPERTIES
    IMPORTED_LOCATION "${_TS_CORE_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_TS_DIR}/include"
)

# Grammars + per-grammar queries dir constants.
set(_TS_GRAMMARS
    c cpp python javascript typescript rust go java bash json
    yaml toml html xml markdown)

foreach(_g ${_TS_GRAMMARS})
    set(_lib "${_TS_DIR}/lib/${_TS_LIB_PREFIX}ts-grammar-${_g}${_TS_LIB_SUFFIX}")
    if(NOT EXISTS "${_lib}")
        message(FATAL_ERROR
            "tree-sitter: grammar lib not found: ${_lib} — \
update GRAMMARS table in _build.sh and bump version")
    endif()
    add_library(ts-grammar-${_g} STATIC IMPORTED GLOBAL)
    set_target_properties(ts-grammar-${_g} PROPERTIES
        IMPORTED_LOCATION "${_lib}"
    )
    set(TS_QUERIES_DIR_${_g} "${_TS_DIR}/queries/${_g}"
        CACHE PATH "Queries dir for tree-sitter-${_g}" FORCE)
endforeach()

list(LENGTH _TS_GRAMMARS _TS_GRAMMARS_COUNT)
message(STATUS "tree-sitter: prebuilt v${YETTY_3RDPARTY_tree-sitter_VERSION} "
               "(core + ${_TS_GRAMMARS_COUNT} grammars; queries under ${_TS_DIR}/queries)")
