# libgit2 — local-filesystem Git library (read-only history/inspection for
# the ygit tool). GPLv2 with a linking exception that explicitly permits
# combining with code under any other license. See the libgit2 source
# tarball's COPYING file or build-tools/3rdparty/libgit2/_build.sh.
#
# Consumes a prebuilt static lib + headers from the 3rdparty release tarball
# published by build-3rdparty-libgit2.yml. The from-source build (cmake,
# no HTTPS/SSH, bundled zlib) lives in build-tools/3rdparty/libgit2/_build.sh.
#
# Exposed target: `libgit2` — IMPORTED static archive. Built self-contained
# (USE_BUNDLED_ZLIB=ON, builtin regex + http-parser, no network transports),
# so it needs only libc + pthread (+ librt on glibc for clock_gettime); those
# are wired as INTERFACE deps below.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET libgit2)
    return()
endif()

yetty_3rdparty_fetch(libgit2 _LIBGIT2_DIR)

if(WIN32)
    set(_LIBGIT2_LIB "${_LIBGIT2_DIR}/lib/git2.lib")
else()
    set(_LIBGIT2_LIB "${_LIBGIT2_DIR}/lib/libgit2.a")
endif()

if(NOT EXISTS "${_LIBGIT2_LIB}")
    message(FATAL_ERROR
        "libgit2: archive not found at ${_LIBGIT2_LIB} — \
tarball layout changed? (check build-tools/3rdparty/libgit2/_build.sh)")
endif()
if(NOT EXISTS "${_LIBGIT2_DIR}/include/git2.h")
    message(FATAL_ERROR
        "libgit2: git2.h not found in ${_LIBGIT2_DIR}/include/ — tarball layout changed?")
endif()

add_library(libgit2 STATIC IMPORTED GLOBAL)
set_target_properties(libgit2 PROPERTIES
    IMPORTED_LOCATION "${_LIBGIT2_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_LIBGIT2_DIR}/include"
)

# Self-contained archive (bundled zlib): the only externals are pthread and,
# on glibc, librt for clock_gettime. Windows' no-HTTPS/no-SSH config still
# calls a few Win32 system libs (UUID, crypto for object hashing, sockets).
find_package(Threads REQUIRED)
if(WIN32)
    target_link_libraries(libgit2 INTERFACE ws2_32 advapi32 rpcrt4 crypt32 ole32 secur32 winhttp)
else()
    target_link_libraries(libgit2 INTERFACE Threads::Threads)
    if(APPLE)
        # macOS links the system zlib: libgit2 1.8.4's bundled zlib doesn't
        # compile against recent macOS SDKs, so the prebuilt is built with
        # -DUSE_BUNDLED_ZLIB=OFF (see build-tools/3rdparty/libgit2/_build.sh).
        target_link_libraries(libgit2 INTERFACE z)
    else()
        target_link_libraries(libgit2 INTERFACE rt)  # glibc clock_gettime; not on macOS
    endif()
endif()

# find_package(libgit2)-compat cache vars some downstream consumers read.
set(LIBGIT2_FOUND       TRUE                          CACHE BOOL     "" FORCE)
set(LIBGIT2_INCLUDE_DIR "${_LIBGIT2_DIR}/include"      CACHE PATH     "" FORCE)
set(LIBGIT2_LIBRARY     libgit2                        CACHE STRING   "" FORCE)
set(LIBGIT2_LIBRARIES   libgit2                        CACHE STRING   "" FORCE)

message(STATUS "libgit2: prebuilt v${YETTY_3RDPARTY_libgit2_VERSION} (${_LIBGIT2_LIB})")
