/*
 * docx.c - WordprocessingML (word/document.xml) → neutral word model.
 *
 * SAX walk over the main document part with two optional side tables parsed
 * first:
 *   - word/styles.xml    → paragraph styleId → heading level (style *names*
 *                          matching "heading N" / "Title")
 *   - word/numbering.xml → numId + indent level → ordered vs bullet
 *
 * Extracted structure: paragraphs (heading level, alignment, list info) with
 * styled runs (bold/italic/underline/strike, size, color, highlight,
 * hyperlink), tables (rows/cells with gridSpan + vMerge), and images as
 * named placeholders with their declared extent. Deleted revisions (w:del)
 * and mc:Fallback duplicates are skipped; drawings/text boxes are not
 * descended into beyond the image placeholder.
 */

#include <yetty/ymsoffice/msoffice.h>

#include "model-internal.h"
#include "xml-internal.h"

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

/* 914400 EMU per inch, 72 pt per inch → 12700 EMU per point. */
#define DOCX_EMU_PER_POINT 12700.0f

#define DOCX_MAX_LIST_LEVELS 9

/*=============================================================================
 * Small helpers
 *===========================================================================*/

/* OOXML boolean attribute: absent/other = true, explicit off values = false. */
static int docx_bool_value(const char *value)
{
    return !(strcmp(value, "false") == 0 || strcmp(value, "0") == 0 || strcmp(value, "none") == 0 ||
             strcmp(value, "off") == 0);
}

/* Parse "RRGGBB". Returns 0 / -1. */
static int docx_parse_hex_color(const char *value, uint32_t *out_rgb)
{
    if (strlen(value) != 6) {
        return -1;
    }
    uint32_t rgb = 0;
    for (int i = 0; i < 6; i++) {
        char c = value[i];
        uint32_t digit;
        if (c >= '0' && c <= '9') {
            digit = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = (uint32_t)(c - 'A' + 10);
        } else {
            return -1;
        }
        rgb = (rgb << 4) | digit;
    }
    *out_rgb = rgb;
    return 0;
}

/* w:highlight named colors (ST_HighlightColor). */
static int docx_highlight_color(const char *name, uint32_t *out_rgb)
{
    static const struct {
        const char *name;
        uint32_t rgb;
    } table[] = {
        {"yellow", 0xFFFF00u},      {"green", 0x00FF00u},     {"cyan", 0x00FFFFu},
        {"magenta", 0xFF00FFu},     {"blue", 0x0000FFu},      {"red", 0xFF0000u},
        {"darkBlue", 0x00008Bu},    {"darkCyan", 0x008B8Bu},  {"darkGreen", 0x006400u},
        {"darkMagenta", 0x8B008Bu}, {"darkRed", 0x8B0000u},   {"darkYellow", 0x808000u},
        {"darkGray", 0xA9A9A9u},    {"lightGray", 0xD3D3D3u}, {"black", 0x000000u},
        {"white", 0xFFFFFFu},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(table[i].name, name) == 0) {
            *out_rgb = table[i].rgb;
            return 0;
        }
    }
    return -1;
}

/* "heading 1" / "Heading2" / "Title" style names → level, 0 = not a heading. */
static int docx_heading_level_from_name(const char *style_name)
{
    if (strcasecmp(style_name, "title") == 0) {
        return 1;
    }
    if (strncasecmp(style_name, "heading", 7) != 0) {
        return 0;
    }
    const char *rest = style_name + 7;
    while (*rest == ' ') {
        rest++;
    }
    int level = atoi(rest);
    if (level < 1) {
        level = 1;
    }
    if (level > 6) {
        level = 6;
    }
    return level;
}

/*=============================================================================
 * styles.xml → styleId → heading level
 *===========================================================================*/

struct docx_style {
    char *style_id;
    int heading_level;
};

struct docx_styles {
    struct docx_style *items;
    size_t count;
    size_t cap;
    /* in-flight state while parsing */
    char *pending_style_id;
    int pending_heading_level;
};

static int docx_styles_element_start(void *ctx, const struct yetty_ymsoffice_xml_walker *walker,
                                     const char *local_name)
{
    (void)walker;
    struct docx_styles *styles = ctx;
    if (strcmp(local_name, "style") == 0) {
        free(styles->pending_style_id);
        styles->pending_style_id = NULL;
        styles->pending_heading_level = 0;
    }
    return 0;
}

