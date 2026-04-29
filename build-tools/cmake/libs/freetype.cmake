# freetype — font rasterization.
#
# Consumes a prebuilt static lib + headers from the 3rdparty release
# tarball published by build-3rdparty-freetype.yml. The from-source
# build (cmake; brotli+bzip2+png deps disabled, zlib via prebuilt zlib
# tarball at compile time) lives in build-tools/3rdparty/freetype/_build.sh.
#
# Exposed targets:
#   freetype           — IMPORTED static archive
#   Freetype::Freetype — alias find_package(Freetype) consumers expect

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

# Idempotency guard keyed on our internal target only. We deliberately do
# NOT bail out when Freetype::Freetype already exists: FindX11 (pulled in
# by Dawn / imgui / yrender) calls find_package(Freetype QUIET) as part of
# its X11-extension search, which silently creates Freetype::Freetype as
# an UNKNOWN IMPORTED target pointing at the system libfreetype.so. If we
# returned here, msdfgen-ext (which links Freetype::Freetype) and yfont
# (which uses FREETYPE_INCLUDE_DIR) would silently bind against the system
# .so instead of our prebuilt static archive — defeating the whole
# 3rdparty-from-github model. Instead we fall through and either alias or
# repoint Freetype::Freetype at our prebuilt below. include_guard(GLOBAL)
# already prevents this file from running twice.
if(TARGET freetype)
    return()
endif()

# zlib resolves first — freetype.a has unresolved zlib symbols.
include(${CMAKE_CURRENT_LIST_DIR}/zlib.cmake)

yetty_3rdparty_fetch(freetype _FREETYPE_DIR)

if(WIN32)
    set(_FREETYPE_LIB "${_FREETYPE_DIR}/lib/freetype.lib")
else()
    set(_FREETYPE_LIB "${_FREETYPE_DIR}/lib/libfreetype.a")
endif()

if(NOT EXISTS "${_FREETYPE_LIB}")
    message(FATAL_ERROR "freetype: archive not found at ${_FREETYPE_LIB} — tarball layout changed?")
endif()

# Upstream installs to include/freetype2/, but we ship include/ flat.
# Accept either layout.
set(_FREETYPE_INC "${_FREETYPE_DIR}/include")
if(EXISTS "${_FREETYPE_DIR}/include/freetype2/ft2build.h")
    set(_FREETYPE_INC "${_FREETYPE_DIR}/include;${_FREETYPE_DIR}/include/freetype2")
endif()

add_library(freetype STATIC IMPORTED GLOBAL)
set_target_properties(freetype PROPERTIES
    IMPORTED_LOCATION "${_FREETYPE_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_FREETYPE_INC}"
    INTERFACE_LINK_LIBRARIES "ZLIB::ZLIB"
)

# If FindX11's cascade already created Freetype::Freetype as IMPORTED
# pointing at the system libfreetype.so, repoint it at our prebuilt static
# archive in place — we can't ALIAS over an existing target. Otherwise
# create the alias as usual. Override per-config IMPORTED_LOCATION_* slots
# too: FindFreetype sets _RELEASE / _DEBUG which take precedence over the
# bare IMPORTED_LOCATION, and the default IMPORTED_CONFIGURATIONS the
# system find_package set may not include the active config.
if(TARGET Freetype::Freetype)
    set_target_properties(Freetype::Freetype PROPERTIES
        IMPORTED_LOCATION         "${_FREETYPE_LIB}"
        IMPORTED_LOCATION_RELEASE "${_FREETYPE_LIB}"
        IMPORTED_LOCATION_DEBUG   "${_FREETYPE_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${_FREETYPE_INC}"
        INTERFACE_LINK_LIBRARIES  "ZLIB::ZLIB"
    )
else()
    add_library(Freetype::Freetype ALIAS freetype)
endif()

set(FREETYPE_INCLUDE_DIR "${_FREETYPE_INC}"  CACHE INTERNAL "")
set(FREETYPE_LIBRARY     "${_FREETYPE_LIB}"  CACHE INTERNAL "")
set(FREETYPE_FOUND       TRUE                CACHE BOOL    "" FORCE)

# Bundle var that downstream code (yetty cmake targets) reads — a
# sane link order for a final executable that uses freetype.
# yetty's previous bundle pulled in brotli + bzip2 + libpng + zlib;
# we keep the same shape so consumers don't change.
set(FREETYPE_ALL_LIBS
    freetype
    png_static
    brotlidec
    brotlicommon
    bz2_static
    ZLIB::ZLIB
    $<$<NOT:$<BOOL:${WIN32}>>:m>
    CACHE INTERNAL "All FreeType static libs in link order"
)

message(STATUS "freetype: prebuilt v${YETTY_3RDPARTY_freetype_VERSION} (${_FREETYPE_LIB})")
