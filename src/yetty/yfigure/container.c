/*
 * yfigure_container — composite figure that holds child figures keyed
 * by parent-scoped id.
 *
 * Storage: each child is wrapped in a `struct child_entry` carrying its
 * id, the owned figure pointer, and a uthash handle. uthash doubles as
 * id → entry lookup AND insertion-ordered linked list (z-order: back-
 * to-front render, raise = move-to-end).
 *
 * ids are parent-scoped: distinct within one container, no global
 * registry required. `id == 0` is reserved (anonymous, no wire address);
 * add_child rejects it.
 *
 * Producers drive the figure tree through the typed yclass mutation slots
 * (create_child / set_child_rect / apply_child_body / …), dispatched
 * locally or over RPC. There is no record-stream decode path.
 */
#include <yetty/yclass/class.h>
/* This TU deliberately does NOT include its own generated header
 * `yetty/yfigure/container.h` — that header is a downstream artifact for
 * other modules. The hit struct (an `expose`d type) is defined directly
 * below, and this TU declares its own `yetty_yfigure_container_ptr_result`
 * (see after the container struct). The figure base type comes from the
 * parent header `figure.h`. */
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>
/* Internal module header: the yfigure-domain slot stub prototypes
 * (yetty_yfigure_render / _destroy / _process_bytes / _dump_state / …)
 * this TU dispatches through. It is self-contained and deliberately does
 * NOT pull in the per-class public headers, so it cannot clash with the
 * yetty_yfigure_container_ptr_result declared below. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ut/uthash.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/ytrace/ytrace.h>

/* Hit-test result: the child whose rect contains the cursor, plus the cursor
 * coordinates inside that child's own pixel space (origin = child rect's
 * top-left). figure_id == 0 means "no hit". Iteration is back-to-front, so
 * for overlapping children the BACK-most match wins. This is an `expose`d
 * type: codegen reads this definition (it sees the whole TU during its parse
 * pass) and re-emits it into the generated container.h for consumers. The
 * definition lives HERE because this TU no longer includes container.h, so it
 * is the sole definition in the real build too — no double definition.
 * `expose` makes codegen re-emit this full definition (it is returned by
 * value from yetty_yfigure_container_hit_test, so consumers need the
 * layout, not just a forward decl) into the generated container.h. That
 * header copy and this one never share a TU — this TU does not include
 * container.h — so there is no clash. */
struct YETTY_ANNOTATE("expose") yetty_yfigure_hit {
    uint32_t figure_id;
    float local_x;
    float local_y;
};

/* Result wrapper for the hit-test return. yetty_yfigure_container_hit_test
 * downcasts the object first, which can fail, so it returns the hit by
 * Result rather than absorbing the downcast error. */
YETTY_YRESULT_DECLARE(yetty_yfigure_hit, struct yetty_yfigure_hit);

/* Visitor for the internal child walk (container_for_each): called once per
 * child in z-order (back-to-front) with its parent-scoped id, figure pointer
 * and the caller's cookie. Returning non-zero stops the walk and is returned.
 * Used only inside this TU (hit-testing), so it stays private — no `expose`. */
typedef int (*container_visitor_fn)(uint32_t id, struct yetty_yfigure_figure *child, void *user);

/* The container's own type, defined below — declared at file scope here so the
 * parameter types in the prototypes that follow refer to it (a struct tag first
 * mentioned inside a parameter list would otherwise get prototype scope and
 * clash with the real definition). */
struct yetty_yfigure_container;