static int docx_styles_attribute(void *ctx, const struct yetty_ymsoffice_xml_walker *walker,
                                 const char *element_local, const char *attribute_name,
                                 const char *value)
{
    struct docx_styles *styles = ctx;
    const char *attribute_local = yetty_ymsoffice_xml_local_name(attribute_name);

    if (strcmp(element_local, "style") == 0 && strcmp(attribute_local, "styleId") == 0) {
        free(styles->pending_style_id);
        styles->pending_style_id = strdup(value);
        if (!styles->pending_style_id) {
            return -1;
        }
        return 0;
    }
    if (strcmp(element_local, "name") == 0 && strcmp(attribute_local, "val") == 0 &&
        strcmp(yetty_ymsoffice_xml_ancestor(walker, 1), "style") == 0) {
        styles->pending_heading_level = docx_heading_level_from_name(value);
    }
    return 0;
}

static int docx_styles_element_end(void *ctx, const struct yetty_ymsoffice_xml_walker *walker,
                                   const char *local_name)
{
    (void)walker;
    struct docx_styles *styles = ctx;
    if (strcmp(local_name, "style") != 0) {
        return 0;
    }
    if (styles->pending_style_id && styles->pending_heading_level > 0) {
        if (styles->count == styles->cap) {
            size_t new_cap = styles->cap ? styles->cap * 2 : 8;
            struct docx_style *grown = realloc(styles->items, new_cap * sizeof(*grown));
            if (!grown) {
                return -1;
            }
            styles->items = grown;
            styles->cap = new_cap;
        }
        styles->items[styles->count].style_id = styles->pending_style_id;
        styles->items[styles->count].heading_level = styles->pending_heading_level;
        styles->count++;
        styles->pending_style_id = NULL;
    }
    free(styles->pending_style_id);
    styles->pending_style_id = NULL;
    styles->pending_heading_level = 0;
    return 0;
}

static void docx_styles_free(struct docx_styles *styles)
{
    for (size_t i = 0; i < styles->count; i++) {
        free(styles->items[i].style_id);
    }
    free(styles->items);
    free(styles->pending_style_id);
    memset(styles, 0, sizeof(*styles));
}

static int docx_styles_heading_level(const struct docx_styles *styles, const char *style_id)
{
    for (size_t i = 0; i < styles->count; i++) {
        if (strcmp(styles->items[i].style_id, style_id) == 0) {
            return styles->items[i].heading_level;
        }
    }
    /* No styles.xml (or unknown id): many producers name the id after the
     * style ("Heading1"), so fall back to the id itself. */
    return docx_heading_level_from_name(style_id);
}

/*=============================================================================
 * numbering.xml → numId + level → ordered flag
 *===========================================================================*/

struct docx_abstract_num {
    char *abstract_id;
    bool ordered[DOCX_MAX_LIST_LEVELS];
};

struct docx_num_link {
    char *num_id;
    char *abstract_id;
};

struct docx_numbering {
    struct docx_abstract_num *abstracts;
    size_t abstract_count;
    size_t abstract_cap;
    struct docx_num_link *links;
    size_t link_count;
    size_t link_cap;
    /* in-flight parse state */
    int current_level;
    char *current_num_id;
};

