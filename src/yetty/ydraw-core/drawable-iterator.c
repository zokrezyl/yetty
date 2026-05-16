/* drawable-iterator.c — streaming primitive iterator implementation.
 *
 * See drawable-iterator.h for design overview. The iter runs on the layer's
 * process_input coroutine; it pulls bytes from the Wire Statemachine and yields the
 * coro when the Wire Statemachine has no more body bytes right now AND the envelope
 * terminator has not been seen. From the caller's view `iter_next` is
 * synchronous: it returns OK with a complete prim, EOE at envelope
 * terminator, or ERR.
 */
#include <stdlib.h>
#include <string.h>

#include <yetty/ydraw-core/drawable-iterator.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/ytrace/ytrace.h>

#include "flyweight-internal.h"

#define PRIM_ITER_INIT_SCRATCH_CAP 64u
#define PRIM_ITER_HEADER_BYTES 8u

/* Framed-envelope header — every BIN/OVERLAY payload starts with:
 *   u32 magic = YDRAW_SERIAL_MAGIC ('YPB1')
 *   f32 scene_min_x, scene_min_y, scene_max_x, scene_max_y
 *   u32 byte_count
 * = 24 bytes. The prim stream follows. The iter consumes these once at
 * envelope start, validates the magic, stashes scene_bounds, then drops
 * into per-prim mode. Producers always emit framed output (see
 * ydraw_core_buffer_serialize / _to_base64). */
#define PRIM_ITER_ENVELOPE_HEADER_BYTES 24u
#define PRIM_ITER_ENVELOPE_MAGIC 0x31425059u /* 'YPB1' little-endian */

struct yetty_ycore_void_result yetty_ydraw_drawable_iter_init(
    struct yetty_ydraw_drawable_iter *iter, struct yetty_ywire_wire_statemachine *wire_statemachine,
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
    iter->flyweight.data = NULL;
    iter->flyweight.ops = NULL;
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

void yetty_ydraw_drawable_iter_destroy(struct yetty_ydraw_drawable_iter *iter)
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
static struct yetty_ycore_void_result iter_ensure_scratch(struct yetty_ydraw_drawable_iter *iter,
                                                          uint32_t want)
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

/* Pull bytes from the Wire Statemachine until `want` is reached OR the Wire Statemachine signals
 * end-of-envelope. Yields the current coro when the Wire Statemachine has nothing
 * right now but the terminator has not been seen yet. Returns
 * YETTY_OK_VOID(); the caller checks `iter->filled` against `want` to
 * distinguish complete vs. EOE-mid-prim. */
static struct yetty_ycore_void_result iter_pull(struct yetty_ydraw_drawable_iter *iter,
                                                uint32_t want)
{
    while (iter->filled < want) {
        uint32_t need = want - iter->filled;
        struct yetty_ycore_size_result rr = yetty_ywire_wire_statemachine_read(
            iter->wire_statemachine, iter->scratch + iter->filled, need);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "prim_iter: wire_statemachine read failed");
        if (rr.value > 0) {
            iter->filled += (uint32_t)rr.value;
            continue;
        }
        if (yetty_ywire_wire_statemachine_at_end(iter->wire_statemachine)) {
            return YETTY_OK_VOID();
        }
        /* Wire Statemachine has nothing AND envelope still open — yield. The Wire Statemachine
         * resumes us from process() when more PTY bytes arrive. */
        yetty_yplatform_coro_yield();
    }
    return YETTY_OK_VOID();
}

/* Pull bytes specifically into the envelope header slot. Same yield
 * semantics as iter_pull. */
