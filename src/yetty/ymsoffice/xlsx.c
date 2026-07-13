/*
 * xlsx.c - SpreadsheetML → neutral sheet model.
 *
 * Parts consumed:
 *   xl/workbook.xml            sheet names + relationship ids, in book order
 *   xl/_rels/workbook.xml.rels relationship id → worksheet part path
 *   xl/sharedStrings.xml       shared-string table (phonetic runs skipped)
 *   xl/worksheets/sheetN.xml   sheetData rows/cells
 *
 * Cell values are resolved to display text at parse time (shared strings,
 * booleans, inline strings); numbers keep their raw value text — number
 * formats are not evaluated. Formulas are kept verbatim alongside the
 * cached value.
 */

#include <yetty/ymsoffice/msoffice.h>

#include "model-internal.h"
#include "rels-internal.h"
#include "xml-internal.h"

#include <stdlib.h>
#include <string.h>

/* Backstop against pathological sheets — the renderer shows far less. */
#define XLSX_MAX_CELLS_PER_SHEET 65536

/*=============================================================================
 * workbook.xml — ordered sheet list
 *===========================================================================*/

struct xlsx_sheet_ref {
    char *name;
    char *relationship_id;
};

struct xlsx_workbook_ctx {
    struct xlsx_sheet_ref *refs;
    size_t count;
    size_t cap;
    char *pending_name;
    char *pending_relationship_id;
};

static int xlsx_workbook_attribute(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                                   const char *element_local, const char *attribute_name,
                                   const char *value)
{
    (void)walker;
    struct xlsx_workbook_ctx *ctx = ctx_ptr;
    if (strcmp(element_local, "sheet") != 0) {
        return 0;
    }
    const char *attribute_local = yetty_ymsoffice_xml_local_name(attribute_name);
    if (strcmp(attribute_local, "name") == 0) {
        free(ctx->pending_name);
        ctx->pending_name = strdup(value);
        return ctx->pending_name ? 0 : -1;
    }
    /* "r:id" — the local part is "id"; sheetId has local "sheetId" so the
     * two never collide. */
    if (strcmp(attribute_local, "id") == 0) {
        free(ctx->pending_relationship_id);
        ctx->pending_relationship_id = strdup(value);
        return ctx->pending_relationship_id ? 0 : -1;
    }
    return 0;
}

static int xlsx_workbook_element_end(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                                     const char *local_name)
{
    (void)walker;
    struct xlsx_workbook_ctx *ctx = ctx_ptr;
    if (strcmp(local_name, "sheet") != 0) {
        return 0;
    }
    if (ctx->pending_name && ctx->pending_relationship_id) {
        if (ctx->count == ctx->cap) {
            size_t new_cap = ctx->cap ? ctx->cap * 2 : 4;
            struct xlsx_sheet_ref *grown = realloc(ctx->refs, new_cap * sizeof(*grown));
            if (!grown) {
                return -1;
            }
            ctx->refs = grown;
            ctx->cap = new_cap;
        }
        ctx->refs[ctx->count].name = ctx->pending_name;
        ctx->refs[ctx->count].relationship_id = ctx->pending_relationship_id;
        ctx->count++;
        ctx->pending_name = NULL;
        ctx->pending_relationship_id = NULL;
        return 0;
    }
    free(ctx->pending_name);
    free(ctx->pending_relationship_id);
    ctx->pending_name = NULL;
    ctx->pending_relationship_id = NULL;
    return 0;
}

static void xlsx_workbook_ctx_free(struct xlsx_workbook_ctx *ctx)
{
    for (size_t i = 0; i < ctx->count; i++) {
        free(ctx->refs[i].name);
        free(ctx->refs[i].relationship_id);
    }
    free(ctx->refs);
    free(ctx->pending_name);
    free(ctx->pending_relationship_id);
    memset(ctx, 0, sizeof(*ctx));
}

/*=============================================================================
 * sharedStrings.xml
 *===========================================================================*/

struct xlsx_shared_strings_ctx {
    char **strings;
    size_t count;
    size_t cap;
    bool in_string_item;
    bool capture;
    size_t skip_until_depth; /* phonetic runs */
    struct yetty_ymsoffice_text_accum text;
};

