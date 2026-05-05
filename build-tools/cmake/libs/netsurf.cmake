# netsurf — NetSurf 3.11 core + helper libraries.
#
# Source tree at ${YETTY_NETSURF_ROOT} (default: tmp/netsurf-all-3.11
# unpacked from netsurf-all-3.11.tar.gz). NetSurf has its own Makefile
# build system; we drive it via add_custom_command + ExternalProject and
# expose the resulting static archives as IMPORTED targets:
#
#   ns_css        ns_dom         ns_hubbub      ns_parserutils
#   ns_wapcaplet  ns_nsutils     ns_nsbmp       ns_nsgif
#   ns_nslog      ns_nspsl       ns_svgtiny     ns_utf8proc
#   netsurf_core  -- netsurf core .o files (everything except frontends/*)
#
# We build with TARGET=monkey because monkey is the smallest viable
# frontend that produces the full set of libs + the netsurf core
# objects. We don't *use* the monkey frontend itself — we ar the core
# objects into a separate libnetsurf_core.a and link it under our own
# ynetsurf frontend tables.

include_guard(GLOBAL)

if(TARGET netsurf_core)
    return()
endif()

if(NOT DEFINED YETTY_NETSURF_ROOT)
    set(YETTY_NETSURF_ROOT "${YETTY_ROOT}/tmp/netsurf-all-3.11"
        CACHE PATH "Path to the unpacked netsurf-all-3.11 tree")
endif()

set(_NS_DIR  "${YETTY_NETSURF_ROOT}")
set(_NS_CORE "${_NS_DIR}/netsurf")
set(_NS_INST "${_NS_DIR}/inst-monkey")

if(NOT EXISTS "${_NS_CORE}/include/netsurf/netsurf.h")
    message(WARNING
        "netsurf: source not found at ${_NS_CORE}; "
        "unpack netsurf-all-3.11.tar.gz or set YETTY_NETSURF_ROOT")
    return()
endif()

set(_NS_LIB_NAMES
    css dom hubbub parserutils wapcaplet
    nsutils nsbmp nsgif nslog nspsl
    svgtiny utf8proc)

set(_NS_LIB_FILES "")
foreach(l IN LISTS _NS_LIB_NAMES)
    list(APPEND _NS_LIB_FILES "${_NS_INST}/lib/lib${l}.a")
endforeach()

# CMake refuses INTERFACE_INCLUDE_DIRECTORIES that don't exist yet, so
# pre-create the inst tree. NetSurf's `make install` lands real headers
# here later — the directory just has to exist at configure time.
file(MAKE_DIRECTORY "${_NS_INST}/include" "${_NS_INST}/lib")

