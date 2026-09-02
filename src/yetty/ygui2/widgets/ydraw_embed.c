/*
 * ygui2 ydraw_embed — hosts an app-supplied drawable list as this widget's
 * group body (T6 of the strategy). set_buffer takes ownership; a new buffer
 * marks the skin dirty (one addressed reopen ships the new records).
 *
 * CONTAINMENT: only render LEAVES are accepted — SDF primitives, text
 * runs, wire fonts, and complex creation records. Command records
 * (group/path/update/delete/reserve/paint-z/node-id/zero) are rejected at
 * set_buffer time: a drawable list is a producer program, and splicing
 * one verbatim would let the embed address nodes outside itself, change
 * the batch reservation, or unbalance ambient scopes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/cmds.h>
#include <yetty/ydraw-list/complex.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ydraw-list/font-resource.h>
#include <yetty/ydraw-list/text-drawable-list.h>
#include <yetty/ygui2/defs.h>
#include <yetty/ysdf/types.gen.h>

#include "yetty/gen/impl/ygui2/widget.h"

YETTY_YRESULT_DECLARE(yetty_ygui2_ydraw_embed_ptr, struct yetty_ygui2_ydraw_embed *);
struct yetty_yclass_ptr_result yetty_ygui2_ydraw_embed_class_get(void);
struct yetty_ygui2_ydraw_embed_ptr_result yetty_ygui2_ydraw_embed_from(
    struct yetty_yclass_object *obj);

struct YETTY_ANNOTATE("class@ygui2:ydraw_embed") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_ydraw_embed {
    struct yetty_ydraw_drawable_list *buffer; /* owned */
};

/* One command type the validator rejects. `yetty_ydraw_is_complex` is a
 * bare >= COMPLEX_TYPE_BASE test that ALIASES every HAS_ID/REF command —
 * commands must be filtered out explicitly BEFORE the complex branch. */
static int embed_type_is_command(uint32_t type)
{
    switch (type) {
    case YETTY_YDRAW_CMD_ZERO:
    case YETTY_YDRAW_CMD_DELETE:
    case YETTY_YDRAW_CMD_UPDATE:
    case YETTY_YDRAW_CMD_GROUP:
    case YETTY_YDRAW_CMD_GROUP_REF:
    case YETTY_YDRAW_CMD_PAINT_Z:
    case YETTY_YDRAW_CMD_PAINT_Z_END:
    case YETTY_YDRAW_CMD_NODE_ID:
    case YETTY_YDRAW_CMD_PATH:
    case YETTY_YDRAW_CMD_RESERVE:
        return 1;
    default:
        return 0;
    }
}

/* Walk the serialized records region and verify every record is an
 * allowed render leaf. Returns 1 when the whole region is clean. */
static int embed_records_allowed(const uint8_t *bytes, size_t byte_count)
{
    size_t offset = 0;
    while (offset + 8 <= byte_count) {
        uint32_t type = 0;
        memcpy(&type, bytes + offset, sizeof(type));
        size_t record_size = 0;
        size_t sdf_size = yetty_ysdf_primitive_size(type & ~YETTY_YDRAW_HAS_ID_FLAG);
        if (embed_type_is_command(type)) {
            return 0; /* producer command — never spliced */
        }
        if (sdf_size > 0) {
            /* SDF leaf. An ADDRESSED one ([type|HAS_ID][id][payload…])
             * carries one extra id word over the anonymous base layout —
             * advancing by the base size alone would misread the id word
             * as the next record's type and reject a valid list. */
            record_size = sdf_size;
            if (type & YETTY_YDRAW_HAS_ID_FLAG) {
                record_size += sizeof(uint32_t);
            }
        } else if (type == YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST ||
                   type == YETTY_YDRAW_RESOURCE_FONT || yetty_ydraw_is_complex(type)) {
            uint32_t payload_size = 0;
            memcpy(&payload_size, bytes + offset + 4, sizeof(payload_size));
            record_size = 8u + payload_size;
        } else {
            return 0; /* unknown family */
        }
        if (record_size == 0 || offset + record_size > byte_count) {
            return 0; /* truncated / would overrun */
        }
        offset += record_size;
    }
    return offset == byte_count;
}

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result ydraw_embed_paint(struct yetty_yclass_object *obj,
                                                        struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_ydraw_embed_ptr_result data_res = yetty_ygui2_ydraw_embed_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 embed paint: data");
    struct yetty_ygui2_ydraw_embed *embed = data_res.value;
    if (!embed->buffer) {
        return YETTY_OK_VOID();
    }
    const uint8_t *raw = NULL;
    size_t raw_size = yetty_ydraw_drawable_list_serialize(embed->buffer, &raw);
    if (!raw || raw_size <= YETTY_YDRAW_SERIAL_HEADER_BYTES) {
        return YETTY_OK_VOID();
    }
    struct yetty_ydraw_id_result splice_res = yetty_ydraw_drawable_list_add_prim(
        list, raw + YETTY_YDRAW_SERIAL_HEADER_BYTES, raw_size - YETTY_YDRAW_SERIAL_HEADER_BYTES);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, splice_res, "ygui2 embed paint: splice");
    return YETTY_OK_VOID();
}

/* Takes ownership of `buffer` (destroys the previous one). NULL clears.
 * The buffer is validated leaf-only; a rejected buffer is NOT adopted (the
 * caller keeps ownership) and the previous content stays. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_ydraw_embed_set_buffer(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *buffer)
{
    struct yetty_ygui2_ydraw_embed_ptr_result data_res = yetty_ygui2_ydraw_embed_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 embed_set_buffer: data");
    struct yetty_ygui2_ydraw_embed *embed = data_res.value;
    if (buffer) {
        const uint8_t *raw = NULL;
        size_t raw_size = yetty_ydraw_drawable_list_serialize(buffer, &raw);
        if (raw && raw_size > YETTY_YDRAW_SERIAL_HEADER_BYTES &&
            !embed_records_allowed(raw + YETTY_YDRAW_SERIAL_HEADER_BYTES,
                                   raw_size - YETTY_YDRAW_SERIAL_HEADER_BYTES)) {
            return YETTY_ERR(yetty_ycore_void,
                             "ygui2 embed_set_buffer: buffer contains non-leaf records");
        }
    }
    if (embed->buffer && embed->buffer != buffer) {
        yetty_ydraw_drawable_list_destroy(embed->buffer);
    }
    embed->buffer = buffer;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("override@ygui2:widget:widget_cleanup")
static struct yetty_ycore_void_result ydraw_embed_cleanup(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_ydraw_embed_ptr_result data_res = yetty_ygui2_ydraw_embed_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 embed cleanup: data");
    if (data_res.value->buffer) {
        yetty_ydraw_drawable_list_destroy(data_res.value->buffer);
        data_res.value->buffer = NULL;
    }
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui2/widgets/ydraw_embed.c"
