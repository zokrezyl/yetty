#ifndef YETTY_YMSOFFICE_MSOFFICE_H
#define YETTY_YMSOFFICE_MSOFFICE_H

/*
 * msoffice.h - parse Microsoft Office OOXML documents (docx / xlsx / pptx)
 * into the neutral ymsoffice model.
 *
 * The parsers extract document structure and character-level styling from
 * the ZIP+XML container (see model.h for exactly what is kept). Typical use:
 *
 *   struct yetty_ymsoffice_document_ptr_result parse_res =
 *       yetty_ymsoffice_parse(bytes, len);
 *   ...
 *   yetty_ymsoffice_document_destroy(parse_res.value);
 *
 * yetty_ymsoffice_parse() sniffs the container kind from its parts; the
 * per-format entry points skip the sniff when the caller already knows.
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ymsoffice/model.h>
#include <yetty/ymsoffice/opc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Identify the document kind from the container's part names
 * (word/document.xml, xl/workbook.xml, ppt/presentation.xml). */
enum yetty_ymsoffice_kind yetty_ymsoffice_opc_kind(const struct yetty_ymsoffice_opc *opc);

/* Sniff + parse in one step. `bytes` only needs to stay alive for the call. */
struct yetty_ymsoffice_document_ptr_result yetty_ymsoffice_parse(const uint8_t *bytes, size_t len);

/* Per-format parsers over an already-open container. */
struct yetty_ymsoffice_document_ptr_result yetty_ymsoffice_docx_parse(
    const struct yetty_ymsoffice_opc *opc);
struct yetty_ymsoffice_document_ptr_result yetty_ymsoffice_xlsx_parse(
    const struct yetty_ymsoffice_opc *opc);
struct yetty_ymsoffice_document_ptr_result yetty_ymsoffice_pptx_parse(
    const struct yetty_ymsoffice_opc *opc);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMSOFFICE_MSOFFICE_H */