static int docx_numbering_attribute(void *ctx, const struct yetty_ymsoffice_xml_walker *walker,
                                    const char *element_local, const char *attribute_name,
                                    const char *value)
{
    struct docx_numbering *numbering = ctx;
    const char *attribute_local = yetty_ymsoffice_xml_local_name(attribute_name);
    if (strcmp(attribute_local, "val") != 0 && strcmp(attribute_local, "abstractNumId") != 0 &&
        strcmp(attribute_local, "numId") != 0 && strcmp(attribute_local, "ilvl") != 0) {
        return 0;
    }

    if (strcmp(element_local, "abstractNum") == 0 &&
        strcmp(attribute_local, "abstractNumId") == 0) {
        if (numbering->abstract_count == numbering->abstract_cap) {
            size_t new_cap = numbering->abstract_cap ? numbering->abstract_cap * 2 : 4;
            struct docx_abstract_num *grown =
                realloc(numbering->abstracts, new_cap * sizeof(*grown));
            if (!grown) {
                return -1;
            }
            numbering->abstracts = grown;
            numbering->abstract_cap = new_cap;
        }
        struct docx_abstract_num *abstract = &numbering->abstracts[numbering->abstract_count];
        memset(abstract, 0, sizeof(*abstract));
        abstract->abstract_id = strdup(value);
        if (!abstract->abstract_id) {
            return -1;
        }
        numbering->abstract_count++;
        numbering->current_level = -1;
        return 0;
    }

    if (strcmp(element_local, "lvl") == 0 && strcmp(attribute_local, "ilvl") == 0 &&
        yetty_ymsoffice_xml_has_ancestor(walker, "abstractNum")) {
        numbering->current_level = atoi(value);
        return 0;
    }

    if (strcmp(element_local, "numFmt") == 0 && strcmp(attribute_local, "val") == 0 &&
        numbering->abstract_count > 0 && numbering->current_level >= 0 &&
        numbering->current_level < DOCX_MAX_LIST_LEVELS &&
        yetty_ymsoffice_xml_has_ancestor(walker, "abstractNum")) {
        struct docx_abstract_num *abstract = &numbering->abstracts[numbering->abstract_count - 1];
        abstract->ordered[numbering->current_level] = strcmp(value, "bullet") != 0;
        return 0;
    }

    if (strcmp(element_local, "num") == 0 && strcmp(attribute_local, "numId") == 0) {
        free(numbering->current_num_id);
        numbering->current_num_id = strdup(value);
        if (!numbering->current_num_id) {
            return -1;
        }
        return 0;
    }

    if (strcmp(element_local, "abstractNumId") == 0 && strcmp(attribute_local, "val") == 0 &&
        numbering->current_num_id) {
        if (numbering->link_count == numbering->link_cap) {
            size_t new_cap = numbering->link_cap ? numbering->link_cap * 2 : 4;
            struct docx_num_link *grown = realloc(numbering->links, new_cap * sizeof(*grown));
            if (!grown) {
                return -1;
            }
            numbering->links = grown;
            numbering->link_cap = new_cap;
        }
        struct docx_num_link *link = &numbering->links[numbering->link_count];
        link->num_id = numbering->current_num_id;
        numbering->current_num_id = NULL;
        link->abstract_id = strdup(value);
        if (!link->abstract_id) {
            free(link->num_id);
            return -1;
        }
        numbering->link_count++;
        return 0;
    }

    return 0;
}

static void docx_numbering_free(struct docx_numbering *numbering)
{
    for (size_t i = 0; i < numbering->abstract_count; i++) {
        free(numbering->abstracts[i].abstract_id);
    }
    free(numbering->abstracts);
    for (size_t i = 0; i < numbering->link_count; i++) {
        free(numbering->links[i].num_id);
        free(numbering->links[i].abstract_id);
    }
    free(numbering->links);
    free(numbering->current_num_id);
    memset(numbering, 0, sizeof(*numbering));
}

static bool docx_numbering_is_ordered(const struct docx_numbering *numbering, const char *num_id,
                                      int level)
{
    if (level < 0 || level >= DOCX_MAX_LIST_LEVELS) {
        return false;
    }
    for (size_t i = 0; i < numbering->link_count; i++) {
        if (strcmp(numbering->links[i].num_id, num_id) != 0) {
            continue;
        }
        for (size_t a = 0; a < numbering->abstract_count; a++) {
            if (strcmp(numbering->abstracts[a].abstract_id, numbering->links[i].abstract_id) == 0) {
                return numbering->abstracts[a].ordered[level];
            }
        }
    }
    return false;
}

/*=============================================================================
 * document.xml walk
 *===========================================================================*/

struct docx_ctx {
    struct yetty_ymsoffice_word_document *word;
    const struct docx_styles *styles;
    const struct docx_numbering *numbering;

    /* paragraph under construction */
    struct yetty_ymsoffice_paragraph paragraph;
    bool in_paragraph;
    char pending_num_id[24];
    int pending_list_level;
    bool has_numbering;

