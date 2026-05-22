// YDraw Buffer - Implementation
//
// The buffer is a single byte stream of primitives. Everything — SDF shapes,
// fonts, text spans, complex prims — is a primitive identified by the type
// word at the start of each entry. Iteration is type-agnostic via the
// flyweight registry (size + aabb come from per-type base ops).
//
// add_font / add_text exist as convenience wrappers that pack the
// flyweight wire layout (font-prim.c / text-span-prim.c) and call
// add_prim — same path SDF emitters take.

#include <stdlib.h>
#include <string.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ydraw-core/font-prim.h>
#include <yetty/ydraw-core/text-span-prim.h>
#include <yetty/ycore/util.h>
#include <yetty/ytrace/ytrace.h>

#include "buffer-internal.h"
#include "flyweight-internal.h"
#include "font-prim-internal.h"
#include "text-span-prim-internal.h"

#define YDRAW_BUFFER_INITIAL_CAPACITY 1024

struct yetty_ydraw_draw_list {
    struct yetty_ycore_named_buffer primitives;

    float scene_min_x, scene_min_y, scene_max_x, scene_max_y;

    /* serialize() scratch — reused across calls, grows on demand. */
    uint8_t *serial_data;
    size_t serial_cap;
};

/* Framed wire format. Magic chosen so it can't look like a valid
 * ysdf primitive header (primitive types are < 256, scene bounds start
 * with a float). Layout:
 *   u32 magic
 *   f32 scene_min_x, scene_min_y, scene_max_x, scene_max_y
 *   u32 byte_count
 *   u8  drawable_bytes[byte_count]
 */
#define YDRAW_SERIAL_MAGIC 0x31425059u /* 'YPB1' little-endian */
#define YDRAW_SERIAL_HEADER_BYTES (4 + 16 + 4)

static struct yetty_ycore_void_result parse_framed_payload(
    struct yetty_ydraw_draw_list *buf, const uint8_t *data, size_t size)
{
    if (size < YDRAW_SERIAL_HEADER_BYTES) {
        return YETTY_ERR(yetty_ycore_void,
                         "framed payload: shorter than header bytes");
    }

    const uint8_t *p = data + 4; /* skip magic, validated by caller */
    memcpy(&buf->scene_min_x, p, 4);
    memcpy(&buf->scene_min_y, p + 4, 4);
    memcpy(&buf->scene_max_x, p + 8, 4);
    memcpy(&buf->scene_max_y, p + 12, 4);
    p += 16;

    uint32_t byte_count;
    memcpy(&byte_count, p, 4);
    p += 4;

    if (byte_count > size - YDRAW_SERIAL_HEADER_BYTES) {
        return YETTY_ERR(yetty_ycore_void,
                         "framed payload: byte_count exceeds remaining bytes");
    }

    if (byte_count > 0) {
        uint8_t *pd = malloc(byte_count);
        if (!pd) {
            return YETTY_ERR(yetty_ycore_void,
                             "framed payload: malloc failed");
        }
        memcpy(pd, p, byte_count);
        free(buf->primitives.buf.data);
        buf->primitives.buf.data = pd;
        buf->primitives.buf.size = byte_count;
        buf->primitives.buf.capacity = byte_count;
    }
    return YETTY_OK_VOID();
}

/* Construct from already-decoded bytes. Owns a private copy. */
struct yetty_ydraw_draw_list_result yetty_ydraw_draw_list_create_from_bytes(
    const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return YETTY_ERR(yetty_ydraw_draw_list, "null or empty bytes");
    }

    struct yetty_ydraw_draw_list *buf = calloc(1, sizeof(struct yetty_ydraw_draw_list));
    if (!buf) {
        return YETTY_ERR(yetty_ydraw_draw_list, "calloc failed");
    }

    /* Framed (magic-tagged) payload = scene_bounds + raw prim stream.
   * Otherwise the bytes are a bare primitive stream (legacy path). */
    uint32_t magic = 0;
    if (len >= 4) {
        memcpy(&magic, data, 4);
    }
    if (len >= 4 && magic == YDRAW_SERIAL_MAGIC) {
        struct yetty_ycore_void_result pr = parse_framed_payload(buf, data, len);
        if (YETTY_IS_ERR(pr)) {
            yetty_ydraw_draw_list_destroy(buf);
            return YETTY_ERR(yetty_ydraw_draw_list,
                             "create_from_bytes: framed payload parse failed", pr);
        }
    } else {
        uint8_t *copy = malloc(len);
        if (!copy) {
            free(buf);
            return YETTY_ERR(yetty_ydraw_draw_list, "malloc failed");
        }
        memcpy(copy, data, len);
        buf->primitives.buf.data = copy;
        buf->primitives.buf.capacity = len;
        buf->primitives.buf.size = len;
    }
    strncpy(buf->primitives.name, "prims", YETTY_YCORE_NAMED_BUFFER_MAX_NAME_LENGTH - 1);
    return YETTY_OK(yetty_ydraw_draw_list, buf);
}

