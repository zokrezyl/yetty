#ifndef YETTY_YMSOFFICE_OPC_H
#define YETTY_YMSOFFICE_OPC_H

/*
 * opc.h - read-only OPC (Open Packaging Conventions) container reader.
 *
 * docx / xlsx / pptx files are ZIP archives holding XML parts. This reader
 * walks the ZIP central directory of an in-memory archive and inflates
 * individual parts on demand (stored and deflate entries; zip64 archives are
 * rejected). The archive bytes are borrowed — the caller must keep them
 * alive for the lifetime of the opened container.
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ymsoffice_opc_entry {
    char *name; /* part name as stored, e.g. "word/document.xml" */
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t method; /* 0 = stored, 8 = deflate */
    uint32_t local_header_offset;
};

struct yetty_ymsoffice_opc {
    const uint8_t *bytes; /* borrowed archive bytes */
    size_t len;
    struct yetty_ymsoffice_opc_entry *entries;
    size_t entry_count;
};

/* One decompressed part. data is malloc'd and NUL-terminated (the NUL is
 * not counted in size) so text parsers can also treat it as a C string. */
struct yetty_ymsoffice_opc_part {
    uint8_t *data;
    size_t size;
};

YETTY_YRESULT_DECLARE(yetty_ymsoffice_opc_ptr, struct yetty_ymsoffice_opc *);
YETTY_YRESULT_DECLARE(yetty_ymsoffice_opc_part, struct yetty_ymsoffice_opc_part);

/* Parse the central directory. `bytes` is borrowed for the container's
 * lifetime. */
struct yetty_ymsoffice_opc_ptr_result yetty_ymsoffice_opc_open(const uint8_t *bytes, size_t len);

void yetty_ymsoffice_opc_destroy(struct yetty_ymsoffice_opc *opc);

/* Exact-name lookup. Returns NULL when the part does not exist. */
const struct yetty_ymsoffice_opc_entry *yetty_ymsoffice_opc_find(
    const struct yetty_ymsoffice_opc *opc, const char *part_name);

/* Decompress one part into a fresh buffer. */
struct yetty_ymsoffice_opc_part_result yetty_ymsoffice_opc_read(
    const struct yetty_ymsoffice_opc *opc, const char *part_name);

void yetty_ymsoffice_opc_part_destroy(struct yetty_ymsoffice_opc_part *part);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMSOFFICE_OPC_H */