    /* run under construction */
    struct yetty_ymsoffice_text_run run;
    char *run_text;
    size_t run_text_len;
    size_t run_text_cap;
    bool in_run;
    bool capture_text;

    int hyperlink_depth;

    /* subtree skipping (mc:Fallback, w:del, w:sectPr) */
    size_t skip_until_depth; /* 0 = not skipping */

    /* image placeholder capture (w:drawing / w:pict / w:object) */
    bool in_drawing;
    size_t drawing_depth;
    char *image_name;
    float image_width_pt;
    float image_height_pt;

    /* table capture — one level deep; nested tables flatten into the cell */
    struct yetty_ymsoffice_table table;
    bool in_table;
    int nested_table_depth;
    bool in_row;
    bool in_cell;

    const char *fail_msg; /* set on abort */
};

static int docx_fail(struct docx_ctx *ctx, const char *msg)
{
    if (!ctx->fail_msg) {
        ctx->fail_msg = msg;
    }
    return -1;
}

static int docx_run_text_append(struct docx_ctx *ctx, const char *chunk, size_t chunk_len)
{
    if (ctx->run_text_len + chunk_len + 1 > ctx->run_text_cap) {
        size_t new_cap = ctx->run_text_cap ? ctx->run_text_cap * 2 : 64;
        while (new_cap < ctx->run_text_len + chunk_len + 1) {
            new_cap *= 2;
        }
        char *grown = realloc(ctx->run_text, new_cap);
        if (!grown) {
            return docx_fail(ctx, "docx: out of memory (run text)");
        }
        ctx->run_text = grown;
        ctx->run_text_cap = new_cap;
    }
    memcpy(ctx->run_text + ctx->run_text_len, chunk, chunk_len);
    ctx->run_text_len += chunk_len;
    ctx->run_text[ctx->run_text_len] = '\0';
    return 0;
}

static struct yetty_ymsoffice_table_row *docx_current_row(struct docx_ctx *ctx)
{
    if (!ctx->in_table || ctx->table.row_count == 0) {
        return NULL;
    }
    return &ctx->table.rows[ctx->table.row_count - 1];
}

static struct yetty_ymsoffice_table_cell *docx_current_cell(struct docx_ctx *ctx)
{
    struct yetty_ymsoffice_table_row *row = docx_current_row(ctx);
    if (!row || !ctx->in_cell || row->cell_count == 0) {
        return NULL;
    }
    return &row->cells[row->cell_count - 1];
}

static int docx_begin_paragraph(struct docx_ctx *ctx)
{
    memset(&ctx->paragraph, 0, sizeof(ctx->paragraph));
    ctx->paragraph.list_level = -1;
    ctx->in_paragraph = true;
    ctx->pending_num_id[0] = '\0';
    ctx->pending_list_level = 0;
    ctx->has_numbering = false;
    return 0;
}

static int docx_finish_paragraph(struct docx_ctx *ctx)
{
    if (!ctx->in_paragraph) {
        return 0;
    }
    ctx->in_paragraph = false;

    if (ctx->has_numbering && ctx->paragraph.heading_level == 0) {
        ctx->paragraph.list_level = ctx->pending_list_level;
        ctx->paragraph.list_ordered =
            docx_numbering_is_ordered(ctx->numbering, ctx->pending_num_id, ctx->pending_list_level);
    }

    struct yetty_ymsoffice_table_cell *cell = docx_current_cell(ctx);
    int rc;
    if (cell) {
        rc = yetty_ymsoffice_table_cell_push_paragraph(cell, ctx->paragraph);
    } else {
        rc = yetty_ymsoffice_word_push_paragraph(ctx->word, ctx->paragraph);
    }
    if (rc < 0) {
        yetty_ymsoffice_paragraph_free(&ctx->paragraph);
        return docx_fail(ctx, "docx: out of memory (paragraph)");
    }
    memset(&ctx->paragraph, 0, sizeof(ctx->paragraph));
    return 0;
}

