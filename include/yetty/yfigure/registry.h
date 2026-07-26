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
/* The client-safe kind token (FNV inline) lives here — this header re-includes
 * it so registry consumers keep seeing yetty_yfigure_kind_token unchanged. A
 * pure wire client that only needs the token includes <yetty/yfigure/kind.h>
 * directly, avoiding the GPU context <yetty/yetty/yetty.h> pulls in above. */
#include <yetty/yfigure/kind.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yfigure_registry;

YETTY_YRESULT_DECLARE(yetty_yfigure_registry_ptr, struct yetty_yfigure_registry *);

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