static int xlsx_shared_element_start(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                                     const char *local_name)
{
    struct xlsx_shared_strings_ctx *ctx = ctx_ptr;
    if (ctx->skip_until_depth) {
        return 0;
    }
    if (strcmp(local_name, "rPh") == 0 || strcmp(local_name, "phoneticPr") == 0) {
        ctx->skip_until_depth = walker->depth;
        return 0;
    }
    if (strcmp(local_name, "si") == 0) {
        ctx->in_string_item = true;
        yetty_ymsoffice_text_accum_reset(&ctx->text);
        return 0;
    }
    if (strcmp(local_name, "t") == 0 && ctx->in_string_item) {
        ctx->capture = true;
    }
    return 0;
}

static int xlsx_shared_element_end(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                                   const char *local_name)
{
    struct xlsx_shared_strings_ctx *ctx = ctx_ptr;
    if (ctx->skip_until_depth) {
        if (walker->depth == ctx->skip_until_depth) {
            ctx->skip_until_depth = 0;
        }
        return 0;
    }
    if (strcmp(local_name, "t") == 0) {
        ctx->capture = false;
        return 0;
    }
    if (strcmp(local_name, "si") == 0 && ctx->in_string_item) {
        ctx->in_string_item = false;
        if (ctx->count == ctx->cap) {
            size_t new_cap = ctx->cap ? ctx->cap * 2 : 32;
            char **grown = realloc(ctx->strings, new_cap * sizeof(*grown));
            if (!grown) {
                return -1;
            }
            ctx->strings = grown;
            ctx->cap = new_cap;
        }
        ctx->strings[ctx->count] = strdup(yetty_ymsoffice_text_accum_cstr(&ctx->text));
        if (!ctx->strings[ctx->count]) {
            return -1;
        }
        ctx->count++;
    }
    return 0;
}

static int xlsx_shared_text(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                            const char *chunk, size_t chunk_len)
{
    (void)walker;
    struct xlsx_shared_strings_ctx *ctx = ctx_ptr;
    if (ctx->skip_until_depth || !ctx->capture) {
        return 0;
    }
    return yetty_ymsoffice_text_accum_append(&ctx->text, chunk, chunk_len);
}

static void xlsx_shared_ctx_free(struct xlsx_shared_strings_ctx *ctx)
{
    for (size_t i = 0; i < ctx->count; i++) {
        free(ctx->strings[i]);
    }
    free(ctx->strings);
    yetty_ymsoffice_text_accum_free(&ctx->text);
    memset(ctx, 0, sizeof(*ctx));
}

/*=============================================================================
 * worksheet part
 *===========================================================================*/

struct xlsx_sheet_ctx {
    struct yetty_ymsoffice_sheet *sheet;
    const struct xlsx_shared_strings_ctx *shared;

    uint32_t current_row; /* 0-based */
    uint32_t next_row;    /* sequential fallback */
    uint32_t pending_col; /* 0-based column of the open cell */
    uint32_t next_col;    /* sequential fallback within the row */
    bool in_cell;
    bool in_value;
    bool in_formula;
    bool in_inline_text;
    char cell_type[12];
    struct yetty_ymsoffice_text_accum value;
    struct yetty_ymsoffice_text_accum formula;
    const char *fail_msg;
};

struct xlsx_cell_address {
    uint32_t row;
    uint32_t col;
    bool valid;
};

/* "BC23" → col 54, row 22 (0-based). */
static struct xlsx_cell_address xlsx_parse_cell_ref(const char *ref)
{
    struct xlsx_cell_address address = {0};
    uint32_t col = 0;
    size_t i = 0;
    while (ref[i] >= 'A' && ref[i] <= 'Z') {
        col = col * 26u + (uint32_t)(ref[i] - 'A' + 1);
        i++;
    }
    if (i == 0 || ref[i] < '1' || ref[i] > '9') {
        return address;
    }
    uint32_t row = (uint32_t)strtoul(ref + i, NULL, 10);
    if (row == 0) {
        return address;
    }
    address.col = col - 1;
    address.row = row - 1;
    address.valid = true;
    return address;
}