struct yetty_yfigure_figure_ptr_result yetty_yfigure_container_as_figure(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_container_add_child(struct yetty_yclass_object *obj,
                                                                 struct yetty_yfigure_figure *child,
                                                                 uint32_t id);
struct yetty_yfigure_figure_ptr_result yetty_yfigure_container_find_child_by_id(
    struct yetty_yclass_object *obj, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_container_remove_child_by_id(
    struct yetty_yclass_object *obj, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_container_raise_child_by_id(
    struct yetty_yclass_object *obj, uint32_t id);

struct child_entry {
    /* uthash key. Parent-scoped, non-zero, unique within the container. */
    uint32_t id;
    /* Owned child figure. Destroyed when the entry is removed or when
     * the container itself is destroyed. */
    struct yetty_yfigure_figure *figure;
    /* Kind tag (YETTY_YFIGURE_KIND_*) captured at mint time. Used by
     * CREATE_CHILD on an existing id to decide between the fast path
     * (same kind → reset_content + process_bytes, reuse GPU state)
     * and the destroy + mint fallback (different kind). */
    uint32_t kind;
    /* Monotonic insertion sequence, assigned at add_child. The z-order
     * comparator breaks ties on (figure->z) with this, giving a total
     * order so the sort is deterministic regardless of uthash sort
     * stability and equal-z children keep insertion order. */
    uint64_t seq;
    /* When set, clear_all (the CLEAR_ALL admin op and the terminal's
     * full-screen-erase / reset hook) skips this child. Used for the host's
     * own structural figures — notably the terminal content grid — that are
     * NOT part of the producer-managed figure set a client means to drop.
     * Container destroy still frees it; only clear_all spares it. */
    int protected_from_clear;
    UT_hash_handle hh;
};

struct YETTY_ANNOTATE("class@yfigure:container") YETTY_ANNOTATE("parent@yfigure:figure")
    yetty_yfigure_container {
    /* uthash head — id → entry. The list is kept sorted by
     * (figure->z, seq) via container_ensure_sorted, so HASH_ITER walks
     * children back-to-front in true z-order (render order; hit-test
     * keeps the last match = front-most). */
    struct child_entry *children;
    /* Next insertion sequence handed to a child_entry. Monotonic. */
    uint64_t next_seq;
    /* Set whenever the child set or any child's z changes; cleared by
     * container_ensure_sorted after it re-sorts the list. Avoids
     * re-sorting on every render when nothing moved. */
    int z_order_dirty;
    /* Borrowed — used for minting children from admin CREATE_CHILD
     * records. Sub-containers minted via that path inherit the same
     * pointers. */
    const struct yetty_context *context;
    struct yetty_yfigure_registry *registry;
    /* Producer emits child rects in pane-local; we add this offset on
     * CREATE_CHILD / SET_CHILD_RECT to land them in absolute target
     * pixel space. Set via yetty_yfigure_container_set_viewport_offset. */
    float viewport_offset_x;
    float viewport_offset_y;
};

/* Result wrapper for the container handle. Declared here (not pulled from
 * container.h, which this TU does not include) so the appended
 * container.gen.c — which defines yetty_yfigure_container_from() returning
 * it — has the type in scope. The public container.h publishes the
 * identical declaration for other modules. */
YETTY_YRESULT_DECLARE(yetty_yfigure_container_ptr, struct yetty_yfigure_container *);

/* Defined in the appended container.gen.c (foot of this TU). Forward-
 * declared here because this TU does not include its own generated
 * header — the class accessor and the obj→body downcast are used by the
 * helpers and the object-keyed public API below. */
struct yetty_yclass_ptr_result yetty_yfigure_container_class_get(void);
struct yetty_yfigure_container_ptr_result yetty_yfigure_container_from(
    struct yetty_yclass_object *obj);

/* The container's own data slice (its fields sit after the figure
 * base slice in the shared yclass object). */
static struct yetty_yclass_void_ptr_result container_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yfigure_container_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "container_from_obj: class");
    return yetty_yclass_object_data(obj, class_r.value);
}

/*===========================================================================
 * Internal helpers
 *=========================================================================*/

/* render/destroy dispatch through the figure's yclass object. Every figure
 * kind is a yclass object (self_obj set at construction); the slots are
 * local@ (in-process) so ctx is NULL. */
static struct yetty_ycore_void_result figure_dispatch_render(struct yetty_yfigure_figure *fig,
                                                             struct yetty_ydraw_target *target)
{
    return yetty_yfigure_render(((struct yetty_yclass_object *)(fig)-1), target);
}

static struct yetty_ycore_void_result figure_dispatch_destroy(struct yetty_yfigure_figure *fig)
{
    return yetty_yfigure_destroy(((struct yetty_yclass_object *)(fig)-1));
}

/* True when fig's concrete class overrides the given figure-base slot
 * (rather than inheriting the base no-op/reject default). This is the
 * yclass replacement for the old "is this vtable op non-NULL" capability
 * check: a figure that doesn't implement, say, process_input inherits the
 * base default, so its dispatch impl equals the base class's impl. */
/* Returns 1 when fig's concrete class overrides the slot, 0 when it inherits
 * the base default. The lookups that back this can fail, so it returns an int
 * Result; callers propagate. */
static struct yetty_ycore_int_result figure_implements(struct yetty_yfigure_figure *fig,
                                                       yetty_yclass_method_id_t method_id)
{
    if (!fig || !((struct yetty_yclass_object *)(fig)-1)) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_yclass_method_slot_result slot_r =
        yetty_yclass_method_slot_get("yetty_yfigure", method_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, slot_r, "figure_implements: method_slot_get");
    struct yetty_yclass_ptr_result base_r = yetty_yfigure_figure_class_get();
    YETTY_RETURN_IF_ERR(yetty_ycore_int, base_r, "figure_implements: figure_class_get");
    struct yetty_yclass_impl_t_result obj_impl =
        yetty_yclass_dispatch_lookup(((struct yetty_yclass_object *)(fig)-1)->klass, slot_r.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, obj_impl, "figure_implements: obj dispatch_lookup");
    struct yetty_yclass_impl_t_result base_impl =
        yetty_yclass_dispatch_lookup(base_r.value, slot_r.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, base_impl, "figure_implements: base dispatch_lookup");
    return YETTY_OK(yetty_ycore_int, obj_impl.value != base_impl.value ? 1 : 0);
}

/* Best-effort: destroy the entry's figure and always free the entry, even
 * if the figure destroy reported an error. Returns that destroy result so
 * the caller can fold it into its own first-error aggregation. */
static struct yetty_ycore_void_result entry_destroy(struct child_entry *e)
{
    struct yetty_ycore_void_result destroy_r = figure_dispatch_destroy(e->figure);
    free(e);
    return destroy_r;
}

/*===========================================================================
 * Polymorphic dump — base wrapper + a tiny string-builder used by both
 * the container's own dump impl and the YAML fallback when a concrete
 * figure doesn't provide one.
 *=========================================================================*/

/* Grow-as-needed heap string. NULL `buf` starts a fresh allocation.
 * Returns the new buffer or NULL on OOM (in which case the old buffer
 * is freed). */
static char *dump_appendf(char *buf, size_t *len, size_t *cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    /* Snprintf needs a real buffer to measure; do it the safe way:
     * try once into the remaining slot, grow if it didn't fit. */
    if (!buf) {
        *len = 0;
        *cap = 64;
        buf = (char *)malloc(*cap);
        if (!buf) {
            va_end(ap);
            return NULL;
        }
        buf[0] = '\0';
    }
    va_list ap2;
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (need < 0) {
        va_end(ap);
        return buf;
    }
    size_t want = *len + (size_t)need + 1u;
    if (want > *cap) {
        size_t ncap = *cap ? *cap : 64;
        while (ncap < want) {
            ncap *= 2;
        }
        char *grown = (char *)realloc(buf, ncap);
        if (!grown) {
            free(buf);
            va_end(ap);
            return NULL;
        }
        buf = grown;
        *cap = ncap;
    }
    int wrote = vsnprintf(buf + *len, *cap - *len, fmt, ap);
    va_end(ap);
    if (wrote < 0) {
        return buf;
    }
    *len += (size_t)wrote;
    return buf;
}

static void dump_indent_spaces(char *buf, size_t cap, int indent)
{
    int n = indent;
    if (n < 0) {
        n = 0;
    }
    if ((size_t)n + 1u > cap) {
        n = (int)cap - 1;
    }
    for (int i = 0; i < n; i++) {
        buf[i] = ' ';
    }
    buf[n] = '\0';
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_char_ptr_result yetty_yfigure_dump(const struct yetty_yfigure_figure *self,
                                                      int indent)
{
    if (!self) {
        char *out = (char *)malloc(8);
        if (!out) {
            return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yfigure_dump: out of memory");
        }
        snprintf(out, 8, "null\n");
        return YETTY_OK(yetty_ycore_char_ptr, out);
    }
    struct yetty_ycore_int_result implements_r = figure_implements(
        (struct yetty_yfigure_figure *)self, (yetty_yclass_method_id_t)yetty_yfigure_dump_state);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, implements_r,
                        "yetty_yfigure_dump: figure_implements");
    if (implements_r.value) {
        return yetty_yfigure_dump_state(
            (struct yetty_yclass_object *)((struct yetty_yclass_object *)(self)-1), indent);
    }
    /* Fallback: just rect + dirty. Concrete kinds that haven't migrated
     * to a real dump still produce something testable. */
    char pad[64];
    dump_indent_spaces(pad, sizeof(pad), indent);
    size_t len = 0, cap = 0;
    char *buf = NULL;
    buf = dump_appendf(buf, &len, &cap, "%skind: unknown\n", pad);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yfigure_dump: out of memory");
    }
    buf = dump_appendf(
        buf, &len, &cap, "%srect: [%.1f, %.1f, %.1f, %.1f]\n", pad,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.min.x,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.min.y,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.max.x,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.max.y);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yfigure_dump: out of memory");
    }
    buf =
        dump_appendf(buf, &len, &cap, "%sdirty: %d\n", pad,
                     yetty_yfigure_figure_dirty_get((struct yetty_yclass_object *)(self)-1).value);
    return YETTY_OK(yetty_ycore_char_ptr, buf);
}