struct yetty_ydraw_draw_list_result yetty_ydraw_draw_list_create_from_base64(
    const struct yetty_ycore_buffer *base64_buf)
{
    if (!base64_buf || !base64_buf->data || base64_buf->size == 0) {
        return YETTY_ERR(yetty_ydraw_draw_list, "null or empty base64 buffer");
    }

    size_t decoded_cap = (base64_buf->size * 3) / 4 + 4;
    uint8_t *decoded = malloc(decoded_cap);
    if (!decoded) {
        return YETTY_ERR(yetty_ydraw_draw_list, "malloc failed");
    }
    size_t decoded_len = yetty_ycore_base64_decode((const char *)base64_buf->data, base64_buf->size,
                                                   (char *)decoded, decoded_cap);

    struct yetty_ydraw_draw_list_result r =
        yetty_ydraw_draw_list_create_from_bytes(decoded, decoded_len);
    free(decoded);
    return r;
}

struct yetty_ydraw_draw_list_result yetty_ydraw_draw_list_config_buffer_create(
    const struct yetty_ydraw_draw_list_config *config)
{
    struct yetty_ydraw_draw_list *buf = calloc(1, sizeof(struct yetty_ydraw_draw_list));
    if (!buf) {
        return YETTY_ERR(yetty_ydraw_draw_list, "calloc failed");
    }

    buf->primitives.buf.data = calloc(1, YDRAW_BUFFER_INITIAL_CAPACITY);
    if (!buf->primitives.buf.data) {
        free(buf);
        return YETTY_ERR(yetty_ydraw_draw_list, "calloc for prims failed");
    }

    buf->primitives.buf.capacity = YDRAW_BUFFER_INITIAL_CAPACITY;
    buf->primitives.buf.size = 0;
    strncpy(buf->primitives.name, "prims", YETTY_YCORE_NAMED_BUFFER_MAX_NAME_LENGTH - 1);

    if (config) {
        buf->scene_min_x = config->scene_min_x;
        buf->scene_min_y = config->scene_min_y;
        buf->scene_max_x = config->scene_max_x;
        buf->scene_max_y = config->scene_max_y;
    }

    return YETTY_OK(yetty_ydraw_draw_list, buf);
}

float yetty_ydraw_draw_list_scene_min_x(const struct yetty_ydraw_draw_list *buf)
{
    return buf ? buf->scene_min_x : 0.0f;
}
float yetty_ydraw_draw_list_scene_min_y(const struct yetty_ydraw_draw_list *buf)
{
    return buf ? buf->scene_min_y : 0.0f;
}
float yetty_ydraw_draw_list_scene_max_x(const struct yetty_ydraw_draw_list *buf)
{
    return buf ? buf->scene_max_x : 0.0f;
}
float yetty_ydraw_draw_list_scene_max_y(const struct yetty_ydraw_draw_list *buf)
{
    return buf ? buf->scene_max_y : 0.0f;
}

void yetty_ydraw_draw_list_destroy(struct yetty_ydraw_draw_list *buf)
{
    if (!buf) {
        return;
    }

    free(buf->primitives.buf.data);
    free(buf->serial_data);
    free(buf);
}

void yetty_ydraw_draw_list_clear(struct yetty_ydraw_draw_list *buf)
{
    if (!buf) {
        yerror("yetty_ydraw_draw_list_clear: buf is NULL");
        return;
    }
    buf->primitives.buf.size = 0;
}

const void *yetty_ydraw_draw_list_data(const struct yetty_ydraw_draw_list *buf)
{
    return buf ? buf->primitives.buf.data : NULL;
}

