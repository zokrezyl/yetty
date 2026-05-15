# yrich - document-centric rich content (spreadsheet, slides, ydoc)
add_library(yetty_yrich STATIC
    yrich-element.c
    yrich-selection.c
    yrich-operation.c
    yrich-command.c
    yrich-document.c
    yrich-yaml.c
    yspreadsheet.c
    yslides.c
    ydoc.c
)

target_include_directories(yetty_yrich PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yrich PUBLIC
    yetty_ycore
    yetty_ydraw_core
    yetty_ysdf
    yaml
)