/*===========================================================================
 * Group ops
 *=========================================================================*/

YETTY_ANNOTATE("override@yfigure:container:destroy")
static struct yetty_ycore_void_result container_destroy(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result container_r = container_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "container_destroy: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    int have_err = 0;
    struct child_entry *e, *tmp;
    HASH_ITER(hh, container->children, e, tmp)
    {
        HASH_DEL(container->children, e);
        struct yetty_ycore_void_result entry_r = entry_destroy(e);
        if (YETTY_IS_ERR(entry_r)) {
            if (!have_err) {
                first_err = entry_r;
                have_err = 1;
            } else {
                yetty_ycore_error_destroy(entry_r.error);
            }
        }
    }
    /* The destroy slot is dispatched with the yclass object header; the
     * container body is one slice inside it. Free the header so both it
     * and every slice (figure base + container) are reclaimed in one go,
     * honouring the allocation contract. */
    struct yetty_ycore_void_result fr = yetty_yclass_object_free(obj);
    if (YETTY_IS_ERR(fr)) {
        if (have_err) {
            yetty_ycore_error_destroy(fr.error);
        } else {
            first_err = fr;
            have_err = 1;
        }
    }
    if (have_err) {
        return YETTY_ERR(yetty_ycore_void, "yfigure container_destroy: child destroy failed",
                         first_err);
    }
    return YETTY_OK_VOID();
}

/* Total order on children: ascending figure->z, ties broken by
 * insertion sequence. A total order means the result is independent of
 * uthash's sort stability, and equal-z children keep insertion order
 * (so a single-z tree sorts to its original order — no behaviour change
 * until something sets a non-zero z). */
YETTY_EXTERNAL_CALLBACK
static int child_z_cmp(struct child_entry *a, struct child_entry *b)
{
    if (yetty_yfigure_figure_z_get((struct yetty_yclass_object *)(a->figure) - 1).value !=
        yetty_yfigure_figure_z_get((struct yetty_yclass_object *)(b->figure) - 1).value) {
        return yetty_yfigure_figure_z_get((struct yetty_yclass_object *)(a->figure) - 1).value <
                       yetty_yfigure_figure_z_get((struct yetty_yclass_object *)(b->figure) - 1)
                           .value
                   ? -1
                   : 1;
    }
    if (a->seq != b->seq) {
        return a->seq < b->seq ? -1 : 1;
    }
    return 0;
}

/* Re-sort the child list in place when a child was added/removed or a
 * z changed. Cheap no-op when nothing moved. After this, every HASH_ITER
 * walk (render, hit-test, dump, serialize) is in z-order. */
static void container_ensure_sorted(struct yetty_yfigure_container *container)
{
    if (!container->z_order_dirty) {
        return;
    }
    HASH_SRT(hh, container->children, child_z_cmp);
    container->z_order_dirty = 0;
}

YETTY_ANNOTATE("override@yfigure:container:render")
static struct yetty_ycore_void_result container_render(struct yetty_yclass_object *obj,
                                                       struct yetty_ydraw_target *target)
{
    struct yetty_yclass_void_ptr_result container_r = container_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "container_render: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    container_ensure_sorted(container);
    /* List is z-sorted; HASH_ITER walks back-to-front. */
    struct child_entry *e, *tmp;
    HASH_ITER(hh, container->children, e, tmp)
    {
        struct yetty_yfigure_figure *c = e->figure;
        if (yetty_yfigure_figure_hidden_get((struct yetty_yclass_object *)(c)-1).value) {
            continue;
        }
        ydebug("yfigure container_render: child id=%u z=%d rect=(%.0f,%.0f)-(%.0f,%.0f)", e->id,
               yetty_yfigure_figure_z_get((struct yetty_yclass_object *)(c)-1).value,
               yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(c)-1).value.min.x,
               yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(c)-1).value.min.y,
               yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(c)-1).value.max.x,
               yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(c)-1).value.max.y);
        struct yetty_ycore_void_result r = figure_dispatch_render(c, target);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yfigure container_render: child render failed");
        {
            struct yetty_ycore_void_result set_r =
                yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(c)-1, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
    }
    return YETTY_OK_VOID();
}