static int docx_finish_run(struct docx_ctx *ctx)
{
    if (!ctx->in_run) {
        return 0;
    }
    ctx->in_run = false;
    ctx->capture_text = false;
    if (!ctx->in_paragraph || ctx->run_text_len == 0) {
        return 0;
    }
    char *text = malloc(ctx->run_text_len + 1);
    if (!text) {
        return docx_fail(ctx, "docx: out of memory (run copy)");
    }
    memcpy(text, ctx->run_text, ctx->run_text_len + 1);
    ctx->run.text = text;
    ctx->run.text_len = ctx->run_text_len;
    if (yetty_ymsoffice_paragraph_push_run(&ctx->paragraph, ctx->run) < 0) {
        free(text);
        return docx_fail(ctx, "docx: out of memory (run push)");
    }
    memset(&ctx->run, 0, sizeof(ctx->run));
    return 0;
}

static int docx_element_start(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                              const char *local_name)
{
    struct docx_ctx *ctx = ctx_ptr;

    if (ctx->skip_until_depth) {
        return 0;
    }
    if (ctx->in_drawing) {
        return 0; /* only attributes (docPr/extent) matter inside drawings */
    }

    /* Skipped subtrees: fallback duplicates, deleted revisions, section
     * properties. */
    if (strcmp(local_name, "Fallback") == 0 || strcmp(local_name, "del") == 0 ||
        strcmp(local_name, "sectPr") == 0) {
        ctx->skip_until_depth = walker->depth;
        return 0;
    }

    if (strcmp(local_name, "drawing") == 0 || strcmp(local_name, "pict") == 0 ||
        strcmp(local_name, "object") == 0) {
        ctx->in_drawing = true;
        ctx->drawing_depth = walker->depth;
        free(ctx->image_name);
        ctx->image_name = NULL;
        ctx->image_width_pt = 0.0f;
        ctx->image_height_pt = 0.0f;
        return 0;
    }

    if (strcmp(local_name, "p") == 0) {
        return docx_begin_paragraph(ctx);
    }
    if (strcmp(local_name, "r") == 0 && ctx->in_paragraph) {
        memset(&ctx->run, 0, sizeof(ctx->run));
        ctx->run_text_len = 0;
        if (ctx->hyperlink_depth > 0) {
            ctx->run.hyperlink = true;
            ctx->run.underline = true;
        }
        ctx->in_run = true;
        return 0;
    }
    if (strcmp(local_name, "t") == 0 && ctx->in_run) {
        ctx->capture_text = true;
        return 0;
    }
    if (strcmp(local_name, "tab") == 0 && ctx->in_run) {
        return docx_run_text_append(ctx, "    ", 4);
    }
    if ((strcmp(local_name, "br") == 0 || strcmp(local_name, "cr") == 0) && ctx->in_run) {
        return docx_run_text_append(ctx, "\n", 1);
    }
    if (strcmp(local_name, "hyperlink") == 0) {
        ctx->hyperlink_depth++;
        return 0;
    }

    /* Run property flags — presence turns them on, a val attribute may turn
     * them back off (docx_attribute below). */
    if (ctx->in_run && strcmp(yetty_ymsoffice_xml_ancestor(walker, 1), "rPr") == 0) {
        if (strcmp(local_name, "b") == 0 || strcmp(local_name, "bCs") == 0) {
            ctx->run.bold = true;
        } else if (strcmp(local_name, "i") == 0 || strcmp(local_name, "iCs") == 0) {
            ctx->run.italic = true;
        } else if (strcmp(local_name, "u") == 0) {
            ctx->run.underline = true;
        } else if (strcmp(local_name, "strike") == 0) {
            ctx->run.strike = true;
        }
        return 0;
    }

    if (strcmp(local_name, "tbl") == 0) {
        if (ctx->in_table) {
            ctx->nested_table_depth++; /* flatten nested tables into the cell */
        } else {
            memset(&ctx->table, 0, sizeof(ctx->table));
            ctx->in_table = true;
        }
        return 0;
    }
    if (strcmp(local_name, "tr") == 0 && ctx->in_table && ctx->nested_table_depth == 0) {
        if (yetty_ymsoffice_table_push_row(&ctx->table) < 0) {
            return docx_fail(ctx, "docx: out of memory (table row)");
        }
        ctx->in_row = true;
        return 0;
    }
    if (strcmp(local_name, "tc") == 0 && ctx->in_row && ctx->nested_table_depth == 0) {
        struct yetty_ymsoffice_table_row *row = docx_current_row(ctx);
        if (!row || yetty_ymsoffice_table_row_push_cell(row) < 0) {
            return docx_fail(ctx, "docx: out of memory (table cell)");
        }
        ctx->in_cell = true;
        return 0;
    }
    if (strcmp(local_name, "vMerge") == 0 && ctx->in_cell &&
        strcmp(yetty_ymsoffice_xml_ancestor(walker, 1), "tcPr") == 0) {
        struct yetty_ymsoffice_table_cell *cell = docx_current_cell(ctx);
        if (cell) {
            cell->merged_continue = true; /* val="restart" clears it below */
        }
        return 0;
    }

    return 0;
}

