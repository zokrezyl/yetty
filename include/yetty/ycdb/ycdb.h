#ifndef YETTY_YCDB_H
#define YETTY_YCDB_H

/*
 * ycdb - Constant Database wrapper
 *
 * Abstracts howerj/cdb (portable) and djb/cdb (Unix) behind a C API.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * Reader
 *===========================================================================*/

struct yetty_ycdb_reader;

YETTY_YRESULT_DECLARE(yetty_ycdb_reader, struct yetty_ycdb_reader *);

struct yetty_ycdb_reader_result yetty_ycdb_reader_open(const char *path);

void yetty_ycdb_reader_close(struct yetty_ycdb_reader *r);

/* Look up key. On success, sets *out_data and *out_len.
 * Caller must free *out_data with free().
 * Returns OK with out_data=NULL if key not found.
 */
struct yetty_ycore_void_result yetty_ycdb_reader_get(struct yetty_ycdb_reader *r, const void *key,
                                                     size_t key_len, void **out_data,
                                                     size_t *out_len);

/*=============================================================================
 * Writer
 *===========================================================================*/

struct yetty_ycdb_writer;

YETTY_YRESULT_DECLARE(yetty_ycdb_writer, struct yetty_ycdb_writer *);

struct yetty_ycdb_writer_result yetty_ycdb_writer_create(const char *path);

struct yetty_ycore_void_result yetty_ycdb_writer_add(struct yetty_ycdb_writer *w, const void *key,
                                                     size_t key_len, const void *value,
                                                     size_t value_len);

/* Finalize and close. Writer is freed. */
struct yetty_ycore_void_result yetty_ycdb_writer_finish(struct yetty_ycdb_writer *w);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCDB_H */
