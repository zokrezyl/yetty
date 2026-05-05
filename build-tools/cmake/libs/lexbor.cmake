# lexbor — Apache-2.0 HTML/CSS parsing + DOM + selector engine.
#
# Source-included for now: builds lexbor's static lib straight out of
# tmp/lexbor (where the user dropped a clone). Long-term this should
# follow the same prebuilt-tarball pattern libcurl/zlib/brotli use
# (build-tools/3rdparty/lexbor/_build.sh + a GitHub release), but for
# the bring-up we compile in tree.
#
# Exposed targets:
#   lexbor_static — all lexbor modules linked together.

include_guard(GLOBAL)

if(TARGET lexbor_static)
    return()
endif()

set(_LEXBOR_DIR "${YETTY_ROOT}/tmp/lexbor"
    CACHE PATH "Path to a lexbor source tree (cloned)")

if(NOT EXISTS "${_LEXBOR_DIR}/source/lexbor/core/core.h")
    message(WARNING
        "lexbor: source not found at ${_LEXBOR_DIR}; skipping ylexbor")
    return()
endif()

# lexbor's CMakeLists installs system-wide by default and turns on
# tests/examples/utils. Override before add_subdirectory so we end up
# with just the static lib.
set(LEXBOR_BUILD_SHARED      OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_STATIC      ON  CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_SEPARATELY  OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_BENCHMARKS  OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_UTILS       OFF CACHE BOOL "" FORCE)
set(LEXBOR_INSTALL_HEADERS   OFF CACHE BOOL "" FORCE)
set(LEXBOR_MAKE_RPM_FILES    OFF CACHE BOOL "" FORCE)
set(LEXBOR_MAKE_DEB_FILES    OFF CACHE BOOL "" FORCE)

# lexbor sets CMP0048 in a way that warns under newer cmake; suppress.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0048 NEW)

add_subdirectory(${_LEXBOR_DIR} ${CMAKE_BINARY_DIR}/3rdparty/lexbor EXCLUDE_FROM_ALL)

# Upstream's static target is named `lexbor_static` (the shared one is
# `lexbor`). Add include directories as a public interface so consumers
# don't have to know about the source layout.
if(TARGET lexbor_static)
    target_include_directories(lexbor_static
        SYSTEM INTERFACE
            "$<BUILD_INTERFACE:${_LEXBOR_DIR}/source>")
    # Suppress the avalanche of -Wpedantic / -Wsign-compare warnings
    # from upstream lexbor — not our code, not our problem.
    target_compile_options(lexbor_static PRIVATE
        -w)
    message(STATUS "lexbor: source-included from ${_LEXBOR_DIR}")
else()
    message(WARNING "lexbor: lexbor_static target not produced — upstream layout changed?")
endif()
