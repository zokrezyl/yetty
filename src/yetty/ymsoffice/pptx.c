/*
 * pptx.c - PresentationML → neutral slides model.
 *
 * Parts consumed:
 *   ppt/presentation.xml            slide size + ordered slide id list
 *   ppt/_rels/presentation.xml.rels relationship id → slide part path
 *   ppt/slides/slideN.xml           shape tree
 *
 * Extracted per slide: shapes (sp/pic/cxnSp/graphicFrame) with their frame
 * (a:off + a:ext, EMU → points), preset geometry mapped to box / ellipse /
 * line, solid fill color, and text bodies as paragraphs of styled runs.
 * Theme-indirect colors (schemeClr), slide masters/layouts, and group
 * transforms are not resolved — group children keep their child-space
 * coordinates, which is approximate but usually close for simple decks.
 */

#include <yetty/ymsoffice/msoffice.h>

#include "model-internal.h"
#include "rels-internal.h"
#include "xml-internal.h"

#include <stdlib.h>
#include <string.h>

#define PPTX_EMU_PER_POINT 12700.0f

/*=============================================================================
 * presentation.xml — slide size + ordered relationship ids
 *===========================================================================*/

struct pptx_presentation_ctx {
    char **slide_relationship_ids;
    size_t count;
    size_t cap;
    float width_pt;
    float height_pt;
};

static int pptx_presentation_attribute(void *ctx_ptr,
                                       const struct yetty_ymsoffice_xml_walker *walker,
                                       const char *element_local, const char *attribute_name,
                                       const char *value)
{
    (void)walker;
    struct pptx_presentation_ctx *ctx = ctx_ptr;
    if (strcmp(element_local, "sldSz") == 0) {
        if (strcmp(attribute_name, "cx") == 0) {
            ctx->width_pt = (float)atol(value) / PPTX_EMU_PER_POINT;
        } else if (strcmp(attribute_name, "cy") == 0) {
            ctx->height_pt = (float)atol(value) / PPTX_EMU_PER_POINT;
        }
        return 0;
    }
    /* sldId carries both its own "id" and the relationship "r:id" — match
     * the prefixed name exactly. */
    if (strcmp(element_local, "sldId") == 0 && strcmp(attribute_name, "r:id") == 0) {
        if (ctx->count == ctx->cap) {
            size_t new_cap = ctx->cap ? ctx->cap * 2 : 8;
            char **grown = realloc(ctx->slide_relationship_ids, new_cap * sizeof(*grown));
            if (!grown) {
                return -1;
            }
            ctx->slide_relationship_ids = grown;
            ctx->cap = new_cap;
        }
        ctx->slide_relationship_ids[ctx->count] = strdup(value);
        if (!ctx->slide_relationship_ids[ctx->count]) {
            return -1;
        }
        ctx->count++;
    }
    return 0;
}

static void pptx_presentation_ctx_free(struct pptx_presentation_ctx *ctx)
{
    for (size_t i = 0; i < ctx->count; i++) {
        free(ctx->slide_relationship_ids[i]);
    }
    free(ctx->slide_relationship_ids);
    memset(ctx, 0, sizeof(*ctx));
}

/*=============================================================================
 * slide part walk
 *===========================================================================*/

struct pptx_slide_ctx {
    struct yetty_ymsoffice_slide *slide;

    /* shape under construction */
    struct yetty_ymsoffice_shape shape;
    bool in_shape;
    size_t shape_depth;

    /* text body under construction */
    struct yetty_ymsoffice_paragraph paragraph;
    bool in_paragraph;
    struct yetty_ymsoffice_text_run run;
    struct yetty_ymsoffice_text_accum run_text;
    bool in_run;
    bool capture_text;

    const char *fail_msg;
};

static int pptx_fail(struct pptx_slide_ctx *ctx, const char *msg)
{
    if (!ctx->fail_msg) {
        ctx->fail_msg = msg;
    }
    return -1;
}

