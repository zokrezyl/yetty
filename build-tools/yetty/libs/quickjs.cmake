# quickjs — MIT-licensed JavaScript engine (quickjs-ng fork).
#
# Consumes a prebuilt static lib + headers from the 3rdparty release
# tarball published by build-3rdparty-quickjs.yml. quickjs-ng is pure C
# / libc-only — no transitive prebuilt deps to thread through.
#
# Exposes `qjs` (STATIC IMPORTED) — the same target name the upstream
# CMakeLists produced when this was source-included, so consumers
# (src/yetty/ybrowser) keep linking `qjs` and gating on `if(TARGET qjs)`
# unchanged.
#
# Graceful-skip: until the `lib-quickjs-<ver>` release is published the
# prebuilt download 404s. Rather than fail the whole configure (JS is an
# optional feature — every consumer already guards with `if(TARGET qjs)`),
# we probe the download and, if it isn't there yet, skip target creation
# so the tree still builds with JavaScript disabled. Publish the release
# (push tag `lib-quickjs-<ver>`) and JS lights up on the next configure.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET qjs)
    return()
endif()

#-----------------------------------------------------------------------------
# Probe: is the prebuilt release reachable? Compute the same filename /
# URL the fetch helper would, and try the download into the shared cache.
# A hit populates the cache so the real fetch below reuses it (no second
# download); a miss means "not published yet" → skip, JS stays off.
#-----------------------------------------------------------------------------
file(READ "${YETTY_ROOT}/build-tools/3rdparty/quickjs/version" _QJS_VER)
string(STRIP "${_QJS_VER}" _QJS_VER)
yetty_3rdparty_target_platform(_QJS_PLAT)
set(_QJS_FILE "quickjs-${_QJS_PLAT}-${_QJS_VER}.tar.gz")
set(_QJS_TARBALL "${YETTY_3RDPARTY_CACHE_DIR}/${_QJS_FILE}")
set(_QJS_URL "${YETTY_3RDPARTY_URL_BASE}/lib-quickjs-${_QJS_VER}/${_QJS_FILE}")
set(_QJS_STAMP "${YETTY_3RDPARTY_OUTPUT_DIR}/quickjs/.fetched-${_QJS_VER}")

if(NOT EXISTS "${_QJS_STAMP}" AND NOT EXISTS "${_QJS_TARBALL}")
    message(STATUS "quickjs: probing prebuilt ${_QJS_FILE}")
    file(DOWNLOAD "${_QJS_URL}" "${_QJS_TARBALL}" STATUS _QJS_DL TLS_VERIFY ON)
    list(GET _QJS_DL 0 _QJS_DL_CODE)

    # Two ways a missing release shows up: a non-zero curl status, OR a
    # "successful" transfer that actually fetched GitHub's 404 page into
    # the file (cmake does not set CURLOPT_FAILONERROR, so HTTP errors can
    # land as status 0). Gate on BOTH — only a real gzip (magic 1f 8b) is
    # treated as a published prebuilt.
    set(_QJS_OK TRUE)
    if(NOT _QJS_DL_CODE EQUAL 0)
        set(_QJS_OK FALSE)
    elseif(EXISTS "${_QJS_TARBALL}")
        file(READ "${_QJS_TARBALL}" _QJS_MAGIC LIMIT 2 HEX)
        if(NOT _QJS_MAGIC STREQUAL "1f8b")
            set(_QJS_OK FALSE)
        endif()
    else()
        set(_QJS_OK FALSE)
    endif()

    if(NOT _QJS_OK)
        file(REMOVE "${_QJS_TARBALL}")
        message(STATUS
            "quickjs: prebuilt not available yet for ${_QJS_PLAT} "
            "(release lib-quickjs-${_QJS_VER}) — JavaScript disabled. "
            "Publish it by pushing tag lib-quickjs-${_QJS_VER} "
            "(.github/workflows/build-3rdparty-quickjs.yml).")
        return()
    endif()
endif()

# Reachable (or already cached/extracted) — let the shared helper handle
# extraction + stamping. It finds the cached tarball and won't re-download.
yetty_3rdparty_fetch(quickjs _QJS_DIR)

# Tarball layout: lib/libqjs.a (Unix) | lib/qjs.lib (Win) + include/quickjs.h
if(WIN32 AND EXISTS "${_QJS_DIR}/lib/qjs.lib")
    set(_QJS_LIB "${_QJS_DIR}/lib/qjs.lib")
elseif(EXISTS "${_QJS_DIR}/lib/libqjs.a")
    set(_QJS_LIB "${_QJS_DIR}/lib/libqjs.a")
else()
    message(FATAL_ERROR
        "quickjs: no static lib found in ${_QJS_DIR}/lib/ — \
tarball layout changed? (check build-tools/3rdparty/quickjs/_build.sh)")
endif()
if(NOT EXISTS "${_QJS_DIR}/include/quickjs.h")
    message(FATAL_ERROR
        "quickjs: quickjs.h not found in ${_QJS_DIR}/include/ — \
tarball layout changed?")
endif()

add_library(qjs STATIC IMPORTED GLOBAL)
set_target_properties(qjs PROPERTIES
    IMPORTED_LOCATION "${_QJS_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_QJS_DIR}/include"
)

# quickjs-ng's `qjs` target links libm + (on POSIX) dl + pthread. Those
# INTERFACE links are lost when importing the bare static archive, so
# replay them here — otherwise final links fail with undefined pow /
# dlopen / pthread_* references. find_package(Threads) here makes the
# import self-sufficient: Threads::Threads resolves to the right thing
# per platform (pthread on linux/macos, nothing on android where it's in
# libc) instead of a bare -lpthread the NDK has no archive for.
if(NOT WIN32)
    find_package(Threads QUIET)
    if(TARGET Threads::Threads)
        set(_QJS_THREADS "Threads::Threads")
    else()
        set(_QJS_THREADS "")
    endif()
    set_target_properties(qjs PROPERTIES
        INTERFACE_LINK_LIBRARIES "m;${CMAKE_DL_LIBS};${_QJS_THREADS}")
endif()

message(STATUS "quickjs: prebuilt v${YETTY_3RDPARTY_quickjs_VERSION}")
