/*
 * ygui-ydraw_embed.c — base widget that hosts a yetty_ydraw_draw_list
 * and paints it translated by the widget's own rect.min.
 *
 * Walks the source buffer, identifies each primitive by type word,
 * copies it into a scratch slot, translates the position fields by
 * the widget's offset, and appends to ctx->ygrid_draw_list.
 *
 * Mirrors src/yetty/ygui-old/ygui_rich.c::emit_buffer_translated —
 * see that file for the wire-format type-id ranges and translation
 * conventions.
 */

#include "../internal.h"

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ydraw-core/text-span-prim.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/ydraw_embed.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RICH_TYPE_BASE(t) ((uint32_t)(t) & ~YETTY_YDRAW_HAS_ID_FLAG)

struct [[clang::annotate("class@ygui:ydraw_embed")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] embed_data {
    struct yetty_ydraw_draw_list *buf;
};

[[clang::annotate("override@ygui:ydraw_embed:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj,
                              yetty_ygui_class_expect(yetty_ygui_ydraw_embed_class_get(),
                                                      "yetty_ygui_ydraw_embed_class_get"),
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ydraw_embed: super");
    struct embed_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_ydraw_embed_class_get(),
                                                         "yetty_ygui_ydraw_embed_class_get"));
    d->buf = NULL;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:ydraw_embed:destructor")]]
static struct yetty_ycore_void_result dtor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct embed_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_ydraw_embed_class_get(),
                                                         "yetty_ygui_ydraw_embed_class_get"));
    if (d->buf) {
        yetty_ydraw_draw_list_destroy(d->buf);
    }
    d->buf = NULL;
    return yetty_ygui_super_void(obj,
                                 yetty_ygui_class_expect(yetty_ygui_ydraw_embed_class_get(),
                                                         "yetty_ygui_ydraw_embed_class_get"),
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
    if (type == YETTY_YDRAW_TYPE_TEXT_SPAN && words >= 4) {
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

[[clang::annotate("override@ygui:ydraw_embed:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "ydraw_embed paint: NULL ctx");
    }
    struct embed_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_ydraw_embed_class_get(),
                                                         "yetty_ygui_ydraw_embed_class_get"));
    if (!d->buf) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float dx = r.min.x, dy = r.min.y;
    const uint8_t *src = (const uint8_t *)yetty_ydraw_draw_list_data(d->buf);
    size_t src_size = yetty_ydraw_draw_list_size(d->buf);
    if (!src || src_size == 0) {
        return YETTY_OK_VOID();
    }
    uint8_t stack[4096];
    uint8_t *heap = NULL;
    size_t heap_cap = 0;
    const uint8_t *p = src;
    size_t remaining = src_size;
    while (remaining >= sizeof(uint32_t)) {
        size_t s = prim_size((const uint32_t *)p, remaining);
        if (s == 0 || s > remaining) {
            free(heap);
            return YETTY_ERR(yetty_ycore_void, "ydraw_embed paint: malformed primitive stream "
                                               "(unknown type or size overruns buffer)");
        }
        uint8_t *work = stack;
        if (s > sizeof(stack)) {
            if (s > heap_cap) {
                uint8_t *g = realloc(heap, s);
                if (!g) {
                    free(heap);
                    return YETTY_ERR(yetty_ycore_void, "ydraw_embed paint: oom");
                }
                heap = g;
                heap_cap = s;
            }
            work = heap;
        }
        memcpy(work, p, s);
        translate_prim((uint32_t *)work, s, dx, dy);
        struct yetty_ydraw_id_result ar =
            yetty_ydraw_draw_list_add_prim(ctx->ygrid_draw_list, work, s);
        if (YETTY_IS_ERR(ar)) {
            free(heap);
            return YETTY_ERR(yetty_ycore_void, "ydraw_embed paint: add_prim", ar);
        }
        p += s;
        remaining -= s;
    }
    if (remaining != 0) {
        free(heap);
        return YETTY_ERR(yetty_ycore_void,
                         "ydraw_embed paint: trailing bytes shorter than a primitive header");
    }
    free(heap);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_ydraw_embed_set_buffer(struct yetty_ygui_object *obj,
                                                                 struct yetty_ydraw_draw_list *buf)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "ydraw_embed_set_buffer: NULL");
    }
    struct embed_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_ydraw_embed_class_get(),
                                                         "yetty_ygui_ydraw_embed_class_get"));
    if (d->buf && d->buf != buf) {
        yetty_ydraw_draw_list_destroy(d->buf);
    }
    d->buf = buf;
    return yetty_ygui_object_set_dirty(obj);
}

#include "ydraw_embed.gen.c"
