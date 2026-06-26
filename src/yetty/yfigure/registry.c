/*
 * yetty_yfigure_registry — kind → factory map. Backed by uthash.
 */
#include <yetty/yfigure/registry.h>

#include <stdlib.h>
#include <string.h>

#include <ut/uthash.h>

#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

struct kind_entry {
    uint32_t kind;
    yetty_ycomposite_factory_fn factory;
    void *user;
    UT_hash_handle hh;
};

struct yetty_yfigure_registry {
    struct kind_entry *kinds;
};

uint32_t yetty_yfigure_kind_token_n(const char *name, size_t name_len)
{
    /* FNV-1a, 32-bit. A name maps to a stable token both producer and
     * receiver compute independently — no shared enum, no negotiation. */
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < name_len; i++) {
        hash ^= (uint32_t)(uint8_t)name[i];
        hash *= 16777619u;
    }
    return hash;
}

uint32_t yetty_yfigure_kind_token(const char *name)
{
    if (!name) {
        return 0;
    }
    return yetty_yfigure_kind_token_n(name, strlen(name));
}

struct yetty_yfigure_registry_ptr_result yetty_yfigure_registry_create(void)
{
    struct yetty_yfigure_registry *r = calloc(1, sizeof(*r));
    if (!r) {
        return YETTY_ERR(yetty_yfigure_registry_ptr, "yfigure_registry_create: oom");
    }
    return YETTY_OK(yetty_yfigure_registry_ptr, r);
}

struct yetty_ycore_void_result yetty_yfigure_registry_destroy(
    struct yetty_yfigure_registry *registry)
{
    if (!registry) {
        return YETTY_OK_VOID();
    }
    struct kind_entry *e, *tmp;
    HASH_ITER(hh, registry->kinds, e, tmp)
    {
        HASH_DEL(registry->kinds, e);
        free(e);
    }
    free(registry);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yfigure_registry_register(
    struct yetty_yfigure_registry *registry, uint32_t kind, yetty_ycomposite_factory_fn factory,
    void *user)
{
    if (!registry || !factory) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_registry_register: NULL arg");
    }
    struct kind_entry *existing;
    HASH_FIND_INT(registry->kinds, &kind, existing);
    if (existing) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_registry_register: kind already registered");
    }
    struct kind_entry *e = calloc(1, sizeof(*e));
    if (!e) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_registry_register: oom");
    }
    e->kind = kind;
    e->factory = factory;
    e->user = user;
    HASH_ADD_INT(registry->kinds, kind, e);
    ydebug("yfigure_registry: registered kind=%u", kind);
    return YETTY_OK_VOID();
}

struct yetty_yfigure_figure_ptr_result yetty_yfigure_registry_mint(
    struct yetty_yfigure_registry *registry, uint32_t kind, struct yetty_ycore_rectangle rect,
    const struct yetty_context *context)
{
    if (!registry) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "yfigure_registry_mint: NULL registry");
    }
    struct kind_entry *e;
    HASH_FIND_INT(registry->kinds, &kind, e);
    if (!e) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "yfigure_registry_mint: kind not registered");
    }
    return e->factory(rect, context, e->user);
}
