# ybrowser — dual-mode HTML tool.
#   --once / non-TTY: render a page and emit one ydraw OSC envelope (legacy).
#   interactive:      a Chrome-like ygui client (tabbar, address bar, nav,
#                     scrollable page) of the host yetty.
#
# main.c owns the one-shot path + shared curl/file slurp; browser-ui.c is the
# ygui interactive client. ygui pulls in ydraw-core / ysdf / ycore / yface /
# yfont / yfigure / yplatform_core transitively; the engine (yetty_ybrowser)
# and curl stay explicit. The framework is libuv-free — we drive it from a
# plain select() loop, so no uv dependency is needed.

add_executable(ybrowser
    main.c
    browser-ui.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ybrowser PRIVATE
    ${YETTY_ROOT}/include
)

find_package(Threads REQUIRED)

target_link_libraries(ybrowser PRIVATE
    ygui
    yetty_ybrowser
    yetty_ydraw_core
    yetty_ysdf
    yetty_yface
    yetty_ycore
    CURL::libcurl
    Threads::Threads
)

target_compile_options(ybrowser PRIVATE -Wall -Wno-unused-parameter)
