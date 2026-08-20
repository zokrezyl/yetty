/*
 * ymsoffice golden test.
 *
 * Parses the committed fixtures (sample.docx / sample.xlsx / sample.pptx —
 * regenerate with make-fixtures.py) into the neutral model and pins the
 * extracted structure: headings, styled runs, lists, tables, sheet cells
 * (shared/inline/boolean/formula), slides and shapes. Then renders each
 * document and checks the primitive families plus byte-exact determinism.
 * Layout is fixed-geometry (no font metrics), so the output is
 * deterministic and headless.
 */

#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ydraw-list/text-drawable-list.h>
#include <yetty/ymsoffice/msoffice.h>
#include <yetty/ymsoffice/render.h>
#include <yetty/ysdf/types.gen.h>

#include "ytest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------
 * Fixture loading
 *-------------------------------------------------------------------------*/

struct fixture {
    uint8_t *bytes;
    size_t len;
};

static struct fixture load_fixture(struct ytest *test, const char *path)
{
    struct fixture fixture = {0};
    FILE *file = fopen(path, "rb");
    YTEST_REQUIRE(test, file != NULL);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    YTEST_REQUIRE(test, size > 0);
    fixture.bytes = malloc((size_t)size);
    YTEST_REQUIRE(test, fixture.bytes != NULL);
    fixture.len = fread(fixture.bytes, 1, (size_t)size, file);
    fclose(file);
    YTEST_REQUIRE_EQ_SIZE(test, fixture.len, (size_t)size);
    return fixture;
}

static struct yetty_ymsoffice_document *parse_fixture(struct ytest *test, const char *path,
                                                      enum yetty_ymsoffice_kind expected_kind)
{
    struct fixture fixture = load_fixture(test, path);
    struct yetty_ymsoffice_document_ptr_result parse_res =
        yetty_ymsoffice_parse(fixture.bytes, fixture.len);
    YTEST_REQUIRE_OK(test, parse_res);
    free(fixture.bytes);
    YTEST_REQUIRE(test, parse_res.value != NULL);
    YTEST_REQUIRE_EQ_INT(test, (int)parse_res.value->kind, (int)expected_kind);
    return parse_res.value;
}

/*---------------------------------------------------------------------------
 * Render helpers (same tag-scan approach as the ymarkdown golden test)
 *-------------------------------------------------------------------------*/

static size_t count_tag(const uint8_t *data, size_t size, uint32_t tag)
{
    uint8_t pattern[4];
    memcpy(pattern, &tag, sizeof(pattern));
    size_t count = 0;
    for (size_t i = 0; i + sizeof(pattern) <= size; i++) {
        if (memcmp(data + i, pattern, sizeof(pattern)) == 0) {
            count++;
        }
    }
    return count;
}

static struct yetty_ydraw_drawable_list *render_document(
    struct ytest *test, const struct yetty_ymsoffice_document *document)
{
    struct yetty_ymsoffice_render_config config = {
        .cell_width = 8,
        .cell_height = 16,
        .width_cells = 80,
        .height_cells = 24,
    };
    struct yetty_ymsoffice_render_result render_res = yetty_ymsoffice_render(document, &config);
    YTEST_REQUIRE_OK(test, render_res);
    YTEST_REQUIRE(test, render_res.value.buffer != NULL);
    return render_res.value.buffer;
}

/*---------------------------------------------------------------------------
 * docx: model structure
 *-------------------------------------------------------------------------*/

static const struct yetty_ymsoffice_paragraph *block_paragraph(
    struct ytest *test, const struct yetty_ymsoffice_word_document *word, size_t index)
{
    YTEST_REQUIRE(test, index < word->block_count);
    YTEST_REQUIRE_EQ_INT(test, (int)word->blocks[index].kind, (int)YETTY_YMSOFFICE_BLOCK_PARAGRAPH);
    return &word->blocks[index].paragraph;
}

