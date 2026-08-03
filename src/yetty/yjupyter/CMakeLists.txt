# yjupyter — Jupyter Server / kernel client (yclass).
#
#   protocol.c — transport-independent Jupyter v5 message codec + session
#                (yjupyter:message, yjupyter:session).
#   client.c   — the live connection: libcurl REST to create/delete a kernel
#                plus a minimal RFC 6455 WebSocket client (yjupyter:client).
#
# Each hand-written .c #includes its generated <name>.gen.c at the foot;
# rpc.gen.c is compiled alongside. All generated files come from
# `make codegen` — never hand-edited. The module is gated at the parent
# CMakeLists on CURL::libcurl (client.c needs it), mirroring ymap.

include(${YETTY_ROOT}/build-tools/yetty/libs/yyjson.cmake)

add_library(yetty_api_yjupyter STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/yjupyter/protocol.c
    ${YETTY_ROOT}/src/yetty/gen/api/yjupyter/client.c
)
target_include_directories(yetty_api_yjupyter PUBLIC ${YETTY_ROOT}/include)
target_link_libraries(yetty_api_yjupyter PUBLIC yetty_ycore yetty_yclass)

add_library(yetty_yjupyter STATIC
    protocol.c
    client.c
)

target_include_directories(yetty_yjupyter PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yjupyter PUBLIC
    yetty_api_yjupyter
    yetty_ycore
    yetty_yclass
    yyjson::yyjson
    CURL::libcurl
)

# protocol.c / client.c carry YETTY_ANNOTATE(...) attributes — same C23
# constraint as every yclass module.
if(NOT MSVC)
    set_target_properties(yetty_yjupyter PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

# Register with the yclass codegen graph — the module list IS these
# call sites (see build-tools/yetty/yclass-codegen.cmake).
yetty_yclass_module(yjupyter)
