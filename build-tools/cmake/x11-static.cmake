# x11-static.cmake — resolve X11/Xext + transitive deps to .a files.
#
# Sets YETTY_X11_STATIC_LIBS to the link list (order matters: dependents
# before dependencies). Expects libbsd's arc4random_buf to be served by
# glibc 2.36+ — earlier glibc would also need libbsd.a/libmd.a here.
#
# Use this in place of find_package(X11) + X11::X11/X11::Xext/${X11_LIBRARIES}
# anywhere we want X11 baked into the binary instead of dlopen'd at runtime.

include_guard(GLOBAL)

if(DEFINED YETTY_X11_STATIC_LIBS)
    return()
endif()

set(_x11_static_search_paths
    /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
    /usr/lib/x86_64-linux-gnu
    /usr/lib/aarch64-linux-gnu
    /usr/lib
    /usr/local/lib
)

find_library(X11_STATIC_LIB     libX11.a     PATHS ${_x11_static_search_paths} REQUIRED)
find_library(XEXT_STATIC_LIB    libXext.a    PATHS ${_x11_static_search_paths} REQUIRED)
find_library(XCB_STATIC_LIB     libxcb.a     PATHS ${_x11_static_search_paths} REQUIRED)
find_library(XAU_STATIC_LIB     libXau.a     PATHS ${_x11_static_search_paths} REQUIRED)
find_library(XDMCP_STATIC_LIB   libXdmcp.a   PATHS ${_x11_static_search_paths} REQUIRED)

# libX11 also wants the X11 headers — find_package gives us X11_INCLUDE_DIR
# without forcing us to take its dynamic libs.
find_package(X11 QUIET COMPONENTS Xext)
if(NOT X11_INCLUDE_DIR)
    message(FATAL_ERROR "x11-static: X11 headers not found (install libx11-dev / xorgproto)")
endif()
set(YETTY_X11_STATIC_INCLUDE_DIR "${X11_INCLUDE_DIR}" CACHE PATH "X11 headers for static link")

# Order: callers (Xext, X11) before providers (xcb -> Xau, Xdmcp).
set(YETTY_X11_STATIC_LIBS
    ${XEXT_STATIC_LIB}
    ${X11_STATIC_LIB}
    ${XCB_STATIC_LIB}
    ${XAU_STATIC_LIB}
    ${XDMCP_STATIC_LIB}
    CACHE STRING "Static X11 link list (Xext, X11, xcb, Xau, Xdmcp)"
)

message(STATUS "x11-static: using ${YETTY_X11_STATIC_LIBS}")
