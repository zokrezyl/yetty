# ybrowser — fork of tools/ylexbor that links yetty_ybrowser (libcss
# cascade) instead of yetty_ylexbor (lexbor cascade). Same CLI surface.

add_executable(ybrowser
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)

target_include_directories(ybrowser PRIVATE
    ${YETTY_ROOT}/include
)

target_link_libraries(ybrowser PRIVATE
    yetty_ybrowser
    yetty_ypaint_core
    yetty_ysdf
    yetty_yface
    yetty_ycore
    CURL::libcurl
)

target_compile_options(ybrowser PRIVATE -Wall -Wno-unused-parameter)
