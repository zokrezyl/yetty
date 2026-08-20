# ymsoffice — Microsoft Office (docx/xlsx/pptx) parsing + ydraw rendering
include(${YETTY_ROOT}/build-tools/yetty/libs/yxml.cmake)
include(${YETTY_ROOT}/build-tools/yetty/libs/zlib.cmake)

add_library(yetty_ymsoffice STATIC
    opc.c
    xml.c
    rels.c
    model.c
    msoffice.c
    docx.c
    xlsx.c
    pptx.c
    render.c
)

target_include_directories(yetty_ymsoffice PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ymsoffice
    PUBLIC
        yetty_ycore
        yetty_ydraw_list
        yetty_ysdf
    PRIVATE
        yxml
        ZLIB::ZLIB
)