size_t yetty_ydraw_draw_list_size(const struct yetty_ydraw_draw_list *buf)
{
    return buf ? buf->primitives.buf.size : 0;
}

void yetty_ydraw_draw_list_set_scene_bounds(struct yetty_ydraw_draw_list *buf, float min_x,
                                               float min_y, float max_x, float max_y)
{
    if (!buf) {
        return;
    }
    buf->scene_min_x = min_x;
    buf->scene_min_y = min_y;
    buf->scene_max_x = max_x;
    buf->scene_max_y = max_y;
}

size_t yetty_ydraw_draw_list_serialize(struct yetty_ydraw_draw_list *buf,
                                          const uint8_t **out_data)
{
    if (!buf || !out_data) {
        if (out_data) {
            *out_data = NULL;
        }
        return 0;
    }

    size_t need = YDRAW_SERIAL_HEADER_BYTES + buf->primitives.buf.size;

    if (buf->serial_cap < need) {
        uint8_t *np = realloc(buf->serial_data, need);
        if (!np) {
            *out_data = NULL;
            return 0;
        }
        buf->serial_data = np;
        buf->serial_cap = need;
    }

    uint8_t *p = buf->serial_data;
    uint32_t magic = YDRAW_SERIAL_MAGIC;
    memcpy(p, &magic, 4);
    p += 4;
    memcpy(p, &buf->scene_min_x, 4);
    p += 4;
    memcpy(p, &buf->scene_min_y, 4);
    p += 4;
    memcpy(p, &buf->scene_max_x, 4);
    p += 4;
    memcpy(p, &buf->scene_max_y, 4);
    p += 4;
    uint32_t byte_count = (uint32_t)buf->primitives.buf.size;
    memcpy(p, &byte_count, 4);
    p += 4;
    if (byte_count > 0) {
        memcpy(p, buf->primitives.buf.data, byte_count);
    }

    *out_data = buf->serial_data;
    return need;
}

/* Single-pass base64 encode of the framed wire format. */
static const char YDRAW_B64_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

struct yetty_ycore_buffer_result yetty_ydraw_draw_list_to_base64(
    const struct yetty_ydraw_draw_list *buf)
{
    if (!buf) {
        return YETTY_ERR(yetty_ycore_buffer, "buf is NULL");
    }

    size_t need = YDRAW_SERIAL_HEADER_BYTES + buf->primitives.buf.size;
    size_t cap = ((need + 2) / 3) * 4 + 1;
    char *out = malloc(cap);
    if (!out) {
        return YETTY_ERR(yetty_ycore_buffer, "malloc failed");
    }

    /* Build the framed bytes into a temporary stack-friendly path: write
   * directly into a small buffer and then base64. For very small
   * payloads avoiding malloc here would be nice, but serialize() does
   * the same allocation pattern via serial_data. Just allocate once. */
    uint8_t *raw = malloc(need);
    if (!raw) {
        free(out);
        return YETTY_ERR(yetty_ycore_buffer, "malloc failed");
    }

    uint8_t *p = raw;
    uint32_t magic = YDRAW_SERIAL_MAGIC;
    memcpy(p, &magic, 4);
    p += 4;
    memcpy(p, &buf->scene_min_x, 4);
    p += 4;
    memcpy(p, &buf->scene_min_y, 4);
    p += 4;
    memcpy(p, &buf->scene_max_x, 4);
    p += 4;
    memcpy(p, &buf->scene_max_y, 4);
    p += 4;
    uint32_t byte_count = (uint32_t)buf->primitives.buf.size;
    memcpy(p, &byte_count, 4);
    p += 4;
    if (byte_count > 0) {
        memcpy(p, buf->primitives.buf.data, byte_count);
    }

    size_t olen = 0;
    for (size_t i = 0; i + 3 <= need; i += 3) {
        uint32_t t = ((uint32_t)raw[i] << 16) | ((uint32_t)raw[i + 1] << 8) | (uint32_t)raw[i + 2];
        out[olen++] = YDRAW_B64_ALPHABET[(t >> 18) & 0x3F];
        out[olen++] = YDRAW_B64_ALPHABET[(t >> 12) & 0x3F];
        out[olen++] = YDRAW_B64_ALPHABET[(t >> 6) & 0x3F];
        out[olen++] = YDRAW_B64_ALPHABET[t & 0x3F];
    }
    size_t rem = need % 3;
    if (rem == 1) {
        uint32_t t = (uint32_t)raw[need - 1] << 16;
        out[olen++] = YDRAW_B64_ALPHABET[(t >> 18) & 0x3F];
        out[olen++] = YDRAW_B64_ALPHABET[(t >> 12) & 0x3F];
        out[olen++] = '=';
        out[olen++] = '=';
    } else if (rem == 2) {
        uint32_t t = ((uint32_t)raw[need - 2] << 16) | ((uint32_t)raw[need - 1] << 8);
        out[olen++] = YDRAW_B64_ALPHABET[(t >> 18) & 0x3F];
        out[olen++] = YDRAW_B64_ALPHABET[(t >> 12) & 0x3F];
        out[olen++] = YDRAW_B64_ALPHABET[(t >> 6) & 0x3F];
        out[olen++] = '=';
    }
    out[olen] = '\0';
    free(raw);

    struct yetty_ycore_buffer b = {0};
    b.data = (uint8_t *)out;
    b.size = olen;
    b.capacity = cap;
    return YETTY_OK(yetty_ycore_buffer, b);
}

