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
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ydraw-core/text-drawable-list.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RICH_TYPE_BASE(t) ((uint32_t)(t) & ~YETTY_YDRAW_HAS_ID_FLAG)

struct YETTY_ANNOTATE("class@ygui:ydraw_embed") YETTY_ANNOTATE("parent@ygui:primitive_widget")
    yetty_ygui_ydraw_embed {
    struct yetty_ydraw_drawable_list *buf;
};

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
                                                       uint8_t **heap, size_t *heap_cap)
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
            struct yetty_ydraw_id_result marker = yetty_ydraw_drawable_list_begin_group(dst, gid);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, marker, "ydraw_embed paint: begin_group");
            struct yetty_ycore_void_result body_res = embed_emit_range(
                dst, p + 3 * sizeof(uint32_t), payload, dx, dy, stack, stack_sz, heap, heap_cap);
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
     * ypdf — have scene_min == 0, so this is a no-op for them. */
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ydraw_embed paint: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    float dx = r.min.x - yetty_ydraw_drawable_list_scene_min_x(d->buf);
    float dy = r.min.y - yetty_ydraw_drawable_list_scene_min_y(d->buf);
    const uint8_t *src = (const uint8_t *)yetty_ydraw_drawable_list_data(d->buf);
    size_t src_size = yetty_ydraw_drawable_list_size(d->buf);
    if (!src || src_size == 0) {
        return YETTY_OK_VOID();
    }
    uint8_t stack[4096];
    uint8_t *heap = NULL;
    size_t heap_cap = 0;
    struct yetty_ycore_void_result walk = embed_emit_range(
        ctx->ygrid_drawable_list, src, src_size, dx, dy, stack, sizeof(stack), &heap, &heap_cap);
    free(heap);
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

#include "ydraw_embed.gen.c"