static void test_docx_model(struct ytest *test)
{
    struct yetty_ymsoffice_document *document =
        parse_fixture(test, YMSOFFICE_TEST_DIR "/sample.docx", YETTY_YMSOFFICE_KIND_WORD);
    const struct yetty_ymsoffice_word_document *word = &document->word;

    /* heading + body + centered + 2 bullets + 2 ordered + table + link */
    YTEST_REQUIRE_EQ_SIZE(test, word->block_count, 9u);

    const struct yetty_ymsoffice_paragraph *heading = block_paragraph(test, word, 0);
    YTEST_CHECK_EQ_INT(test, heading->heading_level, 1);
    YTEST_REQUIRE_EQ_SIZE(test, heading->run_count, 1u);
    YTEST_CHECK(test, strcmp(heading->runs[0].text, "Quarterly Report") == 0);

    const struct yetty_ymsoffice_paragraph *body = block_paragraph(test, word, 1);
    YTEST_REQUIRE_EQ_SIZE(test, body->run_count, 7u);
    YTEST_CHECK(test, body->runs[1].bold);
    YTEST_CHECK(test, strcmp(body->runs[1].text, "bold") == 0);
    YTEST_CHECK(test, body->runs[3].italic);
    YTEST_CHECK(test, body->runs[3].has_color);
    YTEST_CHECK(test, body->runs[3].color_rgb == 0xFF0000u);
    YTEST_CHECK(test, body->runs[5].underline);
    YTEST_CHECK(test, body->runs[5].has_highlight);
    YTEST_CHECK(test, body->runs[5].highlight_rgb == 0xFFFF00u);

    const struct yetty_ymsoffice_paragraph *centered = block_paragraph(test, word, 2);
    YTEST_CHECK_EQ_INT(test, (int)centered->align, (int)YETTY_YMSOFFICE_ALIGN_CENTER);
    YTEST_REQUIRE_EQ_SIZE(test, centered->run_count, 1u);
    YTEST_CHECK(test,
                centered->runs[0].font_size_pt > 23.9f && centered->runs[0].font_size_pt < 24.1f);

    const struct yetty_ymsoffice_paragraph *bullet = block_paragraph(test, word, 3);
    YTEST_CHECK_EQ_INT(test, bullet->list_level, 0);
    YTEST_CHECK(test, !bullet->list_ordered);

    const struct yetty_ymsoffice_paragraph *ordered = block_paragraph(test, word, 5);
    YTEST_CHECK_EQ_INT(test, ordered->list_level, 0);
    YTEST_CHECK(test, ordered->list_ordered);

    YTEST_REQUIRE_EQ_INT(test, (int)word->blocks[7].kind, (int)YETTY_YMSOFFICE_BLOCK_TABLE);
    const struct yetty_ymsoffice_table *table = &word->blocks[7].table;
    YTEST_REQUIRE_EQ_SIZE(test, table->row_count, 3u);
    YTEST_REQUIRE_EQ_SIZE(test, table->rows[0].cell_count, 2u);
    YTEST_REQUIRE_EQ_SIZE(test, table->rows[0].cells[0].paragraph_count, 1u);
    const struct yetty_ymsoffice_paragraph *header_cell = &table->rows[0].cells[0].paragraphs[0];
    YTEST_REQUIRE_EQ_SIZE(test, header_cell->run_count, 1u);
    YTEST_CHECK(test, strcmp(header_cell->runs[0].text, "Region") == 0);
    YTEST_CHECK(test, header_cell->runs[0].bold);
    YTEST_REQUIRE_EQ_SIZE(test, table->rows[2].cell_count, 1u);
    YTEST_CHECK_EQ_INT(test, table->rows[2].cells[0].col_span, 2);

    const struct yetty_ymsoffice_paragraph *link = block_paragraph(test, word, 8);
    YTEST_REQUIRE_EQ_SIZE(test, link->run_count, 1u);
    YTEST_CHECK(test, link->runs[0].hyperlink);
    YTEST_CHECK(test, link->runs[0].underline);

    yetty_ymsoffice_document_destroy(document);
}

/*---------------------------------------------------------------------------
 * xlsx: model structure
 *-------------------------------------------------------------------------*/

static const struct yetty_ymsoffice_sheet_cell *sheet_cell_at(
    const struct yetty_ymsoffice_sheet *sheet, uint32_t row, uint32_t col)
{
    for (size_t i = 0; i < sheet->cell_count; i++) {
        if (sheet->cells[i].row == row && sheet->cells[i].col == col) {
            return &sheet->cells[i];
        }
    }
    return NULL;
}

