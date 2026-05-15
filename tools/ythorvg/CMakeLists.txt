# yetty-ythorvg — CLI that renders SVG/Lottie into a ydraw buffer via the
# ythorvg C interface and emits the result as a ydraw OSC sequence.

add_executable(yetty-ythorvg
    main.c
    # ytrace stubs used by libraries we pull in (ydraw-core, ythorvg).
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    # Platform shims (ymutex_*, yplatform_*) needed by ytrace.
)

target_include_directories(yetty-ythorvg PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty-ythorvg PRIVATE
    yetty_ythorvg
    yetty_ydraw_core
    yetty_yface
    yetty_ycore
    yetty_yplatform_core
)