/* Remove and destroy every child figure, then mark the container dirty so the
 * empty result is repainted. Shared by the CLEAR_ALL admin op and the terminal's
 * full-screen-erase / reset path. Best-effort: keeps tearing down on a per-child
 * error, stashing the first to surface at the end. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yfigure_container_clear_all(struct yetty_yclass_object *obj)
{
    struct yetty_yfigure_container_ptr_result container_r = yetty_yfigure_container_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "container clear_all: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    struct child_entry *e, *tmp;
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    int have_err = 0;
    HASH_ITER(hh, container->children, e, tmp)
    {
        /* Spare structural children a client's CLEAR_ALL must not drop (e.g.
         * the terminal's own content grid). They live and die with the
         * container itself, not with the producer-managed figure set. */
        if (e->protected_from_clear) {
            continue;
        }
        HASH_DEL(container->children, e);
        struct yetty_ycore_void_result entry_r = entry_destroy(e);
        if (YETTY_IS_ERR(entry_r)) {
            if (!have_err) {
                first_err = entry_r;
                have_err = 1;
            } else {
                yetty_ycore_error_destroy(entry_r.error);
            }
        }
    }
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
        if (YETTY_IS_ERR(set_r)) {
            if (have_err) {
                yetty_ycore_error_destroy(first_err.error);
            }
            return YETTY_ERR(yetty_ycore_void, "container clear_all: figure attr set", set_r);
        }
    }
    if (have_err) {
        return YETTY_ERR(yetty_ycore_void, "container clear_all: child destroy", first_err);
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Typed container ops — the canonical implementation of every figure-tree
 * mutation. The yclass method slots (create_child, set_child_rect, …) call
 * these directly with typed args; the same call serves local and RPC. Rects
 * are passed in pane-local space and re-origined here by the container's
 * viewport offset, exactly as the wire path did.
 *=========================================================================*/

/* Mint or refresh a child of `kind_key` at pane-local `rect_local`. `kind_key`
 * is the registry token (yetty_yfigure_kind_token of the kind name). Reuses an
 * existing same-kind child's GPU state via reset_content when possible, else
 * mints a fresh figure (swapping in place on an existing id of a different
 * kind). `init` is the kind-specific init payload (may be empty). */
static struct yetty_ycore_void_result container_do_create_child(
    struct yetty_yclass_object *obj, uint32_t kind_key, uint32_t child_id,
    struct yetty_ycore_rectangle rect_local, const uint8_t *init, size_t init_len)
{
    struct yetty_yfigure_container_ptr_result container_r = yetty_yfigure_container_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "container create_child: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    if (!container->registry) {
        return YETTY_ERR(yetty_ycore_void, "container create_child: no registry");
    }
    struct yetty_ycore_rectangle rect = {
        .min = {.x = rect_local.min.x + container->viewport_offset_x,
                .y = rect_local.min.y + container->viewport_offset_y},
        .max = {.x = rect_local.max.x + container->viewport_offset_x,
                .y = rect_local.max.y + container->viewport_offset_y},
    };
    /* Fast path: existing id of the same kind that supports reset_content.
     * Reuse the figure (and its cached binder / pipeline / textures), just
     * refresh its content. Without this the receiver destroys + remints on
     * every create, dropping the binder cache and forcing a full pipeline
     * rebuild on the next render — visible as ~100 ms hover lag. */
    struct child_entry *existing;
    HASH_FIND_INT(container->children, &child_id, existing);
    int existing_can_reset = 0;
    if (existing && existing->kind == kind_key) {
        struct yetty_ycore_int_result reset_impl_r = figure_implements(
            existing->figure, (yetty_yclass_method_id_t)yetty_yfigure_reset_content);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, reset_impl_r,
                            "container create_child: reset_content support");
        existing_can_reset = reset_impl_r.value;
    }
    if (existing_can_reset) {
        struct yetty_ycore_void_result rc =
            yetty_yfigure_reset_content(((struct yetty_yclass_object *)(existing->figure) - 1));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rc, "container create_child: reset_content");
        {
            struct yetty_ycore_void_result set_r = yetty_yfigure_figure_rect_set(
                (struct yetty_yclass_object *)(existing->figure) - 1, rect);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
        if (init_len > 0) {
            struct yetty_ycore_int_result process_bytes_impl_r = figure_implements(
                existing->figure, (yetty_yclass_method_id_t)yetty_yfigure_process_bytes);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, process_bytes_impl_r,
                                "container create_child: process_bytes support");
            if (!process_bytes_impl_r.value) {
                return YETTY_ERR(yetty_ycore_void,
                                 "container create_child: no process_bytes for non-empty init");
            }
            struct yetty_ycore_void_result pr = yetty_yfigure_process_bytes(
                ((struct yetty_yclass_object *)(existing->figure) - 1), init, init_len);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "container create_child: child re-init");
        }
        {
            struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(
                (struct yetty_yclass_object *)(existing->figure) - 1, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
        {
            struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
        return YETTY_OK_VOID();
    }

    struct yetty_yfigure_figure_ptr_result fr =
        yetty_yfigure_registry_mint(container->registry, kind_key, rect, container->context);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "container create_child: registry mint");
    struct yetty_yfigure_figure *child = fr.value;
    /* Existing id but different kind (or kind has no reset_content): in-place
     * swap the figure pointer. The hash entry stays put, so z-order is
     * preserved, but the old figure's GPU resources are released. Slow
     * fallback — kind changes on the same id are rare. */
    if (existing) {
        {
            struct yetty_ycore_void_result set_r = yetty_yfigure_figure_z_set(
                (struct yetty_yclass_object *)(child)-1,
                yetty_yfigure_figure_z_get((struct yetty_yclass_object *)(existing->figure) - 1)
                    .value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
        struct yetty_ycore_void_result dr = figure_dispatch_destroy(existing->figure);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        existing->figure = child;
        existing->kind = kind_key;
        container->z_order_dirty = 1;
        {
            struct yetty_ycore_void_result set_r =
                yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(child)-1, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
    } else {
        struct yetty_ycore_void_result ar = yetty_yfigure_container_add_child(obj, child, child_id);
        if (YETTY_IS_ERR(ar)) {
            struct yetty_ycore_void_result dr = figure_dispatch_destroy(child);
            if (YETTY_IS_ERR(dr)) {
                yetty_ycore_error_destroy(dr.error);
            }
            return YETTY_ERR(yetty_ycore_void, "container create_child: add_child", ar);
        }
        struct child_entry *fresh;
        HASH_FIND_INT(container->children, &child_id, fresh);
        if (fresh) {
            fresh->kind = kind_key;
        }
    }
    if (init_len > 0) {
        struct yetty_ycore_int_result process_bytes_impl_r =
            figure_implements(child, (yetty_yclass_method_id_t)yetty_yfigure_process_bytes);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, process_bytes_impl_r,
                            "container create_child: process_bytes support");
        if (!process_bytes_impl_r.value) {
            return YETTY_ERR(yetty_ycore_void,
                             "container create_child: child has no process_bytes but init > 0");
        }
        struct yetty_ycore_void_result pr =
            yetty_yfigure_process_bytes(((struct yetty_yclass_object *)(child)-1), init, init_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "container create_child: child init");
    }
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
    }
    return YETTY_OK_VOID();
}

/* Drop a single child by id, marking the container dirty. */
static struct yetty_ycore_void_result container_do_delete_child(struct yetty_yclass_object *obj,
                                                                uint32_t child_id)
{
    struct yetty_ycore_void_result r = yetty_yfigure_container_remove_child_by_id(obj, child_id);
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
    }
    return r;
}

/* Move a child to pane-local `rect_local` without rebuilding it. */
static struct yetty_ycore_void_result container_do_set_child_rect(
    struct yetty_yclass_object *obj, uint32_t child_id, struct yetty_ycore_rectangle rect_local)
{
    struct yetty_yfigure_container_ptr_result container_r = yetty_yfigure_container_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "container set_child_rect: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    struct yetty_yfigure_figure_ptr_result child_res =
        yetty_yfigure_container_find_child_by_id(obj, child_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res, "container set_child_rect: find_child");
    struct yetty_yfigure_figure *child = child_res.value;
    if (!child) {
        ydebug("container set_child_rect: id=%u not bound", child_id);
        return YETTY_OK_VOID();
    }
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_rect_set(
            (struct yetty_yclass_object *)(child)-1,
            (struct yetty_ycore_rectangle){
                .min = {.x = rect_local.min.x + container->viewport_offset_x,
                        .y = rect_local.min.y + container->viewport_offset_y},
                .max = {.x = rect_local.max.x + container->viewport_offset_x,
                        .y = rect_local.max.y + container->viewport_offset_y},
            });
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: child rect set");
    }
    {
        struct yetty_ycore_void_result set_r =
            yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(child)-1, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
    }
    /* Container dirty too, so the host repaints on a child relayout (see the note
     * in container_do_apply_child_body). */
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container set_child_rect: container dirty");
    }
    return YETTY_OK_VOID();
}