const struct yetty_ycore_buffer *yetty_ydraw_draw_list_primitives(
    const struct yetty_ydraw_draw_list *buf)
{
    if (!buf) {
        return NULL;
    }
    return &buf->primitives.buf;
}

struct yetty_ydraw_id_result yetty_ydraw_draw_list_add_prim(
    struct yetty_ydraw_draw_list *buf, const void *data, size_t size)
{
    if (!buf) {
        return YETTY_ERR(yetty_ydraw_id, "buf is NULL");
    }
    if (!data) {
        return YETTY_ERR(yetty_ydraw_id, "data is NULL");
    }

    size_t new_size = buf->primitives.buf.size + size;

    // Grow if needed
    if (new_size > buf->primitives.buf.capacity) {
        size_t new_capacity = buf->primitives.buf.capacity * 2;
        if (new_capacity < new_size) {
            new_capacity = new_size;
        }

        uint8_t *new_data = realloc(buf->primitives.buf.data, new_capacity);
        if (!new_data) {
            return YETTY_ERR(yetty_ydraw_id, "realloc failed");
        }

        buf->primitives.buf.data = new_data;
        buf->primitives.buf.capacity = new_capacity;
    }

    uint32_t id = (uint32_t)buf->primitives.buf.size;
    memcpy(buf->primitives.buf.data + buf->primitives.buf.size, data, size);
    buf->primitives.buf.size = new_size;

    return YETTY_OK(yetty_ydraw_id, id);
}

/* GROUP / DELETE producer helpers — see the comment in draw-list.h.
 *
 * Wire layout for both records:
 *   GROUP(id)   : u32 type=CMD_GROUP | u32 id | u32 payload_size | …payload…
 *   DELETE(id)  : u32 type=CMD_DELETE | u32 id | u32 payload_size=0
 *
 * For GROUP, the payload (nested commands / drawables) is appended to
 * the buffer after begin_group returns; end_group then back-patches the
 * 4-byte payload_size slot at marker+8. The 12-byte header itself
 * already counts against the buffer; end_group only patches, it doesn't
 * append.
 */
struct yetty_ydraw_id_result yetty_ydraw_draw_list_begin_group(
    struct yetty_ydraw_draw_list *buf, uint32_t group_id)
{
    if (!buf) {
        return YETTY_ERR(yetty_ydraw_id, "begin_group: buf is NULL");
    }
    uint32_t header[3] = {YETTY_YDRAW_CMD_GROUP, group_id, 0u};
    return yetty_ydraw_draw_list_add_prim(buf, header, sizeof(header));
}

