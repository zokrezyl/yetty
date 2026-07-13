/*
 * model.c - neutral document model: container growth + deep destruction.
 */

#include "model-internal.h"

#include <stdlib.h>
#include <string.h>

/* Grow a dynamic array to fit one more element. Returns 0 / -1. */
static int model_grow(void **items, size_t *cap, size_t count, size_t item_size)
{
    if (count < *cap) {
        return 0;
    }
    size_t new_cap = *cap ? *cap * 2 : 8;
    void *grown = realloc(*items, new_cap * item_size);
    if (!grown) {
        return -1;
    }
    *items = grown;
    *cap = new_cap;
    return 0;
}

/*=============================================================================
 * Deep free
 *===========================================================================*/

void yetty_ymsoffice_text_run_free(struct yetty_ymsoffice_text_run *run)
{
    if (!run) {
        return;
    }
    free(run->text);
    run->text = NULL;
    run->text_len = 0;
}

void yetty_ymsoffice_paragraph_free(struct yetty_ymsoffice_paragraph *paragraph)
{
    if (!paragraph) {
        return;
    }
    for (size_t i = 0; i < paragraph->run_count; i++) {
        yetty_ymsoffice_text_run_free(&paragraph->runs[i]);
    }
    free(paragraph->runs);
    paragraph->runs = NULL;
    paragraph->run_count = 0;
    paragraph->run_cap = 0;
}

static void model_cell_free(struct yetty_ymsoffice_table_cell *cell)
{
    for (size_t i = 0; i < cell->paragraph_count; i++) {
        yetty_ymsoffice_paragraph_free(&cell->paragraphs[i]);
    }
    free(cell->paragraphs);
    cell->paragraphs = NULL;
    cell->paragraph_count = 0;
    cell->paragraph_cap = 0;
}

void yetty_ymsoffice_table_free(struct yetty_ymsoffice_table *table)
{
    if (!table) {
        return;
    }
    for (size_t r = 0; r < table->row_count; r++) {
        struct yetty_ymsoffice_table_row *row = &table->rows[r];
        for (size_t c = 0; c < row->cell_count; c++) {
            model_cell_free(&row->cells[c]);
        }
        free(row->cells);
    }
    free(table->rows);
    table->rows = NULL;
    table->row_count = 0;
    table->row_cap = 0;
}

void yetty_ymsoffice_shape_free(struct yetty_ymsoffice_shape *shape)
{
    if (!shape) {
        return;
    }
    free(shape->name);
    shape->name = NULL;
    for (size_t i = 0; i < shape->paragraph_count; i++) {
        yetty_ymsoffice_paragraph_free(&shape->paragraphs[i]);
    }
    free(shape->paragraphs);
    shape->paragraphs = NULL;
    shape->paragraph_count = 0;
    shape->paragraph_cap = 0;
}

static void model_word_free(struct yetty_ymsoffice_word_document *word)
{
    for (size_t i = 0; i < word->block_count; i++) {
        struct yetty_ymsoffice_block *block = &word->blocks[i];
        switch (block->kind) {
        case YETTY_YMSOFFICE_BLOCK_PARAGRAPH:
            yetty_ymsoffice_paragraph_free(&block->paragraph);
            break;
        case YETTY_YMSOFFICE_BLOCK_TABLE:
            yetty_ymsoffice_table_free(&block->table);
            break;
        case YETTY_YMSOFFICE_BLOCK_IMAGE:
            free(block->image.name);
            break;
        }
    }
    free(word->blocks);
    word->blocks = NULL;
    word->block_count = 0;
    word->block_cap = 0;
}

static void model_sheet_document_free(struct yetty_ymsoffice_sheet_document *document)
{
    for (size_t s = 0; s < document->sheet_count; s++) {
        struct yetty_ymsoffice_sheet *sheet = &document->sheets[s];
        free(sheet->name);
        for (size_t i = 0; i < sheet->cell_count; i++) {
            free(sheet->cells[i].text);
            free(sheet->cells[i].formula);
        }
        free(sheet->cells);
    }
    free(document->sheets);
    document->sheets = NULL;
    document->sheet_count = 0;
    document->sheet_cap = 0;
}