/* Set the container's OWN rect (absolute pane-root space; no viewport offset). */
static struct yetty_ycore_void_result container_do_set_container_rect(
    struct yetty_yclass_object *obj, struct yetty_ycore_rectangle rect)
{
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_rect_set(obj, rect);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: base rect set");
    }
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
    }
    return YETTY_OK_VOID();
}

/* Set a child's stacking order; re-sorts on change. */
static struct yetty_ycore_void_result container_do_set_child_z(struct yetty_yclass_object *obj,
                                                               uint32_t child_id, int32_t z)
{
    struct yetty_yfigure_container_ptr_result container_r = yetty_yfigure_container_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "container set_child_z: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    struct yetty_yfigure_figure_ptr_result child_res =
        yetty_yfigure_container_find_child_by_id(obj, child_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res, "container set_child_z: find_child");
    struct yetty_yfigure_figure *child = child_res.value;
    if (!child) {
        ydebug("container set_child_z: id=%u not bound", child_id);
        return YETTY_OK_VOID();
    }
    if (yetty_yfigure_figure_z_get((struct yetty_yclass_object *)(child)-1).value != z) {
        {
            struct yetty_ycore_void_result set_r =
                yetty_yfigure_figure_z_set((struct yetty_yclass_object *)(child)-1, z);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
        container->z_order_dirty = 1;
        {
            struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
    }
    return YETTY_OK_VOID();
}

/* Show/hide a child without destroying it. */
static struct yetty_ycore_void_result container_do_set_child_hidden(struct yetty_yclass_object *obj,
                                                                    uint32_t child_id,
                                                                    uint32_t hidden)
{
    struct yetty_yfigure_figure_ptr_result child_res =
        yetty_yfigure_container_find_child_by_id(obj, child_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res, "container set_child_hidden: find_child");
    struct yetty_yfigure_figure *child = child_res.value;
    if (!child) {
        ydebug("container set_child_hidden: id=%u not bound", child_id);
        return YETTY_OK_VOID();
    }
    int want = hidden ? 1 : 0;
    if (yetty_yfigure_figure_hidden_get((struct yetty_yclass_object *)(child)-1).value != want) {
        {
            struct yetty_ycore_void_result set_r =
                yetty_yfigure_figure_hidden_set((struct yetty_yclass_object *)(child)-1, want);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
        {
            struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
    }
    return YETTY_OK_VOID();
}

/* Drive a scrollable child's scroll offset (ignored by non-scrollable kinds). */
static struct yetty_ycore_void_result container_do_set_child_scroll(struct yetty_yclass_object *obj,
                                                                    uint32_t child_id,
                                                                    float scroll_x, float scroll_y)
{
    struct yetty_yfigure_figure_ptr_result child_res =
        yetty_yfigure_container_find_child_by_id(obj, child_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res, "container set_child_scroll: find_child");
    struct yetty_yfigure_figure *child = child_res.value;
    if (!child) {
        ydebug("container set_child_scroll: id=%u not bound", child_id);
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_int_result set_scroll_impl_r =
        figure_implements(child, (yetty_yclass_method_id_t)yetty_yfigure_set_scroll);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_scroll_impl_r,
                        "container set_child_scroll: set_scroll support");
    if (!set_scroll_impl_r.value) {
        ydebug("container set_child_scroll: id=%u not scrollable", child_id);
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result sr =
        yetty_yfigure_set_scroll(((struct yetty_yclass_object *)(child)-1), scroll_x, scroll_y);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "container set_child_scroll: set_scroll");
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
    }
    return YETTY_OK_VOID();
}

/* Declare a scrollable child's content extent (ignored by non-scrollable kinds). */
static struct yetty_ycore_void_result container_do_set_child_content_size(
    struct yetty_yclass_object *obj, uint32_t child_id, float content_w, float content_h)
{
    struct yetty_yfigure_figure_ptr_result child_res =
        yetty_yfigure_container_find_child_by_id(obj, child_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res,
                        "container set_child_content_size: find_child");
    struct yetty_yfigure_figure *child = child_res.value;
    if (!child) {
        ydebug("container set_child_content_size: id=%u not bound", child_id);
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_int_result set_content_size_impl_r =
        figure_implements(child, (yetty_yclass_method_id_t)yetty_yfigure_set_content_size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_content_size_impl_r,
                        "container set_child_content_size: set_content_size support");
    if (!set_content_size_impl_r.value) {
        ydebug("container set_child_content_size: id=%u not scrollable", child_id);
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result sr = yetty_yfigure_set_content_size(
        ((struct yetty_yclass_object *)(child)-1), content_w, content_h);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "container set_child_content_size: set_content_size");
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
    }
    return YETTY_OK_VOID();
}

/* Hand a body buffer to the addressed child's process_bytes. No-op when the
 * id is unbound or the child has no process_bytes. */
static struct yetty_ycore_void_result container_do_apply_child_body(struct yetty_yclass_object *obj,
                                                                    uint32_t child_id,
                                                                    const uint8_t *body,
                                                                    size_t body_len)
{
    struct yetty_yfigure_figure_ptr_result child_res =
        yetty_yfigure_container_find_child_by_id(obj, child_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res, "container apply_child_body: find_child");
    struct yetty_yfigure_figure *child = child_res.value;
    if (!child) {
        ydebug("container apply_child_body: id=%u not bound, skipping %zu", child_id, body_len);
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_int_result process_bytes_impl_r =
        figure_implements(child, (yetty_yclass_method_id_t)yetty_yfigure_process_bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, process_bytes_impl_r,
                        "container apply_child_body: process_bytes support");
    if (!process_bytes_impl_r.value) {
        ydebug("container apply_child_body: id=%u no process_bytes, skipping %zu", child_id,
               body_len);
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result cr =
        yetty_yfigure_process_bytes(((struct yetty_yclass_object *)(child)-1), body, body_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "container apply_child_body: child");
    {
        struct yetty_ycore_void_result set_r =
            yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(child)-1, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
    }
    /* Mark the CONTAINER dirty too, not just the child. The host's figure-frame
     * render-trigger (terminal_pty_pipe_read) repaints only when the root
     * container's own dirty bit is set. apply_child_body is the hot path for
     * every ygui re-emit; without flagging the container here a content update
     * (e.g. a button relabel after a click) is applied to the child but the
     * screen stays stale until some unrelated event forces a render — so
     * interactive updates appear frozen / "seconds late". Mirrors the other
     * container_do_* mutators, which all set the container dirty. */
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container apply_child_body: container dirty");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_char_ptr_result container_dump(const struct yetty_yfigure_figure *self,
                                                         int indent)
{
    struct yetty_yclass_void_ptr_result container_r =
        container_from_obj((struct yetty_yclass_object *)self - 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, container_r, "container_dump: from_obj");
    const struct yetty_yfigure_container *container = container_r.value;
    char pad[64];
    dump_indent_spaces(pad, sizeof(pad), indent);
    size_t len = 0, cap = 0;
    char *buf = NULL;
    buf = dump_appendf(buf, &len, &cap, "%skind: container\n", pad);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
    }
    buf = dump_appendf(
        buf, &len, &cap, "%srect: [%.1f, %.1f, %.1f, %.1f]\n", pad,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.min.x,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.min.y,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.max.x,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.max.y);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
    }
    buf =
        dump_appendf(buf, &len, &cap, "%sdirty: %d\n", pad,
                     yetty_yfigure_figure_dirty_get((struct yetty_yclass_object *)(self)-1).value);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
    }
    buf = dump_appendf(buf, &len, &cap, "%sviewport_offset: [%.1f, %.1f]\n", pad,
                       container->viewport_offset_x, container->viewport_offset_y);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
    }
    /* Children section. The list is sorted by (z, insertion-seq) so the
     * dump lines come out back-to-front in true z-order, matching render
     * order — tests assert z-order from the line order. dump is
     * logically const, but re-sorting for deterministic output is a
     * benign internal reordering, so cast away const for the sort. */
    container_ensure_sorted((struct yetty_yfigure_container *)container);
    if (!container->children) {
        buf = dump_appendf(buf, &len, &cap, "%schildren: {}\n", pad);
        if (!buf) {
            return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
        }
        return YETTY_OK(yetty_ycore_char_ptr, buf);
    }
    buf = dump_appendf(buf, &len, &cap, "%schildren:\n", pad);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
    }
    struct child_entry *e, *tmp;
    HASH_ITER(hh, container->children, e, tmp)
    {
        buf = dump_appendf(buf, &len, &cap, "%s  '%u':\n", pad, e->id);
        if (!buf) {
            return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
        }
        struct yetty_ycore_char_ptr_result child_dump_r = yetty_yfigure_dump(e->figure, indent + 4);
        if (YETTY_IS_ERR(child_dump_r)) {
            free(buf);
            return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: child dump", child_dump_r);
        }
        char *child_dump = child_dump_r.value;
        buf = dump_appendf(buf, &len, &cap, "%s", child_dump);
        free(child_dump);
        if (!buf) {
            return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
        }
    }
    return YETTY_OK(yetty_ycore_char_ptr, buf);
}