struct yetty_ydraw_id_result yetty_ydraw_draw_list_begin_group_with_rect(
    struct yetty_ydraw_draw_list *buf, uint32_t group_id,
    float x, float y, float w, float h)
{
    if (!buf) {
        return YETTY_ERR(yetty_ydraw_id, "begin_group_with_rect: buf is NULL");
    }
    /* 11 u32 words = 44 bytes:
     *   3 HAS_ID header (type, id, payload_size=0)
     *   4 style words (z_order, fill, stroke, stroke_width) all zero
     *   4 geometry words (x, y, w, h as f32)
     * payload_size starts at 0; _end_group patches it once the body
     * has been written. payload_size will end up = 32 (style + rect)
     * + body bytes. */
    uint32_t header[11] = {0};
    header[0] = YETTY_YDRAW_CMD_GROUP;
    header[1] = group_id;
    header[2] = 0u;  /* payload_size — patched by _end_group */
    /* header[3..6] style words stay zero */
    memcpy(&header[7], &x, sizeof(float));
    memcpy(&header[8], &y, sizeof(float));
    memcpy(&header[9], &w, sizeof(float));
    memcpy(&header[10], &h, sizeof(float));
    return yetty_ydraw_draw_list_add_prim(buf, header, sizeof(header));
}

struct yetty_ycore_void_result yetty_ydraw_draw_list_end_group(
    struct yetty_ydraw_draw_list *buf, uint32_t group_marker_offset)
{
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "end_group: buf is NULL");
    }
    if (group_marker_offset + 12u > buf->primitives.buf.size) {
        return YETTY_ERR(yetty_ycore_void, "end_group: marker out of range");
    }
    uint32_t payload_size = (uint32_t)(buf->primitives.buf.size - group_marker_offset - 12u);
    memcpy(buf->primitives.buf.data + group_marker_offset + 8u, &payload_size,
           sizeof(payload_size));
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_draw_list_add_cmd_delete(
    struct yetty_ydraw_draw_list *buf, uint32_t group_id)
{
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "add_cmd_delete: buf is NULL");
    }
    uint32_t record[3] = {YETTY_YDRAW_CMD_DELETE, group_id, 0u};
    struct yetty_ydraw_id_result r = yetty_ydraw_draw_list_add_prim(buf, record, sizeof(record));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "add_cmd_delete: add_prim failed");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_draw_list_add_cmd_group_ref(
    struct yetty_ydraw_draw_list *buf, uint32_t target_id)
{
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "add_cmd_group_ref: buf is NULL");
    }
    /* kind=REF (bits 31:30 = 10) is encoded in the constant itself; no
     * payload_size word — the record is exactly 2 u32s. */
    uint32_t record[2] = {YETTY_YDRAW_CMD_GROUP_REF, target_id};
    struct yetty_ydraw_id_result r = yetty_ydraw_draw_list_add_prim(buf, record, sizeof(record));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "add_cmd_group_ref: add_prim failed");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_draw_list_add_cmd_update(
    struct yetty_ydraw_draw_list *buf, uint32_t target_id, const void *payload,
    size_t payload_size)
{
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "add_cmd_update: buf is NULL");
    }
    if (payload_size > 0 && !payload) {
        return YETTY_ERR(yetty_ycore_void, "add_cmd_update: payload NULL but size > 0");
    }
    if (payload_size > UINT32_MAX) {
        return YETTY_ERR(yetty_ycore_void, "add_cmd_update: payload too large");
    }
    /* Header (3 u32) then payload bytes, written contiguously so the
     * iterator's [type | id | payload_size | bytes] layout holds. */
    uint32_t header[3] = {YETTY_YDRAW_CMD_UPDATE, target_id, (uint32_t)payload_size};
    size_t total = sizeof(header) + payload_size;
    uint8_t *tmp = malloc(total);
    if (!tmp) {
        return YETTY_ERR(yetty_ycore_void, "add_cmd_update: out of memory");
    }
    memcpy(tmp, header, sizeof(header));
    if (payload_size > 0) {
        memcpy(tmp + sizeof(header), payload, payload_size);
    }
    struct yetty_ydraw_id_result r = yetty_ydraw_draw_list_add_prim(buf, tmp, total);
    free(tmp);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "add_cmd_update: add_prim failed");
    return YETTY_OK_VOID();
}

struct yetty_ydraw_primitive_iter_result yetty_ydraw_draw_list_drawable_first(
    const struct yetty_ydraw_draw_list *buf,
    const struct yetty_ydraw_flyweight_registry *reg)
{
    if (!buf) {
        return YETTY_ERR(yetty_ydraw_primitive_iter, "buf is NULL");
    }
    if (!reg) {
        return YETTY_ERR(yetty_ydraw_primitive_iter, "reg is NULL");
    }
    if (!buf->primitives.buf.data || buf->primitives.buf.size == 0) {
        return YETTY_ERR(yetty_ydraw_primitive_iter, "buffer empty");
    }

    const uint32_t *prim = (const uint32_t *)buf->primitives.buf.data;
    struct yetty_ydraw_drawable_flyweight_ptr_result fw_res =
        yetty_ydraw_flyweight_registry_get(reg, prim);
    YETTY_RETURN_IF_ERR(yetty_ydraw_primitive_iter, fw_res,
                        "drawable_first: registry lookup failed");

    struct yetty_ydraw_primitive_iter iter = {.fw = *fw_res.value};
    return YETTY_OK(yetty_ydraw_primitive_iter, iter);
}

