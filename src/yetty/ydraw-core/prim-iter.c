/* prim-iter.c — streaming primitive iterator implementation.
 *
 * See prim-iter.h for design overview. The iter is a small state machine:
 *
 *   STATE A — "no header yet" (total_size == 0):
 *     Read up to 8 bytes from the SM. With <8 buffered we don't know the
 *     stride yet. At end-of-envelope mid-header → ERR (truncated).
 *
 *   STATE B — "header in, body in flight" (total_size > 0):
 *     Read up to (total_size - filled) more bytes. End-of-envelope
 *     mid-body → ERR. WOULD_BLOCK when SM returns 0 and at_end is false.
 *
 *   Transition A → B happens once filled >= 8: call registry_get to
 *   resolve the flyweight ops, then ops->size to get the stride. Grow
 *   scratch if needed.
 *
 *   On completion (filled == total_size), populate iter->fw, reset
 *   filled / total_size to 0, return OK. Caller consumes fw before
 *   re-calling.
 */
#include <stdlib.h>
#include <string.h>

#include <yetty/ydraw-core/prim-iter.h>
#include <yetty/yterm/osc-statemachine.h>
#include <yetty/ytrace/ytrace.h>

#include "flyweight-internal.h"

#define PRIM_ITER_INIT_SCRATCH_CAP   64u
#define PRIM_ITER_HEADER_BYTES       8u

/* Framed-envelope header — every BIN/OVERLAY payload starts with:
 *   u32 magic = YPAINT_SERIAL_MAGIC ('YPB1')
 *   f32 scene_min_x, scene_min_y, scene_max_x, scene_max_y
 *   u32 byte_count
 * = 24 bytes. The prim stream follows. The iter consumes these once at
 * envelope start, validates the magic, stashes scene_bounds, then drops
 * into per-prim mode. Producers always emit framed output (see
 * ypaint_core_buffer_serialize / _to_base64). */
#define PRIM_ITER_ENVELOPE_HEADER_BYTES 24u
#define PRIM_ITER_ENVELOPE_MAGIC        0x31425059u  /* 'YPB1' little-endian */

struct yetty_ycore_void_result yetty_ydraw_core_prim_iter_init(
    struct yetty_ydraw_core_prim_iter *iter,
    struct yetty_yterm_osc_statemachine *sm,
    const struct yetty_ydraw_core_flyweight_registry *reg)
{
    if (!iter) {
        return YETTY_ERR(yetty_ycore_void, "iter is NULL");
    }
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "sm is NULL");
    }
    if (!reg) {
        return YETTY_ERR(yetty_ycore_void, "reg is NULL");
    }
    iter->fw.data = NULL;
    iter->fw.ops = NULL;
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
    iter->sm = sm;
    iter->reg = reg;
    return YETTY_OK_VOID();
}

void yetty_ydraw_core_prim_iter_destroy(struct yetty_ydraw_core_prim_iter *iter)
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
 * PRIM_ITER_INIT_SCRATCH_CAP. */
static struct yetty_ycore_void_result iter_ensure_scratch(
    struct yetty_ydraw_core_prim_iter *iter, uint32_t want)
{
    if (want <= iter->scratch_cap) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = iter->scratch_cap ? iter->scratch_cap : PRIM_ITER_INIT_SCRATCH_CAP;
    while (new_cap < want) {
        new_cap *= 2;
    }
    uint8_t *grown = realloc(iter->scratch, new_cap);
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "prim_iter: scratch realloc failed");
    }
    iter->scratch = grown;
    iter->scratch_cap = new_cap;
    return YETTY_OK_VOID();
}

/* Pull bytes from the SM until either `want` is reached or the SM yields.
 * Returns YETTY_OK_VOID() normally; on read error returns the SM error.
 * Updates iter->filled on the way. */
static struct yetty_ycore_void_result iter_pull(
    struct yetty_ydraw_core_prim_iter *iter, uint32_t want)
{
    while (iter->filled < want) {
        uint32_t need = want - iter->filled;
        struct yetty_ycore_size_result rr =
            yetty_yterm_osc_statemachine_read(iter->sm, iter->scratch + iter->filled, need);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "prim_iter: sm read failed");
        if (rr.value == 0) {
            /* SM has nothing right now — caller decides based on at_end. */
            return YETTY_OK_VOID();
        }
        iter->filled += (uint32_t)rr.value;
    }
    return YETTY_OK_VOID();
}

/* Pull bytes specifically into the envelope header slot. Header lives in
 * iter->scratch[0..24); we keep iter->header_filled separate from
 * iter->filled because the latter is reused as the per-prim fill cursor
 * once header parsing is done. */
