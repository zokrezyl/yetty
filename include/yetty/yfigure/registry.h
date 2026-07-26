/*
 * yetty_yfigure_registry — kind → factory map for minting figures
 * from a wire CREATE_CHILD admin record.
 *
 * One registry per terminal (or per yui instance). Each figure-kind
 * module registers itself at terminal create time with a factory
 * function + a kind-specific user pointer (e.g. ymgui stashes its
 * shared pipeline pointer here so all ymgui figures borrow the same
 * compiled shader).
 *
 * The container holds a borrowed registry pointer and uses it to
 * mint children when an admin CREATE_CHILD record arrives. Nested
 * containers inherit the same registry from their parent.
 */
#ifndef YETTY_YFIGURE_REGISTRY_H
#define YETTY_YFIGURE_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/api/yfigure/figure.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yfigure_registry;

YETTY_YRESULT_DECLARE(yetty_yfigure_registry_ptr, struct yetty_yfigure_registry *);

/* Map a figure-kind name to its registry token. The token is the key the
 * registry stores factories under and the value carried (derived from the
 * self-describing name) on a CREATE_CHILD; there is no central enum of kinds.
 * A figure-kind module registers under yetty_yfigure_kind_token("<name>") and
 * the producer mints by sending the same "<name>" — both sides agree purely on
 * the string, so adding a kind touches no shared header.
 *
 * static inline (FNV-1a over the name bytes): a pure, dependency-free hash, so
 * any producer can compute a token by including this header alone — it needs no
 * link dependency on yetty_yfigure. */
static inline uint32_t yetty_yfigure_kind_token_n(const char *name, size_t name_len)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < name_len; i++) {
        hash ^= (uint32_t)(uint8_t)name[i];
        hash *= 16777619u;
    }
    return hash;
}

static inline uint32_t yetty_yfigure_kind_token(const char *name)
{
    size_t name_len = 0;
    if (name) {
        while (name[name_len] != '\0') {
            name_len++;
        }
    }
    return yetty_yfigure_kind_token_n(name, name_len);
}

/* Factory signature. The registry passes the rect the wire told us to
 * use, the host context (for GPU access), and the per-kind user pointer
 * supplied at register time. The factory mints a freshly-allocated
 * figure; ownership passes to the caller (typically a container that
 * will add it via add_child). */
typedef struct yetty_yfigure_figure_ptr_result (*yetty_ycomposite_factory_fn)(
    struct yetty_ycore_rectangle rect, const struct yetty_context *context, void *user);

struct yetty_yfigure_registry_ptr_result yetty_yfigure_registry_create(void);

struct yetty_ycore_void_result yetty_yfigure_registry_destroy(
    struct yetty_yfigure_registry *registry);

/* Register `factory` under `kind`. Errors on duplicate kind. `user`
 * is an opaque cookie handed back to the factory on every mint — used
 * by figure-kinds (e.g. ymgui) to share state across instances. */
struct yetty_ycore_void_result yetty_yfigure_registry_register(
    struct yetty_yfigure_registry *registry, uint32_t kind, yetty_ycomposite_factory_fn factory,
    void *user);

/* Mint a figure of `kind` at `rect`. Returns YETTY_ERR with a clear
 * cause when `kind` isn't registered. */
struct yetty_yfigure_figure_ptr_result yetty_yfigure_registry_mint(
    struct yetty_yfigure_registry *registry, uint32_t kind, struct yetty_ycore_rectangle rect,
    const struct yetty_context *context);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFIGURE_REGISTRY_H */
