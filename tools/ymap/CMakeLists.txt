# ymap tool — the slippy-map frontend over the `ymap:map` yclass model.
# One-shot: emits the map as a YDRAW_BIN DCS envelope (scrolls with the
# text, ycat-style) + the provider's attribution line. Interactive (-i):
# ships renders to a yview figure and drives pan / cursor-anchored zoom.

add_executable(ymap
    main.c
    interactive.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ymap PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(ymap PRIVATE
    yetty_ymap
    yetty_yview
    Threads::Threads
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(ymap PRIVATE -Wall -Wextra)
endif()
