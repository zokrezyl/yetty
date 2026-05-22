/* drawable-iterator.c — streaming command iterator implementation.
 *
 * See drawable-iterator.h for design overview. The iter runs on the
 * layer's process_input coroutine; it pulls bytes from the
 * Wire-Statemachine and yields the coro when the Wire-Statemachine has
 * no more body bytes right now AND the envelope terminator has not been
 * seen. From the caller's view `iter_next` is synchronous: it returns OK
 * with a complete command, EOE at envelope terminator, or ERR.
 *
 * Each call yields one of two command kinds (see drawable-iterator.h):
 *   - ADD     — populated via the flyweight registry; FAM stride from
 *               ops->size.
 *   - DELETE  — special-cased at the iter level (does not go through
 *               the registry). Wire layout is 12 bytes:
 *                 u32 type = CMD_DELETE  (HAS_ID_FLAG | 1)
 *                 u32 id
 *                 u32 payload_size = 0
 */
#include <stdlib.h>
#include <string.h>

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-iterator.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/ytrace/ytrace.h>

#include "flyweight-internal.h"

#define DRAWABLE_ITER_INIT_SCRATCH_CAP 64u
/* Type word alone — enough to disambiguate DELETE from everything else. */
#define DRAWABLE_ITER_TYPE_BYTES 4u
/* Standard flyweight header — type + payload_size. Sufficient input to
 * ops->size for every flyweight (FAM and fixed-size SDF). */
#define DRAWABLE_ITER_HEADER_BYTES 8u
/* DELETE has a fixed 12-byte wire footprint. */
#define DRAWABLE_ITER_DELETE_BYTES 12u

/* Framed-envelope header — every BIN/OVERLAY payload starts with:
 *   u32 magic = YDRAW_SERIAL_MAGIC ('YPB1')
 *   f32 scene_min_x, scene_min_y, scene_max_x, scene_max_y
 *   u32 byte_count
 * = 24 bytes. The command stream follows. The iter consumes these once
 * at envelope start, validates the magic, stashes scene_bounds, then
 * drops into per-command mode. */
#define DRAWABLE_ITER_ENVELOPE_HEADER_BYTES 24u
#define DRAWABLE_ITER_ENVELOPE_MAGIC 0x31425059u /* 'YPB1' little-endian */

struct yetty_ycore_void_result yetty_ydraw_drawable_iterator_init(
    struct yetty_ydraw_drawable_iterator *iter,
    struct yetty_ywire_wire_statemachine *wire_statemachine,
    const struct yetty_ydraw_flyweight_registry *reg)
{
    if (!iter) {
        return YETTY_ERR(yetty_ycore_void, "iter is NULL");
    }
    if (!wire_statemachine) {
        return YETTY_ERR(yetty_ycore_void, "wire_statemachine is NULL");
    }
    if (!reg) {
        return YETTY_ERR(yetty_ycore_void, "reg is NULL");
    }
    iter->command.kind = YETTY_YDRAW_COMMAND_ADD;
    iter->command.flyweight.data = NULL;
    iter->command.flyweight.ops = NULL;
    iter->scene_min_x = 0.0f;
    iter->scene_min_y = 0.0f;
    iter->scene_max_x = 0.0f;
    iter->scene_max_y = 0.0f;
    iter->scratch = NULL;
    iter->scratch_cap = 0;
    iter->filled = 0;
    iter->total_size = 0;
    iter->header_filled = 0;
    iter->header_done = false;
    iter->wire_statemachine = wire_statemachine;
    iter->reg = reg;
    return YETTY_OK_VOID();
}

void yetty_ydraw_drawable_iterator_destroy(struct yetty_ydraw_drawable_iterator *iter)
{
    if (!iter) {
        return;
    }
    free(iter->scratch);
    iter->scratch = NULL;
    iter->scratch_cap = 0;
    iter->filled = 0;
    iter->total_size = 0;
    iter->header_filled = 0;
    iter->header_done = false;
}

/* Grow scratch to hold at least `want` bytes. Doubling, with a floor of
 * DRAWABLE_ITER_INIT_SCRATCH_CAP. */