static int pptx_finish_run(struct pptx_slide_ctx *ctx)
{
    if (!ctx->in_run) {
        return 0;
    }
    ctx->in_run = false;
    ctx->capture_text = false;
    if (!ctx->in_paragraph || ctx->run_text.len == 0) {
        return 0;
    }
    char *text = strdup(yetty_ymsoffice_text_accum_cstr(&ctx->run_text));
    if (!text) {
        return pptx_fail(ctx, "pptx: out of memory (run text)");
    }
    ctx->run.text = text;
    ctx->run.text_len = ctx->run_text.len;
    if (yetty_ymsoffice_paragraph_push_run(&ctx->paragraph, ctx->run) < 0) {
        free(text);
        return pptx_fail(ctx, "pptx: out of memory (run push)");
    }
    memset(&ctx->run, 0, sizeof(ctx->run));
    return 0;
}

static int pptx_finish_paragraph(struct pptx_slide_ctx *ctx)
{
    if (!ctx->in_paragraph) {
        return 0;
    }
    ctx->in_paragraph = false;
    if (!ctx->in_shape) {
        yetty_ymsoffice_paragraph_free(&ctx->paragraph);
        return 0;
    }
    if (yetty_ymsoffice_shape_push_paragraph(&ctx->shape, ctx->paragraph) < 0) {
        yetty_ymsoffice_paragraph_free(&ctx->paragraph);
        return pptx_fail(ctx, "pptx: out of memory (paragraph)");
    }
    memset(&ctx->paragraph, 0, sizeof(ctx->paragraph));
    return 0;
}

static int pptx_is_shape_element(const char *local_name)
{
    return strcmp(local_name, "sp") == 0 || strcmp(local_name, "pic") == 0 ||
           strcmp(local_name, "cxnSp") == 0 || strcmp(local_name, "graphicFrame") == 0;
}

static int pptx_slide_element_start(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                                    const char *local_name)
{
    struct pptx_slide_ctx *ctx = ctx_ptr;

    if (!ctx->in_shape && pptx_is_shape_element(local_name)) {
        memset(&ctx->shape, 0, sizeof(ctx->shape));
        ctx->in_shape = true;
        ctx->shape_depth = walker->depth;
        if (strcmp(local_name, "pic") == 0) {
            ctx->shape.kind = YETTY_YMSOFFICE_SHAPE_PICTURE;
        } else if (strcmp(local_name, "graphicFrame") == 0) {
            ctx->shape.kind = YETTY_YMSOFFICE_SHAPE_FRAME;
        } else if (strcmp(local_name, "cxnSp") == 0) {
            ctx->shape.kind = YETTY_YMSOFFICE_SHAPE_LINE;
        } else {
            ctx->shape.kind = YETTY_YMSOFFICE_SHAPE_BOX;
        }
        return 0;
    }
    if (!ctx->in_shape) {
        return 0;
    }

    if (strcmp(local_name, "p") == 0 && yetty_ymsoffice_xml_has_ancestor(walker, "txBody")) {
        memset(&ctx->paragraph, 0, sizeof(ctx->paragraph));
        ctx->paragraph.list_level = -1;
        ctx->in_paragraph = true;
        return 0;
    }
    if ((strcmp(local_name, "r") == 0 || strcmp(local_name, "fld") == 0) && ctx->in_paragraph) {
        memset(&ctx->run, 0, sizeof(ctx->run));
        yetty_ymsoffice_text_accum_reset(&ctx->run_text);
        ctx->in_run = true;
        return 0;
    }
    if (strcmp(local_name, "t") == 0 && ctx->in_run) {
        ctx->capture_text = true;
        return 0;
    }
    if (strcmp(local_name, "br") == 0 && ctx->in_paragraph) {
        /* Explicit line break — emit as its own newline run. */
        struct yetty_ymsoffice_text_run break_run = {0};
        break_run.text = strdup("\n");
        if (!break_run.text) {
            return pptx_fail(ctx, "pptx: out of memory (line break)");
        }
        break_run.text_len = 1;
        if (yetty_ymsoffice_paragraph_push_run(&ctx->paragraph, break_run) < 0) {
            free(break_run.text);
            return pptx_fail(ctx, "pptx: out of memory (line break push)");
        }
        return 0;
    }
    return 0;
}