static int docx_attribute(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                          const char *element_local, const char *attribute_name, const char *value)
{
    struct docx_ctx *ctx = ctx_ptr;

    if (ctx->skip_until_depth) {
        return 0;
    }

    if (ctx->in_drawing) {
        if (strcmp(element_local, "docPr") == 0 &&
            strcmp(yetty_ymsoffice_xml_local_name(attribute_name), "name") == 0 &&
            !ctx->image_name) {
            ctx->image_name = strdup(value);
            if (!ctx->image_name) {
                return docx_fail(ctx, "docx: out of memory (image name)");
            }
        } else if (strcmp(element_local, "extent") == 0) {
            const char *attribute_local = yetty_ymsoffice_xml_local_name(attribute_name);
            if (strcmp(attribute_local, "cx") == 0) {
                ctx->image_width_pt = (float)atol(value) / DOCX_EMU_PER_POINT;
            } else if (strcmp(attribute_local, "cy") == 0) {
                ctx->image_height_pt = (float)atol(value) / DOCX_EMU_PER_POINT;
            }
        }
        return 0;
    }

    const char *attribute_local = yetty_ymsoffice_xml_local_name(attribute_name);
    if (strcmp(attribute_local, "val") != 0) {
        return 0;
    }

    /* Paragraph properties. */
    if (ctx->in_paragraph && !ctx->in_run &&
        strcmp(yetty_ymsoffice_xml_ancestor(walker, 1), "pPr") == 0) {
        if (strcmp(element_local, "pStyle") == 0) {
            int level = docx_styles_heading_level(ctx->styles, value);
            if (level > 0) {
                ctx->paragraph.heading_level = level;
            }
            return 0;
        }
        if (strcmp(element_local, "jc") == 0) {
            if (strcmp(value, "center") == 0) {
                ctx->paragraph.align = YETTY_YMSOFFICE_ALIGN_CENTER;
            } else if (strcmp(value, "right") == 0 || strcmp(value, "end") == 0) {
                ctx->paragraph.align = YETTY_YMSOFFICE_ALIGN_RIGHT;
            } else if (strcmp(value, "both") == 0 || strcmp(value, "distribute") == 0) {
                ctx->paragraph.align = YETTY_YMSOFFICE_ALIGN_JUSTIFY;
            }
            return 0;
        }
        if (strcmp(element_local, "outlineLvl") == 0) {
            int level = atoi(value) + 1;
            if (level >= 1 && level <= 6 && ctx->paragraph.heading_level == 0) {
                ctx->paragraph.heading_level = level;
            }
            return 0;
        }
    }
    if (ctx->in_paragraph && strcmp(yetty_ymsoffice_xml_ancestor(walker, 1), "numPr") == 0) {
        if (strcmp(element_local, "ilvl") == 0) {
            ctx->pending_list_level = atoi(value);
            return 0;
        }
        if (strcmp(element_local, "numId") == 0) {
            size_t len = strlen(value);
            if (len < sizeof(ctx->pending_num_id) && strcmp(value, "0") != 0) {
                memcpy(ctx->pending_num_id, value, len + 1);
                ctx->has_numbering = true;
            }
            return 0;
        }
    }

    /* Run properties. */
    if (ctx->in_run && strcmp(yetty_ymsoffice_xml_ancestor(walker, 1), "rPr") == 0) {
        if (strcmp(element_local, "b") == 0 || strcmp(element_local, "bCs") == 0) {
            ctx->run.bold = docx_bool_value(value);
        } else if (strcmp(element_local, "i") == 0 || strcmp(element_local, "iCs") == 0) {
            ctx->run.italic = docx_bool_value(value);
        } else if (strcmp(element_local, "u") == 0) {
            ctx->run.underline = docx_bool_value(value);
        } else if (strcmp(element_local, "strike") == 0) {
            ctx->run.strike = docx_bool_value(value);
        } else if (strcmp(element_local, "sz") == 0) {
            ctx->run.font_size_pt = (float)atof(value) / 2.0f; /* half-points */
        } else if (strcmp(element_local, "color") == 0) {
            uint32_t rgb;
            if (docx_parse_hex_color(value, &rgb) == 0) {
                ctx->run.color_rgb = rgb;
                ctx->run.has_color = true;
            }
        } else if (strcmp(element_local, "highlight") == 0) {
            uint32_t rgb;
            if (docx_highlight_color(value, &rgb) == 0) {
                ctx->run.highlight_rgb = rgb;
                ctx->run.has_highlight = true;
            }
        }
        return 0;
    }

    /* Table cell properties. */
    if (ctx->in_cell && strcmp(yetty_ymsoffice_xml_ancestor(walker, 1), "tcPr") == 0) {
        struct yetty_ymsoffice_table_cell *cell = docx_current_cell(ctx);
        if (!cell) {
            return 0;
        }
        if (strcmp(element_local, "gridSpan") == 0) {
            int span = atoi(value);
            cell->col_span = span >= 1 ? span : 1;
        } else if (strcmp(element_local, "vMerge") == 0 && strcmp(value, "restart") == 0) {
            cell->merged_continue = false;
        }
        return 0;
    }

    return 0;
}