/*===========================================================================
 * Public API
 *=========================================================================*/

/* The obj->container downcast `yetty_yfigure_container_from` is generated (it
 * returns a Result, since the object may not be of this class). Callers handle
 * the Result. */

/* Setters for per-instance runtime state. These are owner-side
 * helpers — they take a body pointer (not a proxy), so they're only
 * meaningful on the side that hosts the actual container instance.
 * That side knows its `context` and `registry` from local C state;
 * neither pointer is meaningful across a wire. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yfigure_container_set_registry(
    struct yetty_yclass_object *obj, struct yetty_yfigure_registry *registry)
{
    struct yetty_yfigure_container_ptr_result container_r = yetty_yfigure_container_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "yfigure_container_set_registry: from_obj");
    container_r.value->registry = registry;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yfigure_container_set_context(
    struct yetty_yclass_object *obj, const struct yetty_context *context)
{
    struct yetty_yfigure_container_ptr_result container_r = yetty_yfigure_container_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "yfigure_container_set_context: from_obj");
    container_r.value->context = context;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yfigure_container_set_rect(struct yetty_yclass_object *obj,
                                                                struct yetty_ycore_rectangle rect)
{
    struct yetty_ycore_void_result rect_r = yetty_yfigure_figure_rect_set(obj, rect);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_r, "yfigure_container_set_rect: rect_set");
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yfigure_container_set_viewport_offset(
    struct yetty_yclass_object *obj, float offset_x, float offset_y)
{
    struct yetty_yfigure_container_ptr_result container_r = yetty_yfigure_container_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r,
                        "yfigure_container_set_viewport_offset: from_obj");
    container_r.value->viewport_offset_x = offset_x;
    container_r.value->viewport_offset_y = offset_y;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_yfigure_figure_ptr_result yetty_yfigure_container_as_figure(
    struct yetty_yclass_object *obj)
{
    struct yetty_yfigure_figure_ptr_result figure_r = yetty_yfigure_figure_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yfigure_figure_ptr, figure_r,
                        "yfigure_container_as_figure: figure_from");
    return YETTY_OK(yetty_yfigure_figure_ptr, figure_r.value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yfigure_container_add_child(struct yetty_yclass_object *obj,
                                                                 struct yetty_yfigure_figure *child,
                                                                 uint32_t id)
{
    struct yetty_yfigure_container_ptr_result container_r = yetty_yfigure_container_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "yfigure_container_add_child: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    if (!child) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_add_child: NULL arg");
    }
    if (id == 0) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_add_child: id=0 is reserved");
    }
    /* Boundary check — every figure is a yclass object carrying its class
     * header in self_obj; the container dispatches all of its ops (render,
     * destroy, process_input, process_bytes, reset_content, dump) through
     * yclass via that header. */
    if (!((struct yetty_yclass_object *)(child)-1)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yfigure_container_add_child: child has no yclass object");
    }
    struct child_entry *existing;
    HASH_FIND_INT(container->children, &id, existing);
    if (existing) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_add_child: id collision");
    }
    struct child_entry *e = (struct child_entry *)calloc(1, sizeof(*e));
    if (!e) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_add_child: oom");
    }
    e->id = id;
    e->figure = child;
    e->seq = container->next_seq++;
    HASH_ADD_INT(container->children, id, e);
    container->z_order_dirty = 1;
    {
        struct yetty_ycore_void_result set_r =
            yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(child)-1, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_yfigure_figure_ptr_result yetty_yfigure_container_find_child_by_id(
    struct yetty_yclass_object *obj, uint32_t id)
{
    if (id == 0) {
        return YETTY_OK(yetty_yfigure_figure_ptr, NULL);
    }
    struct yetty_yfigure_container_ptr_result container_r = yetty_yfigure_container_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yfigure_figure_ptr, container_r,
                        "yfigure_container_find_child_by_id: from_obj");
    struct child_entry *e;
    HASH_FIND_INT(container_r.value->children, &id, e);
    return YETTY_OK(yetty_yfigure_figure_ptr, e ? e->figure : NULL);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yfigure_container_remove_child_by_id(
    struct yetty_yclass_object *obj, uint32_t id)
{
    struct yetty_yfigure_container_ptr_result container_r = yetty_yfigure_container_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r,
                        "yfigure_container_remove_child_by_id: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    if (id == 0) {
        return YETTY_OK_VOID();
    }
    struct child_entry *e;
    HASH_FIND_INT(container->children, &id, e);
    if (!e) {
        return YETTY_OK_VOID(); /* stale delete from wire — benign */
    }
    HASH_DEL(container->children, e);
    struct yetty_ycore_void_result entry_r = entry_destroy(e);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, entry_r,
                        "yfigure_container_remove_child_by_id: child destroy failed");
    return YETTY_OK_VOID();
}