static void test_xlsx_model(struct ytest *test)
{
    struct yetty_ymsoffice_document *document =
        parse_fixture(test, YMSOFFICE_TEST_DIR "/sample.xlsx", YETTY_YMSOFFICE_KIND_SHEET);
    const struct yetty_ymsoffice_sheet_document *sheet_document = &document->sheet;

    YTEST_REQUIRE_EQ_SIZE(test, sheet_document->sheet_count, 2u);
    const struct yetty_ymsoffice_sheet *numbers = &sheet_document->sheets[0];
    YTEST_CHECK(test, strcmp(numbers->name, "Numbers") == 0);
    YTEST_CHECK_EQ_INT(test, (int)numbers->max_row, 3);
    YTEST_CHECK_EQ_INT(test, (int)numbers->max_col, 2);

    const struct yetty_ymsoffice_sheet_cell *header = sheet_cell_at(numbers, 0, 0);
    YTEST_REQUIRE(test, header != NULL);
    YTEST_CHECK(test, strcmp(header->text, "Item") == 0);
    YTEST_CHECK(test, !header->is_number);

    const struct yetty_ymsoffice_sheet_cell *inline_string = sheet_cell_at(numbers, 1, 0);
    YTEST_REQUIRE(test, inline_string != NULL);
    YTEST_CHECK(test, strcmp(inline_string->text, "Widgets") == 0);

    const struct yetty_ymsoffice_sheet_cell *amount = sheet_cell_at(numbers, 1, 1);
    YTEST_REQUIRE(test, amount != NULL);
    YTEST_CHECK(test, strcmp(amount->text, "1234.5") == 0);
    YTEST_CHECK(test, amount->is_number);

    const struct yetty_ymsoffice_sheet_cell *rich = sheet_cell_at(numbers, 2, 0);
    YTEST_REQUIRE(test, rich != NULL);
    YTEST_CHECK(test, strcmp(rich->text, "rich cell") == 0);

    const struct yetty_ymsoffice_sheet_cell *formula = sheet_cell_at(numbers, 2, 1);
    YTEST_REQUIRE(test, formula != NULL);
    YTEST_REQUIRE(test, formula->formula != NULL);
    YTEST_CHECK(test, strcmp(formula->formula, "SUM(B2:B2)") == 0);
    YTEST_CHECK(test, strcmp(formula->text, "1234.5") == 0);

    const struct yetty_ymsoffice_sheet_cell *boolean = sheet_cell_at(numbers, 3, 0);
    YTEST_REQUIRE(test, boolean != NULL);
    YTEST_CHECK(test, strcmp(boolean->text, "TRUE") == 0);

    const struct yetty_ymsoffice_sheet *notes = &sheet_document->sheets[1];
    YTEST_CHECK(test, strcmp(notes->name, "Notes") == 0);
    YTEST_REQUIRE_EQ_SIZE(test, notes->cell_count, 1u);
    YTEST_CHECK(test, strcmp(notes->cells[0].text, "second sheet") == 0);

    yetty_ymsoffice_document_destroy(document);
}

/*---------------------------------------------------------------------------
 * pptx: model structure
 *-------------------------------------------------------------------------*/

static void test_pptx_model(struct ytest *test)
{
    struct yetty_ymsoffice_document *document =
        parse_fixture(test, YMSOFFICE_TEST_DIR "/sample.pptx", YETTY_YMSOFFICE_KIND_SLIDES);
    const struct yetty_ymsoffice_slides_document *slides = &document->slides;

    /* 12192000 EMU / 12700 = 960 pt (16:9). */
    YTEST_CHECK(test, slides->width_pt > 959.5f && slides->width_pt < 960.5f);
    YTEST_CHECK(test, slides->height_pt > 539.5f && slides->height_pt < 540.5f);
    YTEST_REQUIRE_EQ_SIZE(test, slides->slide_count, 2u);

    const struct yetty_ymsoffice_slide *first = &slides->slides[0];
    YTEST_REQUIRE_EQ_SIZE(test, first->shape_count, 3u);

    const struct yetty_ymsoffice_shape *title = &first->shapes[0];
    YTEST_CHECK(test, title->name && strcmp(title->name, "Title 1") == 0);
    YTEST_CHECK(test, title->has_frame);
    YTEST_REQUIRE_EQ_SIZE(test, title->paragraph_count, 1u);
    YTEST_CHECK_EQ_INT(test, (int)title->paragraphs[0].align, (int)YETTY_YMSOFFICE_ALIGN_CENTER);
    YTEST_REQUIRE_EQ_SIZE(test, title->paragraphs[0].run_count, 1u);
    YTEST_CHECK(test, title->paragraphs[0].runs[0].bold);
    YTEST_CHECK(test, title->paragraphs[0].runs[0].font_size_pt > 43.9f &&
                          title->paragraphs[0].runs[0].font_size_pt < 44.1f);
    YTEST_CHECK(test, strcmp(title->paragraphs[0].runs[0].text, "Deck Title") == 0);

    const struct yetty_ymsoffice_shape *box = &first->shapes[1];
    YTEST_CHECK_EQ_INT(test, (int)box->kind, (int)YETTY_YMSOFFICE_SHAPE_BOX);
    YTEST_CHECK(test, box->has_fill);
    YTEST_CHECK(test, box->fill_rgb == 0x6BA892u);

    const struct yetty_ymsoffice_shape *oval = &first->shapes[2];
    YTEST_CHECK_EQ_INT(test, (int)oval->kind, (int)YETTY_YMSOFFICE_SHAPE_ELLIPSE);

    const struct yetty_ymsoffice_slide *second = &slides->slides[1];
    YTEST_REQUIRE_EQ_SIZE(test, second->shape_count, 1u);
    YTEST_CHECK_EQ_INT(test, (int)second->shapes[0].kind, (int)YETTY_YMSOFFICE_SHAPE_PICTURE);
    YTEST_CHECK(test, second->shapes[0].name && strcmp(second->shapes[0].name, "photo.png") == 0);

    yetty_ymsoffice_document_destroy(document);
}