static int docx_text(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                     const char *chunk, size_t chunk_len)
{
    (void)walker;
    struct docx_ctx *ctx = ctx_ptr;
    if (ctx->skip_until_depth || ctx->in_drawing || !ctx->capture_text) {
        return 0;
    }
    return docx_run_text_append(ctx, chunk, chunk_len);
}

static int docx_element_end(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                            const char *local_name)
{
    struct docx_ctx *ctx = ctx_ptr;

    if (ctx->skip_until_depth) {
        if (walker->depth == ctx->skip_until_depth) {
            ctx->skip_until_depth = 0;
        }
        return 0;
    }

    if (ctx->in_drawing) {
        if (walker->depth == ctx->drawing_depth &&
            (strcmp(local_name, "drawing") == 0 || strcmp(local_name, "pict") == 0 ||
             strcmp(local_name, "object") == 0)) {
            ctx->in_drawing = false;
            struct yetty_ymsoffice_image image = {
                .name = ctx->image_name,
                .width_pt = ctx->image_width_pt,
                .height_pt = ctx->image_height_pt,
            };
            ctx->image_name = NULL;
            if (yetty_ymsoffice_word_push_image(ctx->word, image) < 0) {
                free(image.name);
                return docx_fail(ctx, "docx: out of memory (image)");
            }
        }
        return 0;
    }

    if (strcmp(local_name, "t") == 0) {
        ctx->capture_text = false;
        return 0;
    }
    if (strcmp(local_name, "r") == 0) {
        return docx_finish_run(ctx);
    }
    if (strcmp(local_name, "p") == 0) {
        return docx_finish_paragraph(ctx);
    }
    if (strcmp(local_name, "hyperlink") == 0 && ctx->hyperlink_depth > 0) {
        ctx->hyperlink_depth--;
        return 0;
    }
    if (strcmp(local_name, "tc") == 0 && ctx->nested_table_depth == 0) {
        ctx->in_cell = false;
        return 0;
    }
    if (strcmp(local_name, "tr") == 0 && ctx->nested_table_depth == 0) {
        ctx->in_row = false;
        return 0;
    }
    if (strcmp(local_name, "tbl") == 0 && ctx->in_table) {
        if (ctx->nested_table_depth > 0) {
            ctx->nested_table_depth--;
            return 0;
        }
        ctx->in_table = false;
        if (yetty_ymsoffice_word_push_table(ctx->word, ctx->table) < 0) {
            yetty_ymsoffice_table_free(&ctx->table);
            return docx_fail(ctx, "docx: out of memory (table)");
        }
        memset(&ctx->table, 0, sizeof(ctx->table));
        return 0;
    }
    return 0;
}