set(_NS_CORE_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/libnetsurf_core.a")
set(_NS_MESSAGES_EN  "${CMAKE_CURRENT_BINARY_DIR}/netsurf-Messages-en")

# Patch NetSurf's curl handler to NOT request `Accept-Encoding: gzip` —
# our prebuilt libcurl is built with -DCURL_ZLIB=OFF (and no brotli /
# zstd), so any gzip response trips CURLE_BAD_CONTENT_ENCODING. Idempotent
# (the marker comment after the patch makes a second sed a no-op).
set(_NS_CURL_C "${_NS_CORE}/content/fetchers/curl.c")
if(EXISTS "${_NS_CURL_C}")
    execute_process(
        COMMAND grep -q "yetty: gzip disabled" "${_NS_CURL_C}"
        RESULT_VARIABLE _patched)
    if(NOT _patched EQUAL 0)
        execute_process(
            COMMAND sed -i
                "s|SETOPT(CURLOPT_ENCODING, \"gzip\");|SETOPT(CURLOPT_ENCODING, \"\"); /* yetty: gzip disabled — prebuilt libcurl has no zlib */|"
                "${_NS_CURL_C}"
            RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(WARNING "ynetsurf: failed to patch curl.c gzip request")
        else()
            message(STATUS "ynetsurf: patched ${_NS_CURL_C} to drop gzip Accept-Encoding")
        endif()
    endif()
endif()

# NetSurf's per-target build dir lives at netsurf/build/<friendly>-monkey
# (e.g. Linux-monkey, Darwin-monkey). The exact friendly name is computed
# inside netsurf's Makefile machinery; rather than re-derive it here, the
# packaging step (netsurf-archive.cmake) globs for it at run-time, so it
# only has to exist after the make step has run.
set(_NS_BUILDPARENT "${_NS_CORE}/build")

# 1) Drive `make TARGET=monkey` once. Output is the set of static
#    archives — the custom command re-runs only if any are missing.
add_custom_command(
    OUTPUT ${_NS_LIB_FILES}
    COMMAND ${CMAKE_COMMAND} -E env
            "TARGET=monkey"
            "PKG_CONFIG_PATH=${_NS_INST}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}"
            "CFLAGS=-Wno-error=redundant-decls -Wno-error=array-bounds -Wno-error=stringop-overflow -Wno-error=stringop-overread -Wno-error=stringop-truncation -Wno-error=use-after-free -Wno-error=dangling-pointer -Wno-error=address -Wno-error=cast-function-type -Wno-error=unused-result -Wno-error"
            make -C ${_NS_DIR} TARGET=monkey
    WORKING_DIRECTORY "${_NS_DIR}"
    COMMENT "Building NetSurf 3.11 (TARGET=monkey)"
    VERBATIM USES_TERMINAL)

# 2) ar the netsurf core .o files (everything except frontends/*) into
#    a separate static archive that ynetsurf links.
add_custom_command(
    OUTPUT "${_NS_CORE_ARCHIVE}"
    DEPENDS ${_NS_LIB_FILES}
    COMMAND ${CMAKE_COMMAND}
        -DBUILDPARENT=${_NS_BUILDPARENT}
        -DOUTPUT=${_NS_CORE_ARCHIVE}
        -DAR=${CMAKE_AR}
        -P "${YETTY_ROOT}/build-tools/cmake/libs/netsurf-archive.cmake"
    COMMENT "Packaging NetSurf core objects → libnetsurf_core.a"
    VERBATIM)

# 3) Generate per-language Messages from FatMessages using the host-built
#    split-messages tool that `make TARGET=monkey` produces under build/.
add_custom_command(
    OUTPUT "${_NS_MESSAGES_EN}"
    DEPENDS ${_NS_LIB_FILES}
    COMMAND ${CMAKE_COMMAND}
        -DBUILDPARENT=${_NS_BUILDPARENT}
        -DFATMESSAGES=${_NS_CORE}/resources/FatMessages
        -DOUTPUT=${_NS_MESSAGES_EN}
        -P "${YETTY_ROOT}/build-tools/cmake/libs/netsurf-messages.cmake"
    COMMENT "Generating NetSurf Messages (en)"
    VERBATIM)

add_custom_target(netsurf_built ALL
    DEPENDS ${_NS_LIB_FILES} "${_NS_CORE_ARCHIVE}" "${_NS_MESSAGES_EN}")

# 3) Imported targets for the helper libs.
foreach(l IN LISTS _NS_LIB_NAMES)
    add_library(ns_${l} STATIC IMPORTED GLOBAL)
    set_target_properties(ns_${l} PROPERTIES
        IMPORTED_LOCATION "${_NS_INST}/lib/lib${l}.a"
        INTERFACE_INCLUDE_DIRECTORIES "${_NS_INST}/include")
    add_dependencies(ns_${l} netsurf_built)
endforeach()

# 4) Imported target for the netsurf core archive. Public include dirs
#    are the inst-monkey/include tree (where the helper libs install
#    their headers) plus the netsurf source root (for desktop/, utils/,
#    content/ private headers ynetsurf calls into).
add_library(netsurf_core STATIC IMPORTED GLOBAL)
set_target_properties(netsurf_core PROPERTIES
    IMPORTED_LOCATION "${_NS_CORE_ARCHIVE}"
    INTERFACE_INCLUDE_DIRECTORIES
        "${_NS_INST}/include;${_NS_CORE};${_NS_CORE}/include")
add_dependencies(netsurf_core netsurf_built)
target_link_libraries(netsurf_core INTERFACE
    ns_css ns_dom ns_hubbub ns_parserutils ns_wapcaplet
    ns_nsutils ns_nsbmp ns_nsgif ns_nslog ns_nspsl
    ns_svgtiny ns_utf8proc)

# Run-time dependencies that NetSurf core links against.
#
# Use yetty's prebuilt libcurl (which bundles openssl-new) rather than
# find_package(CURL) / find_package(OpenSSL). The system OpenSSL on this
# host is 3.x and lacks symbols (EVP_PKEY_id, FIPS_mode) that the
# prebuilt libssh2 archive expects — pulling system OpenSSL into the
# global target set via find_package() leaks into the main `yetty`
# executable's link line and breaks it. libcurl.cmake gives us a
# self-contained CURL::libcurl target with openssl-new baked in.
include(${YETTY_ROOT}/build-tools/cmake/libs/libcurl.cmake)

find_package(PkgConfig REQUIRED)
pkg_check_modules(_NS_LIBXML  REQUIRED libxml-2.0)
pkg_check_modules(_NS_LIBJPEG REQUIRED libjpeg)
pkg_check_modules(_NS_LIBPNG  REQUIRED libpng)
pkg_check_modules(_NS_LIBWEBP REQUIRED libwebp)

target_link_libraries(netsurf_core INTERFACE
    CURL::libcurl
    ${_NS_LIBXML_LIBRARIES}
    ${_NS_LIBJPEG_LIBRARIES}
    ${_NS_LIBPNG_LIBRARIES}
    ${_NS_LIBWEBP_LIBRARIES}
    z
    m)
target_include_directories(netsurf_core INTERFACE
    ${_NS_LIBXML_INCLUDE_DIRS}
    ${_NS_LIBJPEG_INCLUDE_DIRS}
    ${_NS_LIBPNG_INCLUDE_DIRS}
    ${_NS_LIBWEBP_INCLUDE_DIRS})

message(STATUS "netsurf: ${_NS_DIR} (TARGET=monkey, host=${_NS_HOST})")
