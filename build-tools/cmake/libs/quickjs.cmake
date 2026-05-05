# quickjs — MIT-licensed JavaScript engine (quickjs-ng fork).
#
# Source-included from tmp/quickjs (where the user clones it). Same
# pattern as lexbor — long-term this should follow the prebuilt-tarball
# 3rdparty release flow (libcurl/zlib/brotli), but for the bring-up we
# build in-tree.
#
# Exposed targets:
#   qjs — static library with the JS runtime + parser + stdlib
#         intrinsics (no quickjs-libc, no shell helpers — those are CLI-
#         only and pull in os/syscalls we don't want bound to the JS
#         host program).

include_guard(GLOBAL)

if(TARGET qjs)
    return()
endif()

set(_QJS_DIR "${YETTY_ROOT}/tmp/quickjs"
    CACHE PATH "Path to a quickjs-ng source tree (cloned)")

if(NOT EXISTS "${_QJS_DIR}/CMakeLists.txt")
    message(WARNING
        "quickjs: source not found at ${_QJS_DIR}; skipping. "
        "Clone with: git clone https://github.com/quickjs-ng/quickjs ${_QJS_DIR}")
    return()
endif()

# Turn off everything optional — we only want the static library.
set(QJS_ENABLE_INSTALL              OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES              OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_CLI_STATIC            OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_CLI_WITH_MIMALLOC     OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_CLI_WITH_STATIC_MIMALLOC OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_LIBC                  OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_WERROR                OFF CACHE BOOL "" FORCE)
set(QJS_DISABLE_PARSER              OFF CACHE BOOL "" FORCE)
set(BUILD_QJS_LIBC                  OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING                   OFF CACHE BOOL "" FORCE)

# Suppress -Wpedantic / various pickiness from upstream — not our code.
set(CMAKE_POLICY_DEFAULT_CMP0077    NEW)

add_subdirectory(${_QJS_DIR} ${CMAKE_BINARY_DIR}/3rdparty/quickjs EXCLUDE_FROM_ALL)

if(TARGET qjs)
    # Make the headers visible via $<BUILD_INTERFACE:...> so the source
    # path doesn't end up in INSTALL_INTERFACE — same hygiene fix we
    # applied to lexbor.cmake.
    target_include_directories(qjs SYSTEM INTERFACE
        "$<BUILD_INTERFACE:${_QJS_DIR}>")
    target_compile_options(qjs PRIVATE -w)
    message(STATUS "quickjs: source-included from ${_QJS_DIR}")
else()
    message(WARNING "quickjs: qjs target not produced — upstream layout changed?")
endif()