static int xlsx_sheet_element_start(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                                    const char *local_name)
{
    struct xlsx_sheet_ctx *ctx = ctx_ptr;
    if (strcmp(local_name, "row") == 0 &&
        strcmp(yetty_ymsoffice_xml_ancestor(walker, 1), "sheetData") == 0) {
        ctx->current_row = ctx->next_row;
        ctx->next_row = ctx->current_row + 1;
        ctx->next_col = 0;
        return 0;
    }
    if (strcmp(local_name, "c") == 0 &&
        strcmp(yetty_ymsoffice_xml_ancestor(walker, 1), "row") == 0) {
        ctx->in_cell = true;
        ctx->pending_col = ctx->next_col;
        ctx->next_col = ctx->pending_col + 1;
        ctx->cell_type[0] = '\0';
        yetty_ymsoffice_text_accum_reset(&ctx->value);
        yetty_ymsoffice_text_accum_reset(&ctx->formula);
        return 0;
    }
    if (!ctx->in_cell) {
        return 0;
    }
    if (strcmp(local_name, "v") == 0) {
        ctx->in_value = true;
    } else if (strcmp(local_name, "f") == 0) {
        ctx->in_formula = true;
    } else if (strcmp(local_name, "t") == 0 && yetty_ymsoffice_xml_has_ancestor(walker, "is")) {
        ctx->in_inline_text = true;
    }
    return 0;
}

static int xlsx_sheet_attribute(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                                const char *element_local, const char *attribute_name,
                                const char *value)
{
    (void)walker;
    struct xlsx_sheet_ctx *ctx = ctx_ptr;
    if (strcmp(element_local, "row") == 0 && strcmp(attribute_name, "r") == 0) {
        uint32_t row = (uint32_t)strtoul(value, NULL, 10);
        if (row > 0) {
            ctx->current_row = row - 1;
            ctx->next_row = row;
        }
        return 0;
    }
    if (!ctx->in_cell || strcmp(element_local, "c") != 0) {
        return 0;
    }
    if (strcmp(attribute_name, "r") == 0) {
        struct xlsx_cell_address address = xlsx_parse_cell_ref(value);
        if (address.valid) {
            ctx->pending_col = address.col;
            ctx->next_col = address.col + 1;
        }
        return 0;
    }
    if (strcmp(attribute_name, "t") == 0) {
        size_t len = strlen(value);
        if (len >= sizeof(ctx->cell_type)) {
            len = sizeof(ctx->cell_type) - 1;
        }
        memcpy(ctx->cell_type, value, len);
        ctx->cell_type[len] = '\0';
    }
    return 0;
}

static int xlsx_sheet_text(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                           const char *chunk, size_t chunk_len)
{
    (void)walker;
    struct xlsx_sheet_ctx *ctx = ctx_ptr;
    if (!ctx->in_cell) {
        return 0;
    }
    if (ctx->in_value || ctx->in_inline_text) {
        return yetty_ymsoffice_text_accum_append(&ctx->value, chunk, chunk_len);
    }
    if (ctx->in_formula) {
        return yetty_ymsoffice_text_accum_append(&ctx->formula, chunk, chunk_len);
    }
    return 0;
}

