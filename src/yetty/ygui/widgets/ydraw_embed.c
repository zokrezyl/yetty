/*
 * ygui-ydraw_embed.c — base widget that hosts a yetty_ydraw_drawable_list
 * and paints it translated by the widget's own rect.min.
 *
 * Walks the source buffer, identifies each primitive by type word,
 * copies it into a scratch slot, translates the position fields by
 * the widget's offset, and appends to ctx->ygrid_drawable_list.
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * ydraw_embed.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_ydraw_embed_ptr, struct yetty_ygui_ydraw_embed *);
struct yetty_yclass_ptr_result yetty_ygui_ydraw_embed_class_get(void);
struct yetty_ygui_ydraw_embed_ptr_result yetty_ygui_ydraw_embed_from(
    struct yetty_yclass_object *obj);

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/composite.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ydraw-core/text-drawable-list.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/handler.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ytrace/ytrace.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RICH_TYPE_BASE(t) ((uint32_t)(t) & ~YETTY_YDRAW_HAS_ID_FLAG)

/* Per-chunk diff state for the retained progressive path: the source-byte
 * hash the chunk last shipped with, and every group id that ship carried
 * (the chunk's own group + any nested content groups), so an unchanged —
 * and therefore unwalked — chunk can still vouch for its ids in the
 * stale-group delete diff. */
struct embed_chunk_state {
    uint64_t content_hash;
    uint32_t *group_ids;
    uint32_t group_id_count;
};

struct YETTY_ANNOTATE("class@ygui:ydraw_embed") YETTY_ANNOTATE("parent@ygui:primitive_widget")
    yetty_ygui_ydraw_embed {
    struct yetty_ydraw_drawable_list *buf;
    /* Retained-figure bookkeeping: the group ids this embed emitted in
     * its previous body. A retained receiver keeps child groups until
     * an explicit CMD_DELETE, so ids present last time but absent from
     * the current buffer (an image element removed by the page) must be
     * deleted or they linger painted forever. */
    uint32_t *retained_group_ids;
    uint32_t retained_group_count;
    uint32_t retained_group_cap;
    /* Progressive ship state: one entry per stable content chunk. */
    struct embed_chunk_state *chunk_states;
    uint32_t chunk_state_count;
    uint32_t chunk_state_cap;
};

static void embed_chunk_states_free(struct yetty_ygui_ydraw_embed *d)
{
    for (uint32_t i = 0; i < d->chunk_state_count; i++) {
        free(d->chunk_states[i].group_ids);
    }
    free(d->chunk_states);
    d->chunk_states = NULL;
    d->chunk_state_count = 0;
    d->chunk_state_cap = 0;
}

/* Group ids collected while walking one retained emission. */
struct embed_group_log {
    uint32_t *ids;
    uint32_t count;
    uint32_t cap;
};

static struct yetty_ycore_void_result embed_group_log_add(struct embed_group_log *log,
                                                          uint32_t group_id)
{
    if (log->count == log->cap) {
        uint32_t new_cap = log->cap ? log->cap * 2 : 16;
        uint32_t *grown = realloc(log->ids, (size_t)new_cap * sizeof(uint32_t));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "ydraw_embed: group log alloc");
        }
        log->ids = grown;
        log->cap = new_cap;
    }
    log->ids[log->count++] = group_id;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui:ydraw_embed:constructor")
static struct yetty_ycore_void_result ctor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_ydraw_embed_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ydraw_embed: super");
    struct yetty_ygui_ydraw_embed_ptr_result d_dr = yetty_ygui_ydraw_embed_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ctor: data_get");
    struct yetty_ygui_ydraw_embed *d = d_dr.value;
    d->buf = NULL;
    d->retained_group_ids = NULL;
    d->retained_group_count = 0;
    d->retained_group_cap = 0;
    d->chunk_states = NULL;
    d->chunk_state_count = 0;
    d->chunk_state_cap = 0;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui:ydraw_embed:destructor")