static int pptx_slide_attribute(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                                const char *element_local, const char *attribute_name,
                                const char *value)
{
    struct pptx_slide_ctx *ctx = ctx_ptr;
    if (!ctx->in_shape) {
        return 0;
    }

    /* Frame geometry: a:off / a:ext inside the shape's xfrm. */
    if (strcmp(element_local, "off") == 0 &&
        strcmp(yetty_ymsoffice_xml_ancestor(walker, 1), "xfrm") == 0) {
        if (strcmp(attribute_name, "x") == 0) {
            ctx->shape.x_pt = (float)atol(value) / PPTX_EMU_PER_POINT;
            ctx->shape.has_frame = true;
        } else if (strcmp(attribute_name, "y") == 0) {
            ctx->shape.y_pt = (float)atol(value) / PPTX_EMU_PER_POINT;
            ctx->shape.has_frame = true;
        }
        return 0;
    }
    if (strcmp(element_local, "ext") == 0 &&
        strcmp(yetty_ymsoffice_xml_ancestor(walker, 1), "xfrm") == 0) {
        if (strcmp(attribute_name, "cx") == 0) {
            ctx->shape.width_pt = (float)atol(value) / PPTX_EMU_PER_POINT;
        } else if (strcmp(attribute_name, "cy") == 0) {
            ctx->shape.height_pt = (float)atol(value) / PPTX_EMU_PER_POINT;
        }
        return 0;
    }

    if (strcmp(element_local, "cNvPr") == 0 && strcmp(attribute_name, "name") == 0 &&
        !ctx->shape.name) {
        ctx->shape.name = strdup(value);
        return ctx->shape.name ? 0 : pptx_fail(ctx, "pptx: out of memory (shape name)");
    }

    if (strcmp(element_local, "prstGeom") == 0 && strcmp(attribute_name, "prst") == 0) {
        if (strcmp(value, "ellipse") == 0) {
            ctx->shape.kind = YETTY_YMSOFFICE_SHAPE_ELLIPSE;
        } else if (strcmp(value, "line") == 0 || strncmp(value, "straightConnector", 17) == 0 ||
                   strncmp(value, "bentConnector", 13) == 0 ||
                   strncmp(value, "curvedConnector", 15) == 0) {
            ctx->shape.kind = YETTY_YMSOFFICE_SHAPE_LINE;
        }
        return 0;
    }

    /* Solid fills: shape fill vs run color, disambiguated by ancestry. */
    if (strcmp(element_local, "srgbClr") == 0 && strcmp(attribute_name, "val") == 0 &&
        strcmp(yetty_ymsoffice_xml_ancestor(walker, 1), "solidFill") == 0 && strlen(value) == 6) {
        uint32_t rgb = (uint32_t)strtoul(value, NULL, 16);
        const char *fill_parent = yetty_ymsoffice_xml_ancestor(walker, 2);
        if (strcmp(fill_parent, "spPr") == 0) {
            ctx->shape.fill_rgb = rgb;
            ctx->shape.has_fill = true;
        } else if (ctx->in_run && strcmp(fill_parent, "rPr") == 0) {
            ctx->run.color_rgb = rgb;
            ctx->run.has_color = true;
        }
        return 0;
    }

    /* Run properties live as attributes on a:rPr. */
    if (strcmp(element_local, "rPr") == 0 && ctx->in_run) {
        if (strcmp(attribute_name, "sz") == 0) {
            ctx->run.font_size_pt = (float)atol(value) / 100.0f; /* centipoints */
        } else if (strcmp(attribute_name, "b") == 0) {
            ctx->run.bold = strcmp(value, "1") == 0 || strcmp(value, "true") == 0;
        } else if (strcmp(attribute_name, "i") == 0) {
            ctx->run.italic = strcmp(value, "1") == 0 || strcmp(value, "true") == 0;
        } else if (strcmp(attribute_name, "u") == 0) {
            ctx->run.underline = strcmp(value, "none") != 0;
        } else if (strcmp(attribute_name, "strike") == 0) {
            ctx->run.strike = strcmp(value, "noStrike") != 0;
        }
        return 0;
    }

    /* Paragraph alignment on a:pPr. */
    if (strcmp(element_local, "pPr") == 0 && ctx->in_paragraph &&
        strcmp(attribute_name, "algn") == 0) {
        if (strcmp(value, "ctr") == 0) {
            ctx->paragraph.align = YETTY_YMSOFFICE_ALIGN_CENTER;
        } else if (strcmp(value, "r") == 0) {
            ctx->paragraph.align = YETTY_YMSOFFICE_ALIGN_RIGHT;
        } else if (strcmp(value, "just") == 0 || strcmp(value, "dist") == 0) {
            ctx->paragraph.align = YETTY_YMSOFFICE_ALIGN_JUSTIFY;
        }
        return 0;
    }

    return 0;
}