static int xlsx_sheet_finish_cell(struct xlsx_sheet_ctx *ctx)
{
    ctx->in_cell = false;
    ctx->in_value = false;
    ctx->in_formula = false;
    ctx->in_inline_text = false;

    if (ctx->sheet->cell_count >= XLSX_MAX_CELLS_PER_SHEET) {
        return 0;
    }

    const char *type = ctx->cell_type;
    char *text = NULL;
    bool is_number = false;

    if (strcmp(type, "s") == 0) {
        size_t index = (size_t)strtoul(yetty_ymsoffice_text_accum_cstr(&ctx->value), NULL, 10);
        text = strdup(index < ctx->shared->count ? ctx->shared->strings[index] : "");
    } else if (strcmp(type, "b") == 0) {
        text = strdup(strcmp(yetty_ymsoffice_text_accum_cstr(&ctx->value), "0") == 0 ? "FALSE"
                                                                                     : "TRUE");
    } else {
        text = strdup(yetty_ymsoffice_text_accum_cstr(&ctx->value));
        is_number = (type[0] == '\0' || strcmp(type, "n") == 0);
    }
    if (!text) {
        ctx->fail_msg = "xlsx: out of memory (cell text)";
        return -1;
    }

    size_t text_len = strlen(text);
    if (text_len == 0 && ctx->formula.len == 0) {
        free(text);
        return 0; /* nothing to show */
    }

    struct yetty_ymsoffice_sheet_cell cell = {
        .row = ctx->current_row,
        .col = ctx->pending_col,
        .text = text,
        .text_len = text_len,
        .is_number = is_number && text_len > 0,
    };
    if (ctx->formula.len > 0) {
        cell.formula = strdup(yetty_ymsoffice_text_accum_cstr(&ctx->formula));
        if (!cell.formula) {
            free(text);
            ctx->fail_msg = "xlsx: out of memory (cell formula)";
            return -1;
        }
    }
    if (yetty_ymsoffice_sheet_push_cell(ctx->sheet, cell) < 0) {
        free(cell.text);
        free(cell.formula);
        ctx->fail_msg = "xlsx: out of memory (cell push)";
        return -1;
    }
    return 0;
}

static int xlsx_sheet_element_end(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                                  const char *local_name)
{
    (void)walker;
    struct xlsx_sheet_ctx *ctx = ctx_ptr;
    if (!ctx->in_cell) {
        return 0;
    }
    if (strcmp(local_name, "v") == 0) {
        ctx->in_value = false;
    } else if (strcmp(local_name, "f") == 0) {
        ctx->in_formula = false;
    } else if (strcmp(local_name, "t") == 0) {
        ctx->in_inline_text = false;
    } else if (strcmp(local_name, "c") == 0) {
        return xlsx_sheet_finish_cell(ctx);
    }
    return 0;
}

/*=============================================================================
 * Entry point
 *===========================================================================*/

/* Resolve a workbook-relative target ("worksheets/sheet1.xml" or
 * "/xl/worksheets/sheet1.xml") to a part name. Returns malloc'd string. */
static char *xlsx_resolve_target(const char *target)
{
    if (target[0] == '/') {
        return strdup(target + 1);
    }
    size_t len = strlen(target);
    char *resolved = malloc(len + 4);
    if (!resolved) {
        return NULL;
    }
    memcpy(resolved, "xl/", 3);
    memcpy(resolved + 3, target, len + 1);
    return resolved;
}

