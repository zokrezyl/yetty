# yxml — Yorhel/yxml streaming XML parser, MIT-licensed single-TU lib.
# Consumes a prebuilt static-lib + headers tarball published by
# .github/workflows/build-3rdparty-yxml.yml.
#
# Why this is a 3rdparty package instead of a CPM `GIT_REPOSITORY` fetch:
# upstream lives on https://g.blicky.net/yxml.git which intermittently
# returns missing-blob errors during git clone + rate-limits with HTTP
# 429 on retries. There is no upstream tarball or GitHub mirror. Our
# 3rdparty/_build.sh fetches yxml.c/yxml.h via cgit's `/plain/?id=<sha>`
# endpoint (much more reliable than the git transport against that host)
# and compiles per-platform, then publishes per-target tarballs the same
# way every other 3rdparty lib does.
#
# Exposed target:
#   yxml  (STATIC IMPORTED) — INTERFACE_INCLUDE_DIRECTORIES points at the
#                             extracted include/ dir so consumers can
#                             `#include "yxml.h"` directly.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET yxml)
    return()
endif()

yetty_3rdparty_fetch(yxml _YXML_DIR)

# Tarball layout: lib/libyxml.a (or yxml.lib on windows-MSVC) + include/yxml.h.
if(MSVC)
    set(_YXML_LIB "${_YXML_DIR}/lib/yxml.lib")
else()
    set(_YXML_LIB "${_YXML_DIR}/lib/libyxml.a")
endif()

foreach(_F "${_YXML_LIB}" "${_YXML_DIR}/include/yxml.h")
    if(NOT EXISTS "${_F}")
        message(FATAL_ERROR
            "yxml: missing ${_F} — tarball layout changed? \
(check build-tools/3rdparty/yxml/_build.sh)")
    endif()
endforeach()

add_library(yxml STATIC IMPORTED GLOBAL)
set_target_properties(yxml PROPERTIES
    IMPORTED_LOCATION "${_YXML_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_YXML_DIR}/include")

message(STATUS "yxml: prebuilt @${YETTY_3RDPARTY_yxml_VERSION}")