static void model_slides_free(struct yetty_ymsoffice_slides_document *document)
{
    for (size_t s = 0; s < document->slide_count; s++) {
        struct yetty_ymsoffice_slide *slide = &document->slides[s];
        for (size_t i = 0; i < slide->shape_count; i++) {
            yetty_ymsoffice_shape_free(&slide->shapes[i]);
        }
        free(slide->shapes);
    }
    free(document->slides);
    document->slides = NULL;
    document->slide_count = 0;
    document->slide_cap = 0;
}

/*=============================================================================
 * Push helpers
 *===========================================================================*/

int yetty_ymsoffice_paragraph_push_run(struct yetty_ymsoffice_paragraph *paragraph,
                                       struct yetty_ymsoffice_text_run run)
{
    if (model_grow((void **)&paragraph->runs, &paragraph->run_cap, paragraph->run_count,
                   sizeof(*paragraph->runs)) < 0) {
        return -1;
    }
    paragraph->runs[paragraph->run_count++] = run;
    return 0;
}

int yetty_ymsoffice_word_push_paragraph(struct yetty_ymsoffice_word_document *word,
                                        struct yetty_ymsoffice_paragraph paragraph)
{
    if (model_grow((void **)&word->blocks, &word->block_cap, word->block_count,
                   sizeof(*word->blocks)) < 0) {
        return -1;
    }
    struct yetty_ymsoffice_block *block = &word->blocks[word->block_count++];
    memset(block, 0, sizeof(*block));
    block->kind = YETTY_YMSOFFICE_BLOCK_PARAGRAPH;
    block->paragraph = paragraph;
    return 0;
}

int yetty_ymsoffice_word_push_table(struct yetty_ymsoffice_word_document *word,
                                    struct yetty_ymsoffice_table table)
{
    if (model_grow((void **)&word->blocks, &word->block_cap, word->block_count,
                   sizeof(*word->blocks)) < 0) {
        return -1;
    }
    struct yetty_ymsoffice_block *block = &word->blocks[word->block_count++];
    memset(block, 0, sizeof(*block));
    block->kind = YETTY_YMSOFFICE_BLOCK_TABLE;
    block->table = table;
    return 0;
}

int yetty_ymsoffice_word_push_image(struct yetty_ymsoffice_word_document *word,
                                    struct yetty_ymsoffice_image image)
{
    if (model_grow((void **)&word->blocks, &word->block_cap, word->block_count,
                   sizeof(*word->blocks)) < 0) {
        return -1;
    }
    struct yetty_ymsoffice_block *block = &word->blocks[word->block_count++];
    memset(block, 0, sizeof(*block));
    block->kind = YETTY_YMSOFFICE_BLOCK_IMAGE;
    block->image = image;
    return 0;
}

int yetty_ymsoffice_table_push_row(struct yetty_ymsoffice_table *table)
{
    if (model_grow((void **)&table->rows, &table->row_cap, table->row_count, sizeof(*table->rows)) <
        0) {
        return -1;
    }
    memset(&table->rows[table->row_count], 0, sizeof(table->rows[0]));
    table->row_count++;
    return 0;
}

int yetty_ymsoffice_table_row_push_cell(struct yetty_ymsoffice_table_row *row)
{
    if (model_grow((void **)&row->cells, &row->cell_cap, row->cell_count, sizeof(*row->cells)) <
        0) {
        return -1;
    }
    struct yetty_ymsoffice_table_cell *cell = &row->cells[row->cell_count];
    memset(cell, 0, sizeof(*cell));
    cell->col_span = 1;
    row->cell_count++;
    return 0;
}