/* Mark an existing child as protected from clear_all (the CLEAR_ALL admin op
 * and the terminal's full-screen-erase / reset hook). The container's own
 * structural figures — the terminal content grid above all — use this so a
 * client emitting CLEAR_ALL to drop its figures cannot also wipe the host's
 * content. Idempotent; a stale id (already removed) is a benign no-op. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yfigure_container_protect_child(
    struct yetty_yclass_object *obj, uint32_t id)
{
    struct yetty_yfigure_container_ptr_result container_r = yetty_yfigure_container_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "yfigure_container_protect_child: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    if (id == 0) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_protect_child: id=0 is reserved");
    }
    struct child_entry *e;
    HASH_FIND_INT(container->children, &id, e);
    if (!e) {
        return YETTY_OK_VOID();
    }
    e->protected_from_clear = 1;
    return YETTY_OK_VOID();
}

static int yetty_yfigure_container_for_each(struct yetty_yfigure_container *container,
                                            container_visitor_fn fn, void *user)
{
    if (!container || !fn) {
        return 0;
    }
    container_ensure_sorted(container);
    struct child_entry *e, *tmp;
    HASH_ITER(hh, container->children, e, tmp)
    {
        int rc = fn(e->id, e->figure, user);
        if (rc) {
            return rc;
        }
    }
    return 0;
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yfigure_container_raise_child_by_id(
    struct yetty_yclass_object *obj, uint32_t id)
{
    struct yetty_yfigure_container_ptr_result container_r = yetty_yfigure_container_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r,
                        "yfigure_container_raise_child_by_id: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    if (id == 0) {
        return YETTY_OK_VOID();
    }
    struct child_entry *e;
    HASH_FIND_INT(container->children, &id, e);
    if (!e) {
        return YETTY_OK_VOID();
    }
    /* uthash insertion order is the z-order. Raising to top = delete +
     * re-insert with the same id — the entry pointer stays valid, the
     * child isn't destroyed. */
    HASH_DEL(container->children, e);
    HASH_ADD_INT(container->children, id, e);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Hit-test
 *
 * Walks every child in insertion order (= back-to-front in z) and
 * returns the first one whose rect contains the point. Cursor coords
 * are absolute target pixels, the same space child rects live in.
 *=========================================================================*/

struct hit_visitor_state {
    float x;
    float y;
    /* Pane origin in target pixels (== container viewport offset). Used to
     * report pane-local coords for absolute-coords figures. */
    float viewport_offset_x;
    float viewport_offset_y;
    struct yetty_yfigure_hit hit;
};

YETTY_EXTERNAL_CALLBACK
static int hit_visit(uint32_t id, struct yetty_yfigure_figure *child, void *user)
{
    struct hit_visitor_state *st = user;
    if (yetty_yfigure_figure_hidden_get((struct yetty_yclass_object *)(child)-1).value) {
        return 0;
    }
    if (st->x <
            yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(child)-1).value.min.x ||
        st->x >=
            yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(child)-1).value.max.x ||
        st->y <
            yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(child)-1).value.min.y ||
        st->y >=
            yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(child)-1).value.max.y) {
        return 0;
    }
    /* uthash walks insertion order = back-to-front z-order. The front-most
     * figure must win, so we keep overwriting — the last match in the
     * walk is the top of the z-stack. Returning early on first match
     * would make decoration figures (e.container. shader-glyph at 0xFFFFFFFE,
     * inserted at terminal create) steal hits from interactive figures
     * (ygreeter / ygui chrome) inserted later. */
    st->hit.figure_id = id;
    if (yetty_yfigure_figure_absolute_coords_get((struct yetty_yclass_object *)(child)-1).value) {
        /* Absolute-coords figure (ygui chrome / scrolling sub-figures): its
         * content is laid out and hit-tested in pane-root space and merely
         * scissor-clipped to the rect — no per-figure re-origin. Report the
         * cursor in that same pane-local space so an offset sub-figure (a
         * scrollarea below a tabbar) maps to the widget under the pointer.
         * A full-pane chrome figure has rect.min == viewport offset, so this
         * is identical to the local path for it. */
        st->hit.local_x = st->x - st->viewport_offset_x;
        st->hit.local_y = st->y - st->viewport_offset_y;
    } else {
        /* Local-coords producer figure (yplot/yimage/…): content drawn from
         * the figure's own origin; re-origin the cursor to its rect. */
        struct yetty_ycore_rectangle rect =
            yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(child)-1).value;
        st->hit.local_x = st->x - rect.min.x;
        st->hit.local_y = st->y - rect.min.y;
    }
    return 0;
}

YETTY_ANNOTATE("expose")
struct yetty_yfigure_hit_result yetty_yfigure_container_hit_test(struct yetty_yclass_object *obj,
                                                                 float x, float y)
{
    struct yetty_yfigure_container_ptr_result container_r = yetty_yfigure_container_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yfigure_hit, container_r, "yfigure_container_hit_test: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    struct hit_visitor_state st = {.x = x,
                                   .y = y,
                                   .viewport_offset_x = container->viewport_offset_x,
                                   .viewport_offset_y = container->viewport_offset_y,
                                   .hit = {0, 0, 0}};
    yetty_yfigure_container_for_each(container, hit_visit, &st);
    return YETTY_OK(yetty_yfigure_hit, st.hit);
}

/*===========================================================================
 * yclass slot overrides
 *
 * One wrapper impl per container public method whose signature is
 * wire-marshallable. Skipped (signature incompatible — these stay as
 * direct C calls on the body pointer):
 *   - find_child_by_id, as_figure           — pointer-Result return
 *   - hit_test                              — by-value struct Result return
 *   - for_each                              — function pointer arg
 *   - set_viewport_offset / _rect / etc.    — owner-side setters
 *
 * Every container instance — local OR remote-proxy — has the yclass
 * `struct yetty_yclass_object` header at offset 0 followed by the
 * `struct yetty_yfigure_container` body. Allocation goes through
 * `yetty_yclass_object_alloc`; the body is set up by the constructor
 * slot below. */