static void docx_ctx_free_transient(struct docx_ctx *ctx)
{
    if (ctx->in_paragraph) {
        yetty_ymsoffice_paragraph_free(&ctx->paragraph);
    }
    if (ctx->in_table) {
        yetty_ymsoffice_table_free(&ctx->table);
    }
    free(ctx->run_text);
    free(ctx->image_name);
}

/*=============================================================================
 * Entry point
 *===========================================================================*/

struct yetty_ymsoffice_document_ptr_result yetty_ymsoffice_docx_parse(
    const struct yetty_ymsoffice_opc *opc)
{
    struct yetty_ymsoffice_opc_part_result main_res =
        yetty_ymsoffice_opc_read(opc, "word/document.xml");
    YETTY_RETURN_IF_ERR(yetty_ymsoffice_document_ptr, main_res, "docx: word/document.xml missing");
    struct yetty_ymsoffice_opc_part main_part = main_res.value;

    /* Optional side tables — parse failures degrade gracefully to defaults. */
    struct docx_styles styles = {0};
    struct yetty_ymsoffice_opc_part_result styles_res =
        yetty_ymsoffice_opc_read(opc, "word/styles.xml");
    if (!YETTY_IS_ERR(styles_res)) {
        struct yetty_ymsoffice_xml_callbacks callbacks = {
            .element_start = docx_styles_element_start,
            .element_end = docx_styles_element_end,
            .attribute = docx_styles_attribute,
        };
        yetty_ymsoffice_xml_walk(styles_res.value.data, styles_res.value.size, &callbacks, &styles);
        yetty_ymsoffice_opc_part_destroy(&styles_res.value);
    } else {
        yetty_ycore_error_destroy(styles_res.error);
    }

    struct docx_numbering numbering = {0};
    struct yetty_ymsoffice_opc_part_result numbering_res =
        yetty_ymsoffice_opc_read(opc, "word/numbering.xml");
    if (!YETTY_IS_ERR(numbering_res)) {
        struct yetty_ymsoffice_xml_callbacks callbacks = {
            .attribute = docx_numbering_attribute,
        };
        yetty_ymsoffice_xml_walk(numbering_res.value.data, numbering_res.value.size, &callbacks,
                                 &numbering);
        yetty_ymsoffice_opc_part_destroy(&numbering_res.value);
    } else {
        yetty_ycore_error_destroy(numbering_res.error);
    }

    struct yetty_ymsoffice_document_ptr_result document_res =
        yetty_ymsoffice_document_create(YETTY_YMSOFFICE_KIND_WORD);
    if (YETTY_IS_ERR(document_res)) {
        docx_styles_free(&styles);
        docx_numbering_free(&numbering);
        yetty_ymsoffice_opc_part_destroy(&main_part);
        return YETTY_ERR(yetty_ymsoffice_document_ptr, "docx: document alloc failed", document_res);
    }
    struct yetty_ymsoffice_document *document = document_res.value;

    struct docx_ctx ctx = {0};
    ctx.word = &document->word;
    ctx.styles = &styles;
    ctx.numbering = &numbering;
    ctx.paragraph.list_level = -1;

    struct yetty_ymsoffice_xml_callbacks callbacks = {
        .element_start = docx_element_start,
        .element_end = docx_element_end,
        .attribute = docx_attribute,
        .text = docx_text,
    };
    int walk_rc = yetty_ymsoffice_xml_walk(main_part.data, main_part.size, &callbacks, &ctx);

    docx_ctx_free_transient(&ctx);
    docx_styles_free(&styles);
    docx_numbering_free(&numbering);
    yetty_ymsoffice_opc_part_destroy(&main_part);

    if (walk_rc < 0) {
        yetty_ymsoffice_document_destroy(document);
        return YETTY_ERR(yetty_ymsoffice_document_ptr,
                         ctx.fail_msg ? ctx.fail_msg : "docx: document.xml parse failed");
    }
    return YETTY_OK(yetty_ymsoffice_document_ptr, document);
}