static struct yetty_ycore_void_result iter_ensure_scratch(
    struct yetty_ydraw_drawable_iterator *iter, uint32_t want)
{
    if (want <= iter->scratch_cap) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = iter->scratch_cap ? iter->scratch_cap : DRAWABLE_ITER_INIT_SCRATCH_CAP;
    while (new_cap < want) {
        new_cap *= 2;
    }
    uint8_t *grown = realloc(iter->scratch, new_cap);
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "drawable_iter: scratch realloc failed");
    }
    iter->scratch = grown;
    iter->scratch_cap = new_cap;
    return YETTY_OK_VOID();
}

/* Pull bytes from the Wire-Statemachine until `want` is reached OR the
 * statemachine signals end-of-envelope. Yields the current coro when the
 * statemachine has nothing right now but the terminator has not been
 * seen yet. Returns YETTY_OK_VOID(); the caller checks `iter->filled`
 * against `want` to distinguish complete vs. EOE-mid-record. */
static struct yetty_ycore_void_result iter_pull(struct yetty_ydraw_drawable_iterator *iter,
                                                uint32_t want)
{
    while (iter->filled < want) {
        uint32_t need = want - iter->filled;
        struct yetty_ycore_size_result rr = yetty_ywire_wire_statemachine_read(
            iter->wire_statemachine, iter->scratch + iter->filled, need);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "drawable_iter: wire_statemachine read failed");
        if (rr.value > 0) {
            iter->filled += (uint32_t)rr.value;
            continue;
        }
        if (yetty_ywire_wire_statemachine_at_end(iter->wire_statemachine)) {
            return YETTY_OK_VOID();
        }
        yetty_yplatform_coro_yield();
    }
    return YETTY_OK_VOID();
}

/* Pull bytes specifically into the envelope header slot. Same yield
 * semantics as iter_pull. */
static struct yetty_ycore_void_result iter_pull_header(
    struct yetty_ydraw_drawable_iterator *iter)
{
    while (iter->header_filled < DRAWABLE_ITER_ENVELOPE_HEADER_BYTES) {
        uint32_t need = DRAWABLE_ITER_ENVELOPE_HEADER_BYTES - iter->header_filled;
        struct yetty_ycore_size_result rr = yetty_ywire_wire_statemachine_read(
            iter->wire_statemachine, iter->scratch + iter->header_filled, need);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr,
                            "drawable_iter: wire_statemachine read (header) failed");
        if (rr.value > 0) {
            iter->header_filled += (uint32_t)rr.value;
            continue;
        }
        if (yetty_ywire_wire_statemachine_at_end(iter->wire_statemachine)) {
            return YETTY_OK_VOID();
        }
        yetty_yplatform_coro_yield();
    }
    return YETTY_OK_VOID();
}

struct yetty_ydraw_drawable_iterator_status_result yetty_ydraw_drawable_iterator_next(
    struct yetty_ydraw_drawable_iterator *iter)
{
    if (!iter) {
        return YETTY_ERR(yetty_ydraw_drawable_iterator_status, "iter is NULL");
    }