struct yetty_ydraw_primitive_iter_result yetty_ydraw_draw_list_drawable_next(
    const struct yetty_ydraw_draw_list *buf,
    const struct yetty_ydraw_flyweight_registry *reg,
    const struct yetty_ydraw_primitive_iter *iter)
{
    if (!buf) {
        return YETTY_ERR(yetty_ydraw_primitive_iter, "buf is NULL");
    }
    if (!reg) {
        return YETTY_ERR(yetty_ydraw_primitive_iter, "reg is NULL");
    }
    if (!iter || !iter->fw.ops) {
        return YETTY_ERR(yetty_ydraw_primitive_iter, "iter is NULL");
    }

    const uint8_t *base = buf->primitives.buf.data;
    size_t buf_size = buf->primitives.buf.size;
    struct yetty_ycore_size_result size_res = iter->fw.ops->size(iter->fw.data);
    YETTY_RETURN_IF_ERR(yetty_ydraw_primitive_iter, size_res, "drawable_next: size op failed");
    const uint32_t *next = (const uint32_t *)((const uint8_t *)iter->fw.data + size_res.value);
    size_t offset = (const uint8_t *)next - base;

    if (offset >= buf_size) {
        return YETTY_ERR(yetty_ydraw_primitive_iter, "end of buffer");
    }

    struct yetty_ydraw_drawable_flyweight_ptr_result fw_res =
        yetty_ydraw_flyweight_registry_get(reg, next);
    YETTY_RETURN_IF_ERR(yetty_ydraw_primitive_iter, fw_res,
                        "drawable_next: registry lookup failed");

    struct yetty_ydraw_primitive_iter new_iter = {.fw = *fw_res.value};
    return YETTY_OK(yetty_ydraw_primitive_iter, new_iter);
}

/*=============================================================================
 * Producer convenience: pack flyweight FONT / TEXT_SPAN prims into the stream.
 * Same path as add_prim — these just pack the FAM payload first.
 *===========================================================================*/

struct yetty_ycore_int_result yetty_ydraw_draw_list_add_font(
    struct yetty_ydraw_draw_list *buf, const struct yetty_ycore_buffer *ttf_data,
    const char *name)
{
    if (!buf) {
        return YETTY_ERR(yetty_ycore_int, "buf is NULL");
    }
    if (!ttf_data || !ttf_data->data || ttf_data->size == 0) {
        return YETTY_ERR(yetty_ycore_int, "ttf_data is empty");
    }

    uint32_t name_len = name ? (uint32_t)strlen(name) : 0;
    uint32_t ttf_len = (uint32_t)ttf_data->size;
    size_t drawable_size = yetty_ydraw_font_drawable_size_for(name_len, ttf_len);

    uint8_t *staging = malloc(drawable_size);
    if (!staging) {
        return YETTY_ERR(yetty_ycore_int, "alloc failed");
    }

    /* font_id is producer-assigned; we use the byte-offset-based primitive
   * count would-be, but the canonical id is just the next consecutive one
   * — producers have always referenced fonts by 0,1,2,… so we keep that.
   * The receiver builds its own (buf_font_id → MSDF font*) map. */
    /* Walk existing prims to count fonts so far. Simple, infrequent. */
    int next_id = 0;
    const uint8_t *p = buf->primitives.buf.data;
    const uint8_t *end = p + buf->primitives.buf.size;
    while (p + 8 <= end) {
        uint32_t t, ps;
        memcpy(&t, p, 4);
        memcpy(&ps, p + 4, 4);
        if (t == YETTY_YDRAW_TYPE_FONT) {
            next_id++;
        }
        /* Walk by FAM size for flyweight/complex; otherwise stop — we'd need
     * the registry to walk SDF prims correctly. We only need to count
     * FONTs that precede this insertion, and producers add fonts before
     * other prims in practice (PDF, markdown). If a producer interleaves,
     * they should pass an explicit id (future API). */
        if (t >= 0x40000000u) {
            p += 8 + ps;
        } else {
            break;
        }
    }

    yetty_ydraw_font_drawable_write(staging, (int32_t)next_id, name, name_len, ttf_data->data,
                                      ttf_len);

    struct yetty_ydraw_id_result r =
        yetty_ydraw_draw_list_add_prim(buf, staging, drawable_size);
    free(staging);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, r, "add_font: add_prim failed");
    return YETTY_OK(yetty_ycore_int, next_id);
}

