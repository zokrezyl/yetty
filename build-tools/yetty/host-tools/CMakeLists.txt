# Standalone build for host tools and VM tools
# Used when cross-compiling to build native tools, and for building
# static x86_64 binaries for JSLinux VM (yecho, ycat, ybrowser)
# No X11/GLFW/imgui dependencies

cmake_minimum_required(VERSION 3.20)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version")
cmake_policy(SET CMP0074 NEW)

project(yetty-host-tools LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)

# YETTY_ROOT is three levels up from build-tools/yetty/host-tools/
get_filename_component(YETTY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

include(${YETTY_ROOT}/build-tools/yetty/CPM.cmake)

# spdlog (needed by ytrace)
CPMAddPackage(
    NAME spdlog
    GITHUB_REPOSITORY gabime/spdlog
    GIT_TAG v1.16.0
)

# ytrace for logging
CPMAddPackage(
    NAME ytrace
    GITHUB_REPOSITORY zokrezyl/ytrace
    GIT_TAG v0.0.13
    OPTIONS
        "YTRACE_BUILD_TOOLS OFF"
        "YTRACE_BUILD_EXAMPLES OFF"
        "YTRACE_ENABLE_YTEST ON"
)

# args for command line parsing
CPMAddPackage(
    NAME args
    GITHUB_REPOSITORY Taywee/args
    GIT_TAG 6.4.6
)

# glm for math
CPMAddPackage(
    NAME glm
    GITHUB_REPOSITORY g-truc/glm
    GIT_TAG 1.0.1
)

# zlib (required by libpng and FreeType)
include(${YETTY_ROOT}/build-tools/yetty/libs/zlib.cmake)

# libpng (required by FreeType)
include(${YETTY_ROOT}/build-tools/yetty/libs/libpng.cmake)

# FreeType + its companions (brotli, bzip2) — each fetched as its own
# 3rdparty tarball.
include(${YETTY_ROOT}/build-tools/yetty/libs/brotli.cmake)
include(${YETTY_ROOT}/build-tools/yetty/libs/bzip2.cmake)
include(${YETTY_ROOT}/build-tools/yetty/libs/freetype.cmake)

# msdfgen
CPMAddPackage(
    NAME msdfgen
    GITHUB_REPOSITORY Chlumsky/msdfgen
    GIT_TAG v1.12
    DOWNLOAD_ONLY YES
)

set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME tinyxml2
    GITHUB_REPOSITORY leethomason/tinyxml2
    GIT_TAG 10.0.0
)

if(msdfgen_ADDED)
    file(GLOB MSDFGEN_CORE_SOURCES "${msdfgen_SOURCE_DIR}/core/*.cpp")
    add_library(msdfgen-core STATIC ${MSDFGEN_CORE_SOURCES})
    add_library(msdfgen::msdfgen-core ALIAS msdfgen-core)
    target_include_directories(msdfgen-core PUBLIC ${msdfgen_SOURCE_DIR})
    target_compile_features(msdfgen-core PUBLIC cxx_std_11)
    target_compile_definitions(msdfgen-core PUBLIC MSDFGEN_PUBLIC= MSDFGEN_USE_CPP11)

    set(MSDFGEN_EXT_SOURCES
        ${msdfgen_SOURCE_DIR}/ext/import-font.cpp
        ${msdfgen_SOURCE_DIR}/ext/import-svg.cpp
        ${msdfgen_SOURCE_DIR}/ext/resolve-shape-geometry.cpp
    )
    add_library(msdfgen-ext STATIC ${MSDFGEN_EXT_SOURCES})
    add_library(msdfgen::msdfgen-ext ALIAS msdfgen-ext)
    target_include_directories(msdfgen-ext PUBLIC ${msdfgen_SOURCE_DIR} PRIVATE ${FREETYPE_INCLUDE_DIR})
    target_compile_definitions(msdfgen-ext PUBLIC MSDFGEN_EXT_PUBLIC= MSDFGEN_EXTENSIONS MSDFGEN_USE_TINYXML2 MSDFGEN_DISABLE_PNG)
    target_link_libraries(msdfgen-ext PRIVATE msdfgen::msdfgen-core tinyxml2::tinyxml2)
endif()