static struct yetty_ycore_void_result iter_pull_header(struct yetty_ydraw_drawable_iter *iter)
{
    while (iter->header_filled < PRIM_ITER_ENVELOPE_HEADER_BYTES) {
        uint32_t need = PRIM_ITER_ENVELOPE_HEADER_BYTES - iter->header_filled;
        struct yetty_ycore_size_result rr = yetty_ywire_wire_statemachine_read(
            iter->wire_statemachine, iter->scratch + iter->header_filled, need);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr,
                            "prim_iter: wire_statemachine read (header) failed");
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

struct yetty_ydraw_drawable_iter_status_result yetty_ydraw_drawable_iter_next(
    struct yetty_ydraw_drawable_iter *iter)
{
    if (!iter) {
        return YETTY_ERR(yetty_ydraw_drawable_iter_status, "iter is NULL");
    }

    /* Envelope header: 24 bytes of (magic + scene_bounds + byte_count) at
     * the start of every framed payload. Consume it once before falling
     * into per-prim mode. */
    if (!iter->header_done) {
        struct yetty_ycore_void_result es =
            iter_ensure_scratch(iter, PRIM_ITER_ENVELOPE_HEADER_BYTES);
        if (YETTY_IS_ERR(es)) {
            return YETTY_ERR(yetty_ydraw_drawable_iter_status,
                             "prim_iter: ensure_scratch envelope header", es);
        }
        struct yetty_ycore_void_result pr = iter_pull_header(iter);
        if (YETTY_IS_ERR(pr)) {
            return YETTY_ERR(yetty_ydraw_drawable_iter_status, "prim_iter: envelope header pull",
                             pr);
        }
        if (iter->header_filled < PRIM_ITER_ENVELOPE_HEADER_BYTES) {
            /* iter_pull_header already loops on yield; reaching here
             * with header_filled short means at_end is set. */
            if (iter->header_filled == 0) {
                /* Empty envelope — clean EOE. */
                return YETTY_OK(yetty_ydraw_drawable_iter_status, YETTY_PRIM_ITER_EOE);
            }
            return YETTY_ERR(yetty_ydraw_drawable_iter_status,
                             "prim_iter: truncated envelope header");
        }

        uint32_t magic;
        memcpy(&magic, iter->scratch + 0, 4);
        if (magic != PRIM_ITER_ENVELOPE_MAGIC) {
            yerror("prim_iter: bad envelope magic 0x%08x (expected 0x%08x); "
                   "header_filled=%u scratch[0..24]=%02x %02x %02x %02x  %02x %02x %02x %02x  "
                   "%02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x  "
                   "%02x %02x %02x %02x",
                   magic, PRIM_ITER_ENVELOPE_MAGIC, iter->header_filled, iter->scratch[0],
                   iter->scratch[1], iter->scratch[2], iter->scratch[3], iter->scratch[4],
                   iter->scratch[5], iter->scratch[6], iter->scratch[7], iter->scratch[8],
                   iter->scratch[9], iter->scratch[10], iter->scratch[11], iter->scratch[12],
                   iter->scratch[13], iter->scratch[14], iter->scratch[15], iter->scratch[16],
                   iter->scratch[17], iter->scratch[18], iter->scratch[19], iter->scratch[20],
                   iter->scratch[21], iter->scratch[22], iter->scratch[23]);
            return YETTY_ERR(yetty_ydraw_drawable_iter_status,
                             "prim_iter: envelope magic mismatch");
        }
        memcpy(&iter->scene_min_x, iter->scratch + 4, 4);
        memcpy(&iter->scene_min_y, iter->scratch + 8, 4);
        memcpy(&iter->scene_max_x, iter->scratch + 12, 4);
        memcpy(&iter->scene_max_y, iter->scratch + 16, 4);
        /* byte_count at scratch+20 is currently unused on the consumer
         * side — the Wire Statemachine signals end-of-envelope via at_end, so we don't
         * need to count down explicitly. */
        iter->header_done = true;
        /* scratch is now free to reuse for per-prim buffering. */
    }