struct yetty_ymsoffice_document_ptr_result yetty_ymsoffice_xlsx_parse(
    const struct yetty_ymsoffice_opc *opc)
{
    struct yetty_ymsoffice_opc_part_result workbook_res =
        yetty_ymsoffice_opc_read(opc, "xl/workbook.xml");
    YETTY_RETURN_IF_ERR(yetty_ymsoffice_document_ptr, workbook_res,
                        "xlsx: xl/workbook.xml missing");

    struct xlsx_workbook_ctx workbook = {0};
    {
        struct yetty_ymsoffice_xml_callbacks callbacks = {
            .attribute = xlsx_workbook_attribute,
            .element_end = xlsx_workbook_element_end,
        };
        int rc = yetty_ymsoffice_xml_walk(workbook_res.value.data, workbook_res.value.size,
                                          &callbacks, &workbook);
        yetty_ymsoffice_opc_part_destroy(&workbook_res.value);
        if (rc < 0) {
            xlsx_workbook_ctx_free(&workbook);
            return YETTY_ERR(yetty_ymsoffice_document_ptr, "xlsx: workbook.xml parse failed");
        }
    }

    struct yetty_ymsoffice_rels rels = {0};
    struct yetty_ymsoffice_opc_part_result rels_res =
        yetty_ymsoffice_opc_read(opc, "xl/_rels/workbook.xml.rels");
    if (!YETTY_IS_ERR(rels_res)) {
        yetty_ymsoffice_rels_parse(rels_res.value.data, rels_res.value.size, &rels);
        yetty_ymsoffice_opc_part_destroy(&rels_res.value);
    } else {
        yetty_ycore_error_destroy(rels_res.error);
    }

    struct xlsx_shared_strings_ctx shared = {0};
    struct yetty_ymsoffice_opc_part_result shared_res =
        yetty_ymsoffice_opc_read(opc, "xl/sharedStrings.xml");
    if (!YETTY_IS_ERR(shared_res)) {
        struct yetty_ymsoffice_xml_callbacks callbacks = {
            .element_start = xlsx_shared_element_start,
            .element_end = xlsx_shared_element_end,
            .text = xlsx_shared_text,
        };
        yetty_ymsoffice_xml_walk(shared_res.value.data, shared_res.value.size, &callbacks, &shared);
        yetty_ymsoffice_opc_part_destroy(&shared_res.value);
    } else {
        yetty_ycore_error_destroy(shared_res.error);
    }

    struct yetty_ymsoffice_document_ptr_result document_res =
        yetty_ymsoffice_document_create(YETTY_YMSOFFICE_KIND_SHEET);
    if (YETTY_IS_ERR(document_res)) {
        xlsx_workbook_ctx_free(&workbook);
        yetty_ymsoffice_rels_free(&rels);
        xlsx_shared_ctx_free(&shared);
        return YETTY_ERR(yetty_ymsoffice_document_ptr, "xlsx: document alloc failed", document_res);
    }
    struct yetty_ymsoffice_document *document = document_res.value;

    const char *fail_msg = NULL;
    for (size_t i = 0; i < workbook.count && !fail_msg; i++) {
        const char *target = yetty_ymsoffice_rels_target(&rels, workbook.refs[i].relationship_id);
        if (!target) {
            continue;
        }
        char *part_name = xlsx_resolve_target(target);
        if (!part_name) {
            fail_msg = "xlsx: out of memory (part name)";
            break;
        }
        struct yetty_ymsoffice_opc_part_result sheet_res = yetty_ymsoffice_opc_read(opc, part_name);
        free(part_name);
        if (YETTY_IS_ERR(sheet_res)) {
            yetty_ycore_error_destroy(sheet_res.error);
            continue;
        }

        char *sheet_name = strdup(workbook.refs[i].name);
        if (!sheet_name ||
            yetty_ymsoffice_sheet_document_push_sheet(&document->sheet, sheet_name) < 0) {
            free(sheet_name);
            yetty_ymsoffice_opc_part_destroy(&sheet_res.value);
            fail_msg = "xlsx: out of memory (sheet)";
            break;
        }

        struct xlsx_sheet_ctx sheet_ctx = {0};
        sheet_ctx.sheet = &document->sheet.sheets[document->sheet.sheet_count - 1];
        sheet_ctx.shared = &shared;
        struct yetty_ymsoffice_xml_callbacks callbacks = {
            .element_start = xlsx_sheet_element_start,
            .element_end = xlsx_sheet_element_end,
            .attribute = xlsx_sheet_attribute,
            .text = xlsx_sheet_text,
        };
        int rc = yetty_ymsoffice_xml_walk(sheet_res.value.data, sheet_res.value.size, &callbacks,
                                          &sheet_ctx);
        yetty_ymsoffice_text_accum_free(&sheet_ctx.value);
        yetty_ymsoffice_text_accum_free(&sheet_ctx.formula);
        yetty_ymsoffice_opc_part_destroy(&sheet_res.value);
        if (rc < 0) {
            fail_msg = sheet_ctx.fail_msg ? sheet_ctx.fail_msg : "xlsx: worksheet parse failed";
        }
    }

    xlsx_workbook_ctx_free(&workbook);
    yetty_ymsoffice_rels_free(&rels);
    xlsx_shared_ctx_free(&shared);

    if (fail_msg) {
        yetty_ymsoffice_document_destroy(document);
        return YETTY_ERR(yetty_ymsoffice_document_ptr, fail_msg);
    }
    return YETTY_OK(yetty_ymsoffice_document_ptr, document);
}