# CDB (portable howerj version)
CPMAddPackage(
    NAME howerj_cdb
    URL https://github.com/howerj/cdb/archive/refs/tags/v4.3.2.tar.gz
    VERSION 4.3.2
    DOWNLOAD_ONLY YES
)
if(howerj_cdb_ADDED)
    add_library(cdb STATIC ${howerj_cdb_SOURCE_DIR}/cdb.c)
    target_include_directories(cdb PUBLIC ${howerj_cdb_SOURCE_DIR})
    target_compile_definitions(cdb PUBLIC YETTY_USE_HOWERJ_CDB=1)
    target_compile_options(cdb PRIVATE -w)
endif()

# MSVC: this is a SEPARATE CMake project, so it does not inherit the main
# Windows build's flags (platform/windows/cmake.cmake). The yetty C sources
# compiled below (ycdb, ytrace, yplatform/*/windows) still need the same
# opt-ins:
#   - /Zc:preprocessor — YETTY_ERR(...) and friends use __VA_ARGS__ count-based
#     dispatch, which MSVC's traditional preprocessor mishandles: it passes
#     __VA_ARGS__ as one token, so YETTY_ERR_DISPATCH gets "not enough
#     arguments" (C4003) and the macro mis-expands to the bare message string
#     — which then "cannot convert" to the Result type (C2065/C2440 in ycdb.c).
#   - /std:clatest + /experimental:c11atomics — clears __STDC_NO_ATOMICS__ for
#     TUs that pull in <stdatomic.h>.
# Scoped here (just before the yetty targets) so the third-party CPM builds
# above are untouched. Without this the host yetty-ymsdf-gen build fails to
# compile ycdb.c on Windows.
if(MSVC)
    add_compile_options(/Zc:preprocessor)
    add_compile_options(
        $<$<COMPILE_LANGUAGE:C>:/std:clatest>
        $<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>)
endif()

# ycdb (C wrapper for CDB)
add_library(yetty_ycdb STATIC ${YETTY_ROOT}/src/yetty/ycdb/ycdb.c)
target_include_directories(yetty_ycdb PUBLIC ${YETTY_ROOT}/include)
target_link_libraries(yetty_ycdb PUBLIC cdb)
target_compile_definitions(yetty_ycdb PUBLIC YETTY_USE_HOWERJ_CDB=1)

# ymsdf-gen (MSDF generator library)
add_library(yetty_ymsdf_gen STATIC ${YETTY_ROOT}/src/yetty/ymsdf-gen/ymsdf-gen.cpp)
target_include_directories(yetty_ymsdf_gen PUBLIC ${YETTY_ROOT}/include ${FREETYPE_INCLUDE_DIR})
target_link_libraries(yetty_ymsdf_gen PUBLIC
    msdfgen::msdfgen-core msdfgen::msdfgen-ext ${FREETYPE_ALL_LIBS} yetty_ycdb)

# ytrace (C implementation)
add_library(ytrace_c STATIC ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c)
target_include_directories(ytrace_c PUBLIC ${YETTY_ROOT}/include)

# Platform thread/term/fs implementations for ytrace
if(WIN32)
    set(YETTY_HOST_PLATFORM_SOURCES
        ${YETTY_ROOT}/src/yetty/yplatform/thread/windows.c
        ${YETTY_ROOT}/src/yetty/yplatform/term/windows.c
        ${YETTY_ROOT}/src/yetty/yplatform/fs/windows.c
        ${YETTY_ROOT}/src/yetty/yplatform/time/windows.c
    )
else()
    set(YETTY_HOST_PLATFORM_SOURCES
        ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/fs/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/time/default.c
    )
endif()

# msdf-gen tool
add_executable(yetty-ymsdf-gen
    ${YETTY_ROOT}/tools/msdf/gen-msdf/main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_HOST_PLATFORM_SOURCES}
)
target_include_directories(yetty-ymsdf-gen PRIVATE ${YETTY_ROOT}/include)
target_link_libraries(yetty-ymsdf-gen PRIVATE yetty_ymsdf_gen)

# =============================================================================
# VM tools - statically linked for x86_64 Linux (JSLinux VM)
# =============================================================================

# yecho-static (musl-static x86_64 for the JSLinux VM) is a TODO under the
# C port. The previous C++ implementation lived in src/yetty/yecho/*.cpp;
# the C version moved to src/yetty/yecho/yecho.c and is built by the main
# CMake tree as the `yecho` tool target. Re-wire it here when the VM image
# starts shipping yecho again — needs ycore + ydraw-list + ysdf + yface +
# yfont_shader_glyph sources compiled standalone.

