#ifndef YETTY_YMSOFFICE_MODEL_INTERNAL_H
#define YETTY_YMSOFFICE_MODEL_INTERNAL_H

/*
 * model-internal.h - growable-array helpers the parsers use to build the
 * model. All return 0 on success, -1 on allocation failure; ownership of
 * heap fields inside pushed values transfers to the container.
 */

#include <yetty/ymsoffice/model.h>

int yetty_ymsoffice_paragraph_push_run(struct yetty_ymsoffice_paragraph *paragraph,
                                       struct yetty_ymsoffice_text_run run);

/* Deep-free helpers for values that were built but never pushed. */
void yetty_ymsoffice_text_run_free(struct yetty_ymsoffice_text_run *run);
void yetty_ymsoffice_paragraph_free(struct yetty_ymsoffice_paragraph *paragraph);
void yetty_ymsoffice_table_free(struct yetty_ymsoffice_table *table);
void yetty_ymsoffice_shape_free(struct yetty_ymsoffice_shape *shape);

int yetty_ymsoffice_word_push_paragraph(struct yetty_ymsoffice_word_document *word,
                                        struct yetty_ymsoffice_paragraph paragraph);
int yetty_ymsoffice_word_push_table(struct yetty_ymsoffice_word_document *word,
                                    struct yetty_ymsoffice_table table);
int yetty_ymsoffice_word_push_image(struct yetty_ymsoffice_word_document *word,
                                    struct yetty_ymsoffice_image image);

int yetty_ymsoffice_table_push_row(struct yetty_ymsoffice_table *table);
int yetty_ymsoffice_table_row_push_cell(struct yetty_ymsoffice_table_row *row);
int yetty_ymsoffice_table_cell_push_paragraph(struct yetty_ymsoffice_table_cell *cell,
                                              struct yetty_ymsoffice_paragraph paragraph);

int yetty_ymsoffice_sheet_document_push_sheet(struct yetty_ymsoffice_sheet_document *document,
                                              char *owned_name);
int yetty_ymsoffice_sheet_push_cell(struct yetty_ymsoffice_sheet *sheet,
                                    struct yetty_ymsoffice_sheet_cell cell);

int yetty_ymsoffice_slides_push_slide(struct yetty_ymsoffice_slides_document *document);
int yetty_ymsoffice_slide_push_shape(struct yetty_ymsoffice_slide *slide,
                                     struct yetty_ymsoffice_shape shape);
int yetty_ymsoffice_shape_push_paragraph(struct yetty_ymsoffice_shape *shape,
                                         struct yetty_ymsoffice_paragraph paragraph);

#endif /* YETTY_YMSOFFICE_MODEL_INTERNAL_H */