    /* Phase A: resolve the 8-byte header so we know the prim's stride. */
    if (iter->total_size == 0) {
        /* Need at least one byte; pull as many as the Wire Statemachine gives, up to 8. */
        struct yetty_ycore_void_result es = iter_ensure_scratch(iter, PRIM_ITER_HEADER_BYTES);
        if (YETTY_IS_ERR(es)) {
            return YETTY_ERR(yetty_ydraw_drawable_iter_status, "prim_iter: ensure_scratch header",
                             es);
        }
        struct yetty_ycore_void_result pr = iter_pull(iter, PRIM_ITER_HEADER_BYTES);
        if (YETTY_IS_ERR(pr)) {
            return YETTY_ERR(yetty_ydraw_drawable_iter_status, "prim_iter: header pull", pr);
        }

        if (iter->filled < PRIM_ITER_HEADER_BYTES) {
            /* iter_pull yielded until at_end; partial header at EOE. */
            if (iter->filled == 0) {
                /* Clean tail — envelope ended exactly between prims. */
                return YETTY_OK(yetty_ydraw_drawable_iter_status, YETTY_PRIM_ITER_EOE);
            }
            return YETTY_ERR(yetty_ydraw_drawable_iter_status,
                             "prim_iter: truncated header at envelope end");
        }

        /* Resolve flyweight + stride. */
        uint32_t prim_type;
        memcpy(&prim_type, iter->scratch, sizeof(prim_type));
        struct yetty_ydraw_drawable_flyweight_ptr_result flyweight_res =
            yetty_ydraw_flyweight_registry_get(iter->reg, prim_type,
                                               (const uint32_t *)iter->scratch);
        if (YETTY_IS_ERR(flyweight_res)) {
            yerror("prim_iter: registry lookup failed for type 0x%08x", prim_type);
            return YETTY_ERR(yetty_ydraw_drawable_iter_status, "prim_iter: flyweight lookup failed",
                             flyweight_res);
        }
        struct yetty_ycore_size_result size_res =
            flyweight_res.value->ops->size((const uint32_t *)iter->scratch);
        if (YETTY_IS_ERR(size_res)) {
            return YETTY_ERR(yetty_ydraw_drawable_iter_status, "prim_iter: ops->size failed",
                             size_res);
        }
        if (size_res.value == 0) {
            return YETTY_ERR(yetty_ydraw_drawable_iter_status,
                             "prim_iter: prim ops returned size 0");
        }
        iter->total_size = (uint32_t)size_res.value;
        struct yetty_ycore_void_result es2 = iter_ensure_scratch(iter, iter->total_size);
        if (YETTY_IS_ERR(es2)) {
            return YETTY_ERR(yetty_ydraw_drawable_iter_status, "prim_iter: ensure_scratch body",
                             es2);
        }
    }

    /* Phase B: fill the body. */
    if (iter->filled < iter->total_size) {
        struct yetty_ycore_void_result pr = iter_pull(iter, iter->total_size);
        if (YETTY_IS_ERR(pr)) {
            return YETTY_ERR(yetty_ydraw_drawable_iter_status, "prim_iter: body pull", pr);
        }
        if (iter->filled < iter->total_size) {
            /* iter_pull yielded until at_end; partial prim body. */
            return YETTY_ERR(yetty_ydraw_drawable_iter_status,
                             "prim_iter: truncated body at envelope end");
        }
    }

    /* Prim complete — populate flyweight, reset counters so the next call starts
     * a fresh prim. Caller must consume flyweight before re-calling. */
    uint32_t prim_type;
    memcpy(&prim_type, iter->scratch, sizeof(prim_type));
    struct yetty_ydraw_drawable_flyweight_ptr_result flyweight_res =
        yetty_ydraw_flyweight_registry_get(iter->reg, prim_type, (const uint32_t *)iter->scratch);
    if (YETTY_IS_ERR(flyweight_res)) {
        return YETTY_ERR(yetty_ydraw_drawable_iter_status, "prim_iter: re-resolve flyweight failed",
                         flyweight_res);
    }
    iter->flyweight = *flyweight_res.value;
    iter->filled = 0;
    iter->total_size = 0;
    return YETTY_OK(yetty_ydraw_drawable_iter_status, YETTY_PRIM_ITER_OK);
}