static int pptx_slide_text(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                           const char *chunk, size_t chunk_len)
{
    (void)walker;
    struct pptx_slide_ctx *ctx = ctx_ptr;
    if (!ctx->capture_text) {
        return 0;
    }
    if (yetty_ymsoffice_text_accum_append(&ctx->run_text, chunk, chunk_len) < 0) {
        return pptx_fail(ctx, "pptx: out of memory (text)");
    }
    return 0;
}

static int pptx_slide_element_end(void *ctx_ptr, const struct yetty_ymsoffice_xml_walker *walker,
                                  const char *local_name)
{
    struct pptx_slide_ctx *ctx = ctx_ptr;
    if (!ctx->in_shape) {
        return 0;
    }

    if (strcmp(local_name, "t") == 0) {
        ctx->capture_text = false;
        return 0;
    }
    if (strcmp(local_name, "r") == 0 || strcmp(local_name, "fld") == 0) {
        return pptx_finish_run(ctx);
    }
    if (strcmp(local_name, "p") == 0 && ctx->in_paragraph) {
        return pptx_finish_paragraph(ctx);
    }

    if (walker->depth == ctx->shape_depth && pptx_is_shape_element(local_name)) {
        ctx->in_shape = false;
        if (yetty_ymsoffice_slide_push_shape(ctx->slide, ctx->shape) < 0) {
            yetty_ymsoffice_shape_free(&ctx->shape);
            return pptx_fail(ctx, "pptx: out of memory (shape)");
        }
        memset(&ctx->shape, 0, sizeof(ctx->shape));
    }
    return 0;
}

/*=============================================================================
 * Entry point
 *===========================================================================*/

/* "slides/slide1.xml" (relative to ppt/) or "/ppt/slides/slide1.xml". */
static char *pptx_resolve_target(const char *target)
{
    if (target[0] == '/') {
        return strdup(target + 1);
    }
    size_t len = strlen(target);
    char *resolved = malloc(len + 5);
    if (!resolved) {
        return NULL;
    }
    memcpy(resolved, "ppt/", 4);
    memcpy(resolved + 4, target, len + 1);
    return resolved;
}

