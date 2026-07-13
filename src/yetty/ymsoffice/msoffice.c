/*
 * msoffice.c - container-kind sniff + one-step parse dispatch.
 */

#include <yetty/ymsoffice/msoffice.h>

enum yetty_ymsoffice_kind yetty_ymsoffice_opc_kind(const struct yetty_ymsoffice_opc *opc)
{
    if (!opc) {
        return YETTY_YMSOFFICE_KIND_UNKNOWN;
    }
    if (yetty_ymsoffice_opc_find(opc, "word/document.xml")) {
        return YETTY_YMSOFFICE_KIND_WORD;
    }
    if (yetty_ymsoffice_opc_find(opc, "xl/workbook.xml")) {
        return YETTY_YMSOFFICE_KIND_SHEET;
    }
    if (yetty_ymsoffice_opc_find(opc, "ppt/presentation.xml")) {
        return YETTY_YMSOFFICE_KIND_SLIDES;
    }
    return YETTY_YMSOFFICE_KIND_UNKNOWN;
}

struct yetty_ymsoffice_document_ptr_result yetty_ymsoffice_parse(const uint8_t *bytes, size_t len)
{
    struct yetty_ymsoffice_opc_ptr_result opc_res = yetty_ymsoffice_opc_open(bytes, len);
    YETTY_RETURN_IF_ERR(yetty_ymsoffice_document_ptr, opc_res, "ymsoffice: not a ZIP container");
    struct yetty_ymsoffice_opc *opc = opc_res.value;

    struct yetty_ymsoffice_document_ptr_result document_res;
    switch (yetty_ymsoffice_opc_kind(opc)) {
    case YETTY_YMSOFFICE_KIND_WORD:
        document_res = yetty_ymsoffice_docx_parse(opc);
        break;
    case YETTY_YMSOFFICE_KIND_SHEET:
        document_res = yetty_ymsoffice_xlsx_parse(opc);
        break;
    case YETTY_YMSOFFICE_KIND_SLIDES:
        document_res = yetty_ymsoffice_pptx_parse(opc);
        break;
    case YETTY_YMSOFFICE_KIND_UNKNOWN:
    default:
        document_res = YETTY_ERR(yetty_ymsoffice_document_ptr,
                                 "ymsoffice: ZIP is not a docx/xlsx/pptx package");
        break;
    }

    yetty_ymsoffice_opc_destroy(opc);
    return document_res;
}