    /* Envelope header: 24 bytes of (magic + scene_bounds + byte_count) at
     * the start of every framed payload. Consume once before falling
     * into per-command mode. */
    if (!iter->header_done) {
        struct yetty_ycore_void_result es =
            iter_ensure_scratch(iter, DRAWABLE_ITER_ENVELOPE_HEADER_BYTES);
        if (YETTY_IS_ERR(es)) {
            return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                             "drawable_iter: ensure_scratch envelope header", es);
        }
        struct yetty_ycore_void_result pr = iter_pull_header(iter);
        if (YETTY_IS_ERR(pr)) {
            return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                             "drawable_iter: envelope header pull", pr);
        }
        if (iter->header_filled < DRAWABLE_ITER_ENVELOPE_HEADER_BYTES) {
            if (iter->header_filled == 0) {
                return YETTY_OK(yetty_ydraw_drawable_iterator_status,
                                YETTY_YDRAW_ITERATOR_EOE);
            }
            return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                             "drawable_iter: truncated envelope header");
        }

        uint32_t magic;
        memcpy(&magic, iter->scratch + 0, 4);
        if (magic != DRAWABLE_ITER_ENVELOPE_MAGIC) {
            yerror("drawable_iter: bad envelope magic 0x%08x (expected 0x%08x); "
                   "header_filled=%u scratch[0..24]=%02x %02x %02x %02x  %02x %02x %02x %02x  "
                   "%02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x  "
                   "%02x %02x %02x %02x",
                   magic, DRAWABLE_ITER_ENVELOPE_MAGIC, iter->header_filled, iter->scratch[0],
                   iter->scratch[1], iter->scratch[2], iter->scratch[3], iter->scratch[4],
                   iter->scratch[5], iter->scratch[6], iter->scratch[7], iter->scratch[8],
                   iter->scratch[9], iter->scratch[10], iter->scratch[11], iter->scratch[12],
                   iter->scratch[13], iter->scratch[14], iter->scratch[15], iter->scratch[16],
                   iter->scratch[17], iter->scratch[18], iter->scratch[19], iter->scratch[20],
                   iter->scratch[21], iter->scratch[22], iter->scratch[23]);
            return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                             "drawable_iter: envelope magic mismatch");
        }
        memcpy(&iter->scene_min_x, iter->scratch + 4, 4);
        memcpy(&iter->scene_min_y, iter->scratch + 8, 4);
        memcpy(&iter->scene_max_x, iter->scratch + 12, 4);
        memcpy(&iter->scene_max_y, iter->scratch + 16, 4);
        iter->header_done = true;
    }

    /* Phase A: resolve total_size for the in-flight record.
     *
     * Pull the 4-byte type word first so we can disambiguate the verb.
     * DELETE is handled at the iter level (no registry); ADD goes
     * through ops->size after pulling the standard 8-byte header. */
    if (iter->total_size == 0) {
        struct yetty_ycore_void_result es = iter_ensure_scratch(iter, DRAWABLE_ITER_TYPE_BYTES);
        if (YETTY_IS_ERR(es)) {
            return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                             "drawable_iter: ensure_scratch type", es);
        }
        struct yetty_ycore_void_result pr = iter_pull(iter, DRAWABLE_ITER_TYPE_BYTES);
        if (YETTY_IS_ERR(pr)) {
            return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                             "drawable_iter: type-word pull", pr);
        }
        if (iter->filled < DRAWABLE_ITER_TYPE_BYTES) {
            if (iter->filled == 0) {
                return YETTY_OK(yetty_ydraw_drawable_iterator_status,
                                YETTY_YDRAW_ITERATOR_EOE);
            }
            return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                             "drawable_iter: truncated type word at envelope end");
        }

        uint32_t drawable_type;
        memcpy(&drawable_type, iter->scratch, sizeof(drawable_type));

        if (drawable_type == YETTY_YDRAW_CMD_DELETE) {
            /* DELETE: fixed 12-byte record, no registry/ops involvement. */
            iter->total_size = DRAWABLE_ITER_DELETE_BYTES;
        } else if (drawable_type == YETTY_YDRAW_CMD_UPDATE) {
            /* UPDATE: HAS_ID layout (type | id | payload_size | payload). The
             * payload is opaque to the iter — the canvas hands it to the
             * targeted prim's factory which interprets per its own schema. */
            struct yetty_ycore_void_result es2 = iter_ensure_scratch(iter, 12u);
            if (YETTY_IS_ERR(es2)) {
                return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                                 "drawable_iter: ensure_scratch update header", es2);
            }
            struct yetty_ycore_void_result pr2 = iter_pull(iter, 12u);
            if (YETTY_IS_ERR(pr2)) {
                return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                                 "drawable_iter: update header pull", pr2);
            }
            if (iter->filled < 12u) {
                return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                                 "drawable_iter: truncated update header at envelope end");
            }
            uint32_t payload_size;
            memcpy(&payload_size, iter->scratch + 8, sizeof(payload_size));
            iter->total_size = 12u + payload_size;
        } else if (drawable_type == YETTY_YDRAW_CMD_GROUP) {
            /* GROUP: HAS_ID layout (type | id | payload_size | payload).
             * Size is determined by the wire bytes — no ops->size needed.
             * NOTE: this path is gated to exact CMD_GROUP, not "any
             * HAS_ID bit set", because legacy figure type-ids
             * (yplot/yimage/ymesh) sit in the 0x80000000+ range too
             * without using the id layout. */
            struct yetty_ycore_void_result es2 = iter_ensure_scratch(iter, 12u);
            if (YETTY_IS_ERR(es2)) {
                return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                                 "drawable_iter: ensure_scratch group header", es2);
            }
            struct yetty_ycore_void_result pr2 = iter_pull(iter, 12u);
            if (YETTY_IS_ERR(pr2)) {
                return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                                 "drawable_iter: group header pull", pr2);
            }
            if (iter->filled < 12u) {
                return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                                 "drawable_iter: truncated group header at envelope end");
            }
            uint32_t payload_size;
            memcpy(&payload_size, iter->scratch + 8, sizeof(payload_size));
            iter->total_size = 12u + payload_size;
        } else {
            /* ADD: pull the 8-byte flyweight header, then ask ops->size
             * for the full stride. */
            struct yetty_ycore_void_result es2 =
                iter_ensure_scratch(iter, DRAWABLE_ITER_HEADER_BYTES);
            if (YETTY_IS_ERR(es2)) {
                return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                                 "drawable_iter: ensure_scratch header", es2);
            }
            struct yetty_ycore_void_result pr2 = iter_pull(iter, DRAWABLE_ITER_HEADER_BYTES);
            if (YETTY_IS_ERR(pr2)) {
                return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                                 "drawable_iter: header pull", pr2);
            }
            if (iter->filled < DRAWABLE_ITER_HEADER_BYTES) {
                return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                                 "drawable_iter: truncated header at envelope end");
            }

            struct yetty_ydraw_drawable_flyweight_ptr_result flyweight_res =
                yetty_ydraw_flyweight_registry_get(iter->reg,
                                                   (const uint32_t *)iter->scratch);
            if (YETTY_IS_ERR(flyweight_res)) {
                yerror("drawable_iter: registry lookup failed for type 0x%08x", drawable_type);
                return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                                 "drawable_iter: flyweight lookup failed", flyweight_res);
            }
            struct yetty_ycore_size_result size_res =
                flyweight_res.value->ops->size((const uint32_t *)iter->scratch);
            if (YETTY_IS_ERR(size_res)) {
                return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                                 "drawable_iter: ops->size failed", size_res);
            }
            if (size_res.value == 0) {
                return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                                 "drawable_iter: ops returned size 0");
            }
            iter->total_size = (uint32_t)size_res.value;
        }

        struct yetty_ycore_void_result es3 = iter_ensure_scratch(iter, iter->total_size);
        if (YETTY_IS_ERR(es3)) {
            return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                             "drawable_iter: ensure_scratch body", es3);
        }
    }

    /* Phase B: fill the body. */
    if (iter->filled < iter->total_size) {
        struct yetty_ycore_void_result pr = iter_pull(iter, iter->total_size);
        if (YETTY_IS_ERR(pr)) {
            return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                             "drawable_iter: body pull", pr);
        }
        if (iter->filled < iter->total_size) {
            uint32_t type_seen = 0;
            memcpy(&type_seen, iter->scratch, sizeof(type_seen));
            yerror("drawable_iter: truncated body at envelope end — "
                   "type=0x%08x filled=%u expected=%u",
                   type_seen, iter->filled, iter->total_size);
            return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                             "drawable_iter: truncated body at envelope end");
        }
    }

    /* Record complete — populate command. */
    uint32_t drawable_type;
    memcpy(&drawable_type, iter->scratch, sizeof(drawable_type));

    if (drawable_type == YETTY_YDRAW_CMD_DELETE) {
        uint32_t id;
        memcpy(&id, iter->scratch + 4, sizeof(id));
        iter->command.kind = YETTY_YDRAW_COMMAND_DELETE;
        iter->command.id = id;
    } else if (drawable_type == YETTY_YDRAW_CMD_UPDATE) {
        uint32_t id;
        uint32_t payload_size;
        memcpy(&id, iter->scratch + 4, sizeof(id));
        memcpy(&payload_size, iter->scratch + 8, sizeof(payload_size));
        iter->command.kind = YETTY_YDRAW_COMMAND_UPDATE;
        iter->command.update.id = id;
        iter->command.update.data = (payload_size > 0) ? (iter->scratch + 12u) : NULL;
        iter->command.update.size = payload_size;
    } else {
        struct yetty_ydraw_drawable_flyweight_ptr_result flyweight_res =
            yetty_ydraw_flyweight_registry_get(iter->reg,
                                               (const uint32_t *)iter->scratch);
        if (YETTY_IS_ERR(flyweight_res)) {
            return YETTY_ERR(yetty_ydraw_drawable_iterator_status,
                             "drawable_iter: re-resolve flyweight failed", flyweight_res);
        }
        iter->command.kind = YETTY_YDRAW_COMMAND_ADD;
        iter->command.flyweight = *flyweight_res.value;
    }

    iter->filled = 0;
    iter->total_size = 0;
    return YETTY_OK(yetty_ydraw_drawable_iterator_status, YETTY_YDRAW_ITERATOR_OK);
}

