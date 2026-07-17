# ynet — yclass network-capture module.
#
# capture.c is the hand-written annotated source (#includes capture.gen.c at the
# foot); dissect.c is the dependency-free protocol dissector. The module reads
# pcap / pcapng files offline through libpcap, dissects Ethernet / IPv4 / IPv6 /
# TCP / UDP / ICMP / DNS, and aggregates conversations into flows. The yclass
# model.yaml drives the FFI / host-language bindings. GPU-less — pure data model.
#
# libpcap is a hard requirement of THIS module only; if it is absent the module
# is skipped (target never created) and the ynet tool skips itself in turn, so
# the rest of the tree still builds. libpcap is BSD-3-Clause.

find_package(PkgConfig QUIET)
set(YNET_PCAP_FOUND OFF)
if(PkgConfig_FOUND)
    pkg_check_modules(PCAP QUIET libpcap)
    if(PCAP_FOUND)
        set(YNET_PCAP_FOUND ON)
    endif()
endif()
if(NOT YNET_PCAP_FOUND)
    # Systems without a libpcap.pc: fall back to a plain library/header probe.
    find_library(PCAP_LIBRARY pcap)
    find_path(PCAP_INCLUDE_DIR pcap.h)
    if(PCAP_LIBRARY AND PCAP_INCLUDE_DIR)
        set(YNET_PCAP_FOUND ON)
        set(PCAP_LIBRARIES ${PCAP_LIBRARY})
        set(PCAP_INCLUDE_DIRS ${PCAP_INCLUDE_DIR})
    endif()
endif()
if(NOT YNET_PCAP_FOUND)
    message(STATUS "ynet: libpcap not found — skipping yetty_ynet (network tool disabled)")
    return()
endif()

add_library(yetty_ynet STATIC
    capture.c
    dissect.c
    rpc.gen.c
)

target_include_directories(yetty_ynet PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) live under src/yetty/ynet/.
target_include_directories(yetty_ynet PRIVATE ${YETTY_ROOT}/src ${PCAP_INCLUDE_DIRS})

target_link_libraries(yetty_ynet PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_yface       # YDRAW_BIN DCS envelope encoder (topology figure)
    yetty_ydraw_core  # drawable-list builder
    yetty_ysdf        # SDF primitives (nodes/edges)
)
target_link_libraries(yetty_ynet PRIVATE ${PCAP_LIBRARIES} m)

# capture.c carries `[[clang::annotate(...)]]` (C23 attribute syntax).
if(NOT MSVC)
    set_target_properties(yetty_ynet PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