# =============================================================================
# ycat-static + ybrowser-static — only built when BUILD_VM_TOOLS is ON
# (requires OpenSSL, fontconfig, etc. — available in Docker Alpine build)
# =============================================================================
option(BUILD_VM_TOOLS "Build ycat-static and ybrowser-static VM tools" OFF)

if(BUILD_VM_TOOLS)

# cpr (HTTP client, builds libcurl from source)
# Prefer static OpenSSL for static linking
set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)

# Hide our custom zlib from curl (it tries ZLIB::ZLIB which doesn't match our target)
set(_SAVE_ZLIB_FOUND ${ZLIB_FOUND})
set(_SAVE_ZLIB_LIBRARY ${ZLIB_LIBRARY})
set(ZLIB_FOUND FALSE CACHE BOOL "" FORCE)
unset(ZLIB_LIBRARY CACHE)

set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)

CPMAddPackage(
    NAME cpr
    GITHUB_REPOSITORY libcpr/cpr
    GIT_TAG 1.14.1
    OPTIONS
        "CPR_USE_SYSTEM_CURL OFF"
        "CPR_CURL_USE_LIBPSL OFF"
        "BUILD_SHARED_LIBS OFF"
        "CPR_ENABLE_SSL ON"
        "CPR_BUILD_TESTS OFF"
        "CPR_BUILD_TESTS_SSL OFF"
)

# Restore ZLIB state
set(ZLIB_FOUND ${_SAVE_ZLIB_FOUND} CACHE BOOL "" FORCE)
if(_SAVE_ZLIB_LIBRARY)
    set(ZLIB_LIBRARY ${_SAVE_ZLIB_LIBRARY} CACHE STRING "" FORCE)
endif()

# Tree-sitter + grammars
include(${YETTY_ROOT}/build-tools/yetty/TreeSitter.cmake)

# libmagic (file type detection)
include(${YETTY_ROOT}/build-tools/yetty/Libmagic.cmake)

# Generate tree-sitter query header (same logic as src/yetty/ycat/CMakeLists.txt)
set(TS_GRAMMAR_NAMES
    c cpp python javascript typescript rust go java bash json yaml toml html xml
)
set(TS_GENERATED_HEADER "${CMAKE_CURRENT_BINARY_DIR}/ts_queries_generated.h")

set(TS_QUERY_PARENT_cpp "c")
set(TS_QUERY_PARENT_typescript "javascript")

set(_TS_HEADER "// Auto-generated — do not edit\n#pragma once\n\n")
foreach(NAME ${TS_GRAMMAR_NAMES})
    set(_QUERY_FILE "${TS_QUERIES_DIR_${NAME}}/highlights.scm")
    if(EXISTS "${_QUERY_FILE}")
        set(_QUERY_CONTENT "")
        if(DEFINED TS_QUERY_PARENT_${NAME})
            set(_PARENT "${TS_QUERY_PARENT_${NAME}}")
            set(_PARENT_FILE "${TS_QUERIES_DIR_${_PARENT}}/highlights.scm")
            if(EXISTS "${_PARENT_FILE}")
                file(READ "${_PARENT_FILE}" _QUERY_CONTENT)
                string(APPEND _QUERY_CONTENT "\n")
            endif()
        endif()
        file(READ "${_QUERY_FILE}" _CHILD_CONTENT)
        string(APPEND _QUERY_CONTENT "${_CHILD_CONTENT}")
        string(REPLACE "\\" "\\\\" _QUERY_CONTENT "${_QUERY_CONTENT}")
        # Escape '?' so adjacent '??' sequences in .scm files aren't
        # interpreted as C trigraphs (compilers warn even when trigraphs
        # are disabled, as in C17+).
        string(REPLACE "?" "\\?" _QUERY_CONTENT "${_QUERY_CONTENT}")
        string(REPLACE "\"" "\\\"" _QUERY_CONTENT "${_QUERY_CONTENT}")
        string(REPLACE "\n" "\\n\"\n\"" _QUERY_CONTENT "${_QUERY_CONTENT}")
        string(APPEND _TS_HEADER "static const char TS_QUERY_${NAME}[] =\n\"${_QUERY_CONTENT}\";\n\n")
    else()
        string(APPEND _TS_HEADER "static const char TS_QUERY_${NAME}[] = \"\";\n\n")
    endif()