int yetty_ymsoffice_table_cell_push_paragraph(struct yetty_ymsoffice_table_cell *cell,
                                              struct yetty_ymsoffice_paragraph paragraph)
{
    if (model_grow((void **)&cell->paragraphs, &cell->paragraph_cap, cell->paragraph_count,
                   sizeof(*cell->paragraphs)) < 0) {
        return -1;
    }
    cell->paragraphs[cell->paragraph_count++] = paragraph;
    return 0;
}

int yetty_ymsoffice_sheet_document_push_sheet(struct yetty_ymsoffice_sheet_document *document,
                                              char *owned_name)
{
    if (model_grow((void **)&document->sheets, &document->sheet_cap, document->sheet_count,
                   sizeof(*document->sheets)) < 0) {
        return -1;
    }
    struct yetty_ymsoffice_sheet *sheet = &document->sheets[document->sheet_count];
    memset(sheet, 0, sizeof(*sheet));
    sheet->name = owned_name;
    document->sheet_count++;
    return 0;
}

int yetty_ymsoffice_sheet_push_cell(struct yetty_ymsoffice_sheet *sheet,
                                    struct yetty_ymsoffice_sheet_cell cell)
{
    if (model_grow((void **)&sheet->cells, &sheet->cell_cap, sheet->cell_count,
                   sizeof(*sheet->cells)) < 0) {
        return -1;
    }
    sheet->cells[sheet->cell_count++] = cell;
    if (cell.row > sheet->max_row) {
        sheet->max_row = cell.row;
    }
    if (cell.col > sheet->max_col) {
        sheet->max_col = cell.col;
    }
    return 0;
}

int yetty_ymsoffice_slides_push_slide(struct yetty_ymsoffice_slides_document *document)
{
    if (model_grow((void **)&document->slides, &document->slide_cap, document->slide_count,
                   sizeof(*document->slides)) < 0) {
        return -1;
    }
    memset(&document->slides[document->slide_count], 0, sizeof(document->slides[0]));
    document->slide_count++;
    return 0;
}

int yetty_ymsoffice_slide_push_shape(struct yetty_ymsoffice_slide *slide,
                                     struct yetty_ymsoffice_shape shape)
{
    if (model_grow((void **)&slide->shapes, &slide->shape_cap, slide->shape_count,
                   sizeof(*slide->shapes)) < 0) {
        return -1;
    }
    slide->shapes[slide->shape_count++] = shape;
    return 0;
}

int yetty_ymsoffice_shape_push_paragraph(struct yetty_ymsoffice_shape *shape,
                                         struct yetty_ymsoffice_paragraph paragraph)
{
    if (model_grow((void **)&shape->paragraphs, &shape->paragraph_cap, shape->paragraph_count,
                   sizeof(*shape->paragraphs)) < 0) {
        return -1;
    }
    shape->paragraphs[shape->paragraph_count++] = paragraph;
    return 0;
}

/*=============================================================================
 * Document lifecycle
 *===========================================================================*/

struct yetty_ymsoffice_document_ptr_result yetty_ymsoffice_document_create(
    enum yetty_ymsoffice_kind kind)
{
    struct yetty_ymsoffice_document *document = calloc(1, sizeof(struct yetty_ymsoffice_document));
    if (!document) {
        return YETTY_ERR(yetty_ymsoffice_document_ptr, "ymsoffice: out of memory (document)");
    }
    document->kind = kind;
    return YETTY_OK(yetty_ymsoffice_document_ptr, document);
}

void yetty_ymsoffice_document_destroy(struct yetty_ymsoffice_document *document)
{
    if (!document) {
        return;
    }
    switch (document->kind) {
    case YETTY_YMSOFFICE_KIND_WORD:
        model_word_free(&document->word);
        break;
    case YETTY_YMSOFFICE_KIND_SHEET:
        model_sheet_document_free(&document->sheet);
        break;
    case YETTY_YMSOFFICE_KIND_SLIDES:
        model_slides_free(&document->slides);
        break;
    case YETTY_YMSOFFICE_KIND_UNKNOWN:
        break;
    }
    free(document);
}