static struct yetty_ycore_void_result iter_pull_header(
    struct yetty_ydraw_core_prim_iter *iter)
{
    while (iter->header_filled < PRIM_ITER_ENVELOPE_HEADER_BYTES) {
        uint32_t need = PRIM_ITER_ENVELOPE_HEADER_BYTES - iter->header_filled;
        struct yetty_ycore_size_result rr = yetty_yterm_osc_statemachine_read(
            iter->sm, iter->scratch + iter->header_filled, need);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "prim_iter: sm read (header) failed");
        if (rr.value == 0) {
            return YETTY_OK_VOID();
        }
        iter->header_filled += (uint32_t)rr.value;
    }
    return YETTY_OK_VOID();
}

struct yetty_ydraw_core_prim_iter_status_result yetty_ydraw_core_prim_iter_next(
    struct yetty_ydraw_core_prim_iter *iter)
{
    if (!iter) {
        return YETTY_ERR(yetty_ydraw_core_prim_iter_status, "iter is NULL");
    }

    /* Envelope header: 24 bytes of (magic + scene_bounds + byte_count) at
     * the start of every framed payload. Consume it once before falling
     * into per-prim mode. */
    if (!iter->header_done) {
        struct yetty_ycore_void_result es =
            iter_ensure_scratch(iter, PRIM_ITER_ENVELOPE_HEADER_BYTES);
        if (YETTY_IS_ERR(es)) {
            return YETTY_ERR(yetty_ydraw_core_prim_iter_status,
                             "prim_iter: ensure_scratch envelope header", es);
        }
        struct yetty_ycore_void_result pr = iter_pull_header(iter);
        if (YETTY_IS_ERR(pr)) {
            return YETTY_ERR(yetty_ydraw_core_prim_iter_status,
                             "prim_iter: envelope header pull", pr);
        }
        if (iter->header_filled < PRIM_ITER_ENVELOPE_HEADER_BYTES) {
            int at_end = yetty_yterm_osc_statemachine_at_end(iter->sm);
            if (at_end) {
                if (iter->header_filled == 0) {
                    /* Empty envelope — caller treats this as clean DONE. */
                    return YETTY_OK(yetty_ydraw_core_prim_iter_status,
                                    YETTY_PRIM_ITER_DONE);
                }
                return YETTY_ERR(yetty_ydraw_core_prim_iter_status,
                                 "prim_iter: truncated envelope header");
            }
            return YETTY_OK(yetty_ydraw_core_prim_iter_status,
                            YETTY_PRIM_ITER_WOULD_BLOCK);
        }

        uint32_t magic;
        memcpy(&magic, iter->scratch + 0, 4);
        if (magic != PRIM_ITER_ENVELOPE_MAGIC) {
            yerror("prim_iter: bad envelope magic 0x%08x (expected 0x%08x); "
                   "header_filled=%u scratch[0..24]=%02x %02x %02x %02x  %02x %02x %02x %02x  "
                   "%02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x  "
                   "%02x %02x %02x %02x",
                   magic, PRIM_ITER_ENVELOPE_MAGIC, iter->header_filled,
                   iter->scratch[0], iter->scratch[1], iter->scratch[2], iter->scratch[3],
                   iter->scratch[4], iter->scratch[5], iter->scratch[6], iter->scratch[7],
                   iter->scratch[8], iter->scratch[9], iter->scratch[10], iter->scratch[11],
                   iter->scratch[12], iter->scratch[13], iter->scratch[14], iter->scratch[15],
                   iter->scratch[16], iter->scratch[17], iter->scratch[18], iter->scratch[19],
                   iter->scratch[20], iter->scratch[21], iter->scratch[22], iter->scratch[23]);
            return YETTY_ERR(yetty_ydraw_core_prim_iter_status,
                             "prim_iter: envelope magic mismatch");
        }
        memcpy(&iter->scene_min_x, iter->scratch + 4, 4);
        memcpy(&iter->scene_min_y, iter->scratch + 8, 4);
        memcpy(&iter->scene_max_x, iter->scratch + 12, 4);
        memcpy(&iter->scene_max_y, iter->scratch + 16, 4);
        /* byte_count at scratch+20 is currently unused on the consumer
         * side — the SM signals end-of-envelope via at_end, so we don't
         * need to count down explicitly. */
        iter->header_done = true;
        /* scratch is now free to reuse for per-prim buffering. */
    }

