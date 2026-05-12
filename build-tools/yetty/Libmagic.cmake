# libmagic — file(1) detection library.
#
# Consumes a prebuilt static lib + headers + magic.mgc database from the
# 3rdparty release tarball published by build-3rdparty-libmagic.yml. The
# from-source build (autotools, per-platform handling, native mkmagic
# bootstrap for cross targets) lives in
# build-tools/3rdparty/libmagic/_build.sh.
#
# Exposed target: `magic` (matches what shared.cmake links against).
# Exposed variable: LIBMAGIC_MGC_PATH — runtime path to the compiled
# magic database. yetty embeds it via incbin at link time.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET magic)
    return()
endif()

yetty_3rdparty_fetch(libmagic _LIBMAGIC_DIR)

# Tarball layout: lib/libmagic.a (or magic.lib on Windows native MSVC)
#                + include/magic.h
#                + share/misc/magic.mgc
if(WIN32 AND EXISTS "${_LIBMAGIC_DIR}/lib/magic.lib")
    set(_LIBMAGIC_LIB "${_LIBMAGIC_DIR}/lib/magic.lib")
elseif(EXISTS "${_LIBMAGIC_DIR}/lib/libmagic.a")
    set(_LIBMAGIC_LIB "${_LIBMAGIC_DIR}/lib/libmagic.a")
else()
    message(FATAL_ERROR
        "libmagic: no static lib found in ${_LIBMAGIC_DIR}/lib/ — \
tarball layout changed? (check build-tools/3rdparty/libmagic/_build.sh)")
endif()
if(NOT EXISTS "${_LIBMAGIC_DIR}/include/magic.h")
    message(FATAL_ERROR
        "libmagic: magic.h not found in ${_LIBMAGIC_DIR}/include/ — tarball layout changed?")
endif()
if(NOT EXISTS "${_LIBMAGIC_DIR}/share/misc/magic.mgc")
    message(FATAL_ERROR
        "libmagic: magic.mgc not found in ${_LIBMAGIC_DIR}/share/misc/ — \
producer's native-mkmagic bootstrap failed?")
endif()

add_library(magic STATIC IMPORTED GLOBAL)
set_target_properties(magic PROPERTIES
    IMPORTED_LOCATION "${_LIBMAGIC_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_LIBMAGIC_DIR}/include"
)

# Path to the runtime magic database (yetty embeds via incbin at link time).
# Same cache-var name as the from-source build exported, so existing
# consumers don't need to change.
set(LIBMAGIC_MGC_PATH "${_LIBMAGIC_DIR}/share/misc/magic.mgc"
    CACHE FILEPATH "Path to compiled magic database" FORCE)

message(STATUS "libmagic: prebuilt v${YETTY_3RDPARTY_libmagic_VERSION} "
               "(${_LIBMAGIC_LIB}, mgc: ${LIBMAGIC_MGC_PATH})")