endforeach()
file(WRITE "${TS_GENERATED_HEADER}" "${_TS_HEADER}")

add_executable(ycat-static
    ${YETTY_ROOT}/src/yetty/ycat/main.cpp
    ${YETTY_ROOT}/src/yetty/ycat/osc.cpp
    ${YETTY_ROOT}/src/yetty/ycat/ts/grammars.cpp
    ${YETTY_ROOT}/src/yetty/ycat/ts/highlight.cpp
)
target_include_directories(ycat-static PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src/yetty/ycat
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(ycat-static PRIVATE
    args
    magic
    cpr::cpr
    tree-sitter-core
    ts-grammar-c
    ts-grammar-cpp
    ts-grammar-python
    ts-grammar-javascript
    ts-grammar-typescript
    ts-grammar-rust
    ts-grammar-go
    ts-grammar-java
    ts-grammar-bash
    ts-grammar-json
    ts-grammar-yaml
    ts-grammar-toml
    ts-grammar-html
    ts-grammar-xml
)
target_compile_definitions(ycat-static PRIVATE
    YCAT_HAS_LIBMAGIC=1
    YCAT_MAGIC_MGC_PATH="/usr/share/misc/magic.mgc"
)
target_compile_features(ycat-static PRIVATE cxx_std_23)
set_target_properties(ycat-static PROPERTIES OUTPUT_NAME "ycat")

# =============================================================================
# ybrowser-static — terminal HTML browser for JSLinux VM
# =============================================================================

# litehtml
CPMAddPackage(
    NAME litehtml
    GITHUB_REPOSITORY litehtml/litehtml
    GIT_TAG v0.9
    OPTIONS
        "LITEHTML_BUILD_TESTING OFF"
)

# stb (image loading)
include(${YETTY_ROOT}/build-tools/yetty/libs/stb.cmake)

# fontconfig (system package for font resolution, with static deps)
find_package(PkgConfig REQUIRED)
set(PKG_CONFIG_EXECUTABLE_SAVE ${PKG_CONFIG_EXECUTABLE})
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
# Get static link flags (includes transitive deps like expat)
execute_process(
    COMMAND pkg-config --static --libs fontconfig
    OUTPUT_VARIABLE FONTCONFIG_STATIC_LDFLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
# Parse into a list
separate_arguments(FONTCONFIG_STATIC_LIBS UNIX_COMMAND "${FONTCONFIG_STATIC_LDFLAGS}")

add_executable(ybrowser-static
    ${YETTY_ROOT}/tools/ybrowser/main.cpp
    ${YETTY_ROOT}/tools/ybrowser/stb_impl.cpp
    ${YETTY_ROOT}/src/yetty/yhtml/html-container.cpp
    ${YETTY_ROOT}/src/yetty/yhtml/html-renderer.cpp
    ${YETTY_ROOT}/src/yetty/yhtml/http-fetcher.cpp
    ${YETTY_ROOT}/src/yetty/ydraw/ydraw-buffer.cpp
    ${YETTY_ROOT}/src/yetty/font/raw-font.cpp
    ${YETTY_ROOT}/src/yetty/font/raw-font-manager.cpp
    ${YETTY_ROOT}/src/yetty/font/freetype.cpp
    ${YETTY_ROOT}/src/yetty/ycat/osc.cpp
)
target_include_directories(ybrowser-static PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
    ${YETTY_ROOT}/src/yetty/yhtml
    ${YETTY_ROOT}/src/yetty/ycat
    ${YETTY_ROOT}/src/yetty/ydraw
    ${FREETYPE_INCLUDE_DIR}
    ${FONTCONFIG_INCLUDE_DIRS}
)
target_link_libraries(ybrowser-static PRIVATE
    litehtml
    cpr::cpr
    ${FREETYPE_ALL_LIBS}
    args
    ytrace::ytrace
    stb
    ${FONTCONFIG_STATIC_LIBS}
)
target_compile_definitions(ybrowser-static PRIVATE YETTY_USE_FONTCONFIG=1)
target_compile_features(ybrowser-static PRIVATE cxx_std_20)
set_target_properties(ybrowser-static PROPERTIES OUTPUT_NAME "ybrowser")

endif() # BUILD_VM_TOOLS