    /* Phase A: resolve the 8-byte header so we know the prim's stride. */
    if (iter->total_size == 0) {
        /* Need at least one byte; pull as many as the SM gives, up to 8. */
        struct yetty_ycore_void_result es =
            iter_ensure_scratch(iter, PRIM_ITER_HEADER_BYTES);
        if (YETTY_IS_ERR(es)) {
            return YETTY_ERR(yetty_ydraw_core_prim_iter_status,
                             "prim_iter: ensure_scratch header", es);
        }
        struct yetty_ycore_void_result pr = iter_pull(iter, PRIM_ITER_HEADER_BYTES);
        if (YETTY_IS_ERR(pr)) {
            return YETTY_ERR(yetty_ydraw_core_prim_iter_status,
                             "prim_iter: header pull", pr);
        }

        if (iter->filled < PRIM_ITER_HEADER_BYTES) {
            int at_end = yetty_yterm_osc_statemachine_at_end(iter->sm);
            if (at_end) {
                if (iter->filled == 0) {
                    /* Clean tail. */
                    return YETTY_OK(yetty_ydraw_core_prim_iter_status,
                                    YETTY_PRIM_ITER_DONE);
                }
                /* Some bytes but no full header — truncated stream. */
                return YETTY_ERR(yetty_ydraw_core_prim_iter_status,
                                 "prim_iter: truncated header at envelope end");
            }
            /* Wait for more bytes. */
            return YETTY_OK(yetty_ydraw_core_prim_iter_status,
                            YETTY_PRIM_ITER_WOULD_BLOCK);
        }

        /* Resolve flyweight + stride. */
        uint32_t prim_type;
        memcpy(&prim_type, iter->scratch, sizeof(prim_type));
        struct yetty_ydraw_core_prim_flyweight_ptr_result fw_res =
            yetty_ydraw_core_flyweight_registry_get(
                iter->reg, prim_type, (const uint32_t *)iter->scratch);
        if (YETTY_IS_ERR(fw_res)) {
            yerror("prim_iter: registry lookup failed for type 0x%08x", prim_type);
            return YETTY_ERR(yetty_ydraw_core_prim_iter_status,
                             "prim_iter: flyweight lookup failed", fw_res);
        }
        struct yetty_ycore_size_result size_res =
            fw_res.value->ops->size((const uint32_t *)iter->scratch);
        if (YETTY_IS_ERR(size_res)) {
            return YETTY_ERR(yetty_ydraw_core_prim_iter_status,
                             "prim_iter: ops->size failed", size_res);
        }
        if (size_res.value == 0) {
            return YETTY_ERR(yetty_ydraw_core_prim_iter_status,
                             "prim_iter: prim ops returned size 0");
        }
        iter->total_size = (uint32_t)size_res.value;
        struct yetty_ycore_void_result es2 = iter_ensure_scratch(iter, iter->total_size);
        if (YETTY_IS_ERR(es2)) {
            return YETTY_ERR(yetty_ydraw_core_prim_iter_status,
                             "prim_iter: ensure_scratch body", es2);
        }
    }

    /* Phase B: fill the body. */
    if (iter->filled < iter->total_size) {
        struct yetty_ycore_void_result pr = iter_pull(iter, iter->total_size);
        if (YETTY_IS_ERR(pr)) {
            return YETTY_ERR(yetty_ydraw_core_prim_iter_status,
                             "prim_iter: body pull", pr);
        }
        if (iter->filled < iter->total_size) {
            int at_end = yetty_yterm_osc_statemachine_at_end(iter->sm);
            if (at_end) {
                return YETTY_ERR(yetty_ydraw_core_prim_iter_status,
                                 "prim_iter: truncated body at envelope end");
            }
            return YETTY_OK(yetty_ydraw_core_prim_iter_status,
                            YETTY_PRIM_ITER_WOULD_BLOCK);
        }
    }

    /* Prim complete — populate fw, reset counters so the next call starts
     * a fresh prim. Caller must consume fw before re-calling. */
    uint32_t prim_type;
    memcpy(&prim_type, iter->scratch, sizeof(prim_type));
    struct yetty_ydraw_core_prim_flyweight_ptr_result fw_res =
        yetty_ydraw_core_flyweight_registry_get(
            iter->reg, prim_type, (const uint32_t *)iter->scratch);
    if (YETTY_IS_ERR(fw_res)) {
        return YETTY_ERR(yetty_ydraw_core_prim_iter_status,
                         "prim_iter: re-resolve flyweight failed", fw_res);
    }
    iter->fw = *fw_res.value;
    iter->filled = 0;
    iter->total_size = 0;
    return YETTY_OK(yetty_ydraw_core_prim_iter_status, YETTY_PRIM_ITER_OK);
}