/*---------------------------------------------------------------------------
 * Rendering: primitive families + determinism
 *-------------------------------------------------------------------------*/

static void test_render_families(struct ytest *test)
{
    /* docx: text spans, table grid boxes, highlight box. */
    struct yetty_ymsoffice_document *word_document =
        parse_fixture(test, YMSOFFICE_TEST_DIR "/sample.docx", YETTY_YMSOFFICE_KIND_WORD);
    struct yetty_ydraw_drawable_list *first_render = render_document(test, word_document);
    const uint8_t *data = yetty_ydraw_drawable_list_data(first_render);
    size_t size = yetty_ydraw_drawable_list_size(first_render);
    YTEST_CHECK(test, size > 0);
    YTEST_CHECK(test, count_tag(data, size, YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST) >= 10);
    YTEST_CHECK(test, count_tag(data, size, (uint32_t)YETTY_YSDF_BOX) >= 5); /* cells+highlight */
    YTEST_CHECK(test, count_tag(data, size, (uint32_t)YETTY_YSDF_SEGMENT) >= 2); /* underlines */

    /* Determinism: identical bytes across two renders. */
    struct yetty_ydraw_drawable_list *second_render = render_document(test, word_document);
    size_t second_size = yetty_ydraw_drawable_list_size(second_render);
    YTEST_REQUIRE_EQ_SIZE(test, size, second_size);
    YTEST_CHECK_MEM_EQ(test, data, yetty_ydraw_drawable_list_data(second_render), size);
    yetty_ydraw_drawable_list_destroy(second_render);
    yetty_ydraw_drawable_list_destroy(first_render);
    yetty_ymsoffice_document_destroy(word_document);

    /* xlsx: grid segments + header boxes + cell text. */
    struct yetty_ymsoffice_document *sheet_document =
        parse_fixture(test, YMSOFFICE_TEST_DIR "/sample.xlsx", YETTY_YMSOFFICE_KIND_SHEET);
    struct yetty_ydraw_drawable_list *sheet_render = render_document(test, sheet_document);
    data = yetty_ydraw_drawable_list_data(sheet_render);
    size = yetty_ydraw_drawable_list_size(sheet_render);
    YTEST_CHECK(test, count_tag(data, size, (uint32_t)YETTY_YSDF_SEGMENT) >= 10);
    YTEST_CHECK(test, count_tag(data, size, (uint32_t)YETTY_YSDF_BOX) >= 2);
    YTEST_CHECK(test, count_tag(data, size, YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST) >= 8);
    yetty_ydraw_drawable_list_destroy(sheet_render);
    yetty_ymsoffice_document_destroy(sheet_document);

    /* pptx: slide panels, ellipse, shape text. */
    struct yetty_ymsoffice_document *slides_document =
        parse_fixture(test, YMSOFFICE_TEST_DIR "/sample.pptx", YETTY_YMSOFFICE_KIND_SLIDES);
    struct yetty_ydraw_drawable_list *slides_render = render_document(test, slides_document);
    data = yetty_ydraw_drawable_list_data(slides_render);
    size = yetty_ydraw_drawable_list_size(slides_render);
    YTEST_CHECK(test, count_tag(data, size, (uint32_t)YETTY_YSDF_BOX) >= 3);
    YTEST_CHECK(test, count_tag(data, size, (uint32_t)YETTY_YSDF_ELLIPSE) >= 1);
    YTEST_CHECK(test, count_tag(data, size, YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST) >= 3);
    yetty_ydraw_drawable_list_destroy(slides_render);
    yetty_ymsoffice_document_destroy(slides_document);
}

int main(void)
{
    struct ytest test = ytest_begin("ymsoffice_golden");
    YTEST_RUN(&test, test_docx_model);
    YTEST_RUN(&test, test_xlsx_model);
    YTEST_RUN(&test, test_pptx_model);
    YTEST_RUN(&test, test_render_families);
    return ytest_end(&test);
}