struct yetty_ymsoffice_document_ptr_result yetty_ymsoffice_pptx_parse(
    const struct yetty_ymsoffice_opc *opc)
{
    struct yetty_ymsoffice_opc_part_result presentation_res =
        yetty_ymsoffice_opc_read(opc, "ppt/presentation.xml");
    YETTY_RETURN_IF_ERR(yetty_ymsoffice_document_ptr, presentation_res,
                        "pptx: ppt/presentation.xml missing");

    struct pptx_presentation_ctx presentation = {0};
    {
        struct yetty_ymsoffice_xml_callbacks callbacks = {
            .attribute = pptx_presentation_attribute,
        };
        int rc = yetty_ymsoffice_xml_walk(presentation_res.value.data, presentation_res.value.size,
                                          &callbacks, &presentation);
        yetty_ymsoffice_opc_part_destroy(&presentation_res.value);
        if (rc < 0) {
            pptx_presentation_ctx_free(&presentation);
            return YETTY_ERR(yetty_ymsoffice_document_ptr, "pptx: presentation.xml parse failed");
        }
    }

    struct yetty_ymsoffice_rels rels = {0};
    struct yetty_ymsoffice_opc_part_result rels_res =
        yetty_ymsoffice_opc_read(opc, "ppt/_rels/presentation.xml.rels");
    if (!YETTY_IS_ERR(rels_res)) {
        yetty_ymsoffice_rels_parse(rels_res.value.data, rels_res.value.size, &rels);
        yetty_ymsoffice_opc_part_destroy(&rels_res.value);
    } else {
        yetty_ycore_error_destroy(rels_res.error);
    }

    struct yetty_ymsoffice_document_ptr_result document_res =
        yetty_ymsoffice_document_create(YETTY_YMSOFFICE_KIND_SLIDES);
    if (YETTY_IS_ERR(document_res)) {
        pptx_presentation_ctx_free(&presentation);
        yetty_ymsoffice_rels_free(&rels);
        return YETTY_ERR(yetty_ymsoffice_document_ptr, "pptx: document alloc failed", document_res);
    }
    struct yetty_ymsoffice_document *document = document_res.value;
    document->slides.width_pt = presentation.width_pt;
    document->slides.height_pt = presentation.height_pt;

    const char *fail_msg = NULL;
    for (size_t i = 0; i < presentation.count && !fail_msg; i++) {
        const char *target =
            yetty_ymsoffice_rels_target(&rels, presentation.slide_relationship_ids[i]);
        if (!target) {
            continue;
        }
        char *part_name = pptx_resolve_target(target);
        if (!part_name) {
            fail_msg = "pptx: out of memory (part name)";
            break;
        }
        struct yetty_ymsoffice_opc_part_result slide_res = yetty_ymsoffice_opc_read(opc, part_name);
        free(part_name);
        if (YETTY_IS_ERR(slide_res)) {
            yetty_ycore_error_destroy(slide_res.error);
            continue;
        }

        if (yetty_ymsoffice_slides_push_slide(&document->slides) < 0) {
            yetty_ymsoffice_opc_part_destroy(&slide_res.value);
            fail_msg = "pptx: out of memory (slide)";
            break;
        }

        struct pptx_slide_ctx slide_ctx = {0};
        slide_ctx.slide = &document->slides.slides[document->slides.slide_count - 1];
        struct yetty_ymsoffice_xml_callbacks callbacks = {
            .element_start = pptx_slide_element_start,
            .element_end = pptx_slide_element_end,
            .attribute = pptx_slide_attribute,
            .text = pptx_slide_text,
        };
        int rc = yetty_ymsoffice_xml_walk(slide_res.value.data, slide_res.value.size, &callbacks,
                                          &slide_ctx);
        if (slide_ctx.in_paragraph) {
            yetty_ymsoffice_paragraph_free(&slide_ctx.paragraph);
        }
        if (slide_ctx.in_shape) {
            yetty_ymsoffice_shape_free(&slide_ctx.shape);
        }
        yetty_ymsoffice_text_accum_free(&slide_ctx.run_text);
        yetty_ymsoffice_opc_part_destroy(&slide_res.value);
        if (rc < 0) {
            fail_msg = slide_ctx.fail_msg ? slide_ctx.fail_msg : "pptx: slide parse failed";
        }
    }

    pptx_presentation_ctx_free(&presentation);
    yetty_ymsoffice_rels_free(&rels);

    if (fail_msg) {
        yetty_ymsoffice_document_destroy(document);
        return YETTY_ERR(yetty_ymsoffice_document_ptr, fail_msg);
    }
    return YETTY_OK(yetty_ymsoffice_document_ptr, document);
}
