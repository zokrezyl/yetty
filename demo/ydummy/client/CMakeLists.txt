# ydummy-client — pure wire client demo for the pilot module (see
# src/yetty/ydummy/README.md).
#
# THE LINK BOUNDARY IS THE PRODUCT: this target must never grow a webgpu
# include path or link the implementation library (yetty_ydummy). It links
# the generated call stubs + the yclass runtime, nothing else. A WGPU type
# leaking into the wire header fails this compile; an impl/skel symbol
# leaking into the link is caught by the nm assertion in the server's
# verify flow.

add_executable(ydummy-client
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ydummy-client PRIVATE ${YETTY_ROOT}/include)

target_link_libraries(ydummy-client PRIVATE
    yetty_api_ydummy
    yetty_yclass
    yetty_ycore
    # ytrace.c needs the platform mutex; yplatform_core is platform glue
    # (mutex/fs/paths), no GPU anywhere in it.
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(ydummy-client PRIVATE -Wall -Wextra)
endif()
