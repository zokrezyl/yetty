/*
 * opc.c - read-only OPC (ZIP) container reader.
 *
 * Minimal central-directory walk over an in-memory archive plus per-part
 * inflation via zlib (raw deflate). Only what OOXML packages need: stored
 * and deflate entries, 32-bit sizes. zip64 markers are rejected with a
 * clear error instead of being misread.
 */

#include <yetty/ymsoffice/opc.h>

#include <stdlib.h>
#include <string.h>

#include <zlib.h>

/* ZIP on-disk record layout (all little-endian). */
enum {
    OPC_EOCD_SIGNATURE = 0x06054b50,
    OPC_EOCD_MIN_SIZE = 22,
    OPC_EOCD_MAX_COMMENT = 65535,
    OPC_CENTRAL_SIGNATURE = 0x02014b50,
    OPC_CENTRAL_MIN_SIZE = 46,
    OPC_LOCAL_SIGNATURE = 0x04034b50,
    OPC_LOCAL_MIN_SIZE = 30,
    OPC_METHOD_STORED = 0,
    OPC_METHOD_DEFLATE = 8,
};

static uint16_t opc_read_uint16(const uint8_t *src)
{
    return (uint16_t)(src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t opc_read_uint32(const uint8_t *src)
{
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

/* Locate the end-of-central-directory record by scanning backwards over the
 * (possibly present) archive comment. Returns the EOCD offset or SIZE_MAX. */
static size_t opc_find_eocd(const uint8_t *bytes, size_t len)
{
    if (len < OPC_EOCD_MIN_SIZE) {
        return SIZE_MAX;
    }
    size_t scan_limit = OPC_EOCD_MIN_SIZE + OPC_EOCD_MAX_COMMENT;
    size_t lowest = len > scan_limit ? len - scan_limit : 0;
    for (size_t pos = len - OPC_EOCD_MIN_SIZE + 1; pos-- > lowest;) {
        if (opc_read_uint32(bytes + pos) == OPC_EOCD_SIGNATURE) {
            /* The comment length must make the record end exactly at (or
             * before) the archive end — guards against a stray signature
             * inside file data. */
            uint16_t comment_len = opc_read_uint16(bytes + pos + 20);
            if (pos + OPC_EOCD_MIN_SIZE + comment_len <= len) {
                return pos;
            }
        }
    }
    return SIZE_MAX;
}

struct yetty_ymsoffice_opc_ptr_result yetty_ymsoffice_opc_open(const uint8_t *bytes, size_t len)
{
    if (!bytes || len < OPC_EOCD_MIN_SIZE) {
        return YETTY_ERR(yetty_ymsoffice_opc_ptr, "opc: archive too small");
    }

    size_t eocd = opc_find_eocd(bytes, len);
    if (eocd == SIZE_MAX) {
        return YETTY_ERR(yetty_ymsoffice_opc_ptr, "opc: end-of-central-directory not found");
    }

    uint16_t entry_count = opc_read_uint16(bytes + eocd + 10);
    uint32_t directory_size = opc_read_uint32(bytes + eocd + 12);
    uint32_t directory_offset = opc_read_uint32(bytes + eocd + 16);

    if (entry_count == 0xFFFFu || directory_size == 0xFFFFFFFFu ||
        directory_offset == 0xFFFFFFFFu) {
        return YETTY_ERR(yetty_ymsoffice_opc_ptr, "opc: zip64 archives are not supported");
    }
    if ((size_t)directory_offset + directory_size > eocd) {
        return YETTY_ERR(yetty_ymsoffice_opc_ptr, "opc: central directory out of bounds");
    }

    struct yetty_ymsoffice_opc *opc = calloc(1, sizeof(struct yetty_ymsoffice_opc));
    if (!opc) {
        return YETTY_ERR(yetty_ymsoffice_opc_ptr, "opc: out of memory (container)");
    }
    opc->bytes = bytes;
    opc->len = len;
    opc->entries = calloc(entry_count ? entry_count : 1, sizeof(*opc->entries));
    if (!opc->entries) {
        free(opc);
        return YETTY_ERR(yetty_ymsoffice_opc_ptr, "opc: out of memory (entries)");
    }

    size_t pos = directory_offset;
    size_t directory_end = (size_t)directory_offset + directory_size;
    for (uint16_t index = 0; index < entry_count; index++) {
        if (pos + OPC_CENTRAL_MIN_SIZE > directory_end ||
            opc_read_uint32(bytes + pos) != OPC_CENTRAL_SIGNATURE) {
            yetty_ymsoffice_opc_destroy(opc);
            return YETTY_ERR(yetty_ymsoffice_opc_ptr, "opc: corrupt central directory record");
        }

        uint16_t method = opc_read_uint16(bytes + pos + 10);
        uint32_t compressed_size = opc_read_uint32(bytes + pos + 20);
        uint32_t uncompressed_size = opc_read_uint32(bytes + pos + 24);
        uint16_t name_len = opc_read_uint16(bytes + pos + 28);
        uint16_t extra_len = opc_read_uint16(bytes + pos + 30);
        uint16_t comment_len = opc_read_uint16(bytes + pos + 32);
        uint32_t local_header_offset = opc_read_uint32(bytes + pos + 42);

        if (pos + OPC_CENTRAL_MIN_SIZE + name_len > directory_end) {
            yetty_ymsoffice_opc_destroy(opc);
            return YETTY_ERR(yetty_ymsoffice_opc_ptr, "opc: entry name out of bounds");
        }
        if (compressed_size == 0xFFFFFFFFu || uncompressed_size == 0xFFFFFFFFu ||
            local_header_offset == 0xFFFFFFFFu) {
            yetty_ymsoffice_opc_destroy(opc);
            return YETTY_ERR(yetty_ymsoffice_opc_ptr, "opc: zip64 entry is not supported");
        }

        struct yetty_ymsoffice_opc_entry *entry = &opc->entries[opc->entry_count];
        entry->name = malloc((size_t)name_len + 1u);
        if (!entry->name) {
            yetty_ymsoffice_opc_destroy(opc);
            return YETTY_ERR(yetty_ymsoffice_opc_ptr, "opc: out of memory (entry name)");
        }
        memcpy(entry->name, bytes + pos + OPC_CENTRAL_MIN_SIZE, name_len);
        entry->name[name_len] = '\0';
        entry->compressed_size = compressed_size;
        entry->uncompressed_size = uncompressed_size;
        entry->method = method;
        entry->local_header_offset = local_header_offset;
        opc->entry_count++;

        pos += (size_t)OPC_CENTRAL_MIN_SIZE + name_len + extra_len + comment_len;
    }

    return YETTY_OK(yetty_ymsoffice_opc_ptr, opc);
}

void yetty_ymsoffice_opc_destroy(struct yetty_ymsoffice_opc *opc)
{
    if (!opc) {
        return;
    }
    for (size_t i = 0; i < opc->entry_count; i++) {
        free(opc->entries[i].name);
    }
    free(opc->entries);
    free(opc);
}

const struct yetty_ymsoffice_opc_entry *yetty_ymsoffice_opc_find(
    const struct yetty_ymsoffice_opc *opc, const char *part_name)
{
    if (!opc || !part_name) {
        return NULL;
    }
    /* OPC part names may be referenced with a leading '/' (absolute part
     * URI); the ZIP directory stores them without one. */
    if (part_name[0] == '/') {
        part_name++;
    }
    for (size_t i = 0; i < opc->entry_count; i++) {
        if (strcmp(opc->entries[i].name, part_name) == 0) {
            return &opc->entries[i];
        }
    }
    return NULL;
}

static struct yetty_ymsoffice_opc_part_result opc_inflate_entry(
    const struct yetty_ymsoffice_opc *opc, const struct yetty_ymsoffice_opc_entry *entry,
    size_t data_offset)
{
    uint8_t *out = malloc((size_t)entry->uncompressed_size + 1u);
    if (!out) {
        return YETTY_ERR(yetty_ymsoffice_opc_part, "opc: out of memory (part data)");
    }

    if (entry->method == OPC_METHOD_STORED) {
        memcpy(out, opc->bytes + data_offset, entry->uncompressed_size);
    } else {
        z_stream stream = {0};
        /* Negative window bits = raw deflate, no zlib wrapper — the ZIP
         * entry payload carries no header/trailer. */
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            free(out);
            return YETTY_ERR(yetty_ymsoffice_opc_part, "opc: inflate init failed");
        }
        stream.next_in = (Bytef *)(uintptr_t)(opc->bytes + data_offset);
        stream.avail_in = entry->compressed_size;
        stream.next_out = out;
        stream.avail_out = entry->uncompressed_size;
        int inflate_status = inflate(&stream, Z_FINISH);
        uInt produced = (uInt)stream.total_out;
        inflateEnd(&stream);
        if (inflate_status != Z_STREAM_END || produced != entry->uncompressed_size) {
            free(out);
            return YETTY_ERR(yetty_ymsoffice_opc_part, "opc: part decompression failed");
        }
    }

    out[entry->uncompressed_size] = '\0';
    struct yetty_ymsoffice_opc_part part = {
        .data = out,
        .size = entry->uncompressed_size,
    };
    return YETTY_OK(yetty_ymsoffice_opc_part, part);
}

struct yetty_ymsoffice_opc_part_result yetty_ymsoffice_opc_read(
    const struct yetty_ymsoffice_opc *opc, const char *part_name)
{
    if (!opc) {
        return YETTY_ERR(yetty_ymsoffice_opc_part, "opc: container is NULL");
    }
    const struct yetty_ymsoffice_opc_entry *entry = yetty_ymsoffice_opc_find(opc, part_name);
    if (!entry) {
        return YETTY_ERR(yetty_ymsoffice_opc_part, "opc: part not found");
    }
    if (entry->method != OPC_METHOD_STORED && entry->method != OPC_METHOD_DEFLATE) {
        return YETTY_ERR(yetty_ymsoffice_opc_part, "opc: unsupported compression method");
    }

    /* The local header repeats name/extra with its OWN lengths (the extra
     * field commonly differs from the central directory's copy) — the data
     * offset must be computed from the local record. */
    size_t header_offset = entry->local_header_offset;
    if (header_offset + OPC_LOCAL_MIN_SIZE > opc->len ||
        opc_read_uint32(opc->bytes + header_offset) != OPC_LOCAL_SIGNATURE) {
        return YETTY_ERR(yetty_ymsoffice_opc_part, "opc: corrupt local file header");
    }
    uint16_t local_name_len = opc_read_uint16(opc->bytes + header_offset + 26);
    uint16_t local_extra_len = opc_read_uint16(opc->bytes + header_offset + 28);
    size_t data_offset = header_offset + OPC_LOCAL_MIN_SIZE + local_name_len + local_extra_len;
    if (data_offset + entry->compressed_size > opc->len) {
        return YETTY_ERR(yetty_ymsoffice_opc_part, "opc: part data out of bounds");
    }

    return opc_inflate_entry(opc, entry, data_offset);
}

void yetty_ymsoffice_opc_part_destroy(struct yetty_ymsoffice_opc_part *part)
{
    if (!part) {
        return;
    }
    free(part->data);
    part->data = NULL;
    part->size = 0;
}