struct yetty_ycore_int_result yetty_ydraw_draw_list_add_font_ref(
    struct yetty_ydraw_draw_list *buf, const char *hex16)
{
    if (!buf) {
        return YETTY_ERR(yetty_ycore_int, "buf is NULL");
    }
    if (!hex16) {
        return YETTY_ERR(yetty_ycore_int, "hex16 is NULL");
    }

    /* The receiver distinguishes "ref-by-hash" from "real declaration"
   * purely by ttf_len == 0, and reads the 16-hex-char hash out of the
   * name field. Anything other than exactly 16 chars would be ambiguous
   * with prior PDF tag conventions ("/F1" etc.), so reject early. */
    uint32_t name_len = (uint32_t)strlen(hex16);
    if (name_len != 16) {
        return YETTY_ERR(yetty_ycore_int, "hex16 must be exactly 16 hex chars");
    }

    size_t drawable_size = yetty_ydraw_font_drawable_size_for(name_len, 0);
    uint8_t *staging = malloc(drawable_size);
    if (!staging) {
        return YETTY_ERR(yetty_ycore_int, "alloc failed");
    }

    /* Same envelope-local id discovery as add_font: count existing FONTs. */
    int next_id = 0;
    const uint8_t *p = buf->primitives.buf.data;
    const uint8_t *end = p + buf->primitives.buf.size;
    while (p + 8 <= end) {
        uint32_t t, ps;
        memcpy(&t, p, 4);
        memcpy(&ps, p + 4, 4);
        if (t == YETTY_YDRAW_TYPE_FONT) {
            next_id++;
        }
        if (t >= 0x40000000u) {
            p += 8 + ps;
        } else {
            break;
        }
    }

    yetty_ydraw_font_drawable_write(staging, (int32_t)next_id, hex16, name_len, NULL, 0);

    struct yetty_ydraw_id_result r =
        yetty_ydraw_draw_list_add_prim(buf, staging, drawable_size);
    free(staging);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, r, "add_font_ref: add_prim failed");
    return YETTY_OK(yetty_ycore_int, next_id);
}

struct yetty_ycore_void_result yetty_ydraw_draw_list_add_text_full(
    struct yetty_ydraw_draw_list *buf, float x, float y, const struct yetty_ycore_buffer *text,
    float font_size, uint32_t color, uint32_t layer, int32_t font_id, float rotation,
    float char_spacing, float word_spacing)
{
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "buf is NULL");
    }
    if (!text || !text->data || text->size == 0) {
        return YETTY_ERR(yetty_ycore_void, "text is empty");
    }

    uint32_t text_len = (uint32_t)text->size;
    size_t drawable_size = yetty_ydraw_text_span_drawable_size_for(text_len);

    uint8_t *staging = malloc(drawable_size);
    if (!staging) {
        return YETTY_ERR(yetty_ycore_void, "alloc failed");
    }

    yetty_ydraw_text_span_drawable_write_full(staging, x, y, font_size, rotation, color, layer,
                                                font_id, (const char *)text->data, text_len,
                                                char_spacing, word_spacing);

    struct yetty_ydraw_id_result r =
        yetty_ydraw_draw_list_add_prim(buf, staging, drawable_size);
    free(staging);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "add_text: add_prim failed");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_draw_list_add_text(
    struct yetty_ydraw_draw_list *buf, float x, float y, const struct yetty_ycore_buffer *text,
    float font_size, uint32_t color, uint32_t layer, int32_t font_id, float rotation)
{
    return yetty_ydraw_draw_list_add_text_full(buf, x, y, text, font_size, color, layer, font_id,
                                                  rotation, 0.0f, 0.0f);
}
