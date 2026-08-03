# ytermsink — the abstract terminal-host "sink" interface a terminal-content
# figure calls up to. Pure contract, no data, GPU-free. Migrated to the
# role-split codegen layout; build ownership follows generated-source
# ownership:
#
#   yetty_api_ytermsink  compiles src/yetty/gen/api/ytermsink/sink.c and
#                        exports <yetty/api/ytermsink/sink.h> — the object API
#                        (typed method stubs). What a content figure links to
#                        call the sink; GPU-free (yclass + ycore only).
#
#   yetty_ytermsink      compiles the hand-written sink.c (which #includes the
#                        generated impl glue src/yetty/gen/impl/ytermsink/
#                        sink.c: accessor, check shims, virtual defaults,
#                        registration). Links yetty_api_ytermsink.

add_library(yetty_api_ytermsink STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/ytermsink/sink.c
)

target_include_directories(yetty_api_ytermsink PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_api_ytermsink PUBLIC
    yetty_ycore
    yetty_yclass
)

add_library(yetty_ytermsink STATIC
    sink.c
)

target_include_directories(yetty_ytermsink PUBLIC
    ${YETTY_ROOT}/include
)
# sink.c #includes the generated impl glue via "yetty/gen/impl/...".
target_include_directories(yetty_ytermsink PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_ytermsink PUBLIC
    yetty_api_ytermsink
)

# sink.c carries `[[clang::annotate(...)]]` (C23 attribute syntax); see
# ygrid/CMakeLists.txt for the rationale.
if(NOT MSVC)
    set_target_properties(yetty_ytermsink PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

# Register with the yclass codegen graph — the module list IS these
# call sites (see build-tools/yetty/yclass-codegen.cmake).
yetty_yclass_module(ytermsink)
