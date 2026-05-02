// YPaint Flyweight - primitive handler registry implementation (instance-based)

#include <stdlib.h>
#include <yetty/ypaint-core/flyweight.h>
#include <yetty/ytrace.h>

#define FLYWEIGHT_MAX_HANDLERS 8

struct handler_reg {
    uint32_t type_min;
    uint32_t type_max;
    yetty_ypaint_prim_handler_fn handler;
};

struct yetty_ypaint_core_flyweight_registry {
    yetty_ypaint_prim_handler_fn default_handler;
    struct handler_reg handlers[FLYWEIGHT_MAX_HANDLERS];
    size_t handler_count;
};

struct yetty_ypaint_core_flyweight_registry_ptr_result yetty_ypaint_core_flyweight_registry_create(void)
{
    struct yetty_ypaint_core_flyweight_registry *reg =
        calloc(1, sizeof(struct yetty_ypaint_core_flyweight_registry));
    if (!reg) {
        return YETTY_ERR(yetty_ypaint_core_flyweight_registry_ptr, "calloc failed");
    }
    return YETTY_OK(yetty_ypaint_core_flyweight_registry_ptr, reg);
}

void yetty_ypaint_core_flyweight_registry_destroy(struct yetty_ypaint_core_flyweight_registry *reg)
{
    free(reg);
}

void yetty_ypaint_core_flyweight_registry_set_default(struct yetty_ypaint_core_flyweight_registry *reg,
                                                 yetty_ypaint_prim_handler_fn handler)
{
    if (reg) {
        reg->default_handler = handler;
    }
}

struct yetty_ycore_void_result yetty_ypaint_core_flyweight_registry_add(
    struct yetty_ypaint_core_flyweight_registry *reg, uint32_t type_min, uint32_t type_max,
    yetty_ypaint_prim_handler_fn handler)
{
    if (!reg) {
        return YETTY_ERR(yetty_ycore_void, "reg is NULL");
    }
    if (reg->handler_count >= FLYWEIGHT_MAX_HANDLERS) {
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

struct yetty_ypaint_prim_flyweight_ptr_result yetty_ypaint_core_flyweight_registry_get(
    const struct yetty_ypaint_core_flyweight_registry *reg, uint32_t prim_type,
    const uint32_t *prim_data)
{
    static struct yetty_ypaint_prim_flyweight fw;

    if (!reg) {
        return YETTY_ERR(yetty_ypaint_prim_flyweight_ptr, "registry is NULL");
    }
    if (!prim_data) {
        return YETTY_ERR(yetty_ypaint_prim_flyweight_ptr, "prim_data is NULL");
    }

    ydebug("flyweight_registry_get: prim_type=0x%08x handler_count=%zu", prim_type,
           reg->handler_count);

    // Try default handler first (fast path for SDF types)
    if (reg->default_handler) {
        struct yetty_ypaint_prim_base_ops_ptr_result ops_res = reg->default_handler(prim_type);
        if (YETTY_IS_OK(ops_res)) {
            ydebug("flyweight_registry_get: default handler matched prim_type=0x%08x", prim_type);
            fw.data = prim_data;
            fw.ops = ops_res.value;
            return YETTY_OK(yetty_ypaint_prim_flyweight_ptr, &fw);
        }
    }

    // Fallback to additional handlers by type range
    // TODO: optimize the mapping from id to flyweight
    for (size_t i = 0; i < reg->handler_count; i++) {
        ydebug("flyweight_registry_get: checking handler[%zu] range=[0x%08x, 0x%08x]", i,
               reg->handlers[i].type_min, reg->handlers[i].type_max);
        if (prim_type >= reg->handlers[i].type_min && prim_type <= reg->handlers[i].type_max) {
            struct yetty_ypaint_prim_base_ops_ptr_result ops_res =
                reg->handlers[i].handler(prim_type);
            if (YETTY_IS_OK(ops_res)) {
                ydebug("flyweight_registry_get: handler[%zu] matched prim_type=0x%08x", i,
                       prim_type);
                fw.data = prim_data;
                fw.ops = ops_res.value;
                return YETTY_OK(yetty_ypaint_prim_flyweight_ptr, &fw);
            }
        }
    }

    ydebug("flyweight_registry_get: NO HANDLER for prim_type=0x%08x", prim_type);
    return YETTY_ERR(yetty_ypaint_prim_flyweight_ptr, "no handler for prim_type");
}