static struct yetty_ycore_void_result dtor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_ydraw_embed_ptr_result d_dr = yetty_ygui_ydraw_embed_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "dtor: data_get");
    struct yetty_ygui_ydraw_embed *d = d_dr.value;
    if (d->buf) {
        yetty_ydraw_drawable_list_destroy(d->buf);
    }
    d->buf = NULL;
    free(d->retained_group_ids);
    d->retained_group_ids = NULL;
    d->retained_group_count = 0;
    d->retained_group_cap = 0;
    embed_chunk_states_free(d);
    return yetty_ygui_super_void(obj, yetty_ygui_ydraw_embed_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

static size_t prim_size(const uint32_t *prim, size_t remaining)
{
    if (remaining < sizeof(uint32_t)) {
        return 0;
    }
    uint32_t type = prim[0];
    uint32_t base = RICH_TYPE_BASE(type);
    size_t sdf_bytes = yetty_ysdf_primitive_size(base);
    if (sdf_bytes > 0) {
        size_t s = sdf_bytes + ((type & YETTY_YDRAW_HAS_ID_FLAG) ? sizeof(uint32_t) : 0);
        return s <= remaining ? s : 0;
    }
    if (remaining < 2 * sizeof(uint32_t)) {
        return 0;
    }
    uint32_t payload_size = prim[1];
    size_t s = 2 * sizeof(uint32_t) + payload_size;
    return s <= remaining ? s : 0;
}

static void translate_prim(uint32_t *prim, size_t bytes, float dx, float dy)
{
    if (bytes < sizeof(uint32_t)) {
        return;
    }
    uint32_t type = prim[0];
    size_t words = bytes / sizeof(uint32_t);
    if (type < 0x00010000u) {
        return;
    }
    if (yetty_ysdf_primitive_size(RICH_TYPE_BASE(type)) > 0u) {
        size_t shift = (type & YETTY_YDRAW_HAS_ID_FLAG) ? 1u : 0u;
        size_t geom = 5u + shift;
        if (words < geom + 2u) {
            return;
        }
        float *fprim = (float *)prim;
        fprim[geom + 0] += dx;
        fprim[geom + 1] += dy;
        uint32_t base = RICH_TYPE_BASE(type);
        if (base == YETTY_YSDF_SEGMENT && words >= geom + 4u) {
            fprim[geom + 2] += dx;
            fprim[geom + 3] += dy;
        } else if (base == YETTY_YSDF_TRIANGLE && words >= geom + 6u) {
            fprim[geom + 2] += dx;
            fprim[geom + 3] += dy;
            fprim[geom + 4] += dx;
            fprim[geom + 5] += dy;
        }
        return;
    }
    if (type == YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST && words >= 4) {
        float *fprim = (float *)prim;
        fprim[2] += dx;
        fprim[3] += dy;
        return;
    }
    if (type >= 0x80000000u && words >= 4) {
        float *fprim = (float *)prim;
        fprim[2] += dx;
        fprim[3] += dy;
    }
}

/* Size of one TOP-LEVEL record (leaf prim or addressable cmd) at `p`, or 0
 * on a malformed/truncated stream. Mirrors the cmd matching in
 * embed_emit_range — the 0x8XXXXXXX cmd type words must be matched by exact
 * constant BEFORE the generic prim_size path. */
static size_t embed_record_size(const uint8_t *p, size_t remaining)
{
    if (remaining < sizeof(uint32_t)) {
        return 0;
    }
    uint32_t type = ((const uint32_t *)p)[0];
    if (type == YETTY_YDRAW_CMD_ZERO || type == YETTY_YDRAW_CMD_GROUP_REF) {
        if (remaining < 2 * sizeof(uint32_t)) {
            return 0;
        }
        size_t s = 2 * sizeof(uint32_t);
        if (type == YETTY_YDRAW_CMD_ZERO) {
            s += ((const uint32_t *)p)[1];
        }
        return s <= remaining ? s : 0;
    }
    if (type == YETTY_YDRAW_CMD_GROUP || type == YETTY_YDRAW_CMD_UPDATE ||
        type == YETTY_YDRAW_CMD_DELETE) {
        if (remaining < 3 * sizeof(uint32_t)) {
            return 0;
        }
        size_t s = 3 * sizeof(uint32_t);
        if (type != YETTY_YDRAW_CMD_DELETE) {
            s += ((const uint32_t *)p)[2];
        }
        return s <= remaining ? s : 0;
    }
    return prim_size((const uint32_t *)p, remaining);
}

/* FNV-1a over a chunk's SOURCE bytes, seeded with the translation offset —
 * the emitted bytes depend on (dx, dy), so a widget move must miss. */
static uint64_t embed_chunk_hash(const uint8_t *bytes, size_t len, float dx, float dy)
{
    uint64_t hash = 1469598103934665603ull;
    uint32_t seed[2];
    memcpy(&seed[0], &dx, sizeof(float));
    memcpy(&seed[1], &dy, sizeof(float));
    const uint8_t *seed_bytes = (const uint8_t *)seed;
    for (size_t i = 0; i < sizeof(seed); i++) {
        hash = (hash ^ seed_bytes[i]) * 1099511628211ull;
    }
    for (size_t i = 0; i < len; i++) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}

/* Vertical viewport cull for the emit walk. When an embed lives inside a
 * scrollarea (or any nested figure), the emit context narrows fig_clip to the
 * visible rect; every primitive outside it is scissored away at composite time
 * anyway. Emitting the whole tall page every frame — re-translated by the
 * scroll offset — is what makes a long document scroll at a few fps. So we drop
 * primitives that fall entirely outside the visible band up front: the ygrid
 * only ever holds the on-screen slice, so both the emit and the composite go
 * from O(page) to O(viewport). */
struct embed_cull {
    int active;
    float y_min, y_max;        /* visible band (already padded), in emitted coords */
    size_t leaf_total, culled; /* instrumentation */
};

/* Vertical extent [*y_lo, *y_hi] of a TRANSLATED leaf primitive, or 0 if this
 * primitive's bounds can't be read exactly — in which case it is never culled.
 * Cullable: text runs, composites (yimage/yplot/…, the dominant byte cost of a
 * web page), and bare SDF shapes. Kept unconditionally: id-carrying prims,
 * fonts, and unknown types, whose exact bounds we don't read here. Being
 * conservative is the safety property: an over-large extent only keeps an
 * off-screen prim (a tiny waste); an under-large one would clip a visible prim
 * (a bug), so text is padded by more than a full em. A culled prim is provably
 * outside the same AABB the compositor scissors against, so culling can never
 * drop anything that would have been visible. */
static int embed_prim_extent(const uint32_t *prim, size_t bytes, float *y_lo, float *y_hi)
{
    if (bytes < sizeof(uint32_t)) {
        return 0;
    }
    uint32_t type = prim[0];
    if (type == YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST) {
        if (bytes < 5 * sizeof(uint32_t)) {
            return 0;
        }
        const float *f = (const float *)prim;
        float baseline = f[3]; /* y (translated) */
        float font_size = f[4];
        if (!(font_size > 0.0f)) {
            return 0;
        }
        *y_lo = baseline - font_size * 1.5f; /* ascent + slack */
        *y_hi = baseline + font_size * 0.6f; /* descent + slack */
        return 1;
    }
    /* Composite (yimage / yplot / ymesh …). On a web page this is the
     * dominant BYTE cost — every decoded image bitmap rides inline in its
     * record, so a tall page is tens of MB of off-screen image pixels that
     * we re-emit and re-composite every frame while scrolling. The AABB is
     * the first 16 payload bytes (x,y,w,h); translate_prim already shifted
     * x,y. A composite carries the top type bit but is NOT id-carrying —
     * distinguish it from an id'd SDF (top bit + SDF base) by its base
     * having no SDF size. Culling these is the whole point of the exercise. */
    if (yetty_ydraw_is_composite(type) && yetty_ysdf_primitive_size(RICH_TYPE_BASE(type)) == 0u) {
        struct rectangle_result aabb = yetty_ydraw_composite_record_aabb(prim);
        if (YETTY_IS_ERR(aabb)) {
            yetty_ycore_error_destroy(aabb.error);
            return 0;
        }
        *y_lo = aabb.value.min.y;
        *y_hi = aabb.value.max.y;
        return 1;
    }
    if (type & YETTY_YDRAW_HAS_ID_FLAG) {
        return 0; /* group / delta / id'd prim — keep */
    }
    if (yetty_ysdf_primitive_size(RICH_TYPE_BASE(type)) > 0u) {
        struct rectangle_result aabb = yetty_ysdf_drawable_aabb(prim);
        if (YETTY_IS_ERR(aabb)) {
            yetty_ycore_error_destroy(aabb.error);
            return 0;
        }
        *y_lo = aabb.value.min.y;
        *y_hi = aabb.value.max.y;
        return 1;
    }
    return 0; /* font / unknown — keep */
}

/* Walk `len` bytes of a source ydraw stream at `p`, translating every leaf
 * primitive's position by (dx, dy) and appending it to `dst`. CMD_GROUP
 * records are RE-FRAMED into `dst` — a fresh begin_group/end_group around the
 * recursively-translated body — so the group scope (and its stable entity id)
 * survives the embed instead of being flattened away; this is what lets the
 * hosting ygrid replace one element's prims in place. The other addressable
 * cmd records (DELETE, UPDATE, GROUP_REF) carry no translatable coordinates
 * and are copied through verbatim.
 *
 * These cmd type words share the 0x8XXXXXXX range with composite prim
 * type_ids (yimage = 0x80000004, …), so — exactly like the drawable iterator
 * — each cmd is matched by its exact constant BEFORE the generic prim_size /
 * translate path, which would otherwise misread the id word as a payload
 * size. CMD_ZERO is dropped: embedding inlines content, it never owns the
 * hosting canvas. */
static struct yetty_ycore_void_result embed_emit_range(struct yetty_ydraw_drawable_list *dst,
                                                       const uint8_t *p, size_t len, float dx,
                                                       float dy, uint8_t *stack, size_t stack_sz,
                                                       uint8_t **heap, size_t *heap_cap,
                                                       struct embed_cull *cull,
                                                       struct embed_group_log *group_log)
{
    size_t remaining = len;
    while (remaining >= sizeof(uint32_t)) {
        uint32_t type = ((const uint32_t *)p)[0];

        if (type == YETTY_YDRAW_CMD_ZERO) {
            /* 2-word FAM record [type=0 | payload_size]. Skip it. */
            if (remaining < 2 * sizeof(uint32_t)) {
                return YETTY_ERR(yetty_ycore_void, "ydraw_embed paint: truncated CMD_ZERO header");
            }
            size_t s = 2 * sizeof(uint32_t) + ((const uint32_t *)p)[1];
            if (s > remaining) {
                return YETTY_ERR(yetty_ycore_void, "ydraw_embed paint: CMD_ZERO overruns buffer");
            }
            p += s;
            remaining -= s;
            continue;
        }

        if (type == YETTY_YDRAW_CMD_GROUP) {
            /* [type | id | payload_size | body]. Re-open the group in dst and
             * recurse so the body's prims get the same (dx, dy) translation as
             * bare prims. */
            if (remaining < 3 * sizeof(uint32_t)) {
                return YETTY_ERR(yetty_ycore_void, "ydraw_embed paint: truncated CMD_GROUP header");
            }
            uint32_t gid = ((const uint32_t *)p)[1];
            uint32_t payload = ((const uint32_t *)p)[2];
            size_t s = 3 * sizeof(uint32_t) + payload;
            if (s > remaining) {
                return YETTY_ERR(yetty_ycore_void, "ydraw_embed paint: CMD_GROUP overruns buffer");
            }
            if (group_log) {
                struct yetty_ycore_void_result log_res = embed_group_log_add(group_log, gid);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, log_res, "ydraw_embed paint: group log");
            }
            struct yetty_ydraw_id_result marker = yetty_ydraw_drawable_list_begin_group(dst, gid);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, marker, "ydraw_embed paint: begin_group");
            struct yetty_ycore_void_result body_res =
                embed_emit_range(dst, p + 3 * sizeof(uint32_t), payload, dx, dy, stack, stack_sz,
                                 heap, heap_cap, cull, group_log);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ydraw_embed paint: group body");
            struct yetty_ycore_void_result end_res =
                yetty_ydraw_drawable_list_end_group(dst, marker.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, end_res, "ydraw_embed paint: end_group");
            p += s;
            remaining -= s;
            continue;
        }

        if (type == YETTY_YDRAW_CMD_DELETE) {
            if (remaining < 3 * sizeof(uint32_t)) {
                return YETTY_ERR(yetty_ycore_void, "ydraw_embed paint: truncated CMD_DELETE");
            }
            struct yetty_ycore_void_result del_res =
                yetty_ydraw_drawable_list_add_cmd_delete(dst, ((const uint32_t *)p)[1]);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, del_res, "ydraw_embed paint: CMD_DELETE");
            p += 3 * sizeof(uint32_t);
            remaining -= 3 * sizeof(uint32_t);
            continue;
        }

        if (type == YETTY_YDRAW_CMD_GROUP_REF) {
            /* kind=REF — exactly 2 words [type | target_id], no body. */
            if (remaining < 2 * sizeof(uint32_t)) {
                return YETTY_ERR(yetty_ycore_void, "ydraw_embed paint: truncated CMD_GROUP_REF");
            }
            struct yetty_ycore_void_result ref_res =
                yetty_ydraw_drawable_list_add_cmd_group_ref(dst, ((const uint32_t *)p)[1]);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, ref_res, "ydraw_embed paint: CMD_GROUP_REF");
            p += 2 * sizeof(uint32_t);
            remaining -= 2 * sizeof(uint32_t);
            continue;
        }

        if (type == YETTY_YDRAW_CMD_UPDATE) {
            /* [type | id | payload_size | payload] — opaque bytes, copy as-is. */
            if (remaining < 3 * sizeof(uint32_t)) {
                return YETTY_ERR(yetty_ycore_void, "ydraw_embed paint: truncated CMD_UPDATE");
            }
            size_t s = 3 * sizeof(uint32_t) + ((const uint32_t *)p)[2];
            if (s > remaining) {
                return YETTY_ERR(yetty_ycore_void, "ydraw_embed paint: CMD_UPDATE overruns buffer");
            }
            struct yetty_ydraw_id_result cp = yetty_ydraw_drawable_list_add_prim(dst, p, s);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cp, "ydraw_embed paint: CMD_UPDATE copy");
            p += s;
            remaining -= s;
            continue;
        }

        /* Leaf primitive (SDF / TEXT / FONT / composite). Copy into scratch,
         * translate by the widget offset, append. */
        size_t s = prim_size((const uint32_t *)p, remaining);
        if (s == 0 || s > remaining) {
            return YETTY_ERR(yetty_ycore_void, "ydraw_embed paint: malformed primitive stream "
                                               "(unknown type or size overruns buffer)");
        }
        uint8_t *work = stack;
        if (s > stack_sz) {
            if (s > *heap_cap) {
                uint8_t *grown = realloc(*heap, s);
                if (!grown) {
                    return YETTY_ERR(yetty_ycore_void, "ydraw_embed paint: oom");
                }
                *heap = grown;
                *heap_cap = s;
            }
            work = *heap;
        }
        memcpy(work, p, s);
        translate_prim((uint32_t *)work, s, dx, dy);
        if (cull->active) {
            cull->leaf_total++;
            float y_lo, y_hi;
            if (embed_prim_extent((const uint32_t *)work, s, &y_lo, &y_hi) &&
                (y_hi < cull->y_min || y_lo > cull->y_max)) {
                cull->culled++;
                p += s; /* entirely off-screen — scissored anyway, so drop it */
                remaining -= s;
                continue;
            }
        }
        struct yetty_ydraw_id_result ar = yetty_ydraw_drawable_list_add_prim(dst, work, s);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "ydraw_embed paint: add_prim");
        p += s;
        remaining -= s;
    }
    if (remaining != 0) {
        return YETTY_ERR(yetty_ycore_void,
                         "ydraw_embed paint: trailing bytes shorter than a primitive header");
    }
    return YETTY_OK_VOID();
}