/* yclass instance layout: yclass_object header at offset 0, user data
 * (the `struct yetty_yfigure_container` body) immediately after. Cast
 * via (obj + 1) advances past the header in pointer arithmetic. */
YETTY_ANNOTATE("virtual@yfigure:container:constructor")
static struct yetty_ycore_void_result yetty_yfigure_container_constructor_impl(
    struct yetty_yclass_object *obj)
{
    (void)obj;
    /* Per-instance state (rect, context, registry, viewport_offset) is left
     * zero-initialized here and is wired up by the owner via the public
     * setters below. No per-instance bookkeeping is needed at construction. */
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@yfigure:container:add_child")
static struct yetty_ycore_void_result yetty_yfigure_container_add_child_impl(
    struct yetty_yclass_object *obj, struct yetty_yfigure_figure *child, uint32_t id)
{
    return yetty_yfigure_container_add_child(obj, child, id);
}

YETTY_ANNOTATE("virtual@yfigure:container:remove_child_by_id")
static struct yetty_ycore_void_result yetty_yfigure_container_remove_child_by_id_impl(
    struct yetty_yclass_object *obj, uint32_t id)
{
    return yetty_yfigure_container_remove_child_by_id(obj, id);
}

YETTY_ANNOTATE("virtual@yfigure:container:raise_child_by_id")
static struct yetty_ycore_void_result yetty_yfigure_container_raise_child_by_id_impl(
    struct yetty_yclass_object *obj, uint32_t id)
{
    return yetty_yfigure_container_raise_child_by_id(obj, id);
}

/*---------------------------------------------------------------------------
 * Typed figure-tree mutation slots — the yclass replacement for the admin
 * record stream. Each is wire-marshallable (primitives + rectangle + buffer)
 * so the same callsite dispatches locally (ctx.session == NULL) or over RPC.
 * The producer (ygui) calls the generated stubs (yetty_yfigure_create_child,
 * …) instead of building admin records. The bodies delegate to the typed
 * container_do_* helpers above.
 *-------------------------------------------------------------------------*/

/* Each mutation slot is `oneway@` — the producer fires it and moves on; the
 * host applies it with no reply. Matches the legacy one-way figure wire and
 * keeps an interactive producer's input loop unblocked by RPC round-trips. */
YETTY_ANNOTATE("oneway@yfigure:create_child")
YETTY_ANNOTATE("virtual@yfigure:container:create_child")
static struct yetty_ycore_void_result yetty_yfigure_container_create_child_impl(
    struct yetty_yclass_object *obj, uint32_t kind_token, uint32_t id,
    struct yetty_ycore_rectangle rect, struct yetty_ycore_buffer init)
{
    return container_do_create_child(obj, kind_token, id, rect, init.data, init.size);
}

YETTY_ANNOTATE("oneway@yfigure:delete_child")
YETTY_ANNOTATE("virtual@yfigure:container:delete_child")
static struct yetty_ycore_void_result yetty_yfigure_container_delete_child_impl(
    struct yetty_yclass_object *obj, uint32_t id)
{
    return container_do_delete_child(obj, id);
}

YETTY_ANNOTATE("oneway@yfigure:set_child_rect")
YETTY_ANNOTATE("virtual@yfigure:container:set_child_rect")
static struct yetty_ycore_void_result yetty_yfigure_container_set_child_rect_impl(
    struct yetty_yclass_object *obj, uint32_t id, struct yetty_ycore_rectangle rect)
{
    return container_do_set_child_rect(obj, id, rect);
}

YETTY_ANNOTATE("oneway@yfigure:set_rect")
YETTY_ANNOTATE("virtual@yfigure:container:set_rect")
static struct yetty_ycore_void_result yetty_yfigure_container_set_rect_impl(
    struct yetty_yclass_object *obj, struct yetty_ycore_rectangle rect)
{
    return container_do_set_container_rect(obj, rect);
}

YETTY_ANNOTATE("oneway@yfigure:set_child_z")
YETTY_ANNOTATE("virtual@yfigure:container:set_child_z")
static struct yetty_ycore_void_result yetty_yfigure_container_set_child_z_impl(
    struct yetty_yclass_object *obj, uint32_t id, int32_t z)
{
    return container_do_set_child_z(obj, id, z);
}

YETTY_ANNOTATE("oneway@yfigure:set_child_hidden")
YETTY_ANNOTATE("virtual@yfigure:container:set_child_hidden")
static struct yetty_ycore_void_result yetty_yfigure_container_set_child_hidden_impl(
    struct yetty_yclass_object *obj, uint32_t id, uint32_t hidden)
{
    return container_do_set_child_hidden(obj, id, hidden);
}

YETTY_ANNOTATE("oneway@yfigure:set_child_scroll")
YETTY_ANNOTATE("virtual@yfigure:container:set_child_scroll")
static struct yetty_ycore_void_result yetty_yfigure_container_set_child_scroll_impl(
    struct yetty_yclass_object *obj, uint32_t id, float scroll_x, float scroll_y)
{
    return container_do_set_child_scroll(obj, id, scroll_x, scroll_y);
}

YETTY_ANNOTATE("oneway@yfigure:set_child_content_size")
YETTY_ANNOTATE("virtual@yfigure:container:set_child_content_size")
static struct yetty_ycore_void_result yetty_yfigure_container_set_child_content_size_impl(
    struct yetty_yclass_object *obj, uint32_t id, float content_w, float content_h)
{
    return container_do_set_child_content_size(obj, id, content_w, content_h);
}

YETTY_ANNOTATE("oneway@yfigure:apply_child_body")
YETTY_ANNOTATE("virtual@yfigure:container:apply_child_body")
static struct yetty_ycore_void_result yetty_yfigure_container_apply_child_body_impl(
    struct yetty_yclass_object *obj, uint32_t id, struct yetty_ycore_buffer body)
{
    return container_do_apply_child_body(obj, id, body.data, body.size);
}

YETTY_ANNOTATE("oneway@yfigure:clear_all")
YETTY_ANNOTATE("virtual@yfigure:container:clear_all")
static struct yetty_ycore_void_result yetty_yfigure_container_clear_all_slot(
    struct yetty_yclass_object *obj)
{
    return yetty_yfigure_container_clear_all(obj);
}

YETTY_ANNOTATE("override@yfigure:container:dump_state")
static struct yetty_ycore_char_ptr_result container_dump_state_slot(struct yetty_yclass_object *obj,
                                                                    int indent)
{
    return container_dump((struct yetty_yfigure_figure *)(obj + 1), indent);
}

#include "container.gen.c"