struct yetty_ycore_size_result yetty_ydraw_drawable_command_parse(
    const struct yetty_ydraw_flyweight_registry *reg, const uint8_t *bytes,
    uint32_t bytes_len, struct yetty_ydraw_command *out_command)
{
    if (!reg || !bytes || !out_command) {
        return YETTY_ERR(yetty_ycore_size, "command_parse: null arg");
    }
    if (bytes_len < DRAWABLE_ITER_TYPE_BYTES) {
        return YETTY_ERR(yetty_ycore_size, "command_parse: buffer < type word");
    }

    uint32_t drawable_type;
    memcpy(&drawable_type, bytes, sizeof(drawable_type));

    if (drawable_type == YETTY_YDRAW_CMD_DELETE) {
        if (bytes_len < DRAWABLE_ITER_DELETE_BYTES) {
            return YETTY_ERR(yetty_ycore_size, "command_parse: DELETE truncated");
        }
        uint32_t id;
        memcpy(&id, bytes + 4, sizeof(id));
        out_command->kind = YETTY_YDRAW_COMMAND_DELETE;
        out_command->id = id;
        return YETTY_OK(yetty_ycore_size, (size_t)DRAWABLE_ITER_DELETE_BYTES);
    }

    if (drawable_type == YETTY_YDRAW_CMD_UPDATE) {
        if (bytes_len < 12u) {
            return YETTY_ERR(yetty_ycore_size, "command_parse: UPDATE header truncated");
        }
        uint32_t id;
        uint32_t payload_size;
        memcpy(&id, bytes + 4, sizeof(id));
        memcpy(&payload_size, bytes + 8, sizeof(payload_size));
        uint32_t total = 12u + payload_size;
        if (bytes_len < total) {
            return YETTY_ERR(yetty_ycore_size, "command_parse: UPDATE body truncated");
        }
        out_command->kind = YETTY_YDRAW_COMMAND_UPDATE;
        out_command->update.id = id;
        out_command->update.data = (payload_size > 0) ? (bytes + 12) : NULL;
        out_command->update.size = payload_size;
        return YETTY_OK(yetty_ycore_size, (size_t)total);
    }

    if (drawable_type == YETTY_YDRAW_CMD_GROUP) {
        /* GROUP layout: type | id | payload_size | payload. Size from
         * the wire bytes; no ops->size involvement. Same gating as the
         * wire iter — exact CMD_GROUP, not generic HAS_ID. */
        if (bytes_len < 12u) {
            return YETTY_ERR(yetty_ycore_size, "command_parse: GROUP header truncated");
        }
        uint32_t payload_size;
        memcpy(&payload_size, bytes + 8, sizeof(payload_size));
        uint32_t total = 12u + payload_size;
        if (bytes_len < total) {
            return YETTY_ERR(yetty_ycore_size, "command_parse: GROUP body truncated");
        }
        out_command->kind = YETTY_YDRAW_COMMAND_ADD;
        out_command->flyweight.data = (const uint32_t *)bytes;
        struct yetty_ydraw_drawable_flyweight_ptr_result fw_res =
            yetty_ydraw_flyweight_registry_get(reg, (const uint32_t *)bytes);
        out_command->flyweight.ops = YETTY_IS_OK(fw_res) ? fw_res.value->ops : NULL;
        return YETTY_OK(yetty_ycore_size, (size_t)total);
    }

    if (bytes_len < DRAWABLE_ITER_HEADER_BYTES) {
        return YETTY_ERR(yetty_ycore_size, "command_parse: FAM header truncated");
    }
    struct yetty_ydraw_drawable_flyweight_ptr_result fw_res =
        yetty_ydraw_flyweight_registry_get(reg, (const uint32_t *)bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, fw_res, "command_parse: registry lookup");
    struct yetty_ycore_size_result size_res = fw_res.value->ops->size((const uint32_t *)bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, size_res, "command_parse: ops->size");
    if (size_res.value == 0 || size_res.value > bytes_len) {
        return YETTY_ERR(yetty_ycore_size, "command_parse: bad size");
    }
    out_command->kind = YETTY_YDRAW_COMMAND_ADD;
    out_command->flyweight.data = (const uint32_t *)bytes;
    out_command->flyweight.ops = fw_res.value->ops;
    return YETTY_OK(yetty_ycore_size, size_res.value);
}