/* Progressive retained emission: split the buffer into stable ~16 KiB
 * chunks of whole top-level records, each shipped as its own group with a
 * position-stable id in a reserved band. Re-emission replaces a chunk's
 * content in place at its original paint depth (yscene's group-update
 * semantics), so a body carries ONLY the chunks whose bytes changed — an
 * in-place page update ships O(changed), not O(page). Identity is by
 * position, so an insertion shifts (and re-ships) every later chunk,
 * while in-place mutations — JS text updates, image pixel arrivals —
 * ship one chunk. Unchanged chunks are not walked at all; their group
 * ids are carried forward from the cached state so the stale-group
 * delete diff cannot mistake them for removed content. */
#define EMBED_CHUNK_BUDGET (16u * 1024u)
#define EMBED_CHUNK_INDEX_MAX 0x3FFu

static struct yetty_ycore_void_result embed_emit_retained(
    struct yetty_ygui_ydraw_embed *d, struct yetty_ygui_emit_ctx *ctx, uint32_t widget_id,
    const uint8_t *src, size_t src_size, float dx, float dy, uint8_t *stack, size_t stack_sz,
    uint8_t **heap, size_t *heap_cap, struct embed_cull *cull, uint32_t *changed_out,
    uint32_t *total_out)
{
    /* A body that starts the receiver from scratch (CMD_ZERO / fresh
     * figure) invalidates every cached chunk — ship everything. */
    if (ctx->figure_content_reset) {
        embed_chunk_states_free(d);
        free(d->retained_group_ids);
        d->retained_group_ids = NULL;
        d->retained_group_count = 0;
        d->retained_group_cap = 0;
    }

    /* Pass 1 — chunk boundaries over whole top-level records. */
    struct embed_chunk_range {
        size_t offset;
        size_t length;
    };
    struct embed_chunk_range *ranges = NULL;
    uint32_t range_count = 0;
    uint32_t range_cap = 0;
    size_t cursor = 0;
    while (cursor < src_size) {
        size_t chunk_start = cursor;
        size_t chunk_len = 0;
        while (cursor < src_size &&
               (chunk_len < EMBED_CHUNK_BUDGET || range_count == EMBED_CHUNK_INDEX_MAX)) {
            size_t record_size = embed_record_size(src + cursor, src_size - cursor);
            if (record_size == 0) {
                free(ranges);
                return YETTY_ERR(yetty_ycore_void,
                                 "ydraw_embed retained: malformed record stream");
            }
            cursor += record_size;
            chunk_len += record_size;
        }
        if (range_count == range_cap) {
            uint32_t new_cap = range_cap ? range_cap * 2 : 16;
            struct embed_chunk_range *grown =
                realloc(ranges, (size_t)new_cap * sizeof(struct embed_chunk_range));
            if (!grown) {
                free(ranges);
                return YETTY_ERR(yetty_ycore_void, "ydraw_embed retained: range alloc");
            }
            ranges = grown;
            range_cap = new_cap;
        }
        ranges[range_count].offset = chunk_start;
        ranges[range_count].length = chunk_len;
        range_count++;
    }

    /* Pass 2 — per chunk: unchanged carries its cached ids forward,
     * changed re-ships as a reopened group. New states build in scratch;
     * commit only on full success (a failed walk ships nothing, so the
     * previous state still matches the receiver). */
    struct embed_chunk_state *scratch =
        range_count ? calloc(range_count, sizeof(struct embed_chunk_state)) : NULL;
    if (range_count && !scratch) {
        free(ranges);
        return YETTY_ERR(yetty_ycore_void, "ydraw_embed retained: state alloc");
    }
    struct embed_group_log group_log = {0};
    struct yetty_ycore_void_result walk = YETTY_OK_VOID();
    uint32_t changed = 0;
    for (uint32_t i = 0; i < range_count && YETTY_IS_OK(walk); i++) {
        uint64_t content_hash = embed_chunk_hash(src + ranges[i].offset, ranges[i].length, dx, dy);
        scratch[i].content_hash = content_hash;
        if (i < d->chunk_state_count && d->chunk_states[i].content_hash == content_hash) {
            /* Unchanged — receiver already holds it; vouch for its ids. */
            const struct embed_chunk_state *cached = &d->chunk_states[i];
            for (uint32_t j = 0; j < cached->group_id_count && YETTY_IS_OK(walk); j++) {
                walk = embed_group_log_add(&group_log, cached->group_ids[j]);
            }
            if (YETTY_IS_OK(walk) && cached->group_id_count) {
                scratch[i].group_ids =
                    malloc((size_t)cached->group_id_count * sizeof(uint32_t));
                if (!scratch[i].group_ids) {
                    walk = YETTY_ERR(yetty_ycore_void, "ydraw_embed retained: id copy alloc");
                } else {
                    memcpy(scratch[i].group_ids, cached->group_ids,
                           (size_t)cached->group_id_count * sizeof(uint32_t));
                    scratch[i].group_id_count = cached->group_id_count;
                }
            }
            continue;
        }
        changed++;
        uint32_t chunk_id = 0xFFE00000u | ((widget_id & 0x3FFu) << 10) | i;
        uint32_t log_start = group_log.count;
        walk = embed_group_log_add(&group_log, chunk_id);
        struct yetty_ydraw_id_result marker = {0};
        if (YETTY_IS_OK(walk)) {
            marker = yetty_ydraw_drawable_list_begin_group(ctx->ygrid_drawable_list, chunk_id);
            if (YETTY_IS_ERR(marker)) {
                walk = YETTY_ERR(yetty_ycore_void, "ydraw_embed retained: chunk begin", marker);
            }
        }
        if (YETTY_IS_OK(walk)) {
            walk = embed_emit_range(ctx->ygrid_drawable_list, src + ranges[i].offset,
                                    ranges[i].length, dx, dy, stack, stack_sz, heap, heap_cap,
                                    cull, &group_log);
        }
        if (YETTY_IS_OK(walk)) {
            walk = yetty_ydraw_drawable_list_end_group(ctx->ygrid_drawable_list, marker.value);
        }
        if (YETTY_IS_OK(walk)) {
            uint32_t id_count = group_log.count - log_start;
            scratch[i].group_ids = malloc((size_t)id_count * sizeof(uint32_t));
            if (!scratch[i].group_ids) {
                walk = YETTY_ERR(yetty_ycore_void, "ydraw_embed retained: id slice alloc");
            } else {
                memcpy(scratch[i].group_ids, group_log.ids + log_start,
                       (size_t)id_count * sizeof(uint32_t));
                scratch[i].group_id_count = id_count;
            }
        }
    }
    free(ranges);

    /* Stale groups: ids the receiver holds from the previous body that no
     * current chunk vouches for (removed content groups, trailing chunks
     * of a page that shrank) — delete them. */
    if (YETTY_IS_OK(walk)) {
        for (uint32_t i = 0; i < d->retained_group_count && YETTY_IS_OK(walk); i++) {
            uint32_t prev_id = d->retained_group_ids[i];
            int still_present = 0;
            for (uint32_t j = 0; j < group_log.count; j++) {
                if (group_log.ids[j] == prev_id) {
                    still_present = 1;
                    break;
                }
            }
            if (!still_present) {
                walk = yetty_ydraw_drawable_list_add_cmd_delete(ctx->ygrid_drawable_list, prev_id);
            }
        }
    }

    if (YETTY_IS_ERR(walk)) {
        for (uint32_t i = 0; i < range_count; i++) {
            free(scratch[i].group_ids);
        }
        free(scratch);
        free(group_log.ids);
        return walk;
    }
    embed_chunk_states_free(d);
    d->chunk_states = scratch;
    d->chunk_state_count = range_count;
    d->chunk_state_cap = range_count;
    free(d->retained_group_ids);
    d->retained_group_ids = group_log.ids;
    d->retained_group_count = group_log.count;
    d->retained_group_cap = group_log.cap;
    *changed_out = changed;
    *total_out = range_count;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui:ydraw_embed:widget_paint")
static struct yetty_ycore_void_result paint(struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "ydraw_embed paint: NULL ctx");
    }
    struct yetty_ygui_ydraw_embed_ptr_result d_dr = yetty_ygui_ydraw_embed_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "paint: data_get");
    struct yetty_ygui_ydraw_embed *d = d_dr.value;
    if (!d->buf) {
        return YETTY_OK_VOID();
    }
    /* Map the buffer's scene origin to the widget's top-left. A buffer
     * laid out around a non-zero origin (e.g. ydiagram, whose layout pads
     * the scene bounds and can start slightly negative) would otherwise be
     * offset from the widget rect. Buffers authored from (0,0) — ymarkdown,
     * ypdf — have scene_min == 0, so this is a no-op for them.
     *
     * Inside a RETAINED-scene figure the body is document-space: subtract
     * the figure origin so the buffer lands at the widget's position
     * WITHIN the document, and the receiver's GPU scroll does the rest. */
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ydraw_embed paint: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    float dx = r.min.x - yetty_ydraw_drawable_list_scene_min_x(d->buf);
    float dy = r.min.y - yetty_ydraw_drawable_list_scene_min_y(d->buf);
    if (ctx->figure_retained) {
        dx -= ctx->figure_origin_x;
        dy -= ctx->figure_origin_y;
    }
    const uint8_t *src = (const uint8_t *)yetty_ydraw_drawable_list_data(d->buf);
    size_t src_size = yetty_ydraw_drawable_list_size(d->buf);
    if (!src || src_size == 0) {
        return YETTY_OK_VOID();
    }
    /* Cull to the visible band when a figure ancestor (e.g. a scrollarea) has
     * narrowed the clip. Pad by a chunk so a partially-scrolled row at either
     * edge is always present and small scrolls don't churn the whole emit.
     * A retained figure keeps the WHOLE document on the receiver (that is
     * what makes scrolling free) — never cull it. */
    struct embed_cull cull = {0};
    if (ctx->fig_clip_active && !ctx->figure_retained) {
        const float margin = 256.0f;
        cull.active = 1;
        cull.y_min = ctx->fig_clip.min.y - margin;
        cull.y_max = ctx->fig_clip.max.y + margin;
    }
    size_t before = yetty_ydraw_drawable_list_size(ctx->ygrid_drawable_list);
    uint8_t stack[4096];
    uint8_t *heap = NULL;
    size_t heap_cap = 0;
    uint32_t chunks_changed = 0;
    uint32_t chunks_total = 0;
    struct yetty_ycore_void_result walk;
    if (ctx->figure_retained) {
        struct yetty_ycore_uint32_result id_res = yetty_ygui_widget_id(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "ydraw_embed paint: widget id");
        walk = embed_emit_retained(d, ctx, id_res.value, src, src_size, dx, dy, stack,
                                   sizeof(stack), &heap, &heap_cap, &cull, &chunks_changed,
                                   &chunks_total);
    } else {
        walk = embed_emit_range(ctx->ygrid_drawable_list, src, src_size, dx, dy, stack,
                                sizeof(stack), &heap, &heap_cap, &cull, NULL);
    }
    free(heap);
    size_t after = yetty_ydraw_drawable_list_size(ctx->ygrid_drawable_list);
    ydebug("ydraw_embed cull active=%d retained=%d chunks=%u/%u clip=[%.0f..%.0f] "
           "rect=[%.0f..%.0f] dy=%.0f scene_min=%.0f src=%zuB emitted=%zuB culled=%zu/%zu leaf",
           cull.active, ctx->figure_retained, chunks_changed, chunks_total, cull.y_min, cull.y_max,
           r.min.y, r.max.y, dy, yetty_ydraw_drawable_list_scene_min_y(d->buf), src_size,
           after - before, cull.culled, cull.leaf_total);
    return walk;
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_ydraw_embed_set_buffer(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *buf)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "ydraw_embed_set_buffer: NULL");
    }
    struct yetty_ygui_ydraw_embed_ptr_result d_dr = yetty_ygui_ydraw_embed_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_ydraw_embed_set_buffer: data_get");
    struct yetty_ygui_ydraw_embed *d = d_dr.value;
    if (d->buf && d->buf != buf) {
        yetty_ydraw_drawable_list_destroy(d->buf);
    }
    d->buf = buf;
    return yetty_ygui_widget_set_dirty(obj);
}

#include "yetty/gen/impl/ygui/widgets/ydraw_embed.c"
