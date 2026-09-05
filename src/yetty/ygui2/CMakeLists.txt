# ygui2 — the drawable-contract widget toolkit (strategy.md). Pure PTY
# producer: renders exclusively through DCS drawable envelopes; no figure
# container, no RPC, no GPU dependency. yclass module — the annotated
# sources below are the single source of truth; everything else is codegen.
set(YGUI2_ANNOTATED
    ${CMAKE_CURRENT_SOURCE_DIR}/widget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/framework.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/label.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/panel.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/button.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/checkbox.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/scrollarea.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/complex_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/plot.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/progress.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/table.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/ydraw_embed.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/separator.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/toggle.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/radio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/slider.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/spinner.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/chip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/statusbar.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/stepper.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/textinput.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/tooltip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/popup_menu.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/dropdown.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/dialog.c
)

file(GLOB YGUI2_API_SOURCES
    ${YETTY_ROOT}/src/yetty/gen/api/ygui2/*.c
    ${YETTY_ROOT}/src/yetty/gen/api/ygui2/widgets/*.c
)
add_library(yetty_api_ygui2 STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/api-stub.c
    ${YGUI2_API_SOURCES})
target_include_directories(yetty_api_ygui2 PUBLIC ${YETTY_ROOT}/include)
target_link_libraries(yetty_api_ygui2 PUBLIC yetty_ycore yetty_yclass)

add_library(yetty_ygui2 STATIC ${YGUI2_ANNOTATED})
target_include_directories(yetty_ygui2 PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/yetty
)
target_link_libraries(yetty_ygui2 PUBLIC
    yetty_api_ygui2
    yetty_ysdf
    m
    yetty_ycore
    yetty_yclass
    yetty_yplatform_core # io_write_all — the terminal write path
    yetty_ydraw_list
    yetty_yface
    yetty_api_yplot # plot widget: wraps an api_yplot figure + pack
)

yetty_yclass_module(ygui2)
