# lwIP for webasm — userspace TCP/IP stack compiled into the wasm build.
#
# Unlike libssh2 (which needs its own configure + openssl/mbedtls), lwIP
# is plain portable C we compile with yetty's own emscripten toolchain,
# so FetchContent (source only) is cleaner than ExternalProject: the
# sources join the normal build and pick up the same flags automatically.
#
# Exposed target: `lwip_webasm` — STATIC lib (lwIP core + IPv4 + netif:
# TCP/UDP/ARP/DHCP/DNS). The port (lwipopts.h, arch/cc.h) lives in the
# repo at src/yetty/ynet/lwip-port/; sys_now()/lwip_port_rand() are
# provided by src/yetty/ynet/lwip-port.c (compiled with the ynet module,
# resolved at final link).

include_guard(GLOBAL)
include(FetchContent)

set(_LWIP_PORT_DIR ${YETTY_ROOT}/src/yetty/ynet/lwip-port)

FetchContent_Declare(lwip
    URL https://github.com/lwip-tcpip/lwip/archive/refs/tags/STABLE-2_2_0_RELEASE.tar.gz
    URL_HASH SHA256=631bead5344605141ab5367dc7913eb81671469d5467c40cdb29701313855afe
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(lwip)
if(NOT lwip_POPULATED)
    FetchContent_Populate(lwip)
endif()

# Filelists.cmake defines the lwip*_SRCS lists (it prepends src/ itself,
# so LWIP_DIR is the repo root, not src/).
set(LWIP_DIR ${lwip_SOURCE_DIR})
include(${LWIP_DIR}/src/Filelists.cmake)

add_library(lwip_webasm STATIC
    ${lwipcore_SRCS}
    ${lwipcore4_SRCS}
    ${lwipnetif_SRCS}
)
target_include_directories(lwip_webasm PUBLIC
    ${LWIP_DIR}/src/include
    ${_LWIP_PORT_DIR}
)
# lwIP's own sources are warning-clean only at its tuned level; don't let
# yetty's -Werror (if any) fail the third-party build.
target_compile_options(lwip_webasm PRIVATE -w)
