// YDraw Drawable-list entry - primitive handler registry implementation (instance-based)

#include <stdlib.h>
#include <yetty/ydraw-core/drawable-list-registry.h>
#include <yetty/ytrace/ytrace.h>

#include "drawable-list-registry-internal.h"

#define DRAWABLE_LIST_REGISTRY_MAX_HANDLERS 8

struct yetty_ydraw_handler_reg {
    uint32_t type_min;
    uint32_t type_max;
    yetty_ydraw_drawable_handler_fn handler;
};

struct yetty_ydraw_drawable_list_registry {
    yetty_ydraw_drawable_handler_fn default_handler;
    struct yetty_ydraw_handler_reg handlers[DRAWABLE_LIST_REGISTRY_MAX_HANDLERS];
    size_t handler_count;
};

struct yetty_ydraw_drawable_list_registry_ptr_result yetty_ydraw_drawable_list_registry_create(void)
{
    struct yetty_ydraw_drawable_list_registry *reg =
        calloc(1, sizeof(struct yetty_ydraw_drawable_list_registry));
    if (!reg) {
        return YETTY_ERR(yetty_ydraw_drawable_list_registry_ptr, "calloc failed");
    }
    return YETTY_OK(yetty_ydraw_drawable_list_registry_ptr, reg);
}

void yetty_ydraw_drawable_list_registry_destroy(struct yetty_ydraw_drawable_list_registry *reg)
{
    free(reg);
}

void yetty_ydraw_drawable_list_registry_set_default(struct yetty_ydraw_drawable_list_registry *reg,
                                                yetty_ydraw_drawable_handler_fn handler)
{
    if (reg) {
        reg->default_handler = handler;
    }
}

struct yetty_ycore_void_result yetty_ydraw_drawable_list_registry_add(
    struct yetty_ydraw_drawable_list_registry *reg, uint32_t type_min, uint32_t type_max,
    yetty_ydraw_drawable_handler_fn handler)
{
    if (!reg) {
        return YETTY_ERR(yetty_ycore_void, "reg is NULL");
    }
    if (reg->handler_count >= DRAWABLE_LIST_REGISTRY_MAX_HANDLERS) {
        return YETTY_ERR(yetty_ycore_void, "max handlers reached");
    }
    if (!handler) {
        return YETTY_ERR(yetty_ycore_void, "handler is NULL");
    }
    reg->handlers[reg->handler_count].type_min = type_min;
    reg->handlers[reg->handler_count].type_max = type_max;
    reg->handlers[reg->handler_count].handler = handler;
    reg->handler_count++;
    return YETTY_OK_VOID();
}

struct yetty_ydraw_drawable_list_entry_ptr_result yetty_ydraw_drawable_list_registry_get(
    const struct yetty_ydraw_drawable_list_registry *reg, const uint32_t *drawable_data)
{
    static struct yetty_ydraw_drawable_list_entry fw;

    if (!reg) {
        return YETTY_ERR(yetty_ydraw_drawable_list_entry_ptr, "registry is NULL");
    }
    if (!drawable_data) {
        return YETTY_ERR(yetty_ydraw_drawable_list_entry_ptr, "drawable_data is NULL");
    }

    uint32_t drawable_type = drawable_data[0];
    ydebug("drawable_list_registry_get: drawable_type=0x%08x handler_count=%zu", drawable_type,
           reg->handler_count);

    // Try default handler first (fast path for SDF types)
    if (reg->default_handler) {
        struct yetty_ydraw_drawable_list_entry_ops_ptr_result ops_res =
            reg->default_handler(drawable_type);
        if (YETTY_IS_OK(ops_res)) {
            ydebug("drawable_list_registry_get: default handler matched drawable_type=0x%08x",
                   drawable_type);
            fw.data = drawable_data;
            fw.ops = ops_res.value;
            return YETTY_OK(yetty_ydraw_drawable_list_entry_ptr, &fw);
        }
    }

    // Fallback to additional handlers by type range
    // TODO: optimize the mapping from id to drawable-list entry
    for (size_t i = 0; i < reg->handler_count; i++) {
        ydebug("drawable_list_registry_get: checking handler[%zu] range=[0x%08x, 0x%08x]", i,
               reg->handlers[i].type_min, reg->handlers[i].type_max);
        if (drawable_type >= reg->handlers[i].type_min &&
            drawable_type <= reg->handlers[i].type_max) {
            struct yetty_ydraw_drawable_list_entry_ops_ptr_result ops_res =
                reg->handlers[i].handler(drawable_type);
            if (YETTY_IS_OK(ops_res)) {
                ydebug("drawable_list_registry_get: handler[%zu] matched drawable_type=0x%08x", i,
                       drawable_type);
                fw.data = drawable_data;
                fw.ops = ops_res.value;
                return YETTY_OK(yetty_ydraw_drawable_list_entry_ptr, &fw);
            }
        }
    }

    ydebug("drawable_list_registry_get: NO HANDLER for drawable_type=0x%08x", drawable_type);
    return YETTY_ERR(yetty_ydraw_drawable_list_entry_ptr, "no handler for drawable_type");
}
